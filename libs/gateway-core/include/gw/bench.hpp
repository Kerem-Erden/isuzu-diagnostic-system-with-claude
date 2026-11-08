#pragma once
/**
 * @file bench.hpp
 * @brief Interpretation of the ESP32 bench self-test (W15). Pure logic, host-tested; the
 *        measurements themselves come from firmware/esp32-gateway/main/bench_runner.cpp.
 *
 * BENCH ONLY – the test drives CTX and, in the loopback step, transmits a frame. CANH/CANL must
 * not be connected to a vehicle (vehicle-validation-plan.md §2).
 *
 * Step 1 – GPIO echo: CTX (GPIO21) is driven 1 → 0 → 1 as a plain GPIO; CRX (GPIO22) is read back.
 *   An SN65HVD230 that is powered and in high-speed mode echoes its D input to R, so CRX must follow.
 *   This is exactly the test the reference system failed (CRX stayed 1 → 1 → 1).
 * Step 2 – TWAI self-test loopback: the controller sends one frame in self-test mode (no ACK needed)
 *   and must receive it back. Measured on the ESP32-D0WD-V3 with ESP-IDF 6.1 (2026-11-08): the loopback
 *   passed while step 1 showed no echo, i.e. the frame is looped back *inside* the controller. Step 2
 *   therefore proves controller + bitrate configuration only; the transceiver path is judged by step 1.
 */

#include <array>
#include <cstdint>

#include "diag/error.hpp"
#include "proto/frame.hpp"

namespace gw {
namespace bench {

struct GpioSample {
    std::uint8_t set = 1;    ///< level written to CTX
    std::uint8_t txPad = 1;  ///< CTX pad read back (detects a pin that is shorted or driven elsewhere)
    std::uint8_t rx = 1;     ///< CRX level
};

struct Measurements {
    bool gpioRan = false;
    std::array<GpioSample, 3> gpio{};  ///< for set = 1, 0, 1
    bool loopbackRan = false;
    bool loopbackOk = false;
    diag::Error loopbackError{};       ///< why the loopback failed (enqueue, TX, timeout, mismatch)
};

enum class Finding : std::uint8_t {
    Pass,
    NotRun,
    TxPinNotFollowing,   ///< CTX pad does not show the written level
    RxStuckRecessive,    ///< CRX stays 1 – transceiver does not echo (reference-system symptom)
    RxStuckDominant,     ///< CRX stays 0 – bus held dominant or R shorted low
    RxMismatch,          ///< CRX changes but not like CTX (noise, wrong pin, inverted wiring)
    LoopbackFailed,
};

struct Verdict {
    bool pass = false;
    Finding gpio = Finding::NotRun;
    Finding loopback = Finding::NotRun;
};

Verdict evaluate(const Measurements& m) noexcept;
/// Stable upper-case name used in the serial protocol, e.g. "RX_STUCK_RECESSIVE".
const char* toString(Finding finding) noexcept;
/// One-line, field-safe hint for the operator (no '|', '#', '*').
const char* hint(Finding finding) noexcept;

/// Appends `gpio=…|levels=1/1/1,0/0/1,1/1/1|loopback=…|lberr=STAGE/CODE|verdict=PASS|FAIL`.
proto::FrameWriter& appendResult(proto::FrameWriter& writer, const Measurements& m, const Verdict& v) noexcept;

}  // namespace bench
}  // namespace gw
