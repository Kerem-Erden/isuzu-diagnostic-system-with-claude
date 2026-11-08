#include "gw/bench.hpp"

namespace gw {
namespace bench {

Verdict evaluate(const Measurements& m) noexcept {
    Verdict v;
    if (m.gpioRan) {
        bool padOk = true;
        bool echo = true;
        bool allHigh = true;
        bool allLow = true;
        for (const GpioSample& s : m.gpio) {
            padOk = padOk && s.txPad == s.set;
            echo = echo && s.rx == s.set;
            allHigh = allHigh && s.rx == 1;
            allLow = allLow && s.rx == 0;
        }
        // Order matters: a CTX pin that does not move explains any CRX result, so report it first.
        if (!padOk) v.gpio = Finding::TxPinNotFollowing;
        else if (echo) v.gpio = Finding::Pass;
        else if (allHigh) v.gpio = Finding::RxStuckRecessive;
        else if (allLow) v.gpio = Finding::RxStuckDominant;
        else v.gpio = Finding::RxMismatch;
    }
    if (m.loopbackRan) v.loopback = m.loopbackOk ? Finding::Pass : Finding::LoopbackFailed;
    v.pass = v.gpio == Finding::Pass && v.loopback == Finding::Pass;
    return v;
}

const char* toString(Finding finding) noexcept {
    switch (finding) {
        case Finding::Pass: return "PASS";
        case Finding::NotRun: return "NOT_RUN";
        case Finding::TxPinNotFollowing: return "TX_PIN_NOT_FOLLOWING";
        case Finding::RxStuckRecessive: return "RX_STUCK_RECESSIVE";
        case Finding::RxStuckDominant: return "RX_STUCK_DOMINANT";
        case Finding::RxMismatch: return "RX_MISMATCH";
        case Finding::LoopbackFailed: return "LOOPBACK_FAILED";
    }
    return "UNKNOWN";
}

const char* hint(Finding finding) noexcept {
    switch (finding) {
        case Finding::Pass: return "ok";
        case Finding::NotRun: return "step not executed";
        case Finding::TxPinNotFollowing:
            return "GPIO21 does not follow the written level: short to GND/3V3 or another driver on the pin";
        case Finding::RxStuckRecessive:
            return "transceiver does not echo D to R: check 3V3 at VCC, Rs pin low (high = standby), CRX to GPIO22 wiring, module defect";
        case Finding::RxStuckDominant:
            return "R held low: bus held dominant, CANH/CANL shorted, or CRX wired to a low source";
        case Finding::RxMismatch:
            return "CRX does not follow CTX: wrong pin, swapped CTX/CRX, loose contact or noise";
        case Finding::LoopbackFailed:
            return "TWAI self-test failed: see lberr; if the GPIO step passed check bitrate/pin configuration";
    }
    return "unknown";
}

proto::FrameWriter& appendResult(proto::FrameWriter& writer, const Measurements& m, const Verdict& v) noexcept {
    writer.keyValue("gpio", toString(v.gpio));
    if (m.gpioRan) {
        // set/pad/rx per step, e.g. "1/1/1,0/0/1,1/1/1" for the reference-system failure.
        writer.beginField().append("levels=");
        for (std::size_t i = 0; i < m.gpio.size(); ++i) {
            const char text[6] = {static_cast<char>('0' + m.gpio[i].set), '/', static_cast<char>('0' + m.gpio[i].txPad),
                                  '/', static_cast<char>('0' + m.gpio[i].rx), ','};
            writer.append(std::string_view(text, i + 1 < m.gpio.size() ? 6 : 5));
        }
    }
    writer.keyValue("loopback", toString(v.loopback));
    if (v.loopback == Finding::LoopbackFailed) {
        writer.beginField()
            .append("lberr=")
            .append(diag::toString(m.loopbackError.stage))
            .append("/")
            .append(diag::toString(m.loopbackError.code));
    }
    return writer.keyValue("verdict", v.pass ? "PASS" : "FAIL");
}

}  // namespace bench
}  // namespace gw
