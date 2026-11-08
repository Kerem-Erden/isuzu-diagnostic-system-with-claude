#include "twai_adapter.hpp"

#include <cstring>

#include "esp_timer.h"

namespace {

using diag::Code;
using diag::Stage;

gw::BusState toBusState(twai_error_state_t state) noexcept {
    switch (state) {
        case TWAI_ERROR_ACTIVE: return gw::BusState::Active;
        case TWAI_ERROR_WARNING: return gw::BusState::Warning;
        case TWAI_ERROR_PASSIVE: return gw::BusState::Passive;
        case TWAI_ERROR_BUS_OFF: return gw::BusState::BusOff;
        default: return gw::BusState::Stopped;
    }
}

}  // namespace

TwaiAdapter::~TwaiAdapter() {
    destroyNode();
    if (queue_ != nullptr) vQueueDelete(queue_);
}

esp_err_t TwaiAdapter::begin() noexcept {
    if (queue_ == nullptr) {
        // Static storage: no heap allocation, the queue cannot fail at runtime for lack of memory.
        queue_ = xQueueCreateStatic(kEventDepth, sizeof(Event), queueStorage_, &queueControl_);
    }
    return createNode(true);
}

esp_err_t TwaiAdapter::createNode(bool listenOnly) noexcept {
    twai_onchip_node_config_t config{};
    config.io_cfg.tx = static_cast<gpio_num_t>(config_.txGpio);
    config.io_cfg.rx = static_cast<gpio_num_t>(config_.rxGpio);
    config.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    config.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    config.bit_timing.bitrate = config_.bitrate;
    config.tx_queue_depth = kTxSlots;
    // 0 = no hardware retransmission. A frame that loses ACK is reported as failed exactly once;
    // automatic retries would add bus load and raise TEC repeatedly (reference RK-03).
    config.fail_retry_cnt = 0;
    config.flags.enable_listen_only = listenOnly ? 1 : 0;

    esp_err_t result = twai_new_node_onchip(&config, &node_);
    if (result != ESP_OK) {
        node_ = nullptr;
        return result;
    }
    twai_event_callbacks_t callbacks{};
    callbacks.on_rx_done = &TwaiAdapter::onRxDone;
    callbacks.on_tx_done = &TwaiAdapter::onTxDone;
    callbacks.on_error = &TwaiAdapter::onError;
    result = twai_node_register_event_callbacks(node_, &callbacks, this);
    if (result == ESP_OK) result = twai_node_enable(node_);
    if (result != ESP_OK) {
        destroyNode();
        return result;
    }
    listenOnly_ = listenOnly;
    return ESP_OK;
}

esp_err_t TwaiAdapter::destroyNode() noexcept {
    if (node_ == nullptr) return ESP_OK;
    // ESP-IDF 6.0.2 note from the reference firmware: disable() may report INVALID_STATE while
    // bus-off; deletion is still valid, so its result is not treated as fatal here.
    twai_node_disable(node_);
    const esp_err_t result = twai_node_delete(node_);
    if (result != ESP_OK) return result;  // keep slots reserved: the driver may still own them
    node_ = nullptr;
    // Only node deletion guarantees the driver released every borrowed TX buffer.
    for (TxSlot& slot : slots_) slot.busy.store(false, std::memory_order_release);
    return ESP_OK;
}

diag::Error TwaiAdapter::setTransmitEnabled(bool enabled) noexcept {
    if (node_ != nullptr && listenOnly_ == !enabled) return diag::Error{};
    // Listen-only is a construction-time flag of the on-chip driver, so switching modes means
    // recreating the node. This happens only at session/request boundaries, not per frame.
    esp_err_t result = destroyNode();
    if (result == ESP_OK) result = createNode(!enabled);
    if (result != ESP_OK) return diag::fail(Stage::CanBus, Code::InvalidState, static_cast<std::uint32_t>(result));
    return diag::Error{};
}

