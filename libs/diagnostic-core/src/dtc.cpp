#include "diag/dtc.hpp"

namespace diag {
namespace {

constexpr char kSystems[4] = {'P', 'C', 'B', 'U'};
constexpr char kHex[] = "0123456789ABCDEF";

int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;  // lower-case is rejected on purpose: the wire format is upper-case only
}

}  // namespace

void Dtc::toText(char (&out)[kDtcTextLength + 1]) const noexcept {
    out[0] = kSystems[high >> 6];
    out[1] = kHex[(high >> 4) & 0x03];
    out[2] = kHex[high & 0x0F];
    out[3] = kHex[low >> 4];
    out[4] = kHex[low & 0x0F];
    out[5] = '\0';
}

bool Dtc::fromText(const char* text, Dtc& out) noexcept {
    if (text == nullptr) return false;
    int system = -1;
    for (int i = 0; i < 4; ++i) {
        if (text[0] == kSystems[i]) system = i;
    }
    if (system < 0) return false;
    int digits[4];
    for (int i = 0; i < 4; ++i) {
        digits[i] = hexValue(text[i + 1]);
        if (digits[i] < 0) return false;
    }
    // The first digit has only two bits (0..3); "P4xxx" cannot be encoded.
    if (digits[0] > 3 || text[5] != '\0') return false;
    out.high = static_cast<std::uint8_t>((system << 6) | (digits[0] << 4) | digits[1]);
    out.low = static_cast<std::uint8_t>((digits[2] << 4) | digits[3]);
    return true;
}

}  // namespace diag
