#include <string>

#include "proto/message.hpp"
#include "test_harness.hpp"

using namespace proto;
using diag::Code;
using diag::Stage;

namespace {
std::string bodyOf(std::string_view frame) {
    // strip '#' and "*CRC\n"
    return std::string(frame.substr(1, frame.size() - 1 - 6));
}
}  // namespace

TEST_CASE(message_parses_request_with_arguments) {
    auto parsed = parseBody("REQ|17|READ_PID|0C");
    REQUIRE(parsed.ok());
    CHECK(parsed.value().type == MessageType::Request);
    CHECK_EQ(parsed.value().id, 17u);
    CHECK(parsed.value().name == "READ_PID");
    CHECK_EQ(parsed.value().argCount, 1u);
    CHECK(parsed.value().args[0] == "0C");
}

TEST_CASE(message_parses_ok_response_key_values) {
    auto parsed = parseBody("RES|41|OK|count=2|dtcs=P0087,P1093|ecu=7E8");
    REQUIRE(parsed.ok());
    CHECK(parsed.value().isOk());
    std::string_view value;
    CHECK(parsed.value().find("dtcs", value) && value == "P0087,P1093");
    CHECK(parsed.value().find("count", value) && value == "2");
    CHECK(!parsed.value().find("dtc", value));  // prefix of a key is not a match
}

TEST_CASE(message_parses_structured_error) {
    auto parsed = parseBody("RES|42|ERR|NRC|NEGATIVE_RESPONSE|22");
    REQUIRE(parsed.ok());
    CHECK(!parsed.value().isOk());
    CHECK(parsed.value().error == diag::fail(Stage::NegativeResponse, Code::NegativeResponse, 0x22));
}

TEST_CASE(message_error_round_trip_through_writer) {
    FrameWriter writer;
    beginError(writer, 7, diag::fail(Stage::IsoTp, Code::BadSequence, 0x0203));
    const std::string body = bodyOf(writer.finish());
    CHECK(body == "RES|7|ERR|ISOTP|BAD_SEQUENCE|0203");
    auto parsed = parseBody(body);
    REQUIRE(parsed.ok());
    CHECK(parsed.value().error == diag::fail(Stage::IsoTp, Code::BadSequence, 0x0203));
}

TEST_CASE(message_parses_event_with_zero_sequence) {
    auto parsed = parseBody("EVT|0|BOOT|proto=1|source=SIMULATION");
    REQUIRE(parsed.ok());
    CHECK(parsed.value().type == MessageType::Event);
    CHECK(parsed.value().name == "BOOT");
}

TEST_CASE(message_rejects_malformed_bodies) {
    CHECK(!parseBody("REQ|0|PING").ok());                       // id 0 reserved
    CHECK(!parseBody("REQ|-1|PING").ok());
    CHECK(!parseBody("REQ|2147483648|PING").ok());              // > INT32_MAX
    CHECK(!parseBody("REQ|99999999999|PING").ok());
    CHECK(!parseBody("REQ|1").ok());
    CHECK(!parseBody("REQ|1||PING").ok());                      // empty field
    CHECK(!parseBody("RES|1|MAYBE|x").ok());
    CHECK(!parseBody("RES|1|ERR|NRC").ok());                    // incomplete error
    CHECK(!parseBody("RES|1|ERR|NOPE|TIMEOUT|00").ok());
    CHECK(!parseBody("RES|1|ERR|NRC|OK|00").ok());              // OK is not an error code
    CHECK(!parseBody("RES|1|ERR|NRC|TIMEOUT|zz").ok());
    CHECK(parseBody("HELLO|1|x").error() == diag::fail(Stage::Protocol, Code::UnknownCommand));
}

TEST_CASE(message_limits_argument_count) {
    std::string body = "REQ|1|X";
    for (std::size_t i = 0; i < kMaxArguments; ++i) body += "|a";
    CHECK(parseBody(body).ok());
    body += "|a";
    CHECK(!parseBody(body).ok());
}

TEST_CASE(decimal_and_hex_helpers) {
    std::uint32_t v = 0;
    CHECK(parseDecimal("2147483647", kMaxId, v) && v == kMaxId);
    CHECK(!parseDecimal("+1", kMaxId, v));
    CHECK(!parseDecimal("", kMaxId, v));
    CHECK(parseHex("7e8", v) && v == 0x7E8);
    CHECK(!parseHex("0x7E8", v));
    CHECK(!parseHex("123456789", v));
}
