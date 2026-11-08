#include "uart_transport.hpp"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

namespace {
constexpr uart_port_t kPort = UART_NUM_0;
constexpr int kRxBuffer = 1024;  // > 4 maximum-size frames
constexpr int kTxBuffer = 1024;  // live events are ~45 bytes; a DTC_PART frame ≤ 257 bytes
}  // namespace

esp_err_t UartTransport::begin() noexcept {
    uart_config_t config{};
    config.baud_rate = kBaudRate;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;  // CP210x RTS/CTS lines are not wired
    config.source_clk = UART_SCLK_DEFAULT;
    esp_err_t result = uart_param_config(kPort, &config);
    if (result == ESP_OK) {
        result = uart_driver_install(kPort, kRxBuffer, kTxBuffer, 0, nullptr, 0);
    }
    return result;
}

std::size_t UartTransport::read(char* buffer, std::size_t capacity, unsigned timeoutMs) noexcept {
    const int count = uart_read_bytes(kPort, buffer, static_cast<std::uint32_t>(capacity), pdMS_TO_TICKS(timeoutMs));
    return count > 0 ? static_cast<std::size_t>(count) : 0;
}

void UartTransport::write(std::string_view frame) noexcept {
    uart_write_bytes(kPort, frame.data(), frame.size());
}
