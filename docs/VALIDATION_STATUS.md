# Validation Status

Levels:
`AUTO` automated test · `SIM` simulation · `ESP32` ESP32 hardware ·
`VEHICLE` real vehicle · `ASSUMED` not yet verified · `FAILED` failed/inconclusive.

Nothing in this repository is vehicle-validated. Every feature below is at most
**implemented but not vehicle-validated**.

## New system

| Claim | Level | Evidence (command, date) |
|---|---|---|
| (filled per phase) | | |

## Inherited facts from the reference system (not re-verified here)

| Claim | Level | Source |
|---|---|---|
| TWAI loopback with external feedback passed once | ESP32 | old `docs/verification/week-4-verification.md` |
| Passive ~500 kbit/s traffic observed on vehicle | VEHICLE (passive RX only, no raw capture kept) | old verification record |
| SN65HVD230 path CRX stays recessive (1→1→1) | FAILED | old verification record, two modules |
| Vehicle TX raises TEC by ~8 | FAILED | user report |
| Real ECU Mode 01 / Mode 03 / Mode 04 response | ASSUMED (no evidence) | — |
| 120 Ω termination on module | ASSUMED | visual only |
