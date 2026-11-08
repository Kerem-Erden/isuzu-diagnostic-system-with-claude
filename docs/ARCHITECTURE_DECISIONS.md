# Architecture Decisions

Status: `confirmed` (implemented and verified by build/tests) or `provisional`
(decided, not yet fully exercised).

---

## ADR-001 — Independent repository, reference repo read-only
* **Date:** 2026-11-08 · **Status:** confirmed
* **Problem:** The old repository contains useful knowledge but also defects and a different stack.
* **Options:** (a) fork and port; (b) new repo with `legacy/` copy; (c) new independent repo, selective reuse.
* **Decision:** (c). Only items listed in `reuse-and-redesign-register.md` are re-implemented or imported. No relative path to `../isuzu-diagnostic-system` is used by any build file.
* **Consequences:** The knowledge DB is copied once as a versioned artifact (`data/knowledge/`), with its SHA-256 recorded in `docs/dtc-database.md`.

## ADR-002 — Qt Widgets instead of QML
* **Date:** 2026-11-08 · **Status:** provisional
* **Problem:** Choose the Qt UI technology.
* **Options:** Qt Widgets; Qt Quick/QML.
* **Decision:** Qt Widgets.
* **Rationale:** The old UI is a form/table-centric desktop tool (sidebar + pages, lists, detail panes) which maps directly to `QStackedWidget`, `QTableView` and models. Widgets keeps the whole code base in C++ (the learning goal), is testable with Qt Test without a QML engine, and `windeployqt` output is smaller (no QML imports). QML's strengths (animation, touch, fluid layouts) are not requirements.
* **Consequences:** Styling via one Qt stylesheet with the old palette. View logic stays thin; presentation state lives in `QAbstractItemModel` subclasses and service objects that can be tested headless.

## ADR-003 — Shared pure C++ core libraries for firmware and desktop
* **Date:** 2026-11-08 · **Status:** provisional
* **Problem:** The old system had two independent protocol parsers (C and C#) that could drift; OBD/ISO-TP logic was entangled with ESP-IDF calls.
* **Decision:** Three hardware-independent libraries, written in a C++17 subset without exceptions, RTTI dependence or heap allocation on hot paths:
  * `libs/diagnostic-core` – CAN frame model, ISO-TP, OBD Mode 01/03/(04 disabled), DTC codec, structured errors.
  * `libs/serial-protocol` – frame codec, line framer, message model (used by both sides).
  * `libs/gateway-core` – the gateway application state machine (command handling, live scheduler, DTC scan orchestration) behind a small `CanPort`/`Clock` interface, plus a deterministic `SimulatedEcu`.
* **Consequences:** The firmware simulation profile, desktop simulation mode and integration tests all run the *same* gateway code and ISO-TP stack. Only the TWAI and UART adapters are ESP32-specific.

## ADR-004 — Serial protocol: CRC-protected, versioned ASCII frames
* **Date:** 2026-11-08 · **Status:** provisional
* **Options:** keep `REQ|id|CMD`; JSON Lines; length-prefixed binary; COBS binary; ASCII frames with prefix and CRC.
* **Decision:** One ASCII line per message: `#<body>*<CRC16>\n`, fields separated by `|`. See `serial-protocol.md`.
* **Rationale:** Human-readable in any serial monitor (learning, field debugging); the `#` start marker and CRC let the parser skip ESP-IDF boot logs and recover from garbage; trivially bounded (256 bytes); no JSON parser on the MCU. Binary would save bandwidth that we do not need at 115200 baud and the MVP data rate.
* **Consequences:** Protocol version `1` negotiated by `HELLO`; any incompatible change bumps it.

## ADR-005 — Structured error model
* **Date:** 2026-11-08 · **Status:** provisional
* **Decision:** `diag::Error { Stage stage; Code code; uint32_t detail; }` returned by value (`Result<T>`), no exceptions in shared libs. `Stage` distinguishes CAN enqueue, TX completion, bus error, RX timeout, responder mismatch, ISO-TP, negative response, OBD decode, serial transport, protocol parse, audit, database, capability. `detail` carries the NRC, expected sequence number, etc.
* **Consequences:** The same stage/code names travel over the serial protocol and are shown in the UI.

## ADR-006 — Test frameworks
* **Date:** 2026-11-08 · **Status:** provisional
* **Decision:** Shared libraries are tested with a tiny in-repo header (`tests/unit/test_harness.hpp`, CTest-integrated), so they build with any C++17 compiler (host GCC, MinGW, ESP-IDF host builds) without fetching dependencies. Desktop/Qt code uses **Qt Test**.
* **Rationale:** Avoids network/vendored dependencies; Qt Test is already part of the Qt install.

## ADR-007 — Knowledge DB reused read-only; separate audit DB with migrations
* **Date:** 2026-11-08 · **Status:** confirmed (implemented and tested 2026-11-08)
* **Decision:** `isuzu-knowledge.db` ships as a read-only resource; the application never writes to it. Audit/history lives in `%LOCALAPPDATA%/IsuzuDiagnostic/audit.db`, versioned by `PRAGMA user_version` with ordered migrations embedded as string constants in `apps/desktop-qt/src/data/audit_store.cpp`
  (simpler than resource files; reviewed in one place).
* **Consequences:** Knowledge and audit failures are independent; either missing still leaves raw ECU results visible.

## ADR-008 — Mode 04 defence in depth, disabled by default
* **Date:** 2026-11-08 · **Status:** provisional
* **Decision:** Mode 04 request building exists in `diagnostic-core` but is only reachable when (1) firmware is compiled with `CONFIG_GATEWAY_ENABLE_MODE04=y` (absent in every checked-in profile), (2) the runtime capability is advertised in `HELLO`, (3) the desktop shows an explicit confirmation, (4) RPM = 0 and speed = 0 were read within 5 s, (5) a successful DTC snapshot < 30 s old exists and was audited, (6) the request ID is new (no retry), followed by a read-only rescan. Simulation exercises the full flow.

## ADR-009 — Firmware CAN default is listen-only
* **Date:** 2026-11-08 · **Status:** provisional
* **Decision:** In the vehicle profile the TWAI node boots listen-only. It switches to normal (TX-capable) mode only to service an explicit desktop request and only while a desktop session is active. Live polling stops after N consecutive timeouts (back-off) instead of hammering a silent bus.
* **Rationale:** Old tests showed TEC growth from repeated unacknowledged TX (RK-01, RK-03).

## ADR-010 — Documentation language policy
* **Date:** 2026-11-08 · **Status:** confirmed
* **Decision:** Code identifiers, Doxygen comments and engineering records (ADR, state, validation, registers, protocol spec) in English. Learning guides (`*-tr.md`) and `troubleshooting.md` in Turkish with English technical terms in parentheses. Conversation with the user in Turkish.

## ADR-011 — Toolchain
* **Date:** 2026-11-08 · **Status:** provisional
* **Decision:** Desktop: Qt 6 (local 6.11.1, `mingw_64` kit, MinGW 13.1, Ninja from `C:\Qt\Tools`), CMake ≥ 3.21 presets. CI: Qt via `jurplel/install-qt-action`. Firmware: ESP-IDF 6.0.2 (CI); host builds of shared libs with any C++17 compiler.
* **Consequences:** Local machine currently lacks Qt Serial Port and ESP-IDF 6.0.2 (RK-08, RK-09).
