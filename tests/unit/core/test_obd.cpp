#include "diag/obd.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <cstring>

using namespace diag;
using namespace diag::obd;

namespace {
bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
bool textIs(const Dtc& dtc, const char* expected) {
    char text[kDtcTextLength + 1];
    dtc.toText(text);
    return std::strcmp(text, expected) == 0;
}
}  // namespace

TEST_CASE(mode01_request_payload) {
    std::uint8_t request[2];
    buildMode01Request(0x0C, request);
    CHECK_EQ(request[0], 0x01);
    CHECK_EQ(request[1], 0x0C);
}

TEST_CASE(mode01_decodes_all_mvp_pids) {
    const std::uint8_t rpm[] = {0x41, 0x0C, 0x1A, 0xF8};
    const std::uint8_t coolant[] = {0x41, 0x05, 0x7B};
    const std::uint8_t speed[] = {0x41, 0x0D, 0x32};
    const std::uint8_t load[] = {0x41, 0x04, 0xFF};
    const std::uint8_t volts[] = {0x41, 0x42, 0x36, 0xB0};
    CHECK(near(decodeMode01(0x0C, rpm, sizeof rpm).value(), 1726.0));   // (26*256+248)/4
    CHECK(near(decodeMode01(0x05, coolant, sizeof coolant).value(), 83.0));  // 123-40
    CHECK(near(decodeMode01(0x0D, speed, sizeof speed).value(), 50.0));
    CHECK(near(decodeMode01(0x04, load, sizeof load).value(), 100.0));
    CHECK(near(decodeMode01(0x42, volts, sizeof volts).value(), 14.0));  // 0x36B0 = 14000 mV
}

TEST_CASE(mode01_coolant_minimum_is_minus_40) {
    const std::uint8_t coolant[] = {0x41, 0x05, 0x00};
    CHECK(near(decodeMode01(0x05, coolant, sizeof coolant).value(), -40.0));
}

TEST_CASE(mode01_rejects_wrong_service_pid_and_length) {
    const std::uint8_t wrongService[] = {0x42, 0x0C, 0x1A, 0xF8};
    const std::uint8_t wrongPid[] = {0x41, 0x0D, 0x1A};
    const std::uint8_t tooShort[] = {0x41, 0x0C, 0x1A};
    const std::uint8_t tooLong[] = {0x41, 0x0D, 0x10, 0x00};
    CHECK(decodeMode01(0x0C, wrongService, 4).error() == fail(Stage::ObdDecode, Code::WrongService, 0x42));
    CHECK(decodeMode01(0x0C, wrongPid, 3).error() == fail(Stage::ObdDecode, Code::WrongPid, 0x0D));
    CHECK(decodeMode01(0x0C, tooShort, 3).error().code == Code::MalformedPayload);
    CHECK(decodeMode01(0x0D, tooLong, 4).error().code == Code::MalformedPayload);
    CHECK(decodeMode01(0x0C, nullptr, 0).error().code == Code::MalformedPayload);
}

TEST_CASE(mode01_unknown_pid_is_unsupported) {
    const std::uint8_t maf[] = {0x41, 0x10, 0x01, 0x02};
    CHECK(decodeMode01(0x10, maf, 4).error() == fail(Stage::ObdDecode, Code::UnsupportedPid, 0x10));
}

TEST_CASE(mode01_supported_pid_bitmap) {
    // 0x18 = 0001 1000 → PIDs 0x04 and 0x05; 0x18 in byte B → 0x0C and 0x0D.
    const std::uint8_t bitmap[] = {0x41, 0x00, 0x18, 0x18, 0x00, 0x01};
    Result<std::uint32_t> mask = decodeSupportedPids(bitmap, sizeof bitmap);
    REQUIRE(mask.ok());
    CHECK(isPidSupported(mask.value(), 0x04));
    CHECK(isPidSupported(mask.value(), 0x05));
    CHECK(isPidSupported(mask.value(), 0x0C));
    CHECK(isPidSupported(mask.value(), 0x0D));
    CHECK(!isPidSupported(mask.value(), 0x0E));
    CHECK(isPidSupported(mask.value(), 0x20));  // next range supported
    CHECK(!isPidSupported(mask.value(), 0x42));
}

TEST_CASE(pid_table_lookup) {
    REQUIRE(findPidByName("RPM") != nullptr);
    CHECK_EQ(findPidByName("RPM")->pid, 0x0C);
    CHECK(findPid(0x42) != nullptr);
    CHECK(findPid(0x00) == nullptr);  // bitmap PID is not a live value
    std::size_t count = 0;
    CHECK(livePids(count) != nullptr);
    CHECK_EQ(count, 5u);
}

TEST_CASE(mode03_zero_dtcs) {
    const std::uint8_t none[] = {0x43, 0x00};
    Result<DtcList> list = decodeMode03(none, sizeof none);
    REQUIRE(list.ok());
    CHECK_EQ(list.value().count, 0u);
}

TEST_CASE(mode03_one_and_many_dtcs) {
    const std::uint8_t one[] = {0x43, 0x01, 0x00, 0x87};
    Result<DtcList> single = decodeMode03(one, sizeof one);
    REQUIRE(single.ok());
    CHECK(textIs(single.value().items[0], "P0087"));

    const std::uint8_t many[] = {0x43, 0x04, 0x01, 0x0A, 0x04, 0x01, 0x03, 0xFF, 0xC1, 0x23};
    Result<DtcList> list = decodeMode03(many, sizeof many);
    REQUIRE(list.ok());
    CHECK_EQ(list.value().count, 4u);
    CHECK(textIs(list.value().items[3], "U0123"));
}

TEST_CASE(mode03_rejects_malformed_payloads) {
    const std::uint8_t countTooHigh[] = {0x43, 0x03, 0x01, 0x0A, 0x04, 0x01};
    const std::uint8_t trailingByte[] = {0x43, 0x01, 0x01, 0x0A, 0x00};
    const std::uint8_t nullPair[] = {0x43, 0x01, 0x00, 0x00};
    const std::uint8_t wrongService[] = {0x47, 0x00};
    CHECK(decodeMode03(countTooHigh, sizeof countTooHigh).error().code == Code::MalformedPayload);
    CHECK(decodeMode03(trailingByte, sizeof trailingByte).error().code == Code::MalformedPayload);
    CHECK(decodeMode03(nullPair, sizeof nullPair).error().code == Code::MalformedPayload);
    CHECK(decodeMode03(wrongService, sizeof wrongService).error() == fail(Stage::ObdDecode, Code::WrongService, 0x47));
    const std::uint8_t onlyService[] = {0x43};
    CHECK(!decodeMode03(onlyService, 1).ok());
}

TEST_CASE(negative_response_detection) {
    const std::uint8_t negative[] = {0x7F, 0x03, 0x22};
    std::uint8_t nrc = 0;
    CHECK(isNegativeResponse(negative, 3, 0x03, nrc));
    CHECK_EQ(nrc, 0x22);
    CHECK(!isNegativeResponse(negative, 3, 0x01, nrc));  // NRC for a different service
    CHECK(std::strcmp(nrcName(0x22), "conditionsNotCorrect") == 0);
    CHECK(std::strcmp(nrcName(0x99), "unknownNrc") == 0);
}

#if DIAG_ENABLE_MODE04
#error "Mode 04 must not be enabled for the default core test translation unit"
#endif
