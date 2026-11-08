#pragma once
/**
 * @file isotp.hpp
 * @brief ISO 15765-2 (ISO-TP) transport for one physically addressed ECU, 11-bit normal addressing.
 *
 * ISO-TP lets a payload larger than 8 bytes travel over Classical CAN. The first data byte of
 * every frame is the Protocol Control Information (PCI); its high nibble is the frame type:
 *
 * | Type | Name              | Layout                                   |
 * |------|-------------------|------------------------------------------|
 * | 0    | Single Frame (SF) | `0L d1..dL`               L = 1..7        |
 * | 1    | First Frame (FF)  | `1L LL d1..d6`            12-bit length   |
 * | 2    | Consecutive (CF)  | `2N d1..d7`               N = seq 0..15   |
 * | 3    | Flow Control (FC) | `3S BS STmin`             S: 0=CTS 1=WAIT 2=OVFLW |
 *
 * Receiving a multi-frame response:
 * @code
 *   ECU → FF (total length)      we → FC CTS (BS, STmin)
 *   ECU → CF seq 1, 2, … 15, 0, 1 …  until total length bytes received
 * @endcode
 *
 * Design: the receiver is a *pure* state machine. It never reads a clock, never sends a frame
 * and never blocks. Time comes from CanFrame::timestampUs and poll(); frames to send are
 * returned to the caller. This makes it identical on ESP32, desktop simulation and host tests.
 */

#include <cstddef>
#include <cstdint>

#include "diag/can_frame.hpp"
#include "diag/error.hpp"

namespace diag {

/// PCI frame types (high nibble of byte 0).
enum class IsoTpFrameType : std::uint8_t { Single = 0, First = 1, Consecutive = 2, FlowControl = 3 };

/// Flow-control status values (low nibble of an FC PCI byte).
enum class FlowStatus : std::uint8_t { ContinueToSend = 0, Wait = 1, Overflow = 2 };

struct IsoTpConfig {
    std::uint32_t txId = 0x7E0;  ///< Our request / flow-control identifier (OBD physical ECU #1).
    std::uint32_t rxId = 0x7E8;  ///< Expected responder identifier (= txId + 8 for OBD).
    /// Block size we grant the sender: 0 = "send all remaining CFs without waiting for another FC".
    std::uint8_t blockSize = 0;
    /// Minimum separation time between CFs we ask for (0..127 ms). 0 = as fast as possible; the
    /// ESP32 RX queue (32 frames) is large enough for any MVP response.
    std::uint8_t stMinMs = 0;
    /// ISO 15765-4 requires DLC = 8 for diagnostic frames; unused bytes are padded with this value.
    std::uint8_t padding = 0x00;
    /// N_Cr: maximum gap between two consecutive frames (ISO 15765-2 default 1000 ms).
    std::uint32_t nCrTimeoutMs = 1000;
};

/**
 * @brief Builds a Single Frame carrying @p length bytes (1..7), padded to DLC 8.
 * @return the frame, or Stage::IsoTp / Code::InvalidLength when the payload does not fit.
 */
Result<CanFrame> buildSingleFrame(const IsoTpConfig& config, const std::uint8_t* payload, std::size_t length) noexcept;

/// Builds a Flow Control frame from us (config.txId) with the given status.
CanFrame buildFlowControl(const IsoTpConfig& config, FlowStatus status) noexcept;

/**
 * @brief Receives one ISO-TP message from config.rxId.
 *
 * Lifecycle: arm() → onFrame()/poll() … → state() is Complete or Failed → arm() again.
 * Not thread-safe; owned by one task (firmware) or one object (desktop simulation).
 */
class IsoTpReceiver {
public:
    /// Receive buffer size. 256 bytes covers a Mode 03 reply with 127 DTCs.
    static constexpr std::size_t kCapacity = 256;

    enum class State : std::uint8_t { Idle, WaitingFirstFrame, ReceivingConsecutive, Complete, Failed };

    /// Frame the caller must transmit (a Flow Control) as a reaction to onFrame().
    struct Output {
        bool sendFrame = false;
        CanFrame frame{};
    };

    /// Diagnostic counters; never reset by arm() so they show long-term behaviour.
    struct Counters {
        std::uint32_t unrelatedFrames = 0;  ///< other CAN IDs (normal bus traffic)
        std::uint32_t wrongResponder = 0;   ///< 0x7E8..0x7EF but not config.rxId
        std::uint32_t staleFrames = 0;      ///< timestamp before arm() – reply to an older request
        std::uint32_t ignoredFrames = 0;    ///< right ID, but not expected in the current state
    };

    explicit IsoTpReceiver(const IsoTpConfig& config) noexcept : config_(config) {}

    /**
     * @brief Starts waiting for a new message.
     * @param nowUs       request transmit time; frames received earlier are stale and ignored.
     * @param timeoutMs   absolute deadline for the first frame (the OBD P2 / P2* timer).
     */
    void arm(std::uint64_t nowUs, std::uint32_t timeoutMs) noexcept;

    /// Feeds one received frame. Frames from other IDs are counted and ignored.
    Output onFrame(const CanFrame& frame) noexcept;

    /// Advances time; moves to Failed(RxTimeout) once the active deadline has passed.
    void poll(std::uint64_t nowUs) noexcept;

    /// Returns to Idle and discards any partial payload (counters are kept).
    void reset() noexcept;

    State state() const noexcept { return state_; }
    const Error& error() const noexcept { return error_; }
    const std::uint8_t* payload() const noexcept { return buffer_; }
    std::size_t length() const noexcept { return state_ == State::Complete ? expected_ : 0; }
    const Counters& counters() const noexcept { return counters_; }
    const IsoTpConfig& config() const noexcept { return config_; }
    /// Next expected CF sequence number (useful for traces).
    std::uint8_t nextSequence() const noexcept { return sequence_; }

private:
    Output handleFirstFrameOrSingle(const CanFrame& frame) noexcept;
    Output handleConsecutive(const CanFrame& frame) noexcept;
    void failWith(Code code, std::uint32_t detail, Stage stage = Stage::IsoTp) noexcept;

    IsoTpConfig config_;
    State state_ = State::Idle;
    Error error_{};
    std::uint8_t buffer_[kCapacity]{};
    std::size_t expected_ = 0;   ///< total message length announced by SF/FF
    std::size_t received_ = 0;   ///< bytes copied so far
    std::uint8_t sequence_ = 1;  ///< next expected CF sequence number
    std::uint8_t blockCount_ = 0;
    std::uint64_t armedAtUs_ = 0;
    std::uint64_t deadlineUs_ = 0;
    Counters counters_{};
};

}  // namespace diag
