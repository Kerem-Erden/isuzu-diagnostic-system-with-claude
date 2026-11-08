# Project State

_Last updated: 2026-11-08_

## Current phase
Phase 1 (pure C++ core): `diagnostic-core` complete; `serial-protocol` next.

## Completed
* Phase 0 (commit `bc9a39f`): repository verification, reference analysis, requirements
  matrix, reuse/redesign and risk registers, ADR-001..011, serial protocol v1 spec,
  architecture overview, knowledge DB imported (`data/knowledge/isuzu-knowledge.db`,
  SHA-256 `e07c21259e8afb5bf5da413db46d2db85e802292db04bdefd3331f3135669811`).
* Phase 1a: `libs/diagnostic-core` – structured errors, CAN frame, DTC codec, ISO-TP
  receiver/SF builder/FC, OBD Mode 01/03 codecs (Mode 04 compile-gated), `ObdTransaction`
  with stage-separated errors and NRC 0x78 handling; 48 host tests; CMake presets;
  guide `can-isotp-obd-guide-tr.md`, `testing-guide.md`, `developer-setup.md`.

## In progress
* none (clean checkpoint)

## Changed files in last unit
`CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `libs/diagnostic-core/`, `tests/`, docs.

## Environment facts (2026-11-08)
* Reference repo: `../isuzu-diagnostic-system` @ `67206fd` (read-only, untouched).
* Qt 6.11.1 `mingw_64` at `C:\Qt\6.11.1\mingw_64`; MinGW 13.1 at `C:\Qt\Tools\mingw1310_64`;
  Ninja at `C:\Qt\Tools\Ninja`. Installed Qt modules: Core, Widgets, Sql (QSQLITE), Test, Quick.
  **Qt Serial Port is missing.**
* ESP-IDF installed locally: v5.5.4 only. **6.0.2 missing** → firmware IDF build only in CI.
* CMake 4.3.1.

## Last successful build/test
```
PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;...
cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug
→ 100% tests passed (core_tests: 48 tests, 0 failed), 0 compiler warnings (-Werror, -Wconversion …)
```
Target: Windows 11 host, MinGW 13.1. Source of evidence: host unit tests only (no ESP32, no vehicle).

## Known blockers
* RK-08 Qt Serial Port module missing (blocks desktop serial transport build, not core/simulation).
* RK-09 ESP-IDF 6.0.2 missing locally.
* RK-01 Physical CAN path unproven (blocks any vehicle validation).

## Open questions
* none blocking.

## Next task
W2: implement `libs/serial-protocol` (CRC-16/CCITT-FALSE, bounded line framer with `#` resync,
message encode/decode for REQ/RES/EVT, key=value payload helpers) with host tests, following
`docs/serial-protocol.md`. Then W3 `libs/gateway-core` + `SimulatedEcu`.

## Last checkpoint commit
* see `git log --oneline -1` (Phase 1a commit follows `bc9a39f`)