diag::Error TwaiAdapter::enqueue(const diag::CanFrame& frame) noexcept {
    if (node_ == nullptr) return diag::fail(Stage::CanEnqueue, Code::NotStarted);
    if (listenOnly_) return diag::fail(Stage::CanEnqueue, Code::InvalidState);
    if (!diag::isValidClassicFrame(frame)) return diag::fail(Stage::CanEnqueue, Code::InvalidArgument);

    for (TxSlot& slot : slots_) {
        bool expected = false;
        if (!slot.busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) continue;
        std::memcpy(slot.data, frame.data.data(), frame.dlc);
        slot.frame = twai_frame_t{};
        slot.frame.header.id = frame.id;
        slot.frame.header.ide = frame.extended ? 1 : 0;
        slot.frame.header.dlc = frame.dlc;
        slot.frame.buffer = slot.data;
        slot.frame.buffer_len = frame.dlc;
        // timeout 0: never block the application task waiting for queue space.
        const esp_err_t result = twai_node_transmit(node_, &slot.frame, 0);
        if (result != ESP_OK) {
            slot.busy.store(false, std::memory_order_release);
            const Code code = result == ESP_ERR_TIMEOUT ? Code::QueueFull : Code::InvalidState;
            return diag::fail(Stage::CanEnqueue, code, static_cast<std::uint32_t>(result));
        }
        return diag::Error{};
    }
    return diag::fail(Stage::CanEnqueue, Code::QueueFull);
}

gw::BusStatus TwaiAdapter::status() const noexcept {
    gw::BusStatus status;
    status.rxDropped = rxDropped_.load(std::memory_order_relaxed);
    status.busErrors = busErrors_.load(std::memory_order_relaxed);
    if (node_ == nullptr) return status;  // Stopped
    twai_node_status_t info{};
    if (twai_node_get_info(node_, &info, nullptr) != ESP_OK) return status;
    status.state = toBusState(info.state);
    if (listenOnly_ && status.state == gw::BusState::Active) status.state = gw::BusState::ListenOnly;
    status.tec = info.tx_error_count;
    status.rec = info.rx_error_count;
    return status;
}

void TwaiAdapter::drainEvents(gw::CanEventSink& sink, unsigned maxEvents) noexcept {
    if (queue_ == nullptr) return;
    Event event;
    for (unsigned i = 0; i < maxEvents && xQueueReceive(queue_, &event, 0) == pdTRUE; ++i) {
        if (event.txDone) {
            sink.onTxComplete(event.success);
        } else {
            sink.onCanFrame(event.frame);
        }
    }
}

// ----------------------------------------------------------------- ISR context

bool TwaiAdapter::onRxDone(twai_node_handle_t handle, const twai_rx_done_event_data_t*, void* context) {
    auto* self = static_cast<TwaiAdapter*>(context);
    std::uint8_t buffer[8] = {};
    twai_frame_t rx{};
    rx.buffer = buffer;
    rx.buffer_len = sizeof(buffer);
    if (twai_node_receive_from_isr(handle, &rx) != ESP_OK) return false;
    if (rx.header.fdf || rx.header.rtr || rx.header.dlc > 8) return false;  // classic data frames only

    Event event{};
    event.txDone = false;
    event.frame.id = rx.header.id;
    event.frame.extended = rx.header.ide != 0;
    event.frame.dlc = static_cast<std::uint8_t>(rx.header.dlc);
    std::memcpy(event.frame.data.data(), buffer, event.frame.dlc);
    // Timestamp in the ISR so stale-frame filtering uses arrival time, not processing time.
    event.frame.timestampUs = static_cast<std::uint64_t>(esp_timer_get_time());

    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(self->queue_, &event, &woken) != pdTRUE) {
        self->rxDropped_.fetch_add(1, std::memory_order_relaxed);  // reported in STATUS rxdrop=
    }
    return woken == pdTRUE;
}

bool TwaiAdapter::onTxDone(twai_node_handle_t, const twai_tx_done_event_data_t* data, void* context) {
    auto* self = static_cast<TwaiAdapter*>(context);
    for (TxSlot& slot : self->slots_) {
        if (&slot.frame == data->done_tx_frame) {
            slot.busy.store(false, std::memory_order_release);  // driver no longer reads it
            break;
        }
    }
    Event event{};
    event.txDone = true;
    event.success = data->is_tx_success;
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(self->queue_, &event, &woken) != pdTRUE) {
        // A lost completion makes the transaction time out with Stage::CanTxCompletion.
        self->rxDropped_.fetch_add(1, std::memory_order_relaxed);
    }
    return woken == pdTRUE;
}

bool TwaiAdapter::onError(twai_node_handle_t, const twai_error_event_data_t*, void* context) {
    static_cast<TwaiAdapter*>(context)->busErrors_.fetch_add(1, std::memory_order_relaxed);
    return false;
}
