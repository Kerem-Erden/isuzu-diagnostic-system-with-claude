#!/usr/bin/env python3
"""Drive the ESP32 BENCH self-test firmware (W15) and print the verdict.

SAFETY: the bench firmware drives CTX and transmits one self-test frame. CANH/CANL must NOT be
connected to a vehicle. The tool refuses to run without --vehicle-disconnected and refuses any
gateway that does not report source=BENCH.

Usage (ESP-IDF Python already has pyserial):
    python tools/bench_check.py COM5 --vehicle-disconnected [--csv bench.csv]

Independent implementation of protocol v1 framing/CRC (docs/serial-protocol.md), so it also
cross-checks the C++ codec.
"""
import argparse
import datetime
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit("pyserial missing: use the ESP-IDF Python environment or `pip install pyserial`")


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(body: str) -> bytes:
    return f"#{body}*{crc16(body.encode()):04X}\n".encode()


def parse(line: bytes):
    """Return the body of a valid frame, or None (noise / bad CRC)."""
    line = line.strip()
    start = line.find(b"#")
    if start < 0 or len(line) - start < 7 or line[-5:-4] != b"*":
        return None
    body = line[start + 1:-5]
    try:
        return body.decode() if crc16(body) == int(line[-4:], 16) else None
    except ValueError:
        return None


class Link:
    def __init__(self, port: str):
        self.port = serial.Serial()
        self.port.port, self.port.baudrate, self.port.timeout = port, 115200, 0.05
        self.port.dtr = self.port.rts = False  # ESP32 DevKit: do not reset / enter bootloader
        self.port.open()
        self.buffer = b""
        self.next_id = int(time.time()) % 1_000_000 + 1
        self.events = []

    def request(self, command: str, timeout: float):
        rid = self.next_id
        self.next_id += 1
        self.port.write(frame(f"REQ|{rid}|{command}"))
        end = time.time() + timeout
        while time.time() < end:
            self.buffer += self.port.read(256)
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                body = parse(raw)
                if body is None:
                    continue
                if body.startswith(f"RES|{rid}|"):
                    return body
                if body.startswith("EVT|"):
                    self.events.append(body)
        return None


def fields(body: str) -> dict:
    return dict(f.split("=", 1) for f in body.split("|") if "=" in f)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port")
    parser.add_argument("--vehicle-disconnected", action="store_true",
                        help="confirm that CANH/CANL are NOT connected to a vehicle")
    parser.add_argument("--csv", help="append the result to this CSV file")
    args = parser.parse_args()
    if not args.vehicle_disconnected:
        print("Refusing to run: the bench test drives the CAN transmitter. Disconnect CANH/CANL from any "
              "vehicle and pass --vehicle-disconnected.")
        return 2

    link = Link(args.port)
    hello = link.request("HELLO", 3.0)
    if hello is None:
        print("No HELLO answer. Is the bench firmware flashed and the port free?")
        return 1
    identity = fields(hello)
    if identity.get("source") != "BENCH":
        print(f"Refusing to run: gateway reports source={identity.get('source')}, not BENCH.")
        return 2
    print(f"bench firmware fw={identity.get('fw')} CTX=GPIO{identity.get('ctx')} CRX=GPIO{identity.get('crx')} "
          f"bitrate={identity.get('bitrate')}")

    result = link.request("BENCH_RUN", 5.0)
    if result is None:
        print("No BENCH_RUN answer within 5 s.")
        return 1
    r = fields(result)
    print(f"GPIO echo : {r.get('gpio')}   levels (set/pad/rx): {r.get('levels', '-')}")
    print(f"Loopback  : {r.get('loopback')}   {r.get('lberr', '')}")
    for event in link.events:
        if "|DIAG|bench_" in event:
            step, hint = event.split("|")[3], fields(event).get("hint", "")
            print(f"  {step}: {hint}")
    print(f"VERDICT   : {r.get('verdict')}")
    if args.csv:
        with open(args.csv, "a", encoding="utf-8") as out:
            out.write(f"{datetime.datetime.now().isoformat(timespec='seconds')},{args.port},{identity.get('fw')},"
                      f"{r.get('gpio')},{r.get('levels', '')},{r.get('loopback')},{r.get('lberr', '')},{r.get('verdict')}\n")
    return 0 if r.get("verdict") == "PASS" else 3


if __name__ == "__main__":
    sys.exit(main())
