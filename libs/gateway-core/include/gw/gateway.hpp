#pragma once
/**
 * @file gateway.hpp
 * @brief Gateway application logic: serial commands → OBD transactions → serial responses/events.
 *
 * Runs unchanged on the ESP32 and inside the desktop simulation. It is cooperative: the owner
 * calls onSerialBytes(), onCanFrame(), onTxComplete() as data arrives and poll() periodically
 * (every 1–10 ms). Nothing blocks.
 *
 * Safety rules implemented here (ADR-008, ADR-009):
 *  - CAN transmission is enabled only for a request issued inside a desktop session (HELLO);
 *    after `sessionTimeoutMs` without any desktop frame, live polling stops and the controller
 *    returns to listen-only. A crashed or unplugged desktop therefore cannot leave the gateway
 *    polling the ECU.
 *  - Exactly one OBD transaction at a time; no automatic retry.
 *  - Live polling stops by itself after `maxConsecutiveLiveFailures` timeouts/TX failures.
 *  - Side-effect request ids are de-duplicated (last 8).
 *  - CLEAR_DTC (Mode 04) is answered with CAPABILITY/DISABLED in this build.
 */

#include <array>
#include <cstddef>
#include <cstdint>

#include "diag/obd_transaction.hpp"
#include "gw/ports.hpp"
#include "proto/frame.hpp"
#include "proto/message.hpp"

namespace gw {

enum class DataSource : std::uint8_t { Simulation, Vehicle };
const char* toString(DataSource source) noexcept;

struct GatewayConfig {
    DataSource source = DataSource::Simulation;
    const char* firmwareVersion = "0.1.0";
    const char* buildId = "dev";
    std::uint32_t bitrate = 500000;
    diag::IsoTpConfig isotp{};
    /// Live PIDs: short P2, the ECU must answer within 50 ms per ISO 15765-4.
    diag::ObdTransaction::Timing pidTiming{250, 5000, 8};
    /// Mode 03 may need flash access in the ECU; allow a longer first response.
    diag::ObdTransaction::Timing dtcTiming{1000, 5000, 8};
    /// Smallest allowed LIVE_START period for one full cycle over all live PIDs.
    std::uint32_t minLivePeriodMs = 500;
    /// Silence from the desktop after which the session ends (see file comment).
    std::uint32_t sessionTimeoutMs = 5000;
    std::uint8_t maxConsecutiveLiveFailures = 3;
};

class Gateway final : public CanEventSink {
public:
    /// Number of live PIDs in the round-robin (diag::obd::livePids()).
    static constexpr std::size_t kLivePidCount = 5;
    /// DTCs per response/event chunk so that each frame stays below proto::kMaxFrameLength.
    static constexpr std::size_t kDtcsPerChunk = 30;
    /// Distinct CAN IDs tracked by the passive monitor; further IDs only increase `overflow`.
    static constexpr std::size_t kMonitorCapacity = 64;

    struct Counters {
        std::uint32_t requests = 0;
        std::uint32_t badFrames = 0;       ///< valid CRC but unparsable body or wrong message type
        std::uint32_t duplicates = 0;
        std::uint32_t transactions = 0;
        std::uint32_t transactionFailures = 0;
        std::uint32_t flowControlEnqueueFailures = 0;
        std::uint32_t sessionTimeouts = 0;
    };

    /// The ports must outlive the gateway (they are referenced, not owned).
    Gateway(const GatewayConfig& config, CanPort& can, SerialSink& serial) noexcept;

    /// Announces itself with `EVT|0|BOOT`. Call once after construction.
    void start(std::uint64_t nowUs) noexcept;
    /// Raw bytes from the serial link (any fragmentation, may contain boot-log noise).
    void onSerialBytes(const char* data, std::size_t length, std::uint64_t nowUs) noexcept;
    void onCanFrame(const diag::CanFrame& frame) noexcept override;
    void onTxComplete(bool success) noexcept override;
    /// Timeouts, scheduling, completion handling.
    void poll(std::uint64_t nowUs) noexcept;
    /// Emits `EVT|<seq>|DIAG|<category>|<key>=<value>` for platform problems the gateway logic
    /// cannot see itself (e.g. TWAI driver initialisation failed). Values must be field-safe.
    void reportDiagnostic(std::string_view category, std::string_view key, std::string_view value) noexcept;

