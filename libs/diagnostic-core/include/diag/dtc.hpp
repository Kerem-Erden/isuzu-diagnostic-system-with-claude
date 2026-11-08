#pragma once
/**
 * @file dtc.hpp
 * @brief Diagnostic Trouble Code value type (SAE J1979 / ISO 15031-6 two-byte encoding).
 *
 * A DTC is transmitted as two bytes A B:
 * @code
 *   A bit 7..6 : system letter  00=P powertrain, 01=C chassis, 10=B body, 11=U network
 *   A bit 5..4 : first digit    0..3
 *   A bit 3..0 : second digit   0..F (hex)
 *   B bit 7..4 : third digit    0..F
 *   B bit 3..0 : fourth digit   0..F
 * @endcode
 * Example: 0x01 0x0A → "P010A", 0xC1 0x23 → "U0123".
 */

#include <array>
#include <cstddef>
#include <cstdint>

namespace diag {

/// Text length of a DTC without terminator ("P0087").
constexpr std::size_t kDtcTextLength = 5;

struct Dtc {
    std::uint8_t high = 0;
    std::uint8_t low = 0;

    /// 0x0000 is not a valid DTC (it is used as padding by some non-CAN protocols).
    constexpr bool isNull() const noexcept { return high == 0 && low == 0; }
    /// Writes 5 characters plus NUL into @p out ("P0087").
    void toText(char (&out)[kDtcTextLength + 1]) const noexcept;
    /// Parses "P0087" (exactly 5 chars, upper-case hex digits). @return false on bad input.
    static bool fromText(const char* text, Dtc& out) noexcept;

    friend constexpr bool operator==(const Dtc& a, const Dtc& b) noexcept {
        return a.high == b.high && a.low == b.low;
    }
};

/**
 * @brief Fixed-capacity list of DTCs (no heap).
 *
 * Capacity 127: the ISO-TP receive buffer is 256 bytes, a Mode 03 payload is
 * `43 NN (A B)*NN` → (256 - 2) / 2 = 127 codes.
 */
struct DtcList {
    static constexpr std::size_t kCapacity = 127;
    std::array<Dtc, kCapacity> items{};
    std::size_t count = 0;

    bool push(const Dtc& dtc) noexcept {
        if (count >= kCapacity) return false;
        items[count++] = dtc;
        return true;
    }
};

}  // namespace diag
