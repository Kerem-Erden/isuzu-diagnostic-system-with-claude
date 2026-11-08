#include "gw/simulated_ecu.hpp"

#include <cstring>

#include "diag/dtc.hpp"
#include "diag/obd.hpp"

namespace gw {
namespace {

using diag::Code;
using diag::Stage;

constexpr std::uint64_t kTxDoneDelayUs = 200;      // controller arbitration + transmission
constexpr std::uint64_t kResponseDelayUs = 5000;    // typical ECU P2 well below 50 ms
constexpr std::uint64_t kPendingDelayUs = 300000;   // final answer after NRC 0x78
constexpr std::uint64_t kCfSpacingUs = 1000;        // ECU-side STmin

constexpr const char* kScenarioNames[] = {
    "NORMAL", "NO_DTCS", "SINGLE_DTC", "MANY_DTCS", "UNKNOWN_DTC", "SILENT",
    "NEGATIVE_RESPONSE", "RESPONSE_PENDING", "MALFORMED_DTC", "BAD_SEQUENCE", "NO_ACK", "UNSUPPORTED_VOLTAGE",
};
static_assert(sizeof(kScenarioNames) / sizeof(kScenarioNames[0]) ==
                  static_cast<std::size_t>(EcuScenario::UnsupportedVoltage) + 1,
              "scenario names out of sync");

/// Triangle wave between lo and hi with the given period – deterministic "driving" profile.
double triangle(std::uint64_t nowUs, double periodS, double lo, double hi) noexcept {
    const double t = static_cast<double>(nowUs % static_cast<std::uint64_t>(periodS * 1e6)) / 1e6;
    const double phase = t / periodS;  // 0..1
    const double up = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
    return lo + (hi - lo) * up;
}

std::uint8_t clampByte(double value) noexcept {
    if (value < 0.0) return 0;
    if (value > 255.0) return 255;
    return static_cast<std::uint8_t>(value + 0.5);
}

}  // namespace

const char* toString(EcuScenario scenario) noexcept {
    const auto index = static_cast<std::size_t>(scenario);
    return index < sizeof(kScenarioNames) / sizeof(kScenarioNames[0]) ? kScenarioNames[index] : "UNKNOWN";
}

bool scenarioFromString(std::string_view text, EcuScenario& out) noexcept {
    for (std::size_t i = 0; i < sizeof(kScenarioNames) / sizeof(kScenarioNames[0]); ++i) {
        if (text == kScenarioNames[i]) {
            out = static_cast<EcuScenario>(i);
            return true;
        }
    }
    return false;
}

void SimulatedEcu::setScenario(EcuScenario scenario) noexcept {
    scenario_ = scenario;
    awaitingFlowControl_ = false;
    multiLength_ = 0;
}

diag::Error SimulatedEcu::setTransmitEnabled(bool enabled) noexcept {
    transmitEnabled_ = enabled;
    if (status_.state != BusState::BusOff) status_.state = enabled ? BusState::Active : BusState::ListenOnly;
    return diag::Error{};
}

void SimulatedEcu::schedule(const Event& event) noexcept {
    if (eventCount_ == events_.size()) {
        ++status_.rxDropped;  // behaves like a full driver RX queue
        return;
    }
    // Keep the array ordered by due time (stable for equal times) – at most 48 entries.
    std::size_t position = eventCount_;
    while (position > 0 && events_[position - 1].dueUs > event.dueUs) {
        events_[position] = events_[position - 1];
        --position;
    }
    events_[position] = event;
    ++eventCount_;
}

void SimulatedEcu::poll(std::uint64_t nowUs) noexcept {
    nowUs_ = nowUs;
    while (eventCount_ > 0 && events_[0].dueUs <= nowUs) {
        Event event = events_[0];
        for (std::size_t i = 1; i < eventCount_; ++i) events_[i - 1] = events_[i];
        --eventCount_;
        if (sink_ == nullptr) continue;
        if (event.txDone) {
            sink_->onTxComplete(event.success);
        } else {
            event.frame.timestampUs = event.dueUs;
            sink_->onCanFrame(event.frame);
        }
    }
}

diag::Error SimulatedEcu::enqueue(const diag::CanFrame& frame) noexcept {
    // The gateway stamps outgoing frames with its clock; requests can arrive before our next
    // poll(), so advance our notion of "now" or responses would look older than the request.
    if (frame.timestampUs > nowUs_) nowUs_ = frame.timestampUs;
    if (!transmitEnabled_) return diag::fail(Stage::CanEnqueue, Code::InvalidState);  // listen-only
    if (status_.state == BusState::BusOff) return diag::fail(Stage::CanEnqueue, Code::BusOff);
    if (eventCount_ + 8 > events_.size()) return diag::fail(Stage::CanEnqueue, Code::QueueFull);

    if (scenario_ == EcuScenario::NoAck) {
        // ISO 11898: a transmitter that sees no ACK increments TEC by 8.
        status_.tec = static_cast<std::uint16_t>(status_.tec + 8);
        if (status_.tec > 255) status_.state = BusState::BusOff;
        else if (status_.tec > 127) status_.state = BusState::Passive;
        else if (status_.tec > 95) status_.state = BusState::Warning;
        schedule(Event{nowUs_ + kTxDoneDelayUs, true, false, {}});
        return diag::Error{};
    }
    if (status_.tec > 0) --status_.tec;
    schedule(Event{nowUs_ + kTxDoneDelayUs, true, true, {}});

    if (frame.extended || frame.id != ids_.txId || frame.dlc == 0) return diag::Error{};
    const auto type = static_cast<diag::IsoTpFrameType>(frame.data[0] >> 4);
    if (type == diag::IsoTpFrameType::FlowControl) {
        ++flowControls_;
        if (awaitingFlowControl_ && (frame.data[0] & 0x0F) == 0) {
            awaitingFlowControl_ = false;
            sendConsecutiveFrames(nowUs_ + kCfSpacingUs);
        }
        return diag::Error{};
    }
    if (type == diag::IsoTpFrameType::Single) handleRequest(frame);
    return diag::Error{};
}

void SimulatedEcu::respond(const std::uint8_t* payload, std::size_t length, std::uint64_t atUs) noexcept {
    Event event;
    event.dueUs = atUs;
    event.frame.id = ids_.rxId;
    event.frame.dlc = 8;
    event.frame.data.fill(0xAA);  // ECUs commonly pad with 0xAA or 0x55
    if (length <= 7) {
        event.frame.data[0] = static_cast<std::uint8_t>(length);
        std::memcpy(&event.frame.data[1], payload, length);
        schedule(event);
        return;
    }
    // First Frame, then wait for the tester's Flow Control before sending CFs (real ECU behaviour).
    std::memcpy(multi_, payload, length);
    multiLength_ = length;
    multiOffset_ = 6;
    event.frame.data[0] = static_cast<std::uint8_t>(0x10 | ((length >> 8) & 0x0F));
    event.frame.data[1] = static_cast<std::uint8_t>(length & 0xFF);
    std::memcpy(&event.frame.data[2], payload, 6);
    awaitingFlowControl_ = true;
    schedule(event);
}

void SimulatedEcu::sendConsecutiveFrames(std::uint64_t atUs) noexcept {
    std::uint8_t sequence = scenario_ == EcuScenario::BadSequence ? 2 : 1;  // correct start is 1
    while (multiOffset_ < multiLength_) {
        Event event;
        event.dueUs = atUs;
        event.frame.id = ids_.rxId;
        event.frame.dlc = 8;
        event.frame.data.fill(0xAA);
        event.frame.data[0] = static_cast<std::uint8_t>(0x20 | sequence);
        std::size_t chunk = multiLength_ - multiOffset_;
        if (chunk > 7) chunk = 7;
        std::memcpy(&event.frame.data[1], multi_ + multiOffset_, chunk);
        multiOffset_ += chunk;
        sequence = static_cast<std::uint8_t>((sequence + 1) & 0x0F);
        schedule(event);
        atUs += kCfSpacingUs;
    }
}

std::size_t SimulatedEcu::buildLivePayload(std::uint8_t pid, std::uint8_t (&out)[8]) const noexcept {
    out[0] = diag::obd::kServiceCurrentData + diag::obd::kPositiveResponseOffset;
    out[1] = pid;
    switch (pid) {
        case 0x0C: {  // RPM: idle 750 → 2500 over 20 s; encoded as rpm * 4
            const auto raw = static_cast<std::uint16_t>(triangle(nowUs_, 20.0, 750.0, 2500.0) * 4.0);
            out[2] = static_cast<std::uint8_t>(raw >> 8);
            out[3] = static_cast<std::uint8_t>(raw & 0xFF);
            return 4;
        }
        case 0x05: {  // coolant warms from 20 °C to 90 °C in 60 s, then holds
            const double seconds = static_cast<double>(nowUs_) / 1e6;
            const double temp = seconds >= 60.0 ? 90.0 : 20.0 + seconds * (70.0 / 60.0);
            out[2] = clampByte(temp + 40.0);
            return 3;
        }
        case 0x0D:
            out[2] = clampByte(triangle(nowUs_, 30.0, 0.0, 80.0));
            return 3;
        case 0x04:
            out[2] = clampByte(triangle(nowUs_, 20.0, 15.0, 60.0) * 255.0 / 100.0);
            return 3;
        case 0x42: {  // 14.1 V ± 0.2 V, encoded in mV
            const auto mv = static_cast<std::uint16_t>(triangle(nowUs_, 10.0, 13900.0, 14300.0));
            out[2] = static_cast<std::uint8_t>(mv >> 8);
            out[3] = static_cast<std::uint8_t>(mv & 0xFF);
            return 4;
        }
        case 0x00:  // supported PIDs 01-20: 04, 05, 0C, 0D and "0x20 supported" (for 0x42)
            out[2] = 0x18;
            out[3] = 0x18;
            out[4] = 0x00;
            out[5] = 0x01;
            return 6;
        default:
            return 0;
    }
}

std::size_t SimulatedEcu::buildDtcPayload(std::uint8_t (&out)[96]) const noexcept {
    static const char* const kNormal[] = {"P1093", "P0087", "P3FFF"};
    static const char* const kSingle[] = {"P0087"};
    static const char* const kUnknown[] = {"P3FFF"};
    const char* const* codes = nullptr;
    std::size_t count = 0;
    switch (scenario_) {
        case EcuScenario::NoDtcs: break;
        case EcuScenario::SingleDtc: codes = kSingle; count = 1; break;
        case EcuScenario::UnknownDtc: codes = kUnknown; count = 1; break;
        case EcuScenario::ManyDtcs: count = 40; break;
        default: codes = kNormal; count = 3; break;
    }
    out[0] = diag::obd::kServiceStoredDtc + diag::obd::kPositiveResponseOffset;
    out[1] = static_cast<std::uint8_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        diag::Dtc dtc;
        if (codes != nullptr) {
            diag::Dtc::fromText(codes[i], dtc);
        } else {
            dtc = diag::Dtc{0x01, static_cast<std::uint8_t>(i)};  // P0100 … P0127
        }
        out[2 + 2 * i] = dtc.high;
        out[3 + 2 * i] = dtc.low;
    }
    std::size_t length = 2 + 2 * count;
    if (scenario_ == EcuScenario::MalformedDtc) out[1] = static_cast<std::uint8_t>(count + 1);
    return length;
}

