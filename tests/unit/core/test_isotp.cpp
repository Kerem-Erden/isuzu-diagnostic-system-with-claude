#include "diag/isotp.hpp"
#include "test_harness.hpp"

#include <initializer_list>

using namespace diag;

namespace {

constexpr std::uint64_t kStart = 1'000'000;  // 1 s, arbitrary monotonic origin

CanFrame frame(std::uint32_t id, std::initializer_list<std::uint8_t> bytes, std::uint64_t timeUs) {
    CanFrame result;
    result.id = id;
    result.dlc = 8;
    std::size_t i = 0;
    for (std::uint8_t b : bytes) result.data[i++] = b;
    result.timestampUs = timeUs;
    return result;
}

CanFrame ecu(std::initializer_list<std::uint8_t> bytes, std::uint64_t offsetUs = 1000) {
    return frame(0x7E8, bytes, kStart + offsetUs);
}

}  // namespace

TEST_CASE(isotp_single_frame_request_is_padded_to_dlc8) {
    IsoTpConfig config;
    const std::uint8_t request[] = {0x01, 0x0C};
    Result<CanFrame> built = buildSingleFrame(config, request, sizeof(request));
    REQUIRE(built.ok());
    CHECK_EQ(built.value().id, 0x7E0u);
    CHECK_EQ(built.value().dlc, 8);
    CHECK_EQ(built.value().data[0], 0x02);
    CHECK_EQ(built.value().data[1], 0x01);
    CHECK_EQ(built.value().data[2], 0x0C);
    CHECK_EQ(built.value().data[7], 0x00);
}

TEST_CASE(isotp_single_frame_rejects_empty_or_oversized_payload) {
    IsoTpConfig config;
    const std::uint8_t eight[8] = {};
    CHECK(buildSingleFrame(config, eight, 8).error() == fail(Stage::IsoTp, Code::InvalidLength, 8));
    CHECK(!buildSingleFrame(config, eight, 0).ok());
    CHECK(!buildSingleFrame(config, nullptr, 2).ok());
}

TEST_CASE(isotp_receives_single_frame) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    auto out = rx.onFrame(ecu({0x04, 0x41, 0x0C, 0x1A, 0xF8, 0xAA, 0xAA, 0xAA}));
    CHECK(!out.sendFrame);
    REQUIRE(rx.state() == IsoTpReceiver::State::Complete);
    CHECK_EQ(rx.length(), 4u);
    CHECK_EQ(rx.payload()[3], 0xF8);
}

TEST_CASE(isotp_multi_frame_sends_flow_control_and_reassembles) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    // Reference vector: 10-byte Mode 03 reply with 4 DTCs.
    auto out = rx.onFrame(ecu({0x10, 0x0A, 0x43, 0x04, 0x01, 0x0A, 0x04, 0x01}));
    REQUIRE(out.sendFrame);
    CHECK_EQ(out.frame.id, 0x7E0u);
    CHECK_EQ(out.frame.data[0], 0x30);  // FC, ContinueToSend
    CHECK_EQ(out.frame.data[1], 0x00);  // BS 0: no further FC
    CHECK(rx.state() == IsoTpReceiver::State::ReceivingConsecutive);
    out = rx.onFrame(ecu({0x21, 0x03, 0xFF, 0xC1, 0x23, 0x00, 0x00, 0x00}, 2000));
    CHECK(!out.sendFrame);
    REQUIRE(rx.state() == IsoTpReceiver::State::Complete);
    CHECK_EQ(rx.length(), 10u);
    CHECK_EQ(rx.payload()[9], 0x23);
}

TEST_CASE(isotp_sequence_number_wraps_from_15_to_0) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    // 6 bytes in FF + 16 CFs * 7 = 118 bytes, so sequence numbers 1..15 then 0.
    rx.onFrame(ecu({0x10, 118, 1, 2, 3, 4, 5, 6}));
    std::uint8_t seq = 1;
    for (int i = 0; i < 16; ++i) {
        rx.onFrame(ecu({static_cast<std::uint8_t>(0x20 | seq), 1, 1, 1, 1, 1, 1, 1},
                       2000 + static_cast<std::uint64_t>(i)));
        seq = static_cast<std::uint8_t>((seq + 1) & 0x0F);
    }
    CHECK(rx.state() == IsoTpReceiver::State::Complete);
    CHECK_EQ(rx.length(), 118u);
}

TEST_CASE(isotp_bad_sequence_fails_with_expected_and_received) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    rx.onFrame(ecu({0x10, 0x0A, 0x43, 0x04, 0x01, 0x0A, 0x04, 0x01}));
    rx.onFrame(ecu({0x22, 0x03, 0xFF, 0xC1, 0x23, 0, 0, 0}, 2000));
    REQUIRE(rx.state() == IsoTpReceiver::State::Failed);
    CHECK(rx.error() == fail(Stage::IsoTp, Code::BadSequence, 0x0102));
}

