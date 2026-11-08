#pragma once
/**
 * @file ports.hpp
 * @brief The only seams between gateway logic and the outside world (ADR-003).
 *
 * | Port          | Firmware vehicle     | Firmware / desktop simulation |
 * |---------------|----------------------|-------------------------------|
 * | CanPort       | TwaiAdapter          | SimulatedEcu                  |
 * | CanEventSink  | implemented by Gateway (driver events are delivered *to* it) |
 * | SerialSink    | UART0 writer         | loopback buffer               |
 *
 * Threading contract: every Gateway method and every port callback runs in ONE task/thread.
 * The TWAI ISR therefore only pushes into a queue; the application task drains the queue and
 * calls Gateway::onCanFrame()/onTxComplete(). This removes all locking from the logic.
 */

#include <cstdint>
#include <string_view>

#include "diag/can_frame.hpp"
#include "diag/error.hpp"

namespace gw {

/// CAN controller error state as reported by the driver.
enum class BusState : std::uint8_t { Stopped, ListenOnly, Active, Warning, Passive, BusOff };
const char* toString(BusState state) noexcept;

struct BusStatus {
    BusState state = BusState::Stopped;
    std::uint16_t tec = 0;          ///< transmit error counter (+8 per failed TX, -1 per success)
    std::uint16_t rec = 0;          ///< receive error counter
    std::uint32_t rxDropped = 0;    ///< frames lost because the RX queue was full
};

/// Outgoing CAN side. Implementations must copy the frame: the caller's object may be reused.
class CanPort {
public:
    virtual ~CanPort() = default;
    /**
     * @brief Queues one frame for transmission.
     * @return Ok if queued. Completion is reported later through CanEventSink::onTxComplete.
     *         Errors use Stage::CanEnqueue (QueueFull, BusOff, InvalidState for listen-only).
     */
    virtual diag::Error enqueue(const diag::CanFrame& frame) = 0;
    /// false = listen-only (no ACK, no error frames, cannot disturb the bus); true = normal mode.
    virtual diag::Error setTransmitEnabled(bool enabled) = 0;
    virtual BusStatus status() const = 0;
};

/// Driver → gateway notifications (called in the gateway's task, never from an ISR).
class CanEventSink {
public:
    virtual ~CanEventSink() = default;
    virtual void onCanFrame(const diag::CanFrame& frame) = 0;
    virtual void onTxComplete(bool success) = 0;
};

/// Byte stream towards the desktop. write() receives one complete frame including '\n'.
class SerialSink {
public:
    virtual ~SerialSink() = default;
    virtual void write(std::string_view frame) = 0;
};

}  // namespace gw
