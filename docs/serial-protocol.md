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
| `STATUS` | — | none | `bus=ACTIVE\|WARNING\|PASSIVE\|BUS_OFF\|STOPPED\|LISTEN_ONLY`, `tec=`, `rec=`, `rxdrop=`, `live=0\|1`, `tx=0\|1`, `badframes=`, `crcerr=` |
| `READ_PID` | `<pid hex>` (MVP PIDs only) | one Mode 01 request | `pid=0C`, `name=RPM`, `value=1726.00`, `unit=rpm` |
| `LIVE_START` | `<period_ms>` (≥ 500) | cyclic polling: the 5 PIDs round-robin, one request every `period/5` ms | `period=` |
| `LIVE_STOP` | — | stops polling | `live=0` |
| `READ_DTC` | — | one Mode 03 request | `count=N`, `dtcs=P0087,P1093` (empty when N=0) **or** `parts=K`, `ecu=7E8` |
| `TRACE` | `0\|1` | enables `CAN_TX`/`CAN_RX` events | `trace=` |
| `CLEAR_DTC` | — | **Mode 04 – not implemented in this build**: always `ERR\|CAPABILITY\|DISABLED\|00` | — |

Session rules (implemented in `libs/gateway-core`, ADR-009):

* `HELLO`, `PING`, `STATUS` work at any time. Every other command before `HELLO` returns
  `ERR|PROTOCOL|INVALID_STATE|00` and transmits nothing.
* Any valid request refreshes the session. After **5 s** without one, the session ends:
  live polling stops, queued requests are dropped and the CAN controller returns to
  listen-only. The desktop must send `PING` at least every 2 s while live data runs.
* Only one user request (`READ_PID` / `READ_DTC`) can be outstanding; a second one returns
  `ERR|INTERNAL|BUSY|00`. A user request issued during a live sample runs right after it.
* Live polling stops itself (`EVT DIAG live state=STOPPED reason=no_response`) after 3
  consecutive timeouts / TX failures, and skips a PID the ECU rejects with NRC 0x11/0x12/0x31.

Side-effect commands (`READ_*`, `LIVE_START`, `CLEAR_DTC`) are de-duplicated: the
firmware remembers the last 8 request ids; a repeated id returns
`ERR|PROTOCOL|DUPLICATE_REQUEST|00` and nothing is transmitted on CAN.

Long DTC lists: if more than 30 codes are stored, the codes are sent first as
`EVT|<seq>|DTC_PART|req=<id>|dtcs=...` events (30 per event), then the response
`RES|<id>|OK|count=N|parts=K|ecu=7E8`. The desktop concatenates the parts in order and
checks that the total equals `count`.

## 5. Events (firmware → desktop)

| Kind | Fields | Meaning |
|---|---|---|
| `BOOT` | `proto=1`, `source=` | Firmware (re)started — desktop must redo `HELLO` |
| `LIVE` | `<NAME>`, `<value>`, `<unit>`, `t=<ms>` | One decoded PID sample |
| `LIVE_ERR` | `<NAME>`, `<STAGE>`, `<CODE>`, `<detail hex>` | A cyclic PID request failed |
| `DTC_PART` | `req=<id>`, `dtcs=...` | Part of a long Mode 03 result (see §4) |
| `CAN_TX` / `CAN_RX` | `id=7E0`, `dlc=8`, `data=0201...`, `t=<ms>` | Trace (only when `TRACE 1`; RX only for the configured responder id) |
| `DIAG` | `session\|live\|protocol`, `<key=value>...` | Session end, live stop reason, unparsable frames |

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
