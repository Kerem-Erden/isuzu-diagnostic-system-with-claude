# Requirements Matrix

Source of requirements: the project master brief (2026-11-08) cross-checked with the
reference MVP scope (`docs/requirements/mvp-scope.md` in the old repository).

Status values: `planned`, `in-progress`, `implemented (host-tested)`,
`implemented (simulation)`, `vehicle-validated`, `deferred`, `out of scope`.
"Implemented" never implies vehicle validation; see `VALIDATION_STATUS.md`.

| ID | Requirement | Origin | Module(s) | Verification method | Status |
|---|---|---|---|---|---|
| R-01 | USB serial link ESP32 ↔ desktop at 115200 8N1 | brief §3, old week-1 | firmware `protocol`, desktop `transport` | host codec tests, ESP32 bench | planned |
| R-02 | Safe serial port discovery; CP210x handling; unplug never freezes UI | brief §3, old `70ba8cc` | desktop `transport` | Qt Test with fake port, manual unplug | planned |
| R-03 | Explicit SIMULATION vs VEHICLE source, visible everywhere | brief §3, §13 | protocol `HELLO`, desktop UI | protocol tests, UI model tests | in-progress (HELLO/BOOT source identity done; UI pending) |
| R-04 | Mode 01 PIDs 0x0C RPM, 0x05 ECT, 0x0D speed, 0x04 load, 0x42 module voltage | brief §11 | `diagnostic-core/obd` | unit tests per PID | implemented (host-tested, core) |
| R-05 | Per-PID timestamp, fresh/stale/unsupported/error/timeout state | brief §11, §17 | desktop `services`, `ui` | service tests | implemented (LiveDataService: value/unit/timestamp, NoData/Fresh/Stale/Unsupported/Error; Qt-tested in simulation) |
| R-06 | Bounded, configurable polling rate; no aggressive polling, back-off on silence | brief §11, old D10 | firmware `application` | unit tests of scheduler | implemented (host-tested, gateway-core) |
| R-07 | Mode 03 stored DTC read: zero, one, many | brief §11 | `diagnostic-core/obd` | unit tests | implemented (host-tested, core) |
| R-08 | ISO-TP SF/FF/FC/CF, sequence check, absolute deadline, buffer bounds | brief §9, §11 | `diagnostic-core/isotp` | unit tests | implemented (host-tested, core) |
| R-09 | Negative response incl. NRC 0x78 (response pending, P2* extension) | brief §11 | `diagnostic-core/obd`, `isotp` | unit tests | implemented (host-tested, core) |
| R-10 | Unrelated frame / wrong responder / stale frame ignored | brief §11 | `diagnostic-core` | unit tests | implemented (host-tested, core) |
| R-11 | Structured error stage + code + detail across firmware → protocol → desktop | brief §11 | `diagnostic-core/error`, `serial-protocol` | unit + integration | implemented in core/protocol/gateway (host-tested); desktop pending |
| R-12 | DTC enrichment from knowledge DB; Unknown DTC fallback, no exception | brief §13 | `dtc-knowledge` | Qt Test with fixture DB | implemented (Qt-tested with the real DB + broken fixtures) |
| R-13 | Acquisition, enrichment and audit results kept separate; DB/audit failure never hides a successful ECU result | brief §10, old D3 | desktop `services` | service tests with failing repos | implemented (DtcScanService; Qt-tested incl. real audit failure) |
| R-14 | Sessions, raw results and errors persisted (audit DB with migrations) | brief §3, §13 | desktop `data` | Qt Test | in-progress (sessions + DTC scans persisted; live/error history pending) |
| R-15 | Developer diagnostics: bounded, filterable structured events | brief §3, §17 | firmware `diagnostics`, desktop `ui` | unit + model tests | planned |
| R-16 | Mode 04 architecture present, disabled by default (compile + runtime), never sent to a vehicle without explicit permission | brief §4, §11 | `diagnostic-core/obd`, firmware cfg | simulation tests only | in-progress (codec compile-gated; gateway answers CAPABILITY/DISABLED) |
| R-17 | Versioned serial protocol, request correlation, max size, resync after garbage, boot-log tolerance, duplicate side-effect guard | brief §12 | `serial-protocol` | unit tests | implemented (host-tested) |
| R-18 | Configurable ECU request/response IDs (default 0x7E0/0x7E8), 11-bit, 500 kbit/s | brief §11 | firmware config, protocol `CONFIG` | unit tests | planned |
| R-19 | Vehicle/ECU profile influences connection config or is explicitly marked unsupported | brief §11, old D7 | desktop `domain` | unit tests | planned |
| R-20 | Simulation drives the real protocol/transport path (no UI injection); scenarios: live, 0/1/many/unknown DTC, timeout, malformed, disconnect, NRC | brief §13 | `tools/sim-gateway`, desktop `services` | integration tests | in-progress (SimulatedEcu + gateway done; desktop wiring pending) |
| R-21 | Correlation ID per diagnostic operation; bounded logs | brief §14 | all | review + tests | in-progress (request ids end-to-end in gateway) |
| R-22 | Doxygen comments + Turkish learning guides | brief §15 | docs | review | planned |
| R-23 | CMake presets (Debug/Release), CTest, CI, windeployqt packaging | brief §18 | build | CI run | planned |
| R-24 | ESP-IDF 6.0.2 firmware, simulation and vehicle profiles, build ≠ flash | brief §7, §18 | firmware | CI IDF build | planned |
| R-25 | Read-only vehicle validation plan (document only) | brief §19 phase 6 | docs | review | planned |
| R-26 | Live-data reference comparison with hysteresis and duration confirmation (old MVP feature) | old mvp-scope §3.4 | desktop `services` | unit tests | deferred (post-MVP, data reused) |
| R-27 | Pins configurable constants (CTX GPIO21, CRX GPIO22) | brief §6 | firmware `drivers` | review | planned |

## Conflicts and ambiguities found

| # | Conflict | Resolution |
|---|---|---|
| C1 | Old README states the vehicle profile enables guarded Mode 04 by default; brief requires Mode 04 off by default and only after Mode 01/03 vehicle proof. | Brief wins. Mode 04 disabled at compile time in every checked-in profile. |
| C2 | Old MVP scope includes live reference warnings (hysteresis); the new brief does not list it in the MVP. | Deferred (R-26); knowledge data retained so it can be added without schema change. |
| C3 | Brief §7 mentions "C++17 or what IDF 6.0.2 safely supports" for firmware and C++20 for desktop; shared libraries must compile in both. | Shared libs are written in C++17 subset, desktop compiled with C++20. |
| C4 | Old protocol name `BATTERY_VOLTAGE` is actually PID 0x42 *control module voltage*. | New name `MODULE_VOLTAGE`; UI label "Control module voltage". |
| C5 | Brief §8 lists `libs/dtc-knowledge` as a lib; it needs SQLite. | Implemented as a Qt-SQL-based desktop library; no firmware use. |
