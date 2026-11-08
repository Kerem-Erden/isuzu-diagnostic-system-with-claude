# Risk Register

Likelihood / impact: L = low, M = medium, H = high.

| ID | Risk | L | I | Mitigation | Owner / status |
|---|---|---|---|---|---|
| RK-01 | Physical CAN path (SN65HVD230 ↔ ESP32) is faulty; no ECU response ever arrives | H | H | Electrical checklist and bench 1→0→1 test in `vehicle-validation-plan.md` before any vehicle TX; firmware defaults to listen-only | Open – needs user + instruments |
| RK-02 | Accidental Mode 04 on a real vehicle clears emissions-relevant data / readiness monitors | L | H | Compile-time `GATEWAY_ENABLE_MODE04` absent from all profiles, runtime capability flag false, desktop UI hides action unless capability present; no auto-retry | Mitigated by design |
| RK-03 | Active TX into a vehicle bus with wrong bitrate causes error frames / bus disturbance | M | H | Bitrate is configured, never probed with TX; listen-only boot; TX only on an explicit user request; back-off after silence | Mitigated by design |
| RK-04 | Simulation mistaken for vehicle data | M | H | Source identity in `HELLO`; every screen banner; audit rows carry source; reconnect identity must match | Mitigated by design |
| RK-05 | Boot log / `printf` corrupts protocol stream on shared UART0 | H | M | `#` prefixed frames with CRC-16; non-prefixed lines ignored; ESP-IDF console log level lowered in app profiles | Mitigated by design |
| RK-06 | CP210x driver blocks on close after unplug, freezing UI | M | H | QSerialPort on worker thread, UI talks via queued signals | Mitigated by design |
| RK-07 | Knowledge DB schema drift or missing file | M | M | Startup validation, structured `KnowledgeError`, raw DTC still shown | Mitigated by design |
| RK-08 | Qt Serial Port module missing | — | — | Installed by the user 2026-11-08 (Qt 6.11.1 MinGW); SerialTransport builds | **Closed** |
| RK-09 | Local ESP-IDF is v6.1, target is 6.0.2 – API drift could hide 6.0.2-only build issues | M | L | Both profiles build cleanly with v6.1 locally; CI pins v6.0.2 | Mitigated locally; CI run pending |
| RK-10 | Knowledge content derived from OEM manuals may be inaccurate for a specific vehicle | M | M | UI wording "possible causes", source/application shown, never a definitive diagnosis | Accepted |
| RK-11 | ECU uses a different physical address than 0x7E0/0x7E8 or requires 29-bit IDs | M | M | Configurable IDs; read-only passive capture first to observe responders | Open |
| RK-12 | NRC 0x78 storm or endless pending responses | L | M | P2* deadline bounded, maximum pending count | Mitigated by design |
| RK-13 | Scope creep (live warnings, J1939, more modules) delays MVP | M | M | Deferred list in `REMAINING_WORK.md` | Accepted |
| RK-14 | Multi-session work loses context | M | M | `PROJECT_STATE.md`, atomic local commits | Mitigated by process |
