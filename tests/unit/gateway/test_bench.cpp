#include <string>

#include "gw/bench.hpp"
#include "test_harness.hpp"

using namespace gw::bench;

namespace {

Measurements gpio(std::uint8_t rx0, std::uint8_t rx1, std::uint8_t rx2, std::uint8_t pad1 = 0) {
    Measurements m;
    m.gpioRan = true;
    m.gpio = {GpioSample{1, 1, rx0}, GpioSample{0, pad1, rx1}, GpioSample{1, 1, rx2}};
    return m;
}

std::string body(const Measurements& m) {
    proto::FrameWriter writer;
    writer.field("RES").field(1u).field("OK");
    appendResult(writer, m, evaluate(m));
    std::string frame(writer.finish());
    return frame.substr(1, frame.size() - 7);  // strip '#' and "*CRC\n"
}

}  // namespace

TEST_CASE(bench_healthy_transceiver_passes) {
    Measurements m = gpio(1, 0, 1);
    m.loopbackRan = true;
    m.loopbackOk = true;
    const Verdict v = evaluate(m);
    CHECK(v.pass);
    CHECK(v.gpio == Finding::Pass);
    CHECK(body(m) == "RES|1|OK|gpio=PASS|levels=1/1/1,0/0/0,1/1/1|loopback=PASS|verdict=PASS");
}

TEST_CASE(bench_reference_system_symptom_is_rx_stuck_recessive) {
    // Recorded on the reference system with two SN65HVD230 modules: CRX 1 → 1 → 1.
    Measurements m = gpio(1, 1, 1);
    m.loopbackRan = true;
    m.loopbackError = diag::fail(diag::Stage::CanTxCompletion, diag::Code::Timeout);
    const Verdict v = evaluate(m);
    CHECK(!v.pass);
    CHECK(v.gpio == Finding::RxStuckRecessive);
    CHECK(std::string(hint(v.gpio)).find("Rs pin") != std::string::npos);
    CHECK(body(m) == "RES|1|OK|gpio=RX_STUCK_RECESSIVE|levels=1/1/1,0/0/1,1/1/1|loopback=LOOPBACK_FAILED|lberr=CAN_TX/TIMEOUT|verdict=FAIL");
}

TEST_CASE(bench_classifies_other_gpio_failures) {
    CHECK(evaluate(gpio(0, 0, 0)).gpio == Finding::RxStuckDominant);
    CHECK(evaluate(gpio(1, 0, 0)).gpio == Finding::RxMismatch);
    CHECK(evaluate(gpio(1, 0, 1, /*pad1=*/1)).gpio == Finding::TxPinNotFollowing);  // pad did not go low
}

TEST_CASE(bench_not_run_steps_never_pass) {
    Measurements m;
    CHECK(!evaluate(m).pass);
    CHECK(evaluate(m).gpio == Finding::NotRun);
    m = gpio(1, 0, 1);  // GPIO ok but loopback not executed
    CHECK(!evaluate(m).pass);
}

TEST_CASE(bench_hints_are_field_safe) {
    for (Finding f : {Finding::Pass, Finding::NotRun, Finding::TxPinNotFollowing, Finding::RxStuckRecessive,
                      Finding::RxStuckDominant, Finding::RxMismatch, Finding::LoopbackFailed}) {
        for (const char* c = hint(f); *c != '\0'; ++c) CHECK(proto::isFieldChar(*c));
    }
}
