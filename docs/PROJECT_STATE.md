# Project State

_Last updated: 2026-11-08_

## Current phase
Phase 0 complete → Phase 1 (pure C++ core) in progress.

## Completed
* Phase 0: repository verification, reference analysis, requirements matrix,
  reuse/redesign register, risk register, ADR-001..011, serial protocol v1 spec,
  architecture overview, knowledge DB imported (`data/knowledge/isuzu-knowledge.db`,
  SHA-256 `e07c21259e8afb5bf5da413db46d2db85e802292db04bdefd3331f3135669811`).

## In progress
* Phase 1: `libs/diagnostic-core` (see "Next task").

## Environment facts (2026-11-08)
* Reference repo: `../isuzu-diagnostic-system` @ `67206fd` (read-only).
* Qt 6.11.1 `mingw_64` at `C:\Qt\6.11.1\mingw_64`; MinGW 13.1 at `C:\Qt\Tools\mingw1310_64`; Ninja at `C:\Qt\Tools\Ninja`.
  Installed Qt modules: Core, Widgets, Sql (QSQLITE), Test, Quick. **Qt Serial Port is missing.**
* ESP-IDF installed locally: v5.5.4 only (`C:\Espressif\frameworks`). **6.0.2 missing** → firmware IDF build only in CI.
* CMake 4.3.1.

## Last successful build/test
* none yet

## Known blockers
* RK-08 Qt Serial Port module missing (blocks desktop serial transport build, not core/simulation).
* RK-09 ESP-IDF 6.0.2 missing locally.
* RK-01 Physical CAN path unproven (blocks any vehicle validation).

## Open questions
* none blocking Phase 1.

## Next task
Implement `libs/diagnostic-core` with host tests (W1), then `libs/serial-protocol` (W2).

## Last checkpoint commit
* (see `git log`)
