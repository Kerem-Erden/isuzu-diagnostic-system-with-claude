#include "diag/isotp.hpp"

#include <cstring>

namespace diag {
namespace {

constexpr std::uint64_t msToUs(std::uint32_t ms) noexcept { return static_cast<std::uint64_t>(ms) * 1000u; }

/// OBD-II physical response identifiers 0x7E8..0x7EF (ISO 15765-4 §6.3.2.2).
constexpr bool isObdResponderId(std::uint32_t id) noexcept { return id >= 0x7E8 && id <= 0x7EF; }

CanFrame paddedFrame(const IsoTpConfig& config) noexcept {
    CanFrame frame;
    frame.id = config.txId;
    frame.dlc = kClassicMaxDlc;
    frame.data.fill(config.padding);
    return frame;
}

}  // namespace

Result<CanFrame> buildSingleFrame(const IsoTpConfig& config, const std::uint8_t* payload, std::size_t length) noexcept {
    // A Single Frame with classic CAN holds at most 7 bytes after the PCI byte.
    if (payload == nullptr || length == 0 || length > 7) {
        return fail(Stage::IsoTp, Code::InvalidLength, static_cast<std::uint32_t>(length));
    }
    CanFrame frame = paddedFrame(config);
    frame.data[0] = static_cast<std::uint8_t>(length);  // PCI: type 0, SF_DL = length
    std::memcpy(&frame.data[1], payload, length);
    return frame;
}

CanFrame buildFlowControl(const IsoTpConfig& config, FlowStatus status) noexcept {
    CanFrame frame = paddedFrame(config);
    frame.data[0] = static_cast<std::uint8_t>(0x30 | static_cast<std::uint8_t>(status));
    frame.data[1] = config.blockSize;
    // STmin 0x00..0x7F = milliseconds; larger values have other meanings, so clamp.
    frame.data[2] = config.stMinMs > 0x7F ? 0x7F : config.stMinMs;
    return frame;
}

void IsoTpReceiver::arm(std::uint64_t nowUs, std::uint32_t timeoutMs) noexcept {
    reset();
    state_ = State::WaitingFirstFrame;
    armedAtUs_ = nowUs;
    deadlineUs_ = nowUs + msToUs(timeoutMs);
}

void IsoTpReceiver::reset() noexcept {
    state_ = State::Idle;
    error_ = Error{};
    expected_ = 0;
    received_ = 0;
    sequence_ = 1;
    blockCount_ = 0;
}

void IsoTpReceiver::failWith(Code code, std::uint32_t detail, Stage stage) noexcept {
    state_ = State::Failed;
    error_ = fail(stage, code, detail);
}

void IsoTpReceiver::poll(std::uint64_t nowUs) noexcept {
    const bool waiting = state_ == State::WaitingFirstFrame || state_ == State::ReceivingConsecutive;
    if (waiting && nowUs >= deadlineUs_) {
        // detail tells whether the ECU never answered (0) or stopped mid-message (bytes received).
        failWith(Code::Timeout, static_cast<std::uint32_t>(received_), Stage::RxTimeout);
    }
}

IsoTpReceiver::Output IsoTpReceiver::onFrame(const CanFrame& frame) noexcept {
    if (frame.extended || frame.id != config_.rxId) {
        if (!frame.extended && isObdResponderId(frame.id)) {
            ++counters_.wrongResponder;  // another ECU answered; keep waiting for ours
        } else {
            ++counters_.unrelatedFrames;
        }
        return {};
    }
    if (state_ != State::WaitingFirstFrame && state_ != State::ReceivingConsecutive) {
        ++counters_.ignoredFrames;
        return {};
    }
    if (frame.timestampUs < armedAtUs_) {
        ++counters_.staleFrames;  // queued before our request was sent
        return {};
    }
    // A frame that arrives after the deadline is a timeout, even if poll() was not called yet.
    poll(frame.timestampUs);
    if (state_ == State::Failed) return {};
    if (frame.dlc == 0) {
        failWith(Code::InvalidPci, 0);
        return {};
    }

    const auto type = static_cast<IsoTpFrameType>(frame.data[0] >> 4);
    switch (type) {
        case IsoTpFrameType::Single:
        case IsoTpFrameType::First:
            // ISO 15765-2 §9.8.3: a new SF/FF during reception aborts the old message and
            // starts the new one, therefore both states accept it.
            return handleFirstFrameOrSingle(frame);
        case IsoTpFrameType::Consecutive:
            if (state_ != State::ReceivingConsecutive) {
                ++counters_.ignoredFrames;  // CF without FF – leftover of an aborted message
                return {};
            }
            return handleConsecutive(frame);
        case IsoTpFrameType::FlowControl:
            ++counters_.ignoredFrames;  // we never send multi-frame requests, so FC is unexpected
            return {};
        default:
            failWith(Code::InvalidPci, frame.data[0]);
            return {};
    }
}

IsoTpReceiver::Output IsoTpReceiver::handleFirstFrameOrSingle(const CanFrame& frame) noexcept {
    received_ = 0;
    sequence_ = 1;
    blockCount_ = 0;
    const std::uint8_t pci = frame.data[0];

    if ((pci >> 4) == 0) {
        const std::size_t length = pci & 0x0F;
        // SF_DL = 0 is the CAN-FD escape sequence; values above 7 are invalid on Classical CAN.
        if (length == 0 || length > 7 || length + 1 > frame.dlc) {
            failWith(Code::InvalidLength, static_cast<std::uint32_t>(length));
            return {};
        }
        std::memcpy(buffer_, &frame.data[1], length);
        expected_ = received_ = length;
        state_ = State::Complete;
        return {};
    }

    // First Frame: 12-bit FF_DL across PCI low nibble and byte 1; must use all 8 bytes.
    const std::size_t length = (static_cast<std::size_t>(pci & 0x0F) << 8) | frame.data[1];
    if (frame.dlc != 8 || length <= 7) {
        // FF_DL = 0 would announce a >4095 byte message (ISO 15765-2:2016 escape) – not needed
        // for OBD and rejected; FF_DL 1..7 must have been sent as a Single Frame.
        failWith(Code::InvalidLength, static_cast<std::uint32_t>(length));
        return {};
    }
    if (length > kCapacity) {
        // Tell the sender to stop (FC overflow) instead of letting it fill the bus with CFs.
        failWith(Code::BufferOverflow, static_cast<std::uint32_t>(length));
        return {true, buildFlowControl(config_, FlowStatus::Overflow)};
    }
    expected_ = length;
    std::memcpy(buffer_, &frame.data[2], 6);
    received_ = 6;
    state_ = State::ReceivingConsecutive;
    deadlineUs_ = frame.timestampUs + msToUs(config_.nCrTimeoutMs);
    return {true, buildFlowControl(config_, FlowStatus::ContinueToSend)};
}

IsoTpReceiver::Output IsoTpReceiver::handleConsecutive(const CanFrame& frame) noexcept {
    const std::uint8_t sequence = frame.data[0] & 0x0F;
    if (sequence != sequence_) {
        // A lost or reordered CF corrupts the payload; ISO-TP has no retransmission, so abort.
        failWith(Code::BadSequence, (static_cast<std::uint32_t>(sequence_) << 8) | sequence);
        return {};
    }
    std::size_t chunk = expected_ - received_;
    if (chunk > 7) chunk = 7;
    if (frame.dlc < chunk + 1) {
        failWith(Code::InvalidLength, frame.dlc);
        return {};
    }
    std::memcpy(buffer_ + received_, &frame.data[1], chunk);
    received_ += chunk;
    sequence_ = static_cast<std::uint8_t>((sequence_ + 1) & 0x0F);  // 15 wraps to 0, not 1
    deadlineUs_ = frame.timestampUs + msToUs(config_.nCrTimeoutMs);

    if (received_ == expected_) {
        state_ = State::Complete;
        return {};
    }
    // With BS > 0 the sender waits for a new FC after every BS consecutive frames.
    if (config_.blockSize != 0 && ++blockCount_ == config_.blockSize) {
        blockCount_ = 0;
        return {true, buildFlowControl(config_, FlowStatus::ContinueToSend)};
    }
    return {};
}

}  // namespace diag
