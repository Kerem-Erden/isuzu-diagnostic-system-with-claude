#include "diag/obd_transaction.hpp"

#include "diag/obd.hpp"

namespace diag {

Result<CanFrame> ObdTransaction::begin(const std::uint8_t* request, std::size_t length, std::uint64_t nowUs,
                                       const Timing& timing) noexcept {
    if (state_ == State::AwaitingTxCompletion || state_ == State::AwaitingResponse) {
        // Only one outstanding request per ECU; do not set Failed – the running exchange stays valid.
        return fail(Stage::Internal, Code::Busy);
    }
    Result<CanFrame> frame = buildSingleFrame(receiver_.config(), request, length);
    if (!frame) {
        failWith(frame.error());
        return frame;
    }
    timing_ = timing;
    service_ = request[0];
    pending_ = 0;
    txCompleted_ = false;
    error_ = Error{};
    frame.value().timestampUs = nowUs;
    // Arm *before* transmission: a fast ECU may answer before the TX-done callback is processed.
    receiver_.arm(nowUs, timing.p2Ms);
    state_ = State::AwaitingTxCompletion;
    return frame;
}

void ObdTransaction::onEnqueueResult(const Error& result) noexcept {
    if (state_ != State::AwaitingTxCompletion || result.isOk()) return;
    failWith(Error{Stage::CanEnqueue, result.code, result.detail});
}

void ObdTransaction::onTxComplete(bool success) noexcept {
    if (state_ != State::AwaitingTxCompletion) return;
    if (!success) {
        // Typical cause on a bench: no other node ACKs the frame (TEC rises by 8 per attempt).
        failWith(fail(Stage::CanTxCompletion, Code::TxFailed));
        return;
    }
    txCompleted_ = true;
    state_ = State::AwaitingResponse;
}

IsoTpReceiver::Output ObdTransaction::onFrame(const CanFrame& frame) noexcept {
    if (state_ != State::AwaitingTxCompletion && state_ != State::AwaitingResponse) return {};
    IsoTpReceiver::Output output = receiver_.onFrame(frame);
    evaluateReceiver(frame.timestampUs);
    return output;
}

void ObdTransaction::poll(std::uint64_t nowUs) noexcept {
    if (state_ != State::AwaitingTxCompletion && state_ != State::AwaitingResponse) return;
    receiver_.poll(nowUs);
    evaluateReceiver(nowUs);
}

void ObdTransaction::cancel() noexcept {
    receiver_.reset();
    state_ = State::Idle;
}

void ObdTransaction::failWith(const Error& error) noexcept {
    error_ = error;
    state_ = State::Failed;
    receiver_.reset();
}

void ObdTransaction::evaluateReceiver(std::uint64_t nowUs) noexcept {
    switch (receiver_.state()) {
        case IsoTpReceiver::State::Failed: {
            Error error = receiver_.error();
            if (error.stage == Stage::RxTimeout && !txCompleted_) {
                // Without a TX completion we cannot claim the ECU was silent: our frame may never
                // have left the controller (e.g. bus off, transceiver not driving the bus).
                error.stage = Stage::CanTxCompletion;
            }
            failWith(error);
            return;
        }
        case IsoTpReceiver::State::Complete:
            break;
        default:
            return;
    }

    const std::uint8_t* payload = receiver_.payload();
    const std::size_t length = receiver_.length();
    std::uint8_t nrc = 0;
    if (obd::isNegativeResponse(payload, length, service_, nrc)) {
        if (nrc == obd::kNrcResponsePending) {
            if (++pending_ > timing_.maxPendingResponses) {
                failWith(fail(Stage::NegativeResponse, Code::ResponsePendingLimit, nrc));
                return;
            }
            // The ECU accepted the request; the final answer must arrive within P2* of this frame.
            // (Only reachable from onFrame(), so nowUs is the 0x78 frame's timestamp.)
            receiver_.arm(nowUs, timing_.p2StarMs);
            return;
        }
        failWith(fail(Stage::NegativeResponse, Code::NegativeResponse, nrc));
        return;
    }
    if (length == 0 || payload[0] != static_cast<std::uint8_t>(service_ + obd::kPositiveResponseOffset)) {
        failWith(fail(Stage::ObdDecode, Code::WrongService, length == 0 ? 0u : payload[0]));
        return;
    }
    // A matching positive response is proof that our request reached the ECU, so we complete
    // even if the TX-done callback has not been processed yet (a later onTxComplete is ignored).
    state_ = State::Complete;
}

}  // namespace diag
