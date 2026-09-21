import argparse
import bisect
import csv
from pathlib import Path

LINE_INFO = {
    0: ("FOCUS_DATA_A", "ES_E1_TO_EOS"),
    1: ("SHUTTER_DATA_B", "EOS_TO_ES_E1"),
}


def level_at(times, levels, timestamp):
    index = bisect.bisect_right(times, timestamp) - 1
    return levels[index] if index >= 0 else 1


def decode_channel(rows, channel, baud):
    selected = [row for row in rows if int(row["channel"]) == channel]
    times = [int(row["time_us"]) for row in selected]
    levels = [int(row["level"]) for row in selected]
    bit_us = 1_000_000.0 / baud
    output = []
    next_start = -1.0

    for index in range(1, len(times)):
        start = times[index]
        if start < next_start or levels[index - 1] != 1 or levels[index] != 0:
            continue

        bits = [level_at(times, levels, start + (1.5 + bit) * bit_us) for bit in range(8)]
        value = sum(bit << position for position, bit in enumerate(bits))
        stop_bit = level_at(times, levels, start + 9.5 * bit_us)
        line, direction = LINE_INFO[channel]
        output.append((start, channel, line, direction, f"{value:02X}", value, stop_bit, stop_bit == 1))
        next_start = start + 9.75 * bit_us

    return output


def main():
    parser = argparse.ArgumentParser(description="Decode 8-N-1 UART bytes from an EOS UNO edge CSV")
    parser.add_argument("capture", type=Path, help="CSV produced by decode_edges")
    parser.add_argument("-b", "--baud", type=int, default=9600)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()

    output_path = args.output or args.capture.with_suffix(".uart.csv")
    with args.capture.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))

    decoded = []
    for channel in (0, 1):
        decoded.extend(decode_channel(rows, channel, args.baud))
    decoded.sort(key=lambda row: row[0])

    with output_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(("time_us", "channel", "line", "direction", "byte_hex", "byte_dec", "stop_bit", "framing_ok"))
        writer.writerows(decoded)

    errors = sum(not row[-1] for row in decoded)
    print(f"Decoded {len(decoded)} UART bytes to {output_path}")
    print(f"Framing errors: {errors}")
    for channel in (0, 1):
        values = [row[4] for row in decoded if row[1] == channel]
        print(f"{LINE_INFO[channel][1]} ({len(values)} bytes): {' '.join(values)}")


if __name__ == "__main__":
    main()
