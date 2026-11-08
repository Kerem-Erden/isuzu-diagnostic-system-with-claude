# Read-Only Vehicle Validation Plan

> **Document only.** Nothing in this plan has been executed. Every physical step needs the user's
> explicit permission in the session where it is performed (brief §4). OBD Mode 04 is not part of
> this plan and is not compiled into any firmware profile.

Goal: produce the first timestamped, raw-trace evidence that the gateway receives a real ECU
answer to **one** Mode 01 request and **one** Mode 03 request, without disturbing the vehicle.

## 0. Preconditions

| # | Condition | How it is checked |
|---|---|---|
| P1 | Authorised test vehicle, owner consent, parked, parking brake on, transmission in neutral/park | in person |
| P2 | Ignition ON / engine OFF for all steps except where engine idle is stated | in person |
| P3 | Second person present who can pull the OBD connector immediately | in person |
| P4 | Laptop on battery (no ground loop through a charger) | in person |
| P5 | Fused OBD lead (e.g. 1 A on the module supply if powered from the vehicle; the MVP powers the ESP32 from USB) | visual |
| P6 | Firmware build hash known and recorded (HELLO `build=`), produced from a committed, tagged revision | desktop Connection page |
| P7 | Tools below (§1) implemented and bench-tested | `VALIDATION_STATUS.md` |
| P8 | No Mode 04 anywhere: HELLO reports `mode04=0`; `CLEAR_DTC` answers `CAPABILITY/DISABLED` | bench, before the vehicle |

## 1. Missing tools that must exist first

The current firmware cannot yet support every step. These are prerequisites, tracked as W15–W17 in
`REMAINING_WORK.md`:

| Tool | Why | Status |
|---|---|---|
| W15 Bench self-test mode (TWAI internal loopback + CTX/CRX level check, transceiver disconnected from any bus) | Distinguishes controller, wiring and transceiver faults; the reference system's CRX stayed `1→1→1` | **implemented 2026-11-08** (`bench` firmware profile + `tools/bench_check.py`); never run on hardware |
| W16 Passive monitor (listen-only, per-ID frame counters and DLC, bounded) | Proves the RX path and the bitrate on the real bus without transmitting | **implemented 2026-11-08** (`MONITOR_START/STOP`, Developer page "Passive monitor"); host-tested only |
| W17 Developer-log export (CSV/JSON with timestamps) and a single-request control (one `READ_PID <pid>` without starting live polling) on the Developer page | Raw TX/RX evidence; §4 needs exactly one Mode 01 request | **implemented 2026-11-08** (Developer page: "Send once", "Export CSV…") |

## 2. Bench checks (vehicle NOT connected)

### 2.1 Electrical checklist (multimeter, module unplugged from any bus)

| Check | Expected | Fail → action |
|---|---|---|
| SN65HVD230 VCC to GND | 3.3 V ± 0.1 V | check 3V3 wiring / regulator |
| Rs (slope) pin | ≤ 0.3 V (high-speed mode). **High = standby: receiver works, driver disabled** | tie Rs to GND / check module jumper |
| CTX (D) idle | ≈ 3.3 V (recessive; firmware listen-only holds it high) | wiring GPIO21 |
| CRX (R) idle | ≈ 3.3 V | wiring GPIO22, module defect |
| Continuity GPIO21 → D, GPIO22 → R | < 1 Ω | re-wire |
| CANH–CANL resistance, module unpowered | ≈ 120 Ω if the on-board terminator ("121") is connected | note value; the vehicle bus is already terminated (≈ 60 Ω) |
| CANH, CANL to GND, powered, idle | ≈ 2.3–2.5 V each, difference ≈ 0 V | transceiver defect |

### 2.1b Bench self-test firmware (W15) – module on the bench, **no bus connected**

```powershell
idf.py -B build-bench -D SDKCONFIG=build-bench/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.bench" -p COM5 flash
python tools/bench_check.py COM5 --vehicle-disconnected --csv bench-results.csv
```

| Result | Meaning |
|---|---|
| `gpio=PASS`, `loopback=PASS` | controller, pins and transceiver echo work – continue with 2.2 |
| `gpio=RX_STUCK_RECESSIVE` (levels `…,0/0/1,…`) | the reference-system symptom: transceiver does not echo D→R – measure VCC and **Rs**, check CRX wiring |
| `gpio=RX_STUCK_DOMINANT` | R held low: CANH/CANL short or bus held dominant |
| `gpio=TX_PIN_NOT_FOLLOWING` | GPIO21 shorted or driven by something else |
| `gpio=PASS`, `loopback=LOOPBACK_FAILED` | pins fine; check bitrate / TWAI configuration (`lberr` names the stage) |

