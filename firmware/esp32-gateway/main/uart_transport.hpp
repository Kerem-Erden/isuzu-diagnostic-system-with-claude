#pragma once
/**
 * @file uart_transport.hpp
 * @brief UART0 (CP210x USB bridge) byte stream for protocol v1.
 *
 * UART0 is also the ESP-IDF console; that is acceptable because protocol frames are CRC-framed
 * and the desktop ignores everything else (ADR-004). Installing the driver switches console
 * output to the driver as well, so log lines and frames are serialised per write() call.
 */

#include <cstddef>
#include <string_view>

#include "esp_err.h"
#include "gw/ports.hpp"

class UartTransport final : public gw::SerialSink {
public:
    static constexpr int kBaudRate = 115200;

    /// Configures UART0 at 115200 8N1 and installs the driver with RX/TX ring buffers.
    esp_err_t begin() noexcept;
    /// Reads available bytes, waiting at most @p timeoutMs. @return number of bytes read (≥ 0).
    std::size_t read(char* buffer, std::size_t capacity, unsigned timeoutMs) noexcept;
    /// Queues one complete frame. Blocks only while the 1 KiB TX ring buffer is full.
    void write(std::string_view frame) noexcept override;
};
