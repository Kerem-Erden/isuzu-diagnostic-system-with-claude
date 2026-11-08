/**
 * @file app_main.cpp
 * @brief Firmware entry point: wires UART, CAN port and gw::Gateway, then runs the single
 *        cooperative application loop (all gateway calls happen in this task; see gw/ports.hpp).
 */

#include <cstdint>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "gw/gateway.hpp"
#include "sdkconfig.h"
#include "uart_transport.hpp"

#if CONFIG_ISUZU_GATEWAY_SIMULATION
#include "gw/simulated_ecu.hpp"
#else
#include "twai_adapter.hpp"
#endif
#if CONFIG_ISUZU_GATEWAY_BENCH
#include "bench_app.hpp"
#endif

namespace {

std::uint64_t nowUs() noexcept { return static_cast<std::uint64_t>(esp_timer_get_time()); }

/// First 8 hex digits of the application ELF SHA-256, reported as HELLO build=<hash>.
const char* buildHash() noexcept {
    static char hash[9] = {};
    if (hash[0] == '\0') esp_app_get_elf_sha256(hash, sizeof(hash));
    return hash;
}

gw::GatewayConfig makeConfig() noexcept {
    gw::GatewayConfig config;
#if CONFIG_ISUZU_GATEWAY_SIMULATION
    config.source = gw::DataSource::Simulation;
#else
    config.source = gw::DataSource::Vehicle;
#endif
    config.firmwareVersion = esp_app_get_description()->version;
    config.buildId = buildHash();
    config.bitrate = CONFIG_ISUZU_CAN_BITRATE;
    config.isotp.txId = CONFIG_ISUZU_ECU_REQUEST_ID;
    // ISO 15765-4: physical response identifier = request identifier + 8.
    config.isotp.rxId = CONFIG_ISUZU_ECU_REQUEST_ID + 8;
    return config;
}

UartTransport uart;

}  // namespace

extern "C" void app_main(void) {
    const esp_err_t uartResult = uart.begin();
    if (uartResult != ESP_OK) return;  // no channel to report anything; the desktop will time out

#if CONFIG_ISUZU_GATEWAY_BENCH
    // BENCH profile: no gateway, no OBD – only the self-test protocol (vehicle must be disconnected).
    runBenchApp(uart);
#endif

    static const gw::GatewayConfig config = makeConfig();

#if CONFIG_ISUZU_GATEWAY_SIMULATION
    // SIMULATION — NO VEHICLE DATA: TWAI is never initialised in this profile.
    static gw::SimulatedEcu ecu(config.isotp);
    static gw::Gateway gateway(config, ecu, uart);
    ecu.attach(gateway);
    gw::EcuScenario scenario = gw::EcuScenario::Normal;
    gw::scenarioFromString(CONFIG_ISUZU_SIM_SCENARIO, scenario);
    ecu.setScenario(scenario);
    gateway.start(nowUs());
#else
    TwaiAdapter::Config twaiConfig;
    twaiConfig.txGpio = CONFIG_ISUZU_TWAI_TX_GPIO;
    twaiConfig.rxGpio = CONFIG_ISUZU_TWAI_RX_GPIO;
    twaiConfig.bitrate = CONFIG_ISUZU_CAN_BITRATE;
    static TwaiAdapter twai(twaiConfig);
    static gw::Gateway gateway(config, twai, uart);
    const esp_err_t canResult = twai.begin();  // listen-only
    gateway.start(nowUs());
    if (canResult != ESP_OK) {
        // Keep running: STATUS shows bus=STOPPED and requests fail with CAN_ENQUEUE/NOT_STARTED.
        gateway.reportDiagnostic("can_init", "error", esp_err_to_name(canResult));
    }
#endif

    char buffer[64];
    for (;;) {
        // Blocking at most 1 ms keeps the loop responsive (FC after FF, 1 kHz tick) while still
        // yielding to the idle task so the task watchdog is fed.
        const std::size_t count = uart.read(buffer, sizeof(buffer), 1);
        if (count > 0) gateway.onSerialBytes(buffer, count, nowUs());
#if CONFIG_ISUZU_GATEWAY_SIMULATION
        ecu.poll(nowUs());
#else
        twai.drainEvents(gateway, 32);
#endif
        gateway.poll(nowUs());
    }
}
