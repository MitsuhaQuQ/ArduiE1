import argparse
import csv
import struct
from pathlib import Path

FORMATS = {
    b"EOS1RXA1": ("FOCUS_DATA_A", "ES_E1_TO_EOS"),
    b"EOS1RXB1": ("SHUTTER_DATA_B", "EOS_TO_ES_E1"),
}


def main():
    parser = argparse.ArgumentParser(description="Decode UNO R4 hardware-UART RX records")
    parser.add_argument("capture", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()

    output = args.output or args.capture.with_suffix(".csv")
    raw = args.capture.read_bytes()
    found = [(raw.find(magic), magic) for magic in FORMATS]
    found = [(position, magic) for position, magic in found if position >= 0]
    if not found:
        raise SystemExit("EOS1RXA1/EOS1RXB1 header not found")
    start, magic = min(found)
    line, direction = FORMATS[magic]
    position = start + len(magic)
    if position + 4 > len(raw):
        raise SystemExit("Capture header is truncated")
    baud = struct.unpack_from("<I", raw, position)[0]
    position += 4

    records = []
    while position + 5 <= len(raw):
        timestamp = struct.unpack_from("<I", raw, position)[0]
        value = raw[position + 4]
        records.append((timestamp, line, direction, f"{value:02X}", value))
        position += 5

    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(("time_us", "line", "direction", "byte_hex", "byte_dec"))
        writer.writerows(records)

    print(f"Configured baud: {baud}")
    print(f"Line: {line}; direction: {direction}")
    print(f"Decoded {len(records)} received bytes to {output}")
    for offset in range(0, len(records), 32):
        print(" ".join(row[3] for row in records[offset:offset + 32]))


if __name__ == "__main__":
    main()
