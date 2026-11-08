#pragma once
/**
 * @file can_frame.hpp
 * @brief Hardware-independent Classical CAN frame model.
 *
 * The TWAI driver (firmware) and the simulated ECU (tests/desktop) both convert to and from
 * this struct, so ISO-TP and OBD code never touches a driver type.
 */

#include <array>
#include <cstdint>

namespace diag {

/// Largest 11-bit (standard) identifier.
constexpr std::uint32_t kMaxStandardId = 0x7FF;
/// Largest 29-bit (extended) identifier.
constexpr std::uint32_t kMaxExtendedId = 0x1FFFFFFF;
/// Classical CAN carries at most 8 data bytes (CAN FD is out of scope).
constexpr std::uint8_t kClassicMaxDlc = 8;

struct CanFrame {
    std::uint32_t id = 0;
    bool extended = false;  ///< 29-bit identifier when true (not used by the MVP).
    std::uint8_t dlc = 0;   ///< Number of valid bytes in @c data (0..8). Remote frames unsupported.
    std::array<std::uint8_t, kClassicMaxDlc> data{};
    /// Monotonic receive/transmit time in microseconds. Used to reject frames that were
    /// queued *before* a request was sent (stale replies from a previous transaction).
    std::uint64_t timestampUs = 0;
};

/// @return true if id/extended/dlc describe a frame Classical CAN can carry.
constexpr bool isValidClassicFrame(const CanFrame& frame) noexcept {
    return frame.dlc <= kClassicMaxDlc && frame.id <= (frame.extended ? kMaxExtendedId : kMaxStandardId);
}

}  // namespace diag
