# Remaining Work

Model/effort suggestions follow the policy in the master brief (§20).

| # | Work item | Priority | Depends on | Risk | Acceptance / verification | Safety condition | Suggested model/effort |
|---|---|---|---|---|---|---|---|
| W1 ✅ done 2026-11-08 | `diagnostic-core`: CAN frame, ISO-TP RX/TX, Mode 01/03 codecs, DTC, errors + tests | P0 | — | M | CTest green, all §16 core cases covered | none (host only) | Opus High |
| W2 ✅ done 2026-11-08 | `serial-protocol`: CRC, framer, codec + tests | P0 | W1 (error names) | M | CTest green, resync/garbage/oversize tests | none | Opus/Sonnet High |
| W3 ✅ done 2026-11-08 | `gateway-core`: command handler, live scheduler with back-off, DTC scan orchestration, duplicate guard, `SimulatedEcu` | P0 | W1, W2 | M | host tests incl. scenarios (timeout, NRC, malformed, 0/1/many/unknown DTC) | Mode 04 compile-time off | Opus High |
| W4 🟡 builds with IDF v6.1 (2026-11-08); 6.0.2 CI + ESP32 run pending | Firmware ESP-IDF project: TWAI adapter (listen-only default), UART transport, Kconfig profiles | P1 | W3 | H | IDF 6.0.2 CI build both profiles; no flash | **No flash/vehicle without user permission** | Sonnet High |
| W5 ✅ done 2026-11-08 (bench-verified with simulation firmware on ESP32; physical unplug test pending) | Desktop skeleton: CMake/Qt, transport (QSerialPort worker thread + loopback), GatewayClient | P1 | W2 | M | Qt Test: framing, correlation, timeout, unplug | requires Qt Serial Port module (RK-08) | Sonnet High |
| W6 ✅ done 2026-11-08 | Desktop data: audit DB migrations, knowledge repository, Unknown DTC fallback | P1 | W5 | M | Qt Test with fixture DB and failure injection | — | Sonnet High |
| W7 ✅ done 2026-11-08 (DtcScanService, LiveDataService) | Desktop services: LiveData, DtcScan (separated outcomes), Audit, Simulation | P1 | W5, W6, W3 | M | service tests | — | Sonnet High |
| W8 ✅ done 2026-11-08 (7 screens, screenshot-reviewed in simulation) | Qt Widgets UI: 8 screens, source banner, developer diagnostics | P2 | W7 | L | model tests + manual run | — | Sonnet Medium |
| W9 ✅ done 2026-11-08 (tests/integration/test_end_to_end.cpp) | Integration: desktop ↔ loopback ↔ gateway-core ↔ SimulatedEcu ↔ SQLite ↔ UI model | P1 | W7 | M | CTest integration label | — | Sonnet High |
| W10 ✅ done 2026-11-08 (package script verified locally; CI green) | CI workflow + windeployqt packaging | P2 | W5 | L | green CI run | — | Sonnet Medium |
| W11 | Learning guides (TR) and remaining docs | P2 | each phase | L | review | — | Sonnet Medium |
| W12 ✅ done 2026-11-08 (vehicle-validation-plan.md, hardware-wiring.md) | Read-only vehicle validation plan + hardware wiring doc | P2 | W4 | L | review | document only | Opus High |
| W13 | Mode 04 simulated workflow (desktop + firmware) | P3 | W7, W3 | H | simulation tests only | **never on vehicle** before Mode 01/03 vehicle proof | Opus High |
| W14 | Live-data reference warnings (hysteresis), deferred from old MVP | P3 | W7 | L | unit tests | — | Sonnet Medium |
| W15 | Firmware bench self-test mode (TWAI internal loopback, CTX/CRX level check; transceiver off any bus) | P1 | W4 | M | bench run recorded | bench only, vehicle disconnected | Opus High |
| W16 | Firmware passive monitor (listen-only per-ID counters, bounded) + desktop view | P1 | W4 | M | bench with second node, then passive vehicle run | listen-only; **user permission** for vehicle | Opus High |
| W17 ✅ done 2026-11-08 | Developer-log export (CSV/JSON) + single READ_PID control on the Developer page | P1 | W8 | L | Qt tests | — | Sonnet Medium |
| W18 ✅ done 2026-11-08 (run 42478500299 green) | Push + first CI run (host sanitizers, Qt 6.8.3 desktop, ESP-IDF 6.0.2 firmware) | P1 | — | L | green CI | **push only with user approval** | Sonnet Medium |
