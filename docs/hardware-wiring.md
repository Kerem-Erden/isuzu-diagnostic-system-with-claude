# Hardware Wiring

| SN65HVD230 module | ESP32 DevKit | Kconfig |
|---|---|---|
| CTX (D, driver input) | GPIO21 | `CONFIG_ISUZU_TWAI_TX_GPIO` |
| CRX (R, receiver output) | GPIO22 | `CONFIG_ISUZU_TWAI_RX_GPIO` |
| 3V3 | 3V3 | — |
| GND | GND | — |
| CANH / CANL | vehicle OBD pins 6 / 14 | — |

USB: CP210x USB–UART bridge on the DevKit, UART0, 115200 8N1.

## Unverified items (must be measured before any vehicle TX)

The reference system's bench test failed (CRX stayed recessive with two modules). Nothing below
has been measured yet:

| Check | Expected | Instrument |
|---|---|---|
| Module supply at VCC pin | 3.3 V ± 0.1 V | multimeter |
| Rs pin (slope/standby) | tied to GND (high-speed mode); if high, the chip is in **standby and does not transmit** | multimeter |
| CTX idle level | ~3.3 V (recessive) | multimeter |
| CRX idle level | ~3.3 V (recessive) | multimeter |
| CANH–CANL idle differential | ~0 V; each ~2.3–2.5 V to GND | multimeter |
| Termination on module ("121" = 120 Ω) | ~120 Ω between CANH/CANL with module unpowered and disconnected; ~60 Ω on a terminated vehicle bus | multimeter |
| Continuity GPIO21→CTX, GPIO22→CRX | < 1 Ω | multimeter |

The full step-by-step procedure, including the disconnected bench 1→0→1 test, is in
`vehicle-validation-plan.md` (to be written in Phase 6).

## Firmware behaviour relevant to wiring

* Vehicle profile boots **listen-only**: the controller never drives CTX dominant until a desktop
  session requests data.
* The TX pin stays configured in listen-only so CTX is held recessive (not floating).
