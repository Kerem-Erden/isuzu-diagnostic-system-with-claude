#pragma once
/**
 * @file message.hpp
 * @brief Parsed view of a protocol v1 frame body (REQ / RES / EVT) and encoding helpers.
 *
 * A Message references the body it was parsed from (std::string_view, no copies); it must not
 * outlive that buffer. On the desktop the Qt layer copies what it needs into QString.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "diag/error.hpp"
#include "proto/frame.hpp"

namespace proto {

enum class MessageType : std::uint8_t { Request, Response, Event };

/// Highest request id / event sequence (fits a signed 32-bit int on both sides).
constexpr std::uint32_t kMaxId = 2147483647u;
/// Maximum fields after the fixed header fields.
constexpr std::size_t kMaxArguments = 16;

struct Message {
    MessageType type = MessageType::Request;
    /// REQ/RES: request id (1..kMaxId). EVT: sequence number (0..kMaxId).
    std::uint32_t id = 0;
    /// REQ: command. RES: "OK" or "ERR". EVT: event kind.
    std::string_view name;
    /// RES|ERR only: decoded error. `detail` is parsed from hex.
    diag::Error error{};
    /// Remaining fields (REQ arguments, RES OK key=values, EVT fields).
    std::array<std::string_view, kMaxArguments> args{};
    std::size_t argCount = 0;

    bool isOk() const noexcept { return type == MessageType::Response && name == "OK"; }
    /// Value of the first `key=value` argument, or false when absent.
    bool find(std::string_view key, std::string_view& value) const noexcept;
};

/**
 * @brief Parses a validated frame body.
 * @return Message, or Stage::Protocol with MalformedPayload / InvalidArgument / UnknownCommand.
 */
diag::Result<Message> parseBody(std::string_view body) noexcept;

/// Parses an unsigned decimal without sign, spaces or leading '+'. Rejects overflow > @p max.
bool parseDecimal(std::string_view text, std::uint32_t max, std::uint32_t& out) noexcept;
/// Parses 1..8 upper- or lower-case hex digits (no prefix).
bool parseHex(std::string_view text, std::uint32_t& out) noexcept;

/// Starts `#REQ|<id>|<command>`.
FrameWriter& beginRequest(FrameWriter& writer, std::uint32_t id, std::string_view command) noexcept;
/// Starts `#RES|<id>|OK`.
FrameWriter& beginOk(FrameWriter& writer, std::uint32_t id) noexcept;
/// Complete `#RES|<id>|ERR|<STAGE>|<CODE>|<detail hex>` (not finished – call finish()).
FrameWriter& beginError(FrameWriter& writer, std::uint32_t id, const diag::Error& error) noexcept;
/// Appends `<STAGE>|<CODE>|<detail hex>` (used by RES ERR and LIVE_ERR events).
FrameWriter& appendError(FrameWriter& writer, const diag::Error& error) noexcept;
/// Starts `#EVT|<seq>|<kind>`.
FrameWriter& beginEvent(FrameWriter& writer, std::uint32_t sequence, std::string_view kind) noexcept;

}  // namespace proto
