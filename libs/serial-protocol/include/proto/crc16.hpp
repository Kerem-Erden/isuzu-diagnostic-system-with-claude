#pragma once
/**
 * @file crc16.hpp
 * @brief CRC-16/CCITT-FALSE used to protect serial frames (docs/serial-protocol.md §2).
 *
 * Parameters: width 16, polynomial 0x1021, init 0xFFFF, no input/output reflection, xorout 0.
 * Check value for the ASCII string "123456789" is 0x29B1 (verified in the unit tests).
 * A bitwise implementation is used instead of a 512-byte table: frames are ≤ 256 bytes at
 * 115200 baud, so speed is irrelevant and the code stays easy to read.
 */

#include <cstddef>
#include <cstdint>

namespace proto {

constexpr std::uint16_t crc16CcittFalse(const char* data, std::size_t length) noexcept {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(static_cast<unsigned char>(data[i])) << 8));
        for (int bit = 0; bit < 8; ++bit) {
            // Shift as unsigned: `crc << 1` alone promotes uint16_t to (signed) int first.
            const unsigned shifted = static_cast<unsigned>(crc) << 1;
            crc = static_cast<std::uint16_t>((crc & 0x8000u) != 0 ? (shifted ^ 0x1021u) : shifted);
        }
    }
    return crc;
}

}  // namespace proto
