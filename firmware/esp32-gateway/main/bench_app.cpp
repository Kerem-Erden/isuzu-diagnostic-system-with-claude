#include "bench_app.hpp"

#include <cstring>

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gw/bench.hpp"
#include "gw/ports.hpp"
#include "proto/message.hpp"
#include "sdkconfig.h"
#include "twai_adapter.hpp"

namespace {

using diag::Code;
using diag::Stage;

constexpr gpio_num_t kTx = static_cast<gpio_num_t>(CONFIG_ISUZU_TWAI_TX_GPIO);
constexpr gpio_num_t kRx = static_cast<gpio_num_t>(CONFIG_ISUZU_TWAI_RX_GPIO);
constexpr std::uint32_t kLoopbackId = 0x123;
constexpr std::uint8_t kLoopbackData[4] = {0xDE, 0xAD, 0xBE, 0xEF};

/// Step 1: CTX as plain GPIO (with pad read-back), CRX as input; 20 ms settle per level.
bool runGpioStep(gw::bench::Measurements& m) {
    if (gpio_reset_pin(kTx) != ESP_OK || gpio_reset_pin(kRx) != ESP_OK) return false;
    // Preload recessive before enabling the output so CTX never glitches dominant.
    if (gpio_set_level(kTx, 1) != ESP_OK) return false;
    gpio_config_t tx{};
    tx.pin_bit_mask = 1ULL << kTx;
    tx.mode = GPIO_MODE_INPUT_OUTPUT;  // input enabled so the pad level can be read back
    gpio_config_t rx{};
    rx.pin_bit_mask = 1ULL << kRx;
    rx.mode = GPIO_MODE_INPUT;
    if (gpio_config(&tx) != ESP_OK || gpio_config(&rx) != ESP_OK) return false;
    const std::uint8_t levels[3] = {1, 0, 1};
    for (int i = 0; i < 3; ++i) {
        gpio_set_level(kTx, levels[i]);
        vTaskDelay(pdMS_TO_TICKS(20));
        m.gpio[static_cast<std::size_t>(i)] = gw::bench::GpioSample{levels[i], static_cast<std::uint8_t>(gpio_get_level(kTx)),
                                                                    static_cast<std::uint8_t>(gpio_get_level(kRx))};
    }
    gpio_set_level(kTx, 1);  // leave recessive; the TWAI driver takes the pin over next
    return true;
}

/// Collects TWAI events during the loopback step.
struct LoopbackSink final : gw::CanEventSink {
    bool txDone = false;
    bool txSuccess = false;
    bool received = false;
    bool matches = false;
    void onTxComplete(bool success) override {
        txDone = true;
        txSuccess = success;
    }
    void onCanFrame(const diag::CanFrame& frame) override {
        received = true;
        matches = frame.id == kLoopbackId && !frame.extended && frame.dlc == sizeof(kLoopbackData) &&
                  std::memcmp(frame.data.data(), kLoopbackData, sizeof(kLoopbackData)) == 0;
    }
};

/// Step 2: TWAI self-test + loopback, one frame, 500 ms budget. The adapter (and the node) is
/// destroyed at the end of the scope, releasing the pins.
void runLoopbackStep(gw::bench::Measurements& m) {
    m.loopbackRan = true;
    TwaiAdapter::Config config;
    config.txGpio = CONFIG_ISUZU_TWAI_TX_GPIO;
    config.rxGpio = CONFIG_ISUZU_TWAI_RX_GPIO;
    config.bitrate = CONFIG_ISUZU_CAN_BITRATE;
    config.selfTest = true;
    TwaiAdapter adapter(config);
    if (adapter.begin() != ESP_OK) {
        m.loopbackError = diag::fail(Stage::CanEnqueue, Code::NotStarted);
        return;
    }
    diag::Error error = adapter.setTransmitEnabled(true);
    diag::CanFrame frame;
    frame.id = kLoopbackId;
    frame.dlc = sizeof(kLoopbackData);
    std::memcpy(frame.data.data(), kLoopbackData, sizeof(kLoopbackData));
    if (error.isOk()) error = adapter.enqueue(frame);
    if (!error.isOk()) {
        m.loopbackError = error;
        return;
    }
    LoopbackSink sink;
    const std::int64_t deadline = esp_timer_get_time() + 500000;
    while (esp_timer_get_time() < deadline && !(sink.txDone && sink.received)) {
        adapter.drainEvents(sink, 8);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (sink.txDone && !sink.txSuccess) m.loopbackError = diag::fail(Stage::CanTxCompletion, Code::TxFailed);
    else if (!sink.txDone) m.loopbackError = diag::fail(Stage::CanTxCompletion, Code::Timeout);
    else if (!sink.received) m.loopbackError = diag::fail(Stage::RxTimeout, Code::Timeout);
    else if (!sink.matches) m.loopbackError = diag::fail(Stage::Internal, Code::MalformedPayload);
    else m.loopbackOk = true;
}

}  // namespace

void runBenchApp(UartTransport& uart) {
    proto::FrameReader reader;
    proto::FrameWriter writer;
    std::uint32_t sequence = 0;
    const auto event = [&](std::string_view kind) -> proto::FrameWriter& {
        proto::beginEvent(writer, sequence++, kind);
        return writer;
    };
    uart.write(event("BOOT").keyValue("proto", proto::kProtocolVersion).keyValue("source", "BENCH").finish());

    char buffer[64];
    for (;;) {
        const std::size_t count = uart.read(buffer, sizeof(buffer), 10);
        reader.feed(buffer, count, [&](std::string_view body) {
            const diag::Result<proto::Message> parsed = proto::parseBody(body);
            if (!parsed.ok() || parsed.value().type != proto::MessageType::Request) return;
            const proto::Message& request = parsed.value();
            if (request.name == "HELLO") {
                uart.write(proto::beginOk(writer, request.id)
                               .keyValue("proto", proto::kProtocolVersion)
                               .keyValue("fw", esp_app_get_description()->version)
                               .keyValue("source", "BENCH")
                               .keyValue("bitrate", static_cast<std::uint32_t>(CONFIG_ISUZU_CAN_BITRATE))
                               .keyValue("ctx", static_cast<std::uint32_t>(CONFIG_ISUZU_TWAI_TX_GPIO))
                               .keyValue("crx", static_cast<std::uint32_t>(CONFIG_ISUZU_TWAI_RX_GPIO))
                               .keyValue("mode04", 0u)
                               .finish());
            } else if (request.name == "PING") {
                uart.write(proto::beginOk(writer, request.id).field("pong").finish());
            } else if (request.name == "BENCH_RUN") {
                gw::bench::Measurements m;
                m.gpioRan = runGpioStep(m);
                runLoopbackStep(m);
                const gw::bench::Verdict v = gw::bench::evaluate(m);
                uart.write(event("DIAG").field("bench_gpio").keyValue("hint", gw::bench::hint(v.gpio)).finish());
                uart.write(event("DIAG").field("bench_loopback").keyValue("hint", gw::bench::hint(v.loopback)).finish());
                proto::beginOk(writer, request.id);
                uart.write(gw::bench::appendResult(writer, m, v).finish());
            } else {
                uart.write(proto::beginError(writer, request.id, diag::fail(Stage::Protocol, Code::UnknownCommand)).finish());
            }
        });
    }
}