Re-flash the simulation or vehicle profile afterwards; the bench firmware cannot be used for diagnostics.

### 2.2 Two-node bench bus (proves the transmit path)

Second CAN node (USB–CAN adapter or second ESP32 + transceiver) at 500 kbit/s, 120 Ω at each end.
The second node only ACKs (normal mode, no transmissions).

* Desktop → `READ_PID 0C`. Expected: `RX_TIMEOUT` (nobody answers OBD) **and** `STATUS tec=0`
  (frame was ACKed). `CAN_TX/TX_FAILED` with `tec` rising by 8 → TX path or ACK problem: stop here.
* Repeat with the second node unplugged: expected `CAN_TX/TX_FAILED`, `tec` +8 per attempt. This
  confirms the error is detected and reported, not hidden.

## 3. Passive observation on the vehicle (listen-only, no transmission)

Vehicle profile firmware, desktop connected, **no request sent**. Use Developer page → Passive monitor
**Start**, wait, **Stop + report**; the `MON` rows (category CAN) are the evidence – export them as CSV.
The gateway refuses every transmitting command while the monitor runs.

1. Connect CANH (pin 6), CANL (pin 14), signal ground (pin 5). Ignition ON.
2. `STATUS`: expect `bus=LISTEN_ONLY`, `rec` stable (not rising). Rising REC → wrong bitrate or wiring: disconnect.
3. Passive monitor for 30 s: record IDs, counts, DLCs (summary `frames=`, `overflow=`, `rxdrop=` must be checked:
   `rxdrop` > 0 means the ESP32 could not keep up and counts are lower bounds). Expect regular traffic (previously ≈ 500 kbit/s was seen).
4. Note whether `0x7E8`–`0x7EF` appear spontaneously (they should not).

Abort: any dashboard warning, REC climbing, or unexpected vehicle behaviour.

## 4. One controlled Mode 01 request

Engine idling for RPM plausibility (or ignition ON, engine off: RPM must read 0).

1. Developer page: `TRACE 1`.
2. Live data stays **off**. Send exactly one `READ_PID 0C` (single-request control from W17).
3. Expected trace:
   ```
   CAN_TX id=7E0 dlc=8 data=02010C0000000000
   CAN_RX id=7E8 dlc=8 data=04410C1AF8......   (example, values differ)
   ```
4. Accept when: response from `0x7E8`, service `0x41`, PID `0x0C`, decoded RPM within ±50 rpm of the
   instrument cluster (or 0 with engine off), `STATUS tec=0`.
5. Record the exact frames, timestamps, HELLO identity and `STATUS` before/after.

If there is no answer: do not retry in a loop. One more single request after 10 s at most, then stop
and analyse (§6). Possible causes: different ECU address, 29-bit OBD, gateway module that blocks
OBD, ignition state.

## 5. One controlled Mode 03 request

Only after §4 passed.

1. `TRACE 1`, single `READ_DTC`.
2. Expected: `CAN_TX 7E0 0103…`, then either a single frame `43 NN …` or FF + our FC + CFs.
3. Compare the decoded list (count and codes) with an independent scan tool on the same vehicle
   within 10 minutes. Accept only on an exact match.
4. Record frames, decoded codes, scan-tool screenshot.

## 6. Trace analysis method

For each request: (a) TX frame bytes and timestamp; (b) TX completion (TEC unchanged); (c) first
RX from `0x7E8` and delay; (d) PCI sequence (SF, or FF/FC/CF with sequence numbers); (e) payload
service byte and length; (f) decoded value vs. independent reference. Classify every failure with
the structured stage (`CAN_ENQUEUE`, `CAN_TX`, `RX_TIMEOUT`, `ISOTP`, `NRC`, `OBD_DECODE`).

## 7. Validation record template

```
Date/time:              Vehicle (model, year, engine, VIN last 6):
Firmware build:         Desktop commit:            Knowledge DB sha256:
Bench 2.1 values:       Bench 2.2 result:
Passive 3: IDs seen / REC trend:
Mode 01: TX frame / RX frame / decoded / reference / delay:
Mode 03: TX / RX frames / decoded list / scan-tool list:
STATUS before/after (tec, rec, bus):
Abort events:           Operator / witness:
Conclusion: VEHICLE-validated for … only
```

Results go to `VALIDATION_STATUS.md` with level `VEHICLE` only for exactly what was demonstrated.
