# Isuzu Diagnostic System (C++ / Qt 6 / ESP32)

ESP32 + SN65HVD230 CAN gateway (ESP-IDF 6.0.2, C++) and a Windows Qt 6 Widgets
desktop application for reading OBD Mode 01 live data and Mode 03 stored DTCs from
an Isuzu engine ECU over ISO 15765-4 (11-bit, 500 kbit/s).

> **Validation status:** nothing in this repository has been validated on a real
> vehicle. All features are *implemented but not vehicle-validated*. Mode 04 (clear
> DTCs) is disabled in every build profile. See `docs/VALIDATION_STATUS.md`.

## Repository layout

```
apps/desktop-qt/        Qt 6 Widgets desktop application
firmware/esp32-gateway/ ESP-IDF 6.0.2 firmware project
libs/diagnostic-core/   Pure C++ CAN / ISO-TP / OBD / DTC core (no heap, no I/O)
libs/serial-protocol/   Shared framed serial protocol codec
libs/gateway-core/      Gateway application logic + deterministic simulated ECU
libs/dtc-knowledge/     Qt SQL knowledge lookup with Unknown DTC fallback
data/knowledge/         Read-only Isuzu DTC knowledge database
tests/                  Unit and integration tests (CTest)
docs/                   Architecture, decisions, guides, validation records
```

## Where to start

1. `docs/PROJECT_STATE.md` – current phase and next task
2. `docs/architecture.md` and `docs/ARCHITECTURE_DECISIONS.md`
3. `docs/can-isotp-obd-guide-tr.md` – learning guide (Turkish)
4. `docs/developer-setup.md` – build and test

## Host build and tests (core libraries)

```bash
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Firmware flashing and vehicle tests are never started automatically; see
`docs/build-and-release.md` and `docs/vehicle-validation-plan.md`.
