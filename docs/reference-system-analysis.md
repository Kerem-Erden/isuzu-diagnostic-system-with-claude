# Reference System Analysis

| Field | Value |
|---|---|
| Reference repository | `https://github.com/Kerem-Erden/isuzu-diagnostic-system` (local: `../isuzu-diagnostic-system`) |
| Reviewed branch / HEAD | `mvp/week4-completion` @ `67206fd` (2026-09-24), clean working tree |
| Review date | 2026-11-08 |
| Access mode | Read-only. No file in the reference repository was modified. |

This document records what the reference (old) system *actually proves*, based on
its source code, tests, CI definition, documents and Git history. Claims made in
the old README that are not backed by code, tests or raw logs are marked as such.

## 1. What was reviewed

| Area | Files | Notes |
|---|---|---|
| Top-level docs | `README.md`, `docs/requirements/mvp-scope.md`, `docs/architecture/*.md`, `docs/setup-and-demo.md` | MVP scope and serial contract |
| Validation records | `docs/sdlc/week-1-validation-report.md`, `docs/sdlc/week-4-phases-2-to-5.md`, `docs/verification/week-4-verification.md` | Only week 1 has a test table with physical steps; week 4 lists host tests |
| Planning | `docs/plannig/*.md`, external notes folder (`eksikler.txt`, weekly plans) | Scope drift between plans recorded |
| Firmware | `firmware/esp32-gateway/main/*.c/.h`, `Kconfig.projbuild`, `sdkconfig.defaults.*` | ESP-IDF 6.0.2 new TWAI on-chip driver API (`esp_twai_onchip.h`) |
| Firmware host tests | `tests/firmware/test_protocols.c`, `run.sh`, stub headers | C11 + ASan/UBSan, mocked CAN |
| Desktop | `desktop-app/.../*.cs`, `*.xaml` (WPF, .NET 10) | Serial, protocol, DTC workflow, audit, live rules, 8 views |
| Desktop tests | `tests/desktop/Program.cs` | Console-style assertion runner |
| Knowledge base | `Data/knowledge-base/*.json`, `isuzu-knowledge.db`, `tools/KnowledgeBaseImporter` | 317 DTCs, 541 profiles, 67 live params |
| CI | `.github/workflows/ci.yml` | .NET core, WPF build, firmware host tests, IDF build sim/vehicle |
| Git history | 41 commits, 2026-07-27 … 2026-09-24 | Regressions and safety-relevant changes below |

## 2. Verification classification

### 2.1 Verified working (with evidence)

| Item | Evidence | Level |
|---|---|---|
| ESP32 → USB serial → WPF line reception, partial-line buffering, COM discovery, manual/USB-removal disconnect and reconnect | Week-1 validation table (physical, with **CH340** adapter at that time, simulated values) | ESP32 hardware, desktop; *no vehicle* |
| Strict `REQ|id|CMD` parsing, duplicate clear suppression | `tests/firmware/test_protocols.c::test_gateway` | Automated host test |
| Mode 01 single-frame decode for RPM; counted Mode 03 single-frame decode | `test_pid_and_single_frame_dtc` | Automated host test |
| ISO-TP FF/FC/CF reassembly, wrong-ID filter, bad CF sequence rejection | `test_multiframe_and_filters` | Automated host test (mocked CAN) |
| Negative response → failure, absolute timeout, one-shot Mode 04 | `test_negative_timeout_and_clear` | Automated host test |
| Desktop framer split/oversize handling, response parser, live-value finite check, warning hysteresis/confirmation, clear workflow ordering | `tests/desktop/Program.cs` | Automated host test |

### 2.2 Works only in simulation

| Item | Notes |
|---|---|
| End-to-end DTC scan / clear / rescan with knowledge lookup | `DemoGateway` (desktop) and `CONFIG_GATEWAY_SIMULATION` (firmware) return fixed `P1093,P0087,P3FFF` |
| Live data warning/critical cycle | Fixed sample tables in both simulators |
| All audit records of "successful" DTC and live-data operations | Source `SIMULATION` (per task brief; no raw TX/RX logs exist) |

### 2.3 Verified on ESP32 but not on a vehicle

| Item | Evidence / caveat |
|---|---|
| TWAI internal/external-feedback loopback PASS (at one point) | Reported; firmware `can_bus_run_loopback_test`. Self-test disables ACK — proves the controller only |
| Direct GPIO21→GPIO22 jumper loopback PASS | `docs/verification/week-4-verification.md` |
| `CAN:INIT:ESP_OK`, `SYS:READY`, `SOURCE=VEHICLE;ECU=7E0` | Prove initialisation/configuration only — **not** ECU communication |

### 2.4 Observed on a vehicle (passive only)

| Item | Caveat |
|---|---|
| ~500 kbit/s traffic received in listen-only mode | No stored raw capture with timestamps in the repository |

### 2.5 Experimental / inconclusive / failed

| Item | Observation |
|---|---|
| SN65HVD230 transceiver bench test | CTX toggled, CRX stayed `1 → 1 → 1` (expected `1 → 0 → 1`) on **two** modules → `ESP_FAIL` |
| Vehicle TX attempts | TEC rose by ~+8 per attempt → consistent with missing ACK / no dominant bit read-back |
| 120 Ω resistor marked `121` on module | Electrical connection never measured |
| Supply / CTX / CRX / CANH-CANL levels | Never measured with multimeter or oscilloscope |
| Real ECU Mode 01 / 03 / 04 | **No** timestamped raw TX/RX log exists. Unverified. |

