#pragma once
/**
 * @file frame.hpp
 * @brief Byte-stream framing for protocol v1: `#<body>*<CRC16 hex>\n`.
 *
 * FrameReader turns an arbitrary, possibly fragmented and noisy byte stream (UART0 also carries
 * ESP-IDF boot logs) into validated frame bodies. FrameWriter builds outgoing frames into a
 * caller-owned fixed buffer. Neither allocates memory.
 */

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace proto {

/// Protocol version announced in HELLO / BOOT. Bump on any incompatible change.
constexpr unsigned kProtocolVersion = 1;
/// Maximum frame size from '#' to the last CRC digit (excluding the line terminator).
constexpr std::size_t kMaxFrameLength = 256;
/// '#' + '*' + 4 CRC digits.
constexpr std::size_t kFrameOverhead = 6;
constexpr std::size_t kMaxBodyLength = kMaxFrameLength - kFrameOverhead;

constexpr char kStartMarker = '#';
constexpr char kCrcMarker = '*';
constexpr char kFieldSeparator = '|';

/// @return true for a byte allowed inside a field (printable ASCII except the three markers).
constexpr bool isFieldChar(char c) noexcept {
    return c >= 0x20 && c <= 0x7E && c != kStartMarker && c != kCrcMarker && c != kFieldSeparator;
}

/**
 * @brief Incremental frame reader with resynchronisation.
 *
 * Rules (mirrored by the unit tests):
 *  - bytes outside a frame are noise and ignored;
 *  - '#' always starts a new frame; a frame interrupted by another '#' is counted malformed;
 *  - a frame longer than kMaxFrameLength is discarded up to the next '\n';
 *  - on '\n' the CRC is verified; only valid bodies reach the callback.
 *
 * The body passed to the callback points into the reader's buffer and is only valid during the
 * callback. Not thread-safe.
 */
class FrameReader {
public:
    struct Counters {
        std::uint32_t frames = 0;      ///< valid frames delivered
        std::uint32_t badChecksum = 0;
        std::uint32_t malformed = 0;   ///< missing/invalid CRC field or interrupted frame
        std::uint32_t oversized = 0;
        std::uint32_t noiseBytes = 0;  ///< bytes outside frames (boot log, garbage)
    };

    /**
     * @brief Feeds bytes; calls `onBody(std::string_view body)` for every valid frame.
     * @tparam Callback any callable taking std::string_view.
     */
    template <typename Callback>
    void feed(const char* data, std::size_t length, Callback&& onBody) {
        for (std::size_t i = 0; i < length; ++i) {
            if (push(data[i])) {
                onBody(std::string_view(buffer_ + 1, bodyLength_));
            }
        }
    }

    /// Drops any partial frame (e.g. after reconnect). Counters are kept.
    void reset() noexcept;
    const Counters& counters() const noexcept { return counters_; }

private:
    enum class State : std::uint8_t { Idle, InFrame, Discarding };

    /// @return true when a complete valid frame is in buffer_ (body length in bodyLength_).
    bool push(char c) noexcept;
    bool validate() noexcept;

    char buffer_[kMaxFrameLength]{};
    std::size_t used_ = 0;
    std::size_t bodyLength_ = 0;
    State state_ = State::Idle;
    Counters counters_{};
};

/**
 * @brief Builds one outgoing frame in a fixed buffer.
 *
 * Usage: `FrameWriter w; w.field("RES"); w.field(17u); w.field("OK"); auto f = w.finish();`
 * Any invalid character or overflow makes the writer *poisoned*: finish() then returns an empty
 * view, so a truncated or corrupted frame can never be sent.
 */
class FrameWriter {
public:
    FrameWriter() noexcept { reset(); }
    void reset() noexcept;

    /// Appends a text field (validated with isFieldChar). Empty fields are rejected.
    FrameWriter& field(std::string_view text) noexcept;
    /// Appends an unsigned decimal field.
    FrameWriter& field(std::uint32_t value) noexcept;
    /// Appends `key=value` with a text value; the value may be empty ("dtcs=").
    FrameWriter& keyValue(std::string_view key, std::string_view value) noexcept;
    /// Appends `key=<decimal>`.
    FrameWriter& keyValue(std::string_view key, std::uint32_t value) noexcept;
    /// Appends `key=<upper-case hex>` without prefix, padded to @p digits.
    FrameWriter& keyHex(std::string_view key, std::uint32_t value, unsigned digits) noexcept;
    /// Appends a fixed-point decimal field with 2 fractional digits (no locale, no printf).
    FrameWriter& fixed2(double value) noexcept;
    /// Appends `key=<fixed2 value>`.
    FrameWriter& keyFixed2(std::string_view key, double value) noexcept;

    /// Starts building an additional field character by character (for lists like "P0087,P1093").
    FrameWriter& beginField() noexcept;
    FrameWriter& append(std::string_view text) noexcept;

    /// Appends "*CRC\n". @return the complete frame or an empty view if the writer is poisoned.
    std::string_view finish() noexcept;
    bool ok() const noexcept { return !poisoned_; }

private:
    void put(char c) noexcept;
    void separator() noexcept;
    void putFixed2(double value) noexcept;
    void putDecimal(std::uint64_t value) noexcept;

    char buffer_[kMaxFrameLength + 2]{};  // + '\n' and safety byte
    std::size_t used_ = 0;
    bool poisoned_ = false;
    bool firstField_ = true;
};

}  // namespace proto
