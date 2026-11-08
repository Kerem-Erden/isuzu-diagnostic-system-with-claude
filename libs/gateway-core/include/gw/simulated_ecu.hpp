#pragma once
/**
 * @file simulated_ecu.hpp
 * @brief Deterministic simulated engine ECU behind the CanPort interface.
 *
 * SIMULATION — NO VEHICLE DATA. The simulator answers at CAN-frame level, so the gateway's real
 * ISO-TP receiver, flow control, OBD decoding and error paths are exercised (brief §13: no
 * injection of results into the UI). It is used by:
 *  - the firmware simulation profile (on a real ESP32, no CAN transceiver needed),
 *  - the desktop's simulation source (in-process),
 *  - host and integration tests.
 *
 * Determinism: responses depend only on the scenario, the request and the time passed to poll().
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "diag/isotp.hpp"
#include "gw/ports.hpp"

namespace gw {

enum class EcuScenario : std::uint8_t {
    Normal,             ///< live data + 3 stored DTCs P1093, P0087, P3FFF (multi-frame Mode 03)
    NoDtcs,             ///< Mode 03 answers `43 00`
    SingleDtc,          ///< P0087 only (single frame)
    ManyDtcs,           ///< 40 DTCs (multi-frame, > one serial frame)
    UnknownDtc,         ///< P3FFF only – a valid code absent from the knowledge DB
    Silent,             ///< frames are ACKed but the ECU never answers (RX timeout)
    NegativeResponse,   ///< every request answered `7F <svc> 22` conditionsNotCorrect
    ResponsePending,    ///< `7F <svc> 78` first, real answer 300 ms later
    MalformedDtc,       ///< Mode 03 count byte disagrees with the payload length
    BadSequence,        ///< multi-frame Mode 03 with a wrong CF sequence number
    NoAck,              ///< TX completion reports failure (no other node on the bus)
    UnsupportedVoltage, ///< PID 0x42 answered `7F 01 31` requestOutOfRange
};

const char* toString(EcuScenario scenario) noexcept;
bool scenarioFromString(std::string_view text, EcuScenario& out) noexcept;

class SimulatedEcu final : public CanPort {
public:
    explicit SimulatedEcu(const diag::IsoTpConfig& addressing) noexcept : ids_(addressing) {}

    /// Where completions and response frames are delivered (normally the Gateway).
    void attach(CanEventSink& sink) noexcept { sink_ = &sink; }
    void setScenario(EcuScenario scenario) noexcept;
    EcuScenario scenario() const noexcept { return scenario_; }

    /// Delivers every event due at @p nowUs (TX completions first, in time order).
    void poll(std::uint64_t nowUs) noexcept;

    diag::Error enqueue(const diag::CanFrame& frame) noexcept override;
    diag::Error setTransmitEnabled(bool enabled) noexcept override;
    BusStatus status() const noexcept override { return status_; }

    std::uint32_t requestsReceived() const noexcept { return requests_; }
    std::uint32_t flowControlsReceived() const noexcept { return flowControls_; }

private:
    struct Event {
        std::uint64_t dueUs = 0;
        bool txDone = false;
        bool success = false;
        diag::CanFrame frame{};
    };

    void schedule(const Event& event) noexcept;
    void respond(const std::uint8_t* payload, std::size_t length, std::uint64_t atUs) noexcept;
    void handleRequest(const diag::CanFrame& frame) noexcept;
    void sendConsecutiveFrames(std::uint64_t atUs) noexcept;
    std::size_t buildLivePayload(std::uint8_t pid, std::uint8_t (&out)[8]) const noexcept;
    std::size_t buildDtcPayload(std::uint8_t (&out)[96]) const noexcept;

    diag::IsoTpConfig ids_;
    CanEventSink* sink_ = nullptr;
    EcuScenario scenario_ = EcuScenario::Normal;
    BusStatus status_{BusState::ListenOnly, 0, 0, 0, 0};
    bool transmitEnabled_ = false;
    std::uint64_t nowUs_ = 0;

    std::array<Event, 48> events_{};
    std::size_t eventCount_ = 0;

    // Multi-frame transmission state (ECU → tester).
    std::uint8_t multi_[96]{};
    std::size_t multiLength_ = 0;
    std::size_t multiOffset_ = 0;
    bool awaitingFlowControl_ = false;

    std::uint32_t requests_ = 0;
    std::uint32_t flowControls_ = 0;
};

}  // namespace gw
