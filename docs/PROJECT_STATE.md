# Project State

_Last updated: 2026-11-08_

## Current phase
Phase 1 complete (shared C++ core) and the hardware-independent half of Phase 2
(`gateway-core` + `SimulatedEcu`) complete. Next: ESP-IDF firmware project (W4) or desktop
skeleton (W5).

## Completed
* Phase 0 (`bc9a39f`): analysis, requirements, registers, ADR-001..011, protocol spec, knowledge DB
  (`data/knowledge/isuzu-knowledge.db`, SHA-256 `e07c21259e8afb5bf5da413db46d2db85e802292db04bdefd3331f3135669811`).
* Phase 1a (`718c7e1`): `libs/diagnostic-core` – errors, CAN frame, DTC, ISO-TP, OBD 01/03, ObdTransaction.
* Phase 1b (`0bd2950`): `libs/serial-protocol` – CRC-16, FrameReader/FrameWriter, REQ/RES/EVT codec.
* Phase 2a (this checkpoint): `libs/gateway-core` – Gateway (session gate, listen-only default,
  5 s session timeout, live round-robin with back-off and unsupported-PID skip, duplicate guard,
  one user request + queue behind live sample, DTC_PART chunking, TRACE, Mode 04 disabled) and
  deterministic `SimulatedEcu` (12 scenarios at CAN-frame level). Docs: protocol §4 session rules,
  `firmware-guide-tr.md`.

## In progress
* none (clean checkpoint)

## Environment facts (2026-11-08)
* Reference repo: `../isuzu-diagnostic-system` @ `67206fd` (read-only, untouched).
* Windows: Qt 6.11.1 `mingw_64` (Core, Widgets, Sql, Test, Quick; **Qt Serial Port missing**),
  MinGW 13.1 `C:\Qt\Tools\mingw1310_64`, Ninja `C:\Qt\Tools\Ninja`, CMake 4.3.1, ESP-IDF v5.5.4 only.
* User has ESP-IDF in a **Linux VM** (version to be confirmed with `idf.py --version`; must be 6.0.2).
  Plan: build/flash firmware in the VM, then hand the CP210x USB device back to Windows for the
  desktop app. Repo transfer to the VM: shared folder, `git bundle`, or push (push only with user approval).

## Last successful build/test
```
PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;...
cmake --preset host-debug   && cmake --build --preset host-debug   && ctest --preset host-debug
cmake --preset host-release && cmake --build --preset host-release && ctest --preset host-release
→ 100% passed: core_tests 48, protocol_tests 22, gateway_tests 25; 0 compiler warnings (-Werror)
```
Evidence level: host unit tests + simulation only. No ESP32, no vehicle.

## Known blockers
* RK-08 Qt Serial Port missing (blocks W5 serial transport; loopback/simulation path can proceed).
* RK-09 ESP-IDF 6.0.2 version in the VM unconfirmed (blocks local W4 build; CI can build).
* RK-01 Physical CAN path unproven (blocks vehicle validation).

## Open questions
* VM ESP-IDF version and how the repository reaches the VM.

## Next task
W4: `firmware/esp32-gateway` ESP-IDF 6.0.2 project – component that compiles the three shared libs,
`TwaiAdapter` (C++ RAII over `esp_twai_onchip`, listen-only default, ISR → queue, TX storage lifetime
rule from the reference system, error counters), `UartTransport`, application task loop, Kconfig
(simulation/vehicle, pins GPIO21/22, IDs, bitrate; no Mode 04 option), `sdkconfig.defaults.*`.
Verification: IDF build in the VM/CI only — **no flash without explicit permission**.
Alternative if VM not ready: W5 desktop skeleton with an in-process loopback transport.

## Last checkpoint commit
* see `git log --oneline -1`