void SimulatedEcu::handleRequest(const diag::CanFrame& frame) noexcept {
    const std::size_t length = frame.data[0] & 0x0F;
    if (length == 0 || length > 7) return;
    ++requests_;
    const std::uint8_t service = frame.data[1];
    const std::uint64_t at = nowUs_ + kResponseDelayUs;

    if (scenario_ == EcuScenario::Silent) return;
    if (scenario_ == EcuScenario::NegativeResponse) {
        const std::uint8_t nrc[] = {diag::obd::kNegativeResponse, service, 0x22};
        respond(nrc, sizeof nrc, at);
        return;
    }
    std::uint64_t finalAt = at;
    if (scenario_ == EcuScenario::ResponsePending) {
        const std::uint8_t pending[] = {diag::obd::kNegativeResponse, service, diag::obd::kNrcResponsePending};
        respond(pending, sizeof pending, at);
        finalAt = nowUs_ + kPendingDelayUs;
    }

    if (service == diag::obd::kServiceCurrentData && length == 2) {
        const std::uint8_t pid = frame.data[2];
        if (scenario_ == EcuScenario::UnsupportedVoltage && pid == 0x42) {
            const std::uint8_t nrc[] = {diag::obd::kNegativeResponse, service, 0x31};
            respond(nrc, sizeof nrc, finalAt);
            return;
        }
        std::uint8_t payload[8];
        const std::size_t n = buildLivePayload(pid, payload);
        if (n == 0) {
            // Real OBD ECUs usually stay silent for unsupported PIDs; requestOutOfRange is clearer.
            const std::uint8_t nrc[] = {diag::obd::kNegativeResponse, service, 0x31};
            respond(nrc, sizeof nrc, finalAt);
            return;
        }
        respond(payload, n, finalAt);
        return;
    }
    if (service == diag::obd::kServiceStoredDtc && length == 1) {
        std::uint8_t payload[96];
        respond(payload, buildDtcPayload(payload), finalAt);
        return;
    }
    const std::uint8_t nrc[] = {diag::obd::kNegativeResponse, service, 0x11};  // serviceNotSupported
    respond(nrc, sizeof nrc, finalAt);
}

}  // namespace gw
