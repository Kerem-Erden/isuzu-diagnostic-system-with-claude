# ESP32 ↔ Desktop Serial Protocol — version 1

Implementation: `libs/serial-protocol` (shared by firmware and desktop). Decision: ADR-004.

## 1. Physical layer

USB CDC via CP210x, 115200 baud, 8N1, no flow control. UART0 is shared with the
ESP-IDF console, therefore every application message carries a start marker and a
checksum and every other byte sequence is ignored.

## 2. Frame format

```
#<body>*<CRC>\n
```

| Part | Rule |
|---|---|
| `#` | Start marker. Bytes before it on a line are discarded (boot log, noise). |
| `<body>` | Printable ASCII `0x20..0x7E`, excluding `#`, `*`, `\|` inside fields. Fields are separated by `\|`. |
| `*<CRC>` | `*` followed by exactly 4 upper-case hex digits: CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0) over `<body>` bytes. |
| `\n` | Terminator. A preceding `\r` is tolerated. |
| Max size | 256 bytes from `#` to the last CRC digit. Longer lines are discarded up to the next `\n`. |

A frame that fails any rule is dropped and counted (`malformed_frames`); the parser
resynchronises on the next `#`.

## 3. Message types

```
#REQ|<id>|<COMMAND>[|<arg>...]*CRC
#RES|<id>|OK[|<key=value>...]*CRC
#RES|<id>|ERR|<STAGE>|<CODE>|<detail>*CRC
#EVT|<seq>|<KIND>[|<field>...]*CRC
```

* `<id>`: decimal `1..2147483647`. The desktop starts each connection from a random
  value and increments; the firmware echoes it. A response is matched only by id.
* `<seq>`: firmware event counter starting at 0 (wraps after 2^31-1). A gap means events were dropped.

## 4. Commands (desktop → firmware)

| Command | Args | Side effect | OK payload |
|---|---|---|---|
| `HELLO` | — | none | `proto=1`, `fw=<semver>`, `build=<hash>`, `source=SIMULATION\|VEHICLE`, `bitrate=500000`, `tx=7E0`, `rx=7E8`, `ids=STD`, `mode04=0\|1`, `maxframe=256` |
| `PING` | — | none | `pong` |
| `STATUS` | — | none | `bus=ACTIVE\|WARNING\|PASSIVE\|BUS_OFF\|STOPPED\|LISTEN_ONLY`, `tec=`, `rec=`, `rxdrop=`, `live=0\|1` |
| `READ_PID` | `<pid hex>` | one Mode 01 request | `pid=0C`, `value=`, `unit=` |
| `LIVE_START` | `<period_ms>` (≥ 200) | starts cyclic polling | `period=` |
| `LIVE_STOP` | — | stops polling | `live=0` |
| `READ_DTC` | — | one Mode 03 request | `count=N`, `dtcs=P0087,P1093` (empty when N=0), `ecu=7E8` |
| `CLEAR_DTC` | — | **one Mode 04 request** – only when `mode04=1` | `cleared=1` |

Side-effect commands (`READ_*`, `LIVE_START`, `CLEAR_DTC`) are de-duplicated: the
firmware remembers the last 8 request ids; a repeated id returns
`ERR|PROTOCOL|DUPLICATE_REQUEST` and nothing is transmitted on CAN.

## 5. Events (firmware → desktop)

| Kind | Fields | Meaning |
|---|---|---|
| `BOOT` | `proto=1`, `source=` | Firmware (re)started — desktop must redo `HELLO` |
| `LIVE` | `<NAME>`, `<value>`, `<unit>`, `t=<ms>` | One decoded PID sample |
| `LIVE_ERR` | `<NAME>`, `<STAGE>`, `<CODE>`, `<detail>` | A cyclic PID request failed |
| `CAN_TX` / `CAN_RX` | `id=7E0`, `dlc=8`, `data=0201...`, `t=<ms>` | Trace (only when tracing enabled) |
| `DIAG` | `<category>`, `<key=value>...` | Counters, ISO-TP state, bus state changes |

## 6. Error stages and codes

Stages mirror `diag::Stage` (ADR-005): `CAN_ENQUEUE`, `CAN_TX`, `CAN_BUS`,
`RX_TIMEOUT`, `RESPONDER`, `ISOTP`, `NRC`, `OBD_DECODE`, `SERIAL`, `PROTOCOL`,
`CAPABILITY`, `INTERNAL`. `detail` is mandatory, upper-case hex padded to whole bytes (`00`, `22`, `0203`). For `NRC` it is the NRC byte.

## 7. Examples (valid CRC values)

```
#REQ|41|READ_DTC*AE0F
#RES|41|OK|count=2|dtcs=P0087,P1093|ecu=7E8*C267
#RES|42|ERR|NRC|NEGATIVE_RESPONSE|22*59FC
#EVT|7|LIVE|RPM|1726.00|rpm|t=120345*F8E3
```

See `tests/unit/serial_protocol` for exact frames with valid CRCs.
