# EOS-1V Hardware Validation

Validated platform: Canon EOS-1V, Canon ES-E1, Arduino UNO R4 WiFi, N3 remote
connector.

## Confirmed electrical mapping

| N3 line | PC-mode role | Direction |
|---|---|---|
| `COMMON` | common reference | — |
| `FOCUS` | `DATA-A` | ES-E1/controller -> camera |
| `SHUTTER` | `DATA-B` | camera -> ES-E1/controller |

The lines are idle-high, non-inverted, approximately 0–4.7/5.0 V, 9600 baud,
8-N-1, LSB first. The measured bit interval is about 104.17 us and a byte is
about 1.042 ms. Estimated source resistance is tens of kilohms (about 28.3 kΩ
on DATA-A and 21.5 kΩ on DATA-B in the tested setup); these are explanatory
measurements, not production component values.

Passive capture matched 79/79 bytes from ES-E1 to the camera and 1989/1989
bytes from the camera to ES-E1. No additional line encoding was found.

## DATA-A driver

The camera's DATA-A pull-up disappears during long low pulses. The validated
driver therefore uses two isolated paths:

```text
D4 --10k-- 2N2222 B; E -- COMMON/GND; C --330R-- DATA-A
D5 --1k-- 1N4007 anode; cathode/stripe -- DATA-A
DATA-B --5.1k-- D0 / Serial1 RX
```

Measured behavior:

- after a 104 us low pulse, release recovered high in about 16.15 us;
- after a 312 us low pulse, the camera pull-up could remain low;
- a 10–20 us high assist restored the line;
- permanently driving high before the first `FF` prevented a response.

The state machine releases both paths before the session, uses release-only
high bits for the first `FF`, enters active-high mode after the first run of
two or more low data bits, disables high assist before every low bit, and
releases everything on exit or error.

## Active validation

Without ES-E1, UNO R4 completed:

```text
FF -> F4
F4
F6 -> F6 0E 38 FF 1A 17 C1 18 10 1C 00 04 00 00 00 0D 7E
F1 -> F1 03 01 00 34 35
```

The response matched the ES-E1 capture. C.Fn/P.Fn reads, clock operations,
shooting status, multi-roll E3/E4 download, and both F2 exit forms were also
validated. The final breadboard is suitable for protocol development; scope
measurements, ESD protection, PCB layout, and tolerance testing remain future
hardware work.

