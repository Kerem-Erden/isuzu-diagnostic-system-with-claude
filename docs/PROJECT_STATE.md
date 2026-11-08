# Project State

_Last updated: 2026-11-08_

## Current phase
Phase 3 (desktop infrastructure) started: transport abstraction, SimulationTransport, GatewayClient and
a skeleton window work and are tested. Firmware (Phase 2) builds with ESP-IDF v6.1; not flashed.

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
* Phase 3a (this checkpoint): `apps/desktop-qt` – `desktop_core` (Transport, SimulationTransport,
  GatewayClient, value types), skeleton `MainWindow`, 11 Qt Test cases; `Gateway::emit` renamed to
  `sendFrame` (Qt `emit` macro clash); CMake `enable_testing()` order fixed; CI desktop job.

## In progress
* none

## Environment facts (2026-11-08)
* Reference repo: `../isuzu-diagnostic-system` @ `67206fd` (read-only, untouched).
* Windows: Qt 6.11.1 `mingw_64` (no Qt Serial Port), MinGW 13.1, Ninja, CMake 4.3.1.
  ESP-IDF **v6.1** installed by the user (EIM): `C:/esp/v6.1/esp-idf`, activate with
  `. C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1`. Old v5.5.4 env is broken and unused.
* User's Linux VM has ESP-IDF (version unconfirmed). Repo not yet transferred to the VM.

## Last successful build/test
```
cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug
→ 100% passed: core_tests 48, protocol_tests 22, gateway_tests 26; 0 warnings (MinGW 13.1)
QT_ROOT=C:/Qt/6.11.1/mingw_64; cmake --preset windows-debug && cmake --build --preset windows-debug && ctest --preset windows-debug
→ 4/4 executables pass (desktop_tests: 11 cases), 0 warnings; app smoke-starts offscreen
```
Firmware (ESP-IDF v6.1, Windows): `idf.py -B build-sim ... sdkconfig.defaults.simulation build` and
`idf.py -B build-vehicle ... sdkconfig.defaults.vehicle build` → both "Project build complete", 0 warnings.
CI workflow written but never run (nothing pushed).

## Known blockers
* RK-09 build verified on IDF v6.1 only; 6.0.2 CI run needs a push (user decision).
* RK-08 Qt Serial Port missing (desktop serial transport).
* RK-01 Physical CAN path unproven.

## Open questions
* Whether to push so CI can verify 6.0.2 and the sanitizer host builds.

## Next task
1. W6: desktop data layer – `libs/dtc-knowledge` (Qt SQL, read-only knowledge DB, Unknown DTC fallback,
   structured errors) and the audit DB with `PRAGMA user_version` migrations; Qt tests with fixture DBs
   and failure injection.
2. W7: services (DtcScanService with separate acquisition / enrichment / audit outcomes, LiveDataService
   with fresh/stale/unsupported/error states).
3. SerialTransport (QSerialPort on a worker thread) as soon as the Qt Serial Port module is installed.
Flashing remains a separate step requiring explicit user permission.

## Last checkpoint commit
* see `git log --oneline -1`
