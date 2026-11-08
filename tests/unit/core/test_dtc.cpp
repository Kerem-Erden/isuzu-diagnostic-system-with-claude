#include "diag/dtc.hpp"
#include "test_harness.hpp"

#include <cstring>

using namespace diag;

namespace {
bool textIs(const Dtc& dtc, const char* expected) {
    char text[kDtcTextLength + 1];
    dtc.toText(text);
    return std::strcmp(text, expected) == 0;
}
}  // namespace

TEST_CASE(dtc_bytes_to_text_follow_j1979_layout) {
    // Vectors from the reference system's host tests (test_protocols.c).
    CHECK(textIs(Dtc{0x01, 0x0A}, "P010A"));
    CHECK(textIs(Dtc{0x04, 0x01}, "P0401"));
    CHECK(textIs(Dtc{0x03, 0xFF}, "P03FF"));
    CHECK(textIs(Dtc{0xC1, 0x23}, "U0123"));
    CHECK(textIs(Dtc{0x40, 0x00}, "C0000"));
    CHECK(textIs(Dtc{0xBF, 0xFF}, "B3FFF"));
    CHECK(textIs(Dtc{0x00, 0x87}, "P0087"));
    CHECK(textIs(Dtc{0x10, 0x93}, "P1093"));
}

TEST_CASE(dtc_text_round_trips) {
    const char* codes[] = {"P0087", "P1093", "U0123", "C3FFF", "B0001", "P3FFF"};
    for (const char* code : codes) {
        Dtc dtc;
        CHECK(Dtc::fromText(code, dtc));
        CHECK(textIs(dtc, code));
    }
}

TEST_CASE(dtc_text_rejects_invalid_input) {
    Dtc dtc;
    CHECK(!Dtc::fromText("P4000", dtc));   // first digit has only two bits
    CHECK(!Dtc::fromText("X0087", dtc));
    CHECK(!Dtc::fromText("p0087", dtc));
    CHECK(!Dtc::fromText("P008", dtc));
    CHECK(!Dtc::fromText("P00870", dtc));
    CHECK(!Dtc::fromText("P00g7", dtc));
    CHECK(!Dtc::fromText(nullptr, dtc));
}

TEST_CASE(dtc_list_is_bounded) {
    DtcList list;
    for (std::size_t i = 0; i < DtcList::kCapacity; ++i) CHECK(list.push(Dtc{0x01, 0x01}));
    CHECK(!list.push(Dtc{0x01, 0x01}));
    CHECK_EQ(list.count, DtcList::kCapacity);
}
