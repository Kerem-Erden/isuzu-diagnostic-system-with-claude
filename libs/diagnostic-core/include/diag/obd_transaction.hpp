#pragma once
/**
 * @file obd_transaction.hpp
 * @brief One OBD request/response exchange on top of ISO-TP, with explicit error stages.
 *
 * Sequence and the stage reported for each failure:
 * @code
 *   begin()            → request frame for the caller to enqueue
 *   onEnqueueResult()  → driver refused the frame            → Stage::CanEnqueue
 *   onTxComplete()     → controller reported TX failure       → Stage::CanTxCompletion
 *                        (no completion before the deadline)  → Stage::CanTxCompletion/Timeout
 *   onFrame()          → ISO-TP violations                    → Stage::IsoTp
 *                        7F <svc> <nrc>                       → Stage::NegativeResponse
 *                        7F <svc> 78 (pending) → wait P2* again, bounded by maxPendingResponses
 *                        wrong positive service id            → Stage::ObdDecode/WrongService
 *   poll()             → nothing received in time             → Stage::RxTimeout
 * @endcode
 * The transaction never retransmits: an automatic retry could repeat a side effect and hides
 * the real failure (lesson from the reference system).
 */

#include <cstddef>
#include <cstdint>

#include "diag/can_frame.hpp"
#include "diag/error.hpp"
#include "diag/isotp.hpp"

namespace diag {

class ObdTransaction {
public:
    struct Timing {
        /// P2: time for the first response after the request. ISO 15765-4 requires ECUs to answer
        /// within 50 ms; the tester waits longer to tolerate gateways and slow diesel ECUs.
        std::uint32_t p2Ms = 500;
        /// P2*: extended time after each NRC 0x78 (ISO 15765-4: 5000 ms).
        std::uint32_t p2StarMs = 5000;
        /// Upper bound on consecutive 0x78 responses, so a faulty ECU cannot keep us waiting forever.
        std::uint8_t maxPendingResponses = 8;
    };

    enum class State : std::uint8_t { Idle, AwaitingTxCompletion, AwaitingResponse, Complete, Failed };

    explicit ObdTransaction(const IsoTpConfig& config) noexcept : receiver_(config) {}

    /**
     * @brief Starts an exchange.
     * @param request  OBD request payload (1..7 bytes), e.g. `01 0C`.
     * @param nowUs    current monotonic time; frames received before it are stale.
     * @return the CAN frame the caller must enqueue, or an error (state becomes Failed).
     * @pre state() is not AwaitingTxCompletion/AwaitingResponse (Code::Busy otherwise).
     */
    Result<CanFrame> begin(const std::uint8_t* request, std::size_t length, std::uint64_t nowUs,
                           const Timing& timing) noexcept;

    /// Reports the result of handing the frame to the CAN driver (enqueue, not completion).
    void onEnqueueResult(const Error& result) noexcept;
    /// Reports TX completion from the driver callback (success = ACKed by at least one node).
    void onTxComplete(bool success) noexcept;
    /// Feeds a received frame. @return a Flow Control frame to transmit, if any.
    IsoTpReceiver::Output onFrame(const CanFrame& frame) noexcept;
    /// Advances time for timeouts.
    void poll(std::uint64_t nowUs) noexcept;
    /// Abandons an exchange (e.g. serial disconnect). State becomes Idle.
    void cancel() noexcept;

    State state() const noexcept { return state_; }
    bool finished() const noexcept { return state_ == State::Complete || state_ == State::Failed; }
    const Error& error() const noexcept { return error_; }
    /// Positive response payload (valid in Complete).
    const std::uint8_t* payload() const noexcept { return receiver_.payload(); }
    std::size_t length() const noexcept { return state_ == State::Complete ? receiver_.length() : 0; }
    std::uint8_t pendingResponses() const noexcept { return pending_; }
    const IsoTpReceiver& receiver() const noexcept { return receiver_; }

private:
    void evaluateReceiver(std::uint64_t nowUs) noexcept;
    void failWith(const Error& error) noexcept;

    IsoTpReceiver receiver_;
    Timing timing_{};
    State state_ = State::Idle;
    Error error_{};
    std::uint8_t service_ = 0;
    std::uint8_t pending_ = 0;
    bool txCompleted_ = false;
};

}  // namespace diag
