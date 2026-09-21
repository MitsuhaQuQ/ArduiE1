# Serial console command reference

Open the USB serial console at 115200 baud. Except for the offline safety self-test, camera commands require a fresh PC-mode session.

## Read-only commands

| Command | Action |
|---|---|
| `h` | Full session handshake |
| `i` | Immediate F4-to-F6 handshake variant |
| `r` | Read settings/status flow |
| `d` | Read settings plus the first film header |
| `f` | Download all available film records |
| `c` | Read current and three registered C.Fn groups |
| `p` | Read all P.Fn blocks |
| `k` | Read camera clock and auxiliary data |
| `e` | Read C.Fn, P.Fn, clock, and status in one session, then exit once |
| `g` | Run shooting-mask safety checks offline; sends nothing to the camera |
| `m` | Establish a session and open a 20-second power-loss test window |

## State-changing validation commands

These commands were created for controlled protocol verification. Read the firmware messages and preserve the stated restore value.

| Command | Action |
|---|---|
| `v` / `u` | Temporarily change / restore the P.Fn-4 shutter limit |
| `a` / `b` | Temporarily change / restore the P.Fn-5 aperture limit |
| `3` / `#` | Temporarily change / restore P.Fn-3 |
| `2` / `@` | Temporarily change / restore P.Fn-12 |
| `[` / `]` | Temporarily change / restore the P.Fn global trailing byte |
| `9` / `(` | Temporarily change / restore C.Fn-19 |
| `j` / `l` | Temporarily change / restore camera ID |
| `w` / `x` | Temporarily change / restore the shooting-data field mask |
| `n` / `x` | Switch to 16-byte records / restore the 32-byte baseline |
| `o` / `x` | Switch to 8-byte records / restore the 32-byte baseline |
| `sYYMMDDhhmmss` | Set and verify camera clock |

Every restore command must be issued from a fresh PC-mode session.

## Destructive command

`Z`, followed by `!` within ten seconds, deletes all film records stored in the camera. The two-step sequence is intentional. Back up and verify the records first.
