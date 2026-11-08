# Validation Status

Levels:
`AUTO` automated test · `SIM` simulation · `ESP32` ESP32 hardware ·
`VEHICLE` real vehicle · `ASSUMED` not yet verified · `FAILED` failed/inconclusive.

Nothing in this repository is vehicle-validated. Every feature below is at most
**implemented but not vehicle-validated**.

## New system

| Claim | Level | Evidence (command, date) |
|---|---|---|
| ISO-TP SF/FF/FC/CF receive, sequence wrap, BS>0, overflow FC, stale/wrong-responder filtering, absolute + N_Cr timeouts | AUTO | `ctest --preset host-debug` → 48/48 tests pass, MinGW 13.1, Windows 11, 2026-11-08 |
| Mode 01 decode for 0x04/0x05/0x0C/0x0D/0x42 and supported-PID bitmap | AUTO | same run |
| Mode 03 decode for 0 / 1 / many DTCs, malformed rejection | AUTO | same run |
| OBD transaction error stages (enqueue, TX completion, missing completion, RX timeout, NRC, NRC 0x78 with P2* and limit, wrong service, ISO-TP error), no retry | AUTO | same run |
| Mode 04 codec (simulation-only translation unit) | AUTO | same run |
| Any of the above against a real ECU | ASSUMED | not attempted |

## Inherited facts from the reference system (not re-verified here)

| Claim | Level | Source |
|---|---|---|
| TWAI loopback with external feedback passed once | ESP32 | old `docs/verification/week-4-verification.md` |
| Passive ~500 kbit/s traffic observed on vehicle | VEHICLE (passive RX only, no raw capture kept) | old verification record |
| SN65HVD230 path CRX stays recessive (1→1→1) | FAILED | old verification record, two modules |
| Vehicle TX raises TEC by ~8 | FAILED | user report |
| Real ECU Mode 01 / Mode 03 / Mode 04 response | ASSUMED (no evidence) | — |
| 120 Ω termination on module | ASSUMED | visual only |
