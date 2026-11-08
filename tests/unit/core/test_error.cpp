#include "diag/error.hpp"
#include "test_harness.hpp"

#include <cstring>

using namespace diag;

TEST_CASE(error_names_round_trip_for_every_stage_and_code) {
    for (unsigned i = 0; i <= static_cast<unsigned>(Stage::Internal); ++i) {
        Stage parsed{};
        const auto stage = static_cast<Stage>(i);
        CHECK(stageFromString(toString(stage), parsed));
        CHECK(parsed == stage);
    }
    for (unsigned i = 0; i <= static_cast<unsigned>(Code::StorageFailure); ++i) {
        Code parsed{};
        const auto code = static_cast<Code>(i);
        CHECK(codeFromString(toString(code), parsed));
        CHECK(parsed == code);
    }
}

TEST_CASE(error_names_are_protocol_stable) {
    CHECK(std::strcmp(toString(Stage::NegativeResponse), "NRC") == 0);
    CHECK(std::strcmp(toString(Stage::CanTxCompletion), "CAN_TX") == 0);
    CHECK(std::strcmp(toString(Code::BadSequence), "BAD_SEQUENCE") == 0);
    Code code{};
    CHECK(!codeFromString("bad_sequence", code));  // case-sensitive on purpose
    CHECK(!codeFromString(nullptr, code));
}

TEST_CASE(result_carries_value_or_error) {
    Result<int> good = 7;
    CHECK(good.ok());
    CHECK_EQ(good.value(), 7);
    Result<int> bad = fail(Stage::IsoTp, Code::BadSequence, 0x0102);
    CHECK(!bad.ok());
    CHECK(bad.error() == fail(Stage::IsoTp, Code::BadSequence, 0x0102));
}
