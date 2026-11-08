#include "proto/message.hpp"

namespace proto {
namespace {

using diag::Code;
using diag::Stage;

diag::Error malformed(std::uint32_t detail = 0) noexcept {
    return diag::fail(Stage::Protocol, Code::MalformedPayload, detail);
}

/// Splits @p body at '|'. Empty fields are rejected (they would hide a missing value).
bool split(std::string_view body, std::array<std::string_view, kMaxArguments + 6>& fields,
           std::size_t& count) noexcept {
    count = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= body.size(); ++i) {
        if (i == body.size() || body[i] == kFieldSeparator) {
            if (i == start || count == fields.size()) return false;
            fields[count++] = body.substr(start, i - start);
            start = i + 1;
        } else if (!isFieldChar(body[i])) {
            return false;
        }
    }
    return true;
}

bool nullTerminatedCopy(std::string_view text, char (&out)[32]) noexcept {
    if (text.size() >= sizeof(out)) return false;
    for (std::size_t i = 0; i < text.size(); ++i) out[i] = text[i];
    out[text.size()] = '\0';
    return true;
}

}  // namespace

bool parseDecimal(std::string_view text, std::uint32_t max, std::uint32_t& out) noexcept {
    if (text.empty() || text.size() > 10) return false;
    std::uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value > max) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool parseHex(std::string_view text, std::uint32_t& out) noexcept {
    if (text.empty() || text.size() > 8) return false;
    std::uint32_t value = 0;
    for (char c : text) {
        unsigned digit;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
        else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
        else return false;
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

bool Message::find(std::string_view key, std::string_view& value) const noexcept {
    for (std::size_t i = 0; i < argCount; ++i) {
        const std::string_view arg = args[i];
        if (arg.size() > key.size() && arg.substr(0, key.size()) == key && arg[key.size()] == '=') {
            value = arg.substr(key.size() + 1);
            return true;
        }
    }
    return false;
}

diag::Result<Message> parseBody(std::string_view body) noexcept {
    std::array<std::string_view, kMaxArguments + 6> fields{};
    std::size_t count = 0;
    if (!split(body, fields, count) || count < 3) return malformed(static_cast<std::uint32_t>(count));

    Message message;
    std::size_t next = 3;
    if (fields[0] == "REQ") {
        message.type = MessageType::Request;
        if (!parseDecimal(fields[1], kMaxId, message.id) || message.id == 0) return malformed();
    } else if (fields[0] == "RES") {
        message.type = MessageType::Response;
        if (!parseDecimal(fields[1], kMaxId, message.id) || message.id == 0) return malformed();
        if (fields[2] == "ERR") {
            // ERR|<STAGE>|<CODE>|<detail hex> – all three are mandatory so errors stay structured.
            if (count < 6) return malformed(static_cast<std::uint32_t>(count));
            char text[32];
            if (!nullTerminatedCopy(fields[3], text) || !diag::stageFromString(text, message.error.stage)) {
                return diag::fail(Stage::Protocol, Code::InvalidArgument, 3);
            }
            if (!nullTerminatedCopy(fields[4], text) || !diag::codeFromString(text, message.error.code) ||
                message.error.code == Code::Ok) {
                return diag::fail(Stage::Protocol, Code::InvalidArgument, 4);
            }
            if (!parseHex(fields[5], message.error.detail)) return diag::fail(Stage::Protocol, Code::InvalidArgument, 5);
            next = 6;
        } else if (fields[2] != "OK") {
            return malformed();
        }
    } else if (fields[0] == "EVT") {
        message.type = MessageType::Event;
        if (!parseDecimal(fields[1], kMaxId, message.id)) return malformed();
    } else {
        return diag::fail(Stage::Protocol, Code::UnknownCommand);
    }
    message.name = fields[2];
    for (std::size_t i = next; i < count; ++i) {
        if (message.argCount == kMaxArguments) return malformed(static_cast<std::uint32_t>(count));
        message.args[message.argCount++] = fields[i];
    }
    return message;
}

FrameWriter& beginRequest(FrameWriter& writer, std::uint32_t id, std::string_view command) noexcept {
    writer.reset();
    return writer.field("REQ").field(id).field(command);
}

FrameWriter& beginOk(FrameWriter& writer, std::uint32_t id) noexcept {
    writer.reset();
    return writer.field("RES").field(id).field("OK");
}

FrameWriter& beginError(FrameWriter& writer, std::uint32_t id, const diag::Error& error) noexcept {
    writer.reset();
    writer.field("RES").field(id).field("ERR").field(diag::toString(error.stage)).field(diag::toString(error.code));
    // detail as whole bytes of hex (NRC "22", sequence "0203"): byte-oriented values read naturally.
    unsigned digits = 2;
    while (digits < 8 && (error.detail >> (digits * 4)) != 0) digits += 2;
    static constexpr char kHex[] = "0123456789ABCDEF";
    char text[8];
    for (unsigned i = 0; i < digits; ++i) text[i] = kHex[(error.detail >> ((digits - 1 - i) * 4)) & 0x0F];
    return writer.beginField().append(std::string_view(text, digits));
}

FrameWriter& beginEvent(FrameWriter& writer, std::uint32_t sequence, std::string_view kind) noexcept {
    writer.reset();
    return writer.field("EVT").field(sequence).field(kind);
}

}  // namespace proto
