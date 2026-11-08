# Architecture

## 1. System overview

```mermaid
flowchart LR
  ECU[Vehicle ECU 0x7E0/0x7E8] -- CAN 500k --- XCVR[SN65HVD230]
  XCVR -- CTX GPIO21 / CRX GPIO22 --- FW
  subgraph FW[ESP32 firmware]
    TWAI[drivers/TwaiAdapter] --> GW[gateway-core Gateway]
    GW --> CORE[diagnostic-core ISO-TP/OBD]
    GW --> SP1[serial-protocol codec]
    SP1 --> UART[drivers/UartTransport]
  end
  UART -- USB CP210x 115200 --- DT
  subgraph DT[Qt desktop]
    TR[transport SerialTransport] --> GC[gateway GatewayClient]
    GC --> SV[services Live/DTC/Audit]
    SV --> KN[dtc-knowledge]
    SV --> AU[(audit.db)]
    KN --> KDB[(isuzu-knowledge.db RO)]
    SV --> UI[ui Widgets]
  end
  SIM[gateway-core + SimulatedEcu] -. loopback transport .- TR
```

The key idea (ADR-003): `gateway-core` is identical in the firmware and in the
desktop's simulation mode. Only the edges differ:

| Edge | Firmware vehicle | Firmware simulation | Desktop simulation / tests |
|---|---|---|---|
| CAN port | `TwaiAdapter` | `SimulatedEcu` | `SimulatedEcu` |
| Byte stream | UART0 | UART0 | in-process loopback |
| Clock | `esp_timer` | `esp_timer` | `QElapsedTimer` / fake clock |

## 2. Library layering

```mermaid
flowchart TB
  desktop[apps/desktop-qt] --> knowledge[libs/dtc-knowledge Qt SQL]
  desktop --> gateway[libs/gateway-core]
  desktop --> protocol[libs/serial-protocol]
  firmware[firmware/esp32-gateway] --> gateway
  gateway --> protocol
  gateway --> core[libs/diagnostic-core]
  protocol --> core
```

Rules: `diagnostic-core` depends on nothing but the C++17 standard library
(`<cstdint>`, `<array>`, `<cstring>`); no heap, no exceptions, no I/O.

## 3. Modules

| Module | Phase | Guide |
|---|---|---|
| `libs/diagnostic-core` | 1 ✅ | `can-isotp-obd-guide-tr.md` |
| `libs/serial-protocol` | 1 ✅ | `serial-protocol.md` |
| `libs/gateway-core` | 2 ✅ (host) | `firmware-guide-tr.md` |
| `firmware/esp32-gateway` | 2 | `firmware-guide-tr.md` |
| `apps/desktop-qt` | 3 🟡 (skeleton) | `desktop-guide-tr.md` |
| `libs/dtc-knowledge` | 3 ✅ | `dtc-database.md` |

## 4. Mode 03 end-to-end sequence

```mermaid
sequenceDiagram
  participant UI
  participant Svc as DtcScanService
  participant GC as GatewayClient
  participant GW as Gateway (fw)
  participant IT as IsoTpReceiver
  participant ECU
  UI->>Svc: readDtcs()
  Svc->>GC: request(READ_DTC)  id=41
  GC->>GW: #REQ|41|READ_DTC*CRC
  GW->>ECU: 7E0 [01 03 ...] (enqueue → TX completion)
  ECU-->>IT: 7E8 FF 10 0A 43 04 ...
  IT->>ECU: 7E0 FC 30 00 0A
  ECU-->>IT: 7E8 CF 21 ...
  IT-->>GW: payload 43 04 ...
  GW->>GW: decodeMode03()
  GW-->>GC: #RES|41|OK|count=4|dtcs=...*CRC
  GC-->>Svc: ScanResult(raw)
  Svc->>Svc: enrich (knowledge) — independent outcome
  Svc->>Svc: audit write — independent outcome
  Svc-->>UI: raw + enrichment + audit status
```
