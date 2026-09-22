# Original Canon application compatibility validation

Date: 2026-09-22  
Host: Windows 11  
Camera: Canon EOS-1V  
Bridge: Arduino UNO R4 WiFi, RA4 camera bridge firmware

## Configuration

The patched original Canon software was run from an isolated test directory. `EOSBRIDGE.INI` forced the Arduino backend:

```ini
[Bridge]
Backend=Arduino
```

The experimental UMDF package previously used while evaluating a virtual-device approach was uninstalled. The production path is entirely user mode: the patched Canon driver calls `EOSHOOKX.dll`, and the DLL converts its split serial writes and reads into the firmware's `O1` exchange frames.

## Verified behavior

- UNO R4 discovery by USB hardware ID `VID_2341&PID_1002`.
- 115200-baud host link and valid `O1` request/response framing.
- EOS-1V startup handshake: `FF/F4`, acknowledgement, `F6` identification, and `F1` identity.
- Continued idle receive polling in one open Canon session.
- C.Fn reads through the original Canon UI.
- P.Fn reads through the original Canon UI.
- C.Fn writes and the application's read-back verification.
- P.Fn writes and the application's read-back verification.
- Shooting-data transfer, TMP-to-EFD conversion, and automatic Memory launch.
- Normal Remote exit, completed final `F2` exchange, and visible removal of the camera PC-mode indicator.

## Original Remote session lifetime

The original application keeps the connection alive across high-level operations. One Windows process and one pseudo-COM handle carried the initial identity exchange, C.Fn and P.Fn reads and writes, shooting-data status, all `E3/E4` roll transfers, and the return to idle polling. Opening or completing an individual settings dialog did not produce a real `F2` camera exit.

While idle, Remote repeatedly performs short reads so that it can receive asynchronous `F4` synchronization bytes. Additional `FF/F4/F1` and `F4/F6` sequences observed between actions are in-session synchronization boundaries. They do not mean that the physical PC-mode session was closed and recreated.

Remote does write an intermediate one-byte `F2` while tearing down its COM-style layer. `EOSHOOKX.dll` acknowledges that write locally and defers the real camera command. The real exit is sent only from the final process-exit path. In the verified run the camera first answered `F4`; the bridge acknowledged it, waited, sent the final `F2`, received `F2`, and then released both drivers. The camera's PC indicator disappeared at that point.

This behavior is a compatibility requirement: a replacement must keep transport and camera state across multiple UI actions, continue servicing asynchronous input while idle, and send one real exit only at final shutdown or explicit error cleanup.

The first attempted run failed because the original ES-E1 and the Arduino interface were both connected to the camera-side bus. With the Arduino interface as the only camera connection, the same build completed the handshake and setting operations.

A subsequent shooting-data attempt showed that the Canon driver reads a variable `E3` packet in two calls: a two-byte command/length header followed by the body. The first Arduino implementation requested only those first two bytes from the MCU, allowing the remaining camera burst to overrun the small hardware UART buffer before the second host request. The compatibility DLL now reads ahead up to one complete 64-byte camera packet for a two-byte header request, returns the header immediately, and queues the remainder for the Canon driver's next read.

The corrected build completed a fresh end-to-end download. `E3` and `E4` packets of 36, 20, and 4 total bytes were returned to the Canon driver using its expected two-call layout without truncation. Five roll segments were written as temporary files, renamed by the original Canon logic to `FI000000.EFD` through `FI000004.EFD`, and the original Remote-to-Memory launch redirect succeeded.

## Remaining regression checks

- camera clock read/write through the original UI;
- camera ID read/write when the camera permits it;
- forced `Backend=ES-E1` operation with the original cable.
