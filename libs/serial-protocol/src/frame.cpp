#include "proto/frame.hpp"

#include <cmath>

#include "proto/crc16.hpp"

namespace proto {
namespace {

constexpr char kHex[] = "0123456789ABCDEF";

int hexDigit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

// ---------------------------------------------------------------- FrameReader

void FrameReader::reset() noexcept {
    state_ = State::Idle;
    used_ = 0;
    bodyLength_ = 0;
}

bool FrameReader::push(char c) noexcept {
    if (c == kStartMarker) {
        if (state_ == State::InFrame) ++counters_.malformed;  // previous frame never terminated
        state_ = State::InFrame;
        buffer_[0] = c;
        used_ = 1;
        return false;
    }
    switch (state_) {
        case State::Idle:
            ++counters_.noiseBytes;
            return false;
        case State::Discarding:
            if (c == '\n') state_ = State::Idle;
            return false;
        case State::InFrame:
            break;
    }
    if (c == '\r') return false;  // tolerate CRLF from serial monitors
    if (c == '\n') {
        state_ = State::Idle;
        return validate();
    }
    if (used_ >= kMaxFrameLength) {
        ++counters_.oversized;
        state_ = State::Discarding;  // drop through the newline so the tail is never executed
        return false;
    }
    buffer_[used_++] = c;
    return false;
}

bool FrameReader::validate() noexcept {
    // Minimum: '#', one body char, '*', 4 hex digits.
    if (used_ < kFrameOverhead + 1 || buffer_[used_ - 5] != kCrcMarker) {
        ++counters_.malformed;
        return false;
    }
    unsigned received = 0;
    for (std::size_t i = used_ - 4; i < used_; ++i) {
        const int digit = hexDigit(buffer_[i]);
        if (digit < 0) {
            ++counters_.malformed;
            return false;
        }
        received = (received << 4) | static_cast<unsigned>(digit);
    }
    bodyLength_ = used_ - kFrameOverhead;
    if (crc16CcittFalse(buffer_ + 1, bodyLength_) != received) {
        ++counters_.badChecksum;
        return false;
    }
    ++counters_.frames;
    return true;
}

// ---------------------------------------------------------------- FrameWriter

void FrameWriter::reset() noexcept {
    buffer_[0] = kStartMarker;
    used_ = 1;
    poisoned_ = false;
    firstField_ = true;
}

void FrameWriter::put(char c) noexcept {
    // Reserve room for "*XXXX" so finish() can never overflow.
    if (poisoned_ || used_ + 1 > kMaxFrameLength - 5) {
        poisoned_ = true;
        return;
    }
    buffer_[used_++] = c;
}

void FrameWriter::separator() noexcept {
    if (!firstField_) put(kFieldSeparator);
    firstField_ = false;
}

FrameWriter& FrameWriter::beginField() noexcept {
    separator();
    return *this;
}

FrameWriter& FrameWriter::append(std::string_view text) noexcept {
    for (char c : text) {
        if (!isFieldChar(c)) poisoned_ = true;
        put(c);
    }
    return *this;
}

FrameWriter& FrameWriter::field(std::string_view text) noexcept {
    if (text.empty()) poisoned_ = true;
    separator();
    return append(text);
}

void FrameWriter::putDecimal(std::uint64_t value) noexcept {
    char digits[20];
    std::size_t n = 0;
    do {
        digits[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (n > 0) put(digits[--n]);
}

void FrameWriter::putFixed2(double value) noexcept {
    // Avoid printf("%f"): it is locale-dependent on the desktop and costly on the MCU.
    if (!std::isfinite(value) || std::fabs(value) > 1.0e9) {
        poisoned_ = true;
        return;
    }
    if (value < 0) {
        put('-');
        value = -value;
    }
    const auto hundredths = static_cast<std::uint64_t>(std::llround(value * 100.0));
    putDecimal(hundredths / 100);
    const auto fraction = static_cast<unsigned>(hundredths % 100);
    put('.');
    put(static_cast<char>('0' + fraction / 10));
    put(static_cast<char>('0' + fraction % 10));
}

FrameWriter& FrameWriter::field(std::uint32_t value) noexcept {
    separator();
    putDecimal(value);
    return *this;
}

FrameWriter& FrameWriter::keyValue(std::string_view key, std::string_view value) noexcept {
    if (key.empty()) poisoned_ = true;
    separator();
    append(key);
    put('=');
    return append(value);
}

FrameWriter& FrameWriter::keyValue(std::string_view key, std::uint32_t value) noexcept {
    if (key.empty()) poisoned_ = true;
    separator();
    append(key);
    put('=');
    putDecimal(value);
    return *this;
}

FrameWriter& FrameWriter::keyHex(std::string_view key, std::uint32_t value, unsigned digits) noexcept {
    if (key.empty() || digits == 0 || digits > 8) poisoned_ = true;
    separator();
    append(key);
    put('=');
    for (unsigned i = digits; i > 0; --i) put(kHex[(value >> ((i - 1) * 4)) & 0x0F]);
    return *this;
}

FrameWriter& FrameWriter::fixed2(double value) noexcept {
    separator();
    putFixed2(value);
    return *this;
}

FrameWriter& FrameWriter::keyFixed2(std::string_view key, double value) noexcept {
    if (key.empty()) poisoned_ = true;
    separator();
    append(key);
    put('=');
    putFixed2(value);
    return *this;
}

std::string_view FrameWriter::finish() noexcept {
    if (poisoned_ || used_ < 2) return {};
    const std::uint16_t crc = crc16CcittFalse(buffer_ + 1, used_ - 1);
    buffer_[used_++] = kCrcMarker;
    for (int shift = 12; shift >= 0; shift -= 4) buffer_[used_++] = kHex[(crc >> shift) & 0x0F];
    buffer_[used_++] = '\n';
    return std::string_view(buffer_, used_);
}

}  // namespace proto
