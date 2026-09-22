# Active N3 Interface Validation

The UNO R4 WiFi directly established an EOS-1V PC session without ES-E1:

```text
FF RX (1/1): F4
F6 RX (17/17): F6 0E 38 FF 1A 17 C1 18 10 1C 00 04 00 00 00 0D 7E
F1 RX (6/6): F1 03 01 00 34 35
FULL HANDSHAKE OK
EXIT RX1 (1/1): F2
```

The camera accepted both `F2 -> F2` and the original application's two-stage
`F2 -> F4`, host `F4`, delay, `F2 -> F2` exit.

The same `ra4_camera_bridge.ino` was subsequently compiled for
`arduino:renesas_uno:minima` and flashed to an UNO R4 Minima. USB-CDC host
communication and the EOS-1V bridge exchange then completed normally. The
WiFi and Minima builds use the same RA4 camera-side transport; only the board
FQBN and USB hardware path differ.

## Read coverage

The active firmware completed `E8`, `FC`, `E1`, all four C.Fn blocks, all 16
P.Fn blocks, F3/A1/D1 clock flow, E3 roll headers, variable E4 records, roll
end and all-end packets. Two rolls/50 records matched the ES-E1 capture. An
empty database correctly returned `E1=0000` and skipped E3/E4. The firmware
uses a 100-roll safety limit and terminates normally only on the explicit E3
all-end packet.

## DATA-A state machine

1. D4/D5 are disabled before the session.
2. The initial `FF` uses an NPN low start bit and released high bits.
3. After the first two-or-more-low run, D5 actively maintains high/idle.
4. D5 is disabled before every NPN low bit.
5. Both drivers are disabled on normal exit, timeout, or protocol failure.

The line-bit captures were `FF: 0111111111`, `F4: 0001011111`, and
`F6: 0011011111` (start, data bits, stop).
