# EOS-1V / ES-E1 to UNO R4 Wiring

## Canon N3 line definitions

| Canon line | Normal shutter function | EOS-1V PC-mode function |
|---|---|---|
| `COMMON` | remote common/reference | signal reference and return |
| `FOCUS` | half-press/focus contact | bidirectional `DATA-A` |
| `SHUTTER` | full-press/shutter contact | bidirectional `DATA-B` |

These are electrical functions, not guaranteed connector pin numbers or wire
colors. Identify conductors with a continuity meter from the actual plug before
connecting the UNO; plug-face drawings can be mirrored.

## Passive receive wiring

Use two 5.1 kΩ series resistors (about 10.2 kΩ total) on each data line and
external clamps. Do not enable internal pull-ups or connect an UNO output:

```text
FOCUS/DATA-A   --5.1k--5.1k-- UNO D2 (input)
SHUTTER/DATA-B --5.1k--5.1k-- UNO D3 (input)
COMMON ---------------------- UNO GND
```

For each protected input node, connect a 1N4007 high clamp (anode at the node,
striped cathode at UNO +5 V) and a low clamp (anode at GND, striped cathode at
the node). A single 5.1 kΩ resistor per line is acceptable only for the tested
0–5 V capture setup.

## Active DATA-A wiring

```text
D4 --10k-- 2N2222 base
2N2222 emitter -- COMMON/GND
2N2222 collector --330R-- FOCUS/DATA-A
D5 --1k-- 1N4007 anode
1N4007 striped cathode -- FOCUS/DATA-A
SHUTTER/DATA-B --5.1k-- D0 / Serial1 RX
```

D1/TX must remain disconnected. D2 is not used by the final active firmware.
Check continuity, resistor values, diode stripe direction, and absence of
shorts before powering the camera. Power the UNO before attaching the camera so
the protection clamps have a defined 5 V rail. Keep both active drivers off
until the camera is confirmed in PC mode.

