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
| Serial protocol v1: CRC-16 check value, framing, fragmentation, noise/boot-log resync, oversize discard, structured error round trip | AUTO | `ctest --preset host-debug` and `host-release` → 3/3 executables pass (core 48, protocol 22, gateway 25), MinGW 13.1, 2026-11-08 |
| Gateway: listen-only boot, HELLO gate, 0/1/3/40 DTC (multi-frame + DTC_PART), NRC 0x22, NRC 0x78, RX timeout, no-ACK, malformed, bad sequence, duplicate id, Mode 04 disabled, live round robin, back-off, unsupported PID skip, session timeout | AUTO + SIM | same run (`gateway_tests`, SimulatedEcu at CAN-frame level) |
| Any of the above against a real ECU or on an ESP32 | ASSUMED | not attempted |
| Firmware ESP-IDF project builds (simulation + vehicle profiles) | AUTO (build only) | ESP-IDF **v6.1** on Windows, 2026-11-08: both profiles `Project build complete`, 0 warnings; sim bin 0x2EA00, vehicle bin 0x34340 bytes |
| Firmware builds with the target ESP-IDF 6.0.2 | ASSUMED | CI job defined, not run yet (nothing pushed) |
| Simulation firmware runs on an ESP32 and speaks protocol v1 over the CP210x link | ESP32 | 2026-11-08, ESP32-D0WD-V3 rev 3.1 (MAC c8:85:41:c2:af:9c) on COM5, build d0636b0e (simulation profile, IDF v6.1), flashed with user permission. Independent Python/pyserial client: HELLO (source=SIMULATION, proto 1, 7E0/7E8, mode04=0), PING, READ_DTC → P1093,P0087,P3FFF, READ_PID 0C/05, CLEAR_DTC → CAPABILITY/DISABLED, repeated id → PROTOCOL/DUPLICATE_REQUEST, LIVE_START 500 → 17 samples in 1.6 s (100 ms spacing), wrong-CRC frame ignored (STATUS crcerr=1), STATUS bus=LISTEN_ONLY tx=0 |
| Desktop GatewayClient + SerialTransport ↔ ESP32 (simulation firmware) | ESP32 | 2026-11-08, `ISUZU_BENCH_PORT=COM5 bench_serial_tests.exe` → pass: identity SIMULATION, READ_DTC, 13 live samples in 1.2 s, keep-alive held the session > 6 s, idle tx=0, 23 frames with 0 CRC/malformed/noise, clean disconnect. Opening the port did not reset the board (no BOOT seen) |
| TWAI / CAN path on the ESP32 | ASSUMED | simulation profile does not initialise TWAI; nothing tested on CAN |
| Desktop GatewayClient: HELLO identity, correlation, DTC part merge, NRC, desktop timeout, unplug, async failure when disconnected, noise/unknown id, keep-alive, reboot re-handshake, live events | AUTO + SIM | `ctest --preset windows-debug` → 4/4 (desktop_tests 11 cases pass), Qt 6.11.1 MinGW, 2026-11-08 |
| Desktop app starts | MANUAL smoke | `isuzu_diagnostic.exe` with `QT_QPA_PLATFORM=offscreen` still running after 3 s; no UI interaction verified |
| SerialTransport without hardware: CP210x id recognition, enumeration, async open failure → closed once, client reports connectionFailed, no-op when closed, safe destruction during open, no transport switch while connected | AUTO | `ctest --preset windows-debug` → 5/5 (desktop_serial_tests 7 cases), 2026-11-08 |
| Desktop handling of a physical USB unplug | ASSUMED | not yet tested by pulling the cable |
| TwaiAdapter listen-only boot, TX slot lifetime, ISR → queue | ASSUMED | design reviewed only; requires ESP32 bench test |

## Inherited facts from the reference system (not re-verified here)

| Claim | Level | Source |
|---|---|---|
| TWAI loopback with external feedback passed once | ESP32 | old `docs/verification/week-4-verification.md` |
| Passive ~500 kbit/s traffic observed on vehicle | VEHICLE (passive RX only, no raw capture kept) | old verification record |
| SN65HVD230 path CRX stays recessive (1→1→1) | FAILED | old verification record, two modules |
| Vehicle TX raises TEC by ~8 | FAILED | user report |
| Real ECU Mode 01 / Mode 03 / Mode 04 response | ASSUMED (no evidence) | — |
| 120 Ω termination on module | ASSUMED | visual only |
