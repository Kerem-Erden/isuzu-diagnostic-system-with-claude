#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "proto/crc16.hpp"
#include "proto/frame.hpp"
#include "test_harness.hpp"

using namespace proto;

namespace {

struct Collector {
    std::vector<std::string> bodies;
    void feed(FrameReader& reader, const std::string& bytes) {
        reader.feed(bytes.data(), bytes.size(), [this](std::string_view body) { bodies.emplace_back(body); });
    }
};

/// Builds a valid frame from a body like "RES|1|OK|pong" by writing each field separately.
std::string frameOf(const std::string& body) {
    FrameWriter writer;
    std::size_t start = 0;
    while (start <= body.size()) {
        const std::size_t end = std::min(body.find('|', start), body.size());
        writer.field(std::string_view(body).substr(start, end - start));
        start = end + 1;
    }
    return std::string(writer.finish());
}

}  // namespace

TEST_CASE(crc16_ccitt_false_check_value) {
    CHECK_EQ(crc16CcittFalse("123456789", 9), 0x29B1);
    CHECK_EQ(crc16CcittFalse("", 0), 0xFFFF);
}

TEST_CASE(writer_produces_marker_crc_and_newline) {
    FrameWriter writer;
    writer.field("REQ").field(41u).field("READ_DTC");
    const std::string frame(writer.finish());
    const std::string body = "REQ|41|READ_DTC";
    char crc[5];
    static constexpr char hex[] = "0123456789ABCDEF";
    const std::uint16_t value = crc16CcittFalse(body.data(), body.size());
    for (int i = 0; i < 4; ++i) crc[i] = hex[(value >> (12 - 4 * i)) & 0xF];
    crc[4] = 0;
    CHECK(frame == "#" + body + "*" + crc + "\n");
}

TEST_CASE(writer_matches_documented_examples) {
    // CRCs in docs/serial-protocol.md §7 were computed by an independent Python implementation.
    CHECK(frameOf("REQ|41|READ_DTC") == "#REQ|41|READ_DTC*AE0F\n");
    CHECK(frameOf("RES|42|ERR|NRC|NEGATIVE_RESPONSE|22") == "#RES|42|ERR|NRC|NEGATIVE_RESPONSE|22*59FC\n");
}

TEST_CASE(writer_formats_fixed_point_without_locale) {
    FrameWriter writer;
    writer.field("V").fixed2(1726.0).fixed2(-40.0).fixed2(14.005).fixed2(0.0);
    const std::string frame(writer.finish());
    CHECK(frame.rfind("#V|1726.00|-40.00|14.01|0.00*", 0) == 0);  // 14.005 rounds half away from zero
}

TEST_CASE(writer_rejects_forbidden_characters_and_overflow) {
    FrameWriter writer;
    writer.field("A|B");
    CHECK(writer.finish().empty());
    writer.reset();
    writer.field("");
    CHECK(writer.finish().empty());
    writer.reset();
    writer.field(std::string(kMaxBodyLength + 1, 'X'));
    CHECK(writer.finish().empty());
    writer.reset();
    writer.field(std::string(kMaxBodyLength, 'X'));
    const std::string_view largest = writer.finish();
    CHECK_EQ(largest.size(), kMaxFrameLength + 1);  // + '\n'
    writer.reset();
    writer.fixed2(std::numeric_limits<double>::infinity());
    CHECK(writer.finish().empty());
}

TEST_CASE(reader_accepts_valid_frame_and_largest_frame) {
    FrameReader reader;
    Collector c;
    c.feed(reader, frameOf("RES|1|OK|pong"));
    REQUIRE(c.bodies.size() == 1);
    CHECK(c.bodies[0] == "RES|1|OK|pong");
    c.feed(reader, frameOf(std::string(kMaxBodyLength, 'Y')));
    CHECK_EQ(c.bodies.size(), 2u);
}

TEST_CASE(reader_reassembles_fragmented_input_byte_by_byte) {
    FrameReader reader;
    Collector c;
    for (char ch : frameOf("EVT|3|LIVE|RPM|800.00")) c.feed(reader, std::string(1, ch));
    REQUIRE(c.bodies.size() == 1);
    CHECK(c.bodies[0] == "EVT|3|LIVE|RPM|800.00");
}

TEST_CASE(reader_handles_multiple_frames_in_one_read) {
    FrameReader reader;
    Collector c;
    c.feed(reader, frameOf("A|1") + frameOf("B|2") + frameOf("C|3"));
    CHECK_EQ(c.bodies.size(), 3u);
}

TEST_CASE(reader_ignores_boot_log_noise_and_resynchronises) {
    FrameReader reader;
    Collector c;
    c.feed(reader, "I (312) boot: ESP-IDF v6.0.2 2nd stage bootloader\r\nets Jun  8 2016 00:22:57\n");
    c.feed(reader, "garbage" + frameOf("RES|2|OK|pong"));
    REQUIRE(c.bodies.size() == 1);
    CHECK(reader.counters().noiseBytes > 0);
}

TEST_CASE(reader_tolerates_crlf) {
    FrameReader reader;
    Collector c;
    std::string frame = frameOf("RES|9|OK|pong");
    frame.insert(frame.size() - 1, "\r");
    c.feed(reader, frame);
    CHECK_EQ(c.bodies.size(), 1u);
}

TEST_CASE(reader_rejects_bad_checksum) {
    FrameReader reader;
    Collector c;
    std::string frame = frameOf("REQ|5|READ_DTC");
    frame[3] = 'X';  // corrupt the body, keep the CRC
    c.feed(reader, frame);
    CHECK(c.bodies.empty());
    CHECK_EQ(reader.counters().badChecksum, 1u);
}

TEST_CASE(reader_rejects_missing_or_invalid_crc_field) {
    FrameReader reader;
    Collector c;
    c.feed(reader, "#REQ|5|PING\n");
    c.feed(reader, "#REQ|5|PING*12G4\n");
    c.feed(reader, "#*FFFF\n");  // empty body
    CHECK(c.bodies.empty());
    CHECK_EQ(reader.counters().malformed, 3u);
}

TEST_CASE(reader_interrupted_frame_restarts_on_new_marker) {
    FrameReader reader;
    Collector c;
    c.feed(reader, "#REQ|5|PI" + frameOf("REQ|6|PING"));  // log line cut a frame in half
    REQUIRE(c.bodies.size() == 1);
    CHECK(c.bodies[0] == "REQ|6|PING");
    CHECK_EQ(reader.counters().malformed, 1u);
}

TEST_CASE(reader_discards_oversized_frame_through_newline) {
    FrameReader reader;
    Collector c;
    // The suffix after the overflow looks like a command; it must never be executed.
    c.feed(reader, "#" + std::string(kMaxFrameLength + 10, 'Z') + "REQ|9|CLEAR_DTC*0000\n");
    CHECK(c.bodies.empty());
    CHECK_EQ(reader.counters().oversized, 1u);
    c.feed(reader, frameOf("RES|1|OK|pong"));
    CHECK_EQ(c.bodies.size(), 1u);  // recovers afterwards
}
