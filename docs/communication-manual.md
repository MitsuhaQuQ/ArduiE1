# EOS-1V Communication Behavior Manual

Status date: 2026-09-21  
Validated with: Canon EOS-1V, Arduino UNO R4 WiFi, Canon N3 connector

This document records behavior observed on a real camera. “Validated” means the
camera completed the operation. Offline parser and build checks are identified
as such; unknown meanings must not be treated as protocol constants.

## Physical layer

| N3 line | PC-mode function | Direction |
|---|---|---|
| `COMMON` | reference/return | — |
| `FOCUS` | `DATA-A` | controller to camera |
| `SHUTTER` | `DATA-B` | camera to controller |

The link is idle-high, non-inverted, approximately 0–4.7/5.0 V, 9600 baud,
8-N-1, LSB first. A bit is about 104.17 us and a complete character about
1.042 ms. Passive captures matched 79/79 controller bytes and 1989/1989
camera bytes from the original ES-E1 session.

`DATA-A` needs an isolated low driver and an isolated high-assist path. Before
the first `FF`, both drivers must be high impedance; a permanently driven UART
TX prevents the camera from answering. After a long low pulse the camera's
weak pull-up may not recover, so the high-assist path is enabled only after
the session has entered active transmission. The validated breadboard is:

```text
LOW:  D4 --10k-- 2N2222 base; emitter -- COMMON/GND
      collector --330R-- FOCUS/DATA-A
HIGH: D5 --1k-- 1N4007 anode; cathode/stripe -- FOCUS/DATA-A
RX:   SHUTTER/DATA-B --5.1k-- D0 / Serial1 RX
```

D1/TX and D2 are disconnected in the final active circuit. D4 and D5 are
released on startup, error, timeout, and exit.

## Frames and checks

Normal replies are `command length payload checksum`; total length is normally
`length + 3`, and the checksum is the low byte of the payload sum. Validate
command, declared length, actual length, bounds, and checksum. `F4` is an
in-band synchronization event. `E4 01 00 00` ends one roll and `E3 01 00 00`
ends the complete film download.

## Session lifecycle

The camera must already be in PC mode:

```text
FF -> F4
F4
F6 -> identification block
F1 -> identity/status block
```

`F6` is dynamic and must not be hard-coded as a device signature. If the
initial `FF` fails, release both drivers and send no `F6`, `F1`, or `F2`.

Two exits were observed:

```text
single stage: F2 -> F2
two stage:    F2 -> F4, host F4, wait about 300 ms, F2 -> F2
```

After exit the camera returns to normal metering/shutter mode and must be put
back into PC mode before an independent session.

## Continuous logical actions

The original Remote application keeps one physical COM/PC-mode session open
while it performs multiple logical actions. It does not send `F2` after each
dialog or feature. A later action uses an `FF/F4/F1` boundary inside the same
physical session; asynchronous `F4` must be acknowledged while waiting. A
normal multi-action sequence has been validated as:

1. C.Fn read/write and verification;
2. P.Fn read/write and verification;
3. clock read/write;
4. `E8/FC/E1` shooting-data status;
5. variable-length `E3/E4` film download;
6. one final exit at application shutdown.

The shutdown of a high-level operation, closing a virtual COM handle, and the
final camera PC-mode exit are separate layers. A compatibility implementation
must not turn every handle close into a physical `F2`.

## Validated command groups

- `F1/F9`: camera identity, status, and ID read/write (`0 -> 12 -> 0`).
- `F3/F8`: BCD clock read/write (`YY MM DD hh mm ss`) with read-back.
- `D1/D5/D7/D9`: current and registered C.Fn blocks.
- `D3 DD C5 C6 C1 C3 C4 CB CC CA C7 C8 C0 CD CF CE`: all P.Fn blocks.
- `E7/E8/E9`: shooting-field width/mask, validated at 8, 16, and 32 bytes.
- `E1/E3/E4`: roll count, variable-length roll headers and records.
- `E2`: controlled delete-all, followed by a new-session status check.

## Film data and safety

Read `E1` first. When the roll count is zero, do not send `E3` or `E4`. For a
nonzero count, read each variable-length `E3` header and `E4` record until the
explicit roll/all-end packets. Do not assume 36-byte records or a fixed frame
count; the camera can retain up to 100 roll segments. Preserve order and full
records because repeated frame numbers can be legitimate.

Before any write, read and validate the exact baseline, write once, and read
back. Never automatically retry an ambiguous write or delete. Restore test
values in a fresh PC-mode session and verify again. The MCU field-mask safety
checks reject missing mandatory bits, incomplete composite fields, invalid
Bulb combinations, over-capacity masks, and E7/mask width mismatches.

## Known unknowns

The semantics of auxiliary `A1` bytes, D1 trailing bytes, reserved P.Fn bits,
some E4 flags, and the physical erase scope of `E2` remain unresolved. Keep
these bytes intact and expose them as raw data rather than inventing meanings.

