import argparse
import csv
import struct
from pathlib import Path

MAGIC = b"EOS1EDGE"
TIME_MASK = 0x3FFFFFFF
WRAP = 1 << 30
LINES = {0: "FOCUS_DATA_A", 1: "SHUTTER_DATA_B"}


def main():
    parser = argparse.ArgumentParser(description="Decode an EOS UNO R4 edge capture")
    parser.add_argument("capture", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()

    output = args.output or args.capture.with_suffix(".csv")
    raw = args.capture.read_bytes()
    start = raw.find(MAGIC)
    if start < 0:
        raise SystemExit("EOS1EDGE header not found; verify the UNO sketch and COM port")
    payload = raw[start + len(MAGIC):]
    payload = payload[: len(payload) - (len(payload) % 4)]
    words = list(struct.unpack(f"<{len(payload) // 4}I", payload))

    wrap_base = 0
    previous_raw = None
    previous_by_line = {0: None, 1: None}
    dropped = 0
    rows = []
    index = 0
    while index < len(words):
        word = words[index]
        index += 1
        if word == 0xFFFFFFFF and index < len(words):
            dropped += words[index]
            index += 1
            continue

        channel = 1 if word & 0x80000000 else 0
        level = 1 if word & 0x40000000 else 0
        raw_time = word & TIME_MASK
        if previous_raw is not None and raw_time < previous_raw and previous_raw - raw_time > WRAP // 2:
            wrap_base += WRAP
        absolute_time = wrap_base + raw_time
        previous_raw = raw_time
        previous_line = previous_by_line[channel]
        delta = "" if previous_line is None else absolute_time - previous_line
        previous_by_line[channel] = absolute_time
        rows.append((absolute_time, channel, LINES[channel], level, delta))

    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(("time_us", "channel", "line", "level", "delta_same_line_us"))
        writer.writerows(rows)

    print(f"Decoded {len(rows)} edges to {output}")
    print(f"Dropped events reported by recorder: {dropped}")


if __name__ == "__main__":
    main()
