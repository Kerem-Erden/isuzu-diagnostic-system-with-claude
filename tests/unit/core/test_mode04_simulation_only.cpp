// SIMULATION-ONLY test of the Mode 04 codec. Enabling DIAG_ENABLE_MODE04 here affects only this
// translation unit (the functions are inline); the diagnostic_core library itself is built without
// it, so no product binary gains Mode 04 from this file (ADR-008).
#define DIAG_ENABLE_MODE04 1
#include "diag/obd.hpp"
#include "test_harness.hpp"

using namespace diag;

TEST_CASE(mode04_codec_validates_positive_response_simulation_only) {
    CHECK_EQ(obd::kMode04Request[0], 0x04);
    const std::uint8_t positive[] = {0x44};
    const std::uint8_t wrong[] = {0x44, 0x00};
    CHECK(obd::validateMode04Response(positive, 1).isOk());
    CHECK(!obd::validateMode04Response(wrong, 2).isOk());
    CHECK(!obd::validateMode04Response(nullptr, 0).isOk());
}