**Conclusion:** the physical CAN TX/RX path is unproven and is the main blocker
for any vehicle claim. The most likely hypotheses (to be tested, not assumed):
transceiver not powered/in standby (Rs pin), CRX not wired to GPIO22, swapped
CTX/CRX, wrong logic level, or defective/clone modules.

## 3. Known defects and regressions in the reference system

| # | Defect | Location | Severity |
|---|---|---|---|
| D1 | Commit `67206fd` ("enable complete vehicle diagnostic MVP") switched `sdkconfig.defaults.vehicle` to `CONFIG_GATEWAY_ALLOW_CLEAR=y` and removed the separate clear CI profile **without any new vehicle evidence**. Default vehicle build can send Mode 04. | firmware config, README | **Safety – high** |
| D2 | Vehicle mode starts the TWAI node in normal (TX-capable) mode at boot; there is no listen-only/diagnostic runtime switch, so any request immediately transmits. | `esp32_gateway.c`, `can_bus.c` | Safety – medium |
| D3 | Acquisition is coupled to audit: `DtcWorkflow.ScanCore` writes the audit before returning; if SQLite fails, the successful ECU result is lost and the UI shows a failure. | `Services/DtcWorkflow.cs` | Correctness – high |
| D4 | Errors collapse into `esp_err_t` (`ESP_FAIL`) – negative response, bad sequence, malformed payload and decode error are indistinguishable at the desktop. NRC value is lost. | `obd_protocol.c` | Observability – high |
| D5 | NRC 0x78 (response pending) keeps the *original* deadline; no P2* extension, so slow ECUs time out. | `receive_payload` | Correctness – medium |
| D6 | Application messages, ESP-IDF boot log and `printf` diagnostics share UART0 with no framing/checksum; a log line interleaved mid-message corrupts it. | `esp32_gateway.c` | Robustness – medium |
| D7 | Vehicle profile (model/engine/ECU type) is purely cosmetic; ECU address is compile-time only. | `VehicleProfileCatalog.cs`, Kconfig | Requirements gap |
| D8 | Duplicate `RequestIdGenerator` / `RequestIdGenarator` classes; dead `GatewayResponseSimulator`; hard-coded titles for P0087/P1093 in mapper. | desktop | Tech debt – low |
| D9 | A single global TX buffer and `s_tx_pending` flag; a timed-out TX blocks all later TX until completion is consumed (safe, but no visibility of the state to the desktop). | `can_bus.c` | Observability – low |
| D10 | Live polling issues one PID every 200 ms with 250 ms timeout; a missing ECU produces a continuous stream of TX attempts (TEC growth) with no back-off. | `stream_sample` | Safety/bus load – medium |
| D11 | Protocol has no version field; `INFO` lacks firmware version, build hash and CAN configuration. | protocol | Requirements gap |
| D12 | ISO-TP negative response is only detected inside the first frame; a FF response length check allows `total>7` up to 66 bytes only (32 DTC). OK for MVP but not documented. | `obd_protocol.c` | Low |
| D13 | Week-1 validation used a CH340 adapter; current hardware is CP210x. Serial driver behaviour on CP210x was fixed later (`e70dc4f`, `70ba8cc`: `SerialPort.Close` blocking on unplug). | desktop | Lesson |

Lessons from Git history worth keeping:

* `70ba8cc`/`e70dc4f`: **never call serial driver close/read on the UI thread**; CP210x close can block after unplug. Qt design must keep `QSerialPort` on a worker thread or use non-blocking signals only.
* `bfe44e7` review notes: TX completion must check `is_tx_success`; asynchronous TX storage must outlive a timeout; Mode 03 over CAN carries a DTC-count byte.
* `0fda7e8`: bitrate probing was added and later removed from the main path; bitrate must be configured, not guessed, on a live vehicle.

## 4. Technical debt of the old architecture

* Firmware is C with file-scope globals; protocol, OBD and CAN layers call each other directly (no seams except link-time mocks).
* No shared protocol implementation between firmware and desktop – two hand-written parsers.
* WPF views contain orchestration logic (code-behind, 557-line shell window).
* The knowledge DB schema is generated by a C# importer and has no `user_version`; audit tables live in a different DB created ad-hoc without migration.
* Tests are custom console runners without a test framework or CTest integration.

## 5. Reuse vs. redesign summary

See `reuse-and-redesign-register.md` for the full table. In short:

* **Reuse (data/knowledge):** `isuzu-knowledge.db` content (317 DTCs, profiles, causes, steps, related live data) as a read-only seed; PID formulas; DTC byte layout; test vectors from `test_protocols.c`; UI colour palette and screen flow.
* **Reuse (design lessons):** TX storage lifetime rule, one-owner-task CAN API, absolute deadlines, stale-frame timestamp filter, clear-transaction ordering, unknown-outcome handling.
* **Redesign:** everything in code – firmware in C++ with a pure core library, a shared C++ serial codec, Qt 6 desktop, structured error model, new versioned framed serial protocol, migrations.