TEST_CASE(isotp_block_size_requests_new_flow_control) {
    IsoTpConfig config;
    config.blockSize = 2;
    IsoTpReceiver rx{config};
    rx.arm(kStart, 500);
    CHECK(rx.onFrame(ecu({0x10, 30, 0, 0, 0, 0, 0, 0})).sendFrame);  // FC after FF
    CHECK(!rx.onFrame(ecu({0x21, 0, 0, 0, 0, 0, 0, 0}, 2000)).sendFrame);
    auto out = rx.onFrame(ecu({0x22, 0, 0, 0, 0, 0, 0, 0}, 3000));  // block of 2 complete
    CHECK(out.sendFrame);
    CHECK_EQ(out.frame.data[1], 2);
    CHECK(!rx.onFrame(ecu({0x23, 0, 0, 0, 0, 0, 0, 0}, 4000)).sendFrame);
    // 6 (FF) + 3 * 7 = 27 < 30 bytes: the fourth CF completes the message, no FC needed.
    CHECK(!rx.onFrame(ecu({0x24, 0, 0, 0, 0, 0, 0, 0}, 5000)).sendFrame);
    CHECK(rx.state() == IsoTpReceiver::State::Complete);
}

TEST_CASE(isotp_overflow_answers_flow_control_overflow) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    auto out = rx.onFrame(ecu({0x11, 0x01, 0, 0, 0, 0, 0, 0}));  // 257 bytes > capacity
    REQUIRE(out.sendFrame);
    CHECK_EQ(out.frame.data[0], 0x32);
    CHECK(rx.error() == fail(Stage::IsoTp, Code::BufferOverflow, 257));
}

TEST_CASE(isotp_rejects_invalid_lengths) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    rx.onFrame(ecu({0x00, 0, 0, 0, 0, 0, 0, 0}));  // SF_DL = 0 (CAN FD escape)
    CHECK(rx.error().code == Code::InvalidLength);
    rx.arm(kStart, 500);
    rx.onFrame(ecu({0x10, 0x05, 0, 0, 0, 0, 0, 0}));  // FF announcing 5 bytes
    CHECK(rx.error().code == Code::InvalidLength);
    rx.arm(kStart, 500);
    CanFrame shortSingle = ecu({0x05, 0x41, 0x0C});
    shortSingle.dlc = 3;  // SF_DL 5 but only 2 bytes present
    rx.onFrame(shortSingle);
    CHECK(rx.error().code == Code::InvalidLength);
}

TEST_CASE(isotp_ignores_unrelated_wrong_responder_and_stale_frames) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    rx.onFrame(frame(0x123, {0x02, 0x41, 0x0D}, kStart + 10));       // normal bus traffic
    rx.onFrame(frame(0x7E9, {0x03, 0x41, 0x0D, 0x00}, kStart + 20)); // another ECU
    rx.onFrame(frame(0x7E8, {0x03, 0x41, 0x0D, 0x00}, kStart - 5));  // queued before request
    CHECK(rx.state() == IsoTpReceiver::State::WaitingFirstFrame);
    CHECK_EQ(rx.counters().unrelatedFrames, 1u);
    CHECK_EQ(rx.counters().wrongResponder, 1u);
    CHECK_EQ(rx.counters().staleFrames, 1u);
    rx.onFrame(ecu({0x03, 0x41, 0x0D, 0x32}));
    CHECK(rx.state() == IsoTpReceiver::State::Complete);
}

TEST_CASE(isotp_consecutive_without_first_frame_is_ignored) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    rx.onFrame(ecu({0x21, 1, 2, 3, 4, 5, 6, 7}));
    CHECK(rx.state() == IsoTpReceiver::State::WaitingFirstFrame);
    CHECK_EQ(rx.counters().ignoredFrames, 1u);
}

TEST_CASE(isotp_new_single_frame_restarts_reception) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    rx.onFrame(ecu({0x10, 0x0A, 0x43, 0x04, 0x01, 0x0A, 0x04, 0x01}));
    rx.onFrame(ecu({0x02, 0x43, 0x00}, 1500));  // ISO 15765-2: new SF aborts the old message
    REQUIRE(rx.state() == IsoTpReceiver::State::Complete);
    CHECK_EQ(rx.length(), 2u);
}

TEST_CASE(isotp_first_frame_timeout_is_rx_timeout) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    rx.poll(kStart + 499'999);
    CHECK(rx.state() == IsoTpReceiver::State::WaitingFirstFrame);
    rx.poll(kStart + 500'000);
    REQUIRE(rx.state() == IsoTpReceiver::State::Failed);
    CHECK(rx.error() == fail(Stage::RxTimeout, Code::Timeout, 0));
}

TEST_CASE(isotp_consecutive_gap_timeout_reports_received_bytes) {
    IsoTpConfig config;
    config.nCrTimeoutMs = 100;
    IsoTpReceiver rx{config};
    rx.arm(kStart, 500);
    rx.onFrame(ecu({0x10, 0x0A, 0x43, 0x04, 0x01, 0x0A, 0x04, 0x01}));
    rx.poll(kStart + 1000 + 100'000);
    CHECK(rx.error() == fail(Stage::RxTimeout, Code::Timeout, 6));
}

TEST_CASE(isotp_late_frame_counts_as_timeout) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 50);
    rx.onFrame(ecu({0x03, 0x41, 0x0D, 0x00}, 60'000));  // arrives 60 ms after a 50 ms deadline
    CHECK(rx.error().stage == Stage::RxTimeout);
}

TEST_CASE(isotp_frames_after_completion_are_ignored) {
    IsoTpReceiver rx{IsoTpConfig{}};
    rx.arm(kStart, 500);
    rx.onFrame(ecu({0x03, 0x41, 0x0D, 0x10}));
    rx.onFrame(ecu({0x03, 0x41, 0x0D, 0x20}, 2000));
    CHECK_EQ(rx.payload()[2], 0x10);
    CHECK_EQ(rx.counters().ignoredFrames, 1u);
}
