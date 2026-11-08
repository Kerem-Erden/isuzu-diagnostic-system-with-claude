#pragma once
/**
 * @file twai_adapter.hpp
 * @brief gw::CanPort implementation on the ESP-IDF on-chip TWAI driver (`esp_twai_onchip.h`).
 *
 * Responsibilities and the reasons behind them:
 *  - **ISR/task boundary.** Driver callbacks run in interrupt context. They only copy the frame
 *    into a statically allocated FreeRTOS queue and update atomic counters – no logging, no
 *    allocation, no gateway calls. drainEvents() (application task) forwards the events to the
 *    Gateway, so all protocol logic runs in one task (see gw/ports.hpp).
 *  - **TX storage lifetime.** `twai_node_transmit()` *borrows* the twai_frame_t and its data
 *    buffer until the TX-done callback. They therefore live in fixed slots owned by this object,
 *    and a slot is released only by that callback or by deleting the node (reference-system lesson:
 *    a timed-out transmit must not reuse storage the driver may still read).
 *  - **Listen-only by default.** The node is created with `enable_listen_only`; it never ACKs or
 *    transmits. setTransmitEnabled(true) recreates it in normal mode for a desktop request (ADR-009).
 *    The TX pin stays assigned in listen-only so the controller keeps it recessive; leaving the pin
 *    unconfigured could let the transceiver's D input float (deviation from the old firmware).
 *
 * RAII: the destructor disables and deletes the node and the queue. Not copyable/movable because
 * the driver holds `this` as callback context.
 */

#include <array>
#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gw/ports.hpp"

class TwaiAdapter final : public gw::CanPort {
public:
    struct Config {
        int txGpio = 21;
        int rxGpio = 22;
        std::uint32_t bitrate = 500000;
    };

    explicit TwaiAdapter(const Config& config) noexcept : config_(config) {}
    ~TwaiAdapter() override;
    TwaiAdapter(const TwaiAdapter&) = delete;
    TwaiAdapter& operator=(const TwaiAdapter&) = delete;

    /// Creates the event queue and a listen-only node, then enables it.
    esp_err_t begin() noexcept;

    /// Forwards up to @p maxEvents queued ISR events to @p sink (call from the application task).
    void drainEvents(gw::CanEventSink& sink, unsigned maxEvents) noexcept;

    diag::Error enqueue(const diag::CanFrame& frame) noexcept override;
    diag::Error setTransmitEnabled(bool enabled) noexcept override;
    gw::BusStatus status() const noexcept override;

private:
    struct Event {
        bool txDone;
        bool success;
        diag::CanFrame frame;
    };

    /// One borrowed TX buffer. `busy` is set by enqueue() (task) and cleared by the ISR.
    struct TxSlot {
        twai_frame_t frame{};
        std::uint8_t data[8]{};
        std::atomic<bool> busy{false};
    };

    static constexpr unsigned kTxSlots = 4;       ///< request + flow control + margin
    static constexpr unsigned kEventDepth = 32;   ///< > largest MVP response (FF + 11 CF)

    esp_err_t createNode(bool listenOnly) noexcept;
    esp_err_t destroyNode() noexcept;

    static bool onRxDone(twai_node_handle_t handle, const twai_rx_done_event_data_t* data, void* context);
    static bool onTxDone(twai_node_handle_t handle, const twai_tx_done_event_data_t* data, void* context);
    static bool onError(twai_node_handle_t handle, const twai_error_event_data_t* data, void* context);

    Config config_;
    twai_node_handle_t node_ = nullptr;
    bool listenOnly_ = true;

    std::array<TxSlot, kTxSlots> slots_{};

    StaticQueue_t queueControl_{};
    std::uint8_t queueStorage_[kEventDepth * sizeof(Event)]{};
    QueueHandle_t queue_ = nullptr;

    std::atomic<std::uint32_t> rxDropped_{0};
    std::atomic<std::uint32_t> busErrors_{0};
};