    bool sessionActive() const noexcept { return sessionActive_; }
    bool liveActive() const noexcept { return liveActive_; }
    bool monitorActive() const noexcept { return monitorActive_; }
    bool transmitEnabled() const noexcept { return transmitEnabled_; }
    const Counters& counters() const noexcept { return counters_; }
    const proto::FrameReader::Counters& serialCounters() const noexcept { return reader_.counters(); }

private:
    enum class Kind : std::uint8_t { None, LivePid, UserPid, UserDtc };
    struct PendingRequest {
        Kind kind = Kind::None;
        std::uint32_t id = 0;
        std::uint8_t pid = 0;
    };

    void handleBody(std::string_view body, std::uint64_t nowUs) noexcept;
    void handleRequest(const proto::Message& request, std::uint64_t nowUs) noexcept;
    void sendHello(std::uint32_t id) noexcept;
    void sendStatus(std::uint32_t id) noexcept;
    void sendOkSimple(std::uint32_t id, std::string_view key, std::uint32_t value) noexcept;
    void sendError(std::uint32_t id, const diag::Error& error) noexcept;
    void submitUser(const PendingRequest& request, std::uint64_t nowUs) noexcept;
    bool isDuplicate(std::uint32_t id) noexcept;

    void startTransaction(const PendingRequest& request, std::uint64_t nowUs) noexcept;
    void finishTransaction(std::uint64_t nowUs) noexcept;
    void finishLive(std::uint64_t nowUs) noexcept;
    void finishUserPid() noexcept;
    void finishUserDtc() noexcept;
    void scheduleLive(std::uint64_t nowUs) noexcept;
    void stopLive(const char* reason) noexcept;
    void endSession() noexcept;
    void updateTransmitState() noexcept;
    void recordMonitorFrame(const diag::CanFrame& frame) noexcept;
    void finishMonitor(std::uint32_t requestId) noexcept;

    // Not named "emit": Qt defines `emit` as a macro and this header is included by the desktop.
    void sendFrame(std::string_view frame) noexcept;
    proto::FrameWriter& beginEvent(std::string_view kind) noexcept;
    void traceFrame(const char* kind, const diag::CanFrame& frame) noexcept;

    GatewayConfig config_;
    CanPort& can_;
    SerialSink& serial_;
    proto::FrameReader reader_;
    proto::FrameWriter writer_;
    diag::ObdTransaction transaction_;
    PendingRequest active_{};
    PendingRequest queued_{};

    bool sessionActive_ = false;
    bool transmitEnabled_ = false;
    bool trace_ = false;
    std::uint64_t lastHostActivityUs_ = 0;
    std::uint64_t nowUs_ = 0;

    bool liveActive_ = false;
    std::uint64_t liveIntervalUs_ = 0;
    std::uint64_t liveNextDueUs_ = 0;
    std::size_t liveIndex_ = 0;
    std::uint8_t liveFailures_ = 0;
    std::array<bool, kLivePidCount> liveUnsupported_{};

    /// Passive monitor (W16): listen-only frame statistics per CAN ID, no transmission allowed.
    struct MonitorEntry {
        std::uint32_t id = 0;
        bool extended = false;
        std::uint8_t lastDlc = 0;
        std::uint32_t count = 0;
        std::uint32_t firstMs = 0;  ///< relative to MONITOR_START
        std::uint32_t lastMs = 0;
    };
    bool monitorActive_ = false;
    std::uint64_t monitorStartUs_ = 0;
    std::array<MonitorEntry, kMonitorCapacity> monitor_{};
    std::size_t monitorUsed_ = 0;
    std::uint32_t monitorFrames_ = 0;
    std::uint32_t monitorOverflow_ = 0;

    std::array<std::uint32_t, 8> recentIds_{};
    std::size_t recentNext_ = 0;
    std::uint32_t eventSequence_ = 0;
    Counters counters_{};
};

}  // namespace gw
