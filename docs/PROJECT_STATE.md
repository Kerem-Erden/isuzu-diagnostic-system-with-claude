# Project State

_Last updated: 2026-11-08_

## Current phase
Phase 4 (UI) done in simulation: all seven screens built and screenshot-reviewed. Next: Phase 5 (integration,
CI run, packaging) and Phase 6 (written vehicle-validation plan). Firmware (Phase 2) builds with ESP-IDF v6.1; not flashed.

## Completed
* Phase 0 (`bc9a39f`): analysis, requirements, registers, ADR-001..011, protocol spec, knowledge DB
  (`data/knowledge/isuzu-knowledge.db`, SHA-256 `e07c21259e8afb5bf5da413db46d2db85e802292db04bdefd3331f3135669811`).
* Phase 1a (`718c7e1`): `libs/diagnostic-core`.
* Phase 1b (`0bd2950`): `libs/serial-protocol`.
* Phase 2a (`81d8809`): `libs/gateway-core` + `SimulatedEcu`.
* Phase 2b (`b5f7168`, build verified with IDF v6.1 in the next commit): `firmware/esp32-gateway` – `isuzu_shared` component
  compiling the libs, Kconfig (source, scenario, bitrate, ECU ID, pins; no Mode 04 option),
  layered `sdkconfig.defaults*`, `TwaiAdapter` (listen-only default, static event queue, 4 TX slots
  released only by TX-done/node delete, no HW retry), `UartTransport`, `app_main` loop.
  Gateway gained `reportDiagnostic()` and `STATUS buserr=`. CI workflow (host GCC/Clang with
  ASan/UBSan, IDF 6.0.2 sim/vehicle builds). Docs: `build-and-release.md`, `hardware-wiring.md`,
  firmware guide §7.
* Phase 2c (`ec5b86f`): firmware build verified with ESP-IDF v6.1 (both profiles, 0 warnings).
* Phase 3a (`8e83b4d`): `apps/desktop-qt` – `desktop_core` (Transport, SimulationTransport,
  GatewayClient, value types), skeleton `MainWindow`, 11 Qt Test cases; `Gateway::emit` renamed to
  `sendFrame` (Qt `emit` macro clash); CMake `enable_testing()` order fixed; CI desktop job.
* Phase 3b (`a06ae99`): `SerialTransport` (QSerialPort on a worker thread, async open, unplug via
  ResourceError, DTR/RTS low, bounded stop without terminate), port enumeration with CP210x marking,
  `GatewayClient::setTransport()`, failed-vs-lost connection distinction, port combo in MainWindow,
  `desktop_serial_tests` (7 hardware-free cases).
* First ESP32 bench run (2026-11-08, user permission): simulation firmware (build d0636b0e) flashed to the
  ESP32-D0WD-V3 on COM5; protocol verified with an independent Python client and with the C++ desktop path
  (`bench_serial_tests`). TWAI/CAN not exercised (simulation profile).
* Phase 3c (`2a70309`): `libs/dtc-knowledge` (read-only KnowledgeRepository, schema validation,
  Unknown DTC fallback), `AuditStore` (sessions + dtc_scans, user_version migrations), `DtcScanService`
  (acquisition / enrichment / audit reported separately), window shows Known/Unknown/unavailable and audit
  status; knowledge DB copied next to the exe. `desktop_data_tests` 18 cases. Docs: `dtc-database.md`.
* Phase 3d (`7c0106e`): `LiveDataService` (per-PID value/unit/timestamps/state, source label,
  unsupported/error handling, gateway stop reasons, immediate staleness on disconnect) and a live table in
  the window. `desktop_live_tests` 9 cases. Phase 3 (desktop infrastructure) complete.
* Phase 4 (this checkpoint): Qt Widgets shell (`AppWindow`: banner, sidebar, stack, status bar) and seven pages
  (Connection + profile + identity, Dashboard, Live data, DTC list/detail, History, Developer, Settings);
  `ConnectionController`, view models, `--screenshots` developer tour. `desktop_model_tests` 6 cases.

## In progress
* none

## Environment facts (2026-11-08)
* Reference repo: `../isuzu-diagnostic-system` @ `67206fd` (read-only, untouched).
* Windows: Qt 6.11.1 `mingw_64` incl. Serial Port (installed 2026-11-08), MinGW 13.1, Ninja, CMake 4.3.1.
  COM5 = Silicon Labs CP210x (VID 10C4 PID EA60) = ESP32 DevKit; currently runs the SIMULATION firmware.
  ESP-IDF **v6.1** installed by the user (EIM): `C:/esp/v6.1/esp-idf`, activate with
  `. C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1`. Old v5.5.4 env is broken and unused.
* User's Linux VM has ESP-IDF (version unconfirmed). Repo not yet transferred to the VM.

## Last successful build/test
```
cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug
→ 100% passed: core_tests 48, protocol_tests 22, gateway_tests 26; 0 warnings (MinGW 13.1)
QT_ROOT=C:/Qt/6.11.1/mingw_64; cmake --preset windows-debug && cmake --build --preset windows-debug && ctest --preset windows-debug
→ 9/9 pass (desktop 11, serial 7, data 18, live 9, models 6; bench skips without ISUZU_BENCH_PORT), 0 warnings
ISUZU_BENCH_PORT=COM5 bench_serial_tests.exe → pass against the ESP32 (simulation firmware)
```
Firmware (ESP-IDF v6.1, Windows): `idf.py -B build-sim ... sdkconfig.defaults.simulation build` and
`idf.py -B build-vehicle ... sdkconfig.defaults.vehicle build` → both "Project build complete", 0 warnings.
CI workflow written but never run (nothing pushed).

## Known blockers
* RK-09 build verified on IDF v6.1 only; 6.0.2 CI run needs a push (user decision).
* RK-01 Physical CAN path unproven.

## Open questions
* Whether to push so CI can verify 6.0.2 and the sanitizer host builds.

## Next task
1. Phase 5: windeployqt packaging script + `build-and-release.md` desktop section; integration test of the
   full chain desktop → protocol → gateway-core → SimulatedEcu → SQLite → models (most links already covered,
   add one end-to-end test); decide with the user whether to push so CI (sanitizers, IDF 6.0.2, Qt 6.8) runs.
2. Phase 6 (documents only): `vehicle-validation-plan.md` (electrical checklist, passive capture, single
   controlled Mode 01 / Mode 03), `troubleshooting.md`, `known-limitations.md`.
3. Manual: run the GUI on Windows against COM5 and pull the USB cable once (unplug path).

## Last checkpoint commit
* see `git log --oneline -1`
