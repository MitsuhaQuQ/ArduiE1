# UNO R4 Minima ES-E1 USB identity test

This repository deliberately keeps three firmware paths:

| Path | USB identity | Host transport | Status |
|---|---|---|---|
| `ra4_camera_bridge` | Arduino UNO R4 (`2341:0069` on Minima) | CDC ACM carrying O1 frames | Stable and hardware verified |
| `ra4_es_e1_id_bridge` | Canon ES-E1 test identity (`04A9:3040`) | CDC ACM carrying the same O1 frames | Experimental; camera communication verified through open1V |
| `ra4_es_e1_klsi_bridge` | Canon ES-E1 identity (`04A9:3040`) | Original 64-byte KLSI/MCCI-style vendor protocol | Experimental; transport hardware verified |

The `ra4_es_e1_id_bridge` image changes only the USB identity and strings. It does not
implement the original ES-E1 KLSI/MCCI USB transport and therefore is not a
drop-in replacement for the original `EOSmdm` driver. The current open1V host
software can use it as a COM device because its serial payload remains O1.

## Build

Install Arduino CLI and version 1.6.0 of `arduino:renesas_uno`, then run:

```powershell
.\tools\build-es-e1-id-test.ps1
```

The script temporarily changes the Minima VID/PID and USB string table, performs
a clean build, and restores both core files in a `finally` block. It refuses to
patch an unfamiliar core layout. The stable firmware build remains:

```powershell
arduino-cli compile --fqbn arduino:renesas_uno:minima firmware/eos1v_winusb_bridge/ra4_camera_bridge
```

## Windows binding and recovery

`04A9:3040` is also the identity of a real Canon ES-E1. Windows stores driver
selection by hardware identity, so the test image and a real cable can collide
in driver binding. Disconnect the real ES-E1 while testing. Use Zadig or Device
Manager to choose the required CDC driver; this repository does not provide an
INF.

USB upload discovery may stop after the test identity starts. Press the Minima
RESET button twice to enter its Arduino DFU bootloader, then upload the stable
`ra4_camera_bridge` sketch to restore the normal identity.

## Required hardware validation

1. Windows enumerates `VID_04A9&PID_3040` and creates a COM port.
2. open1V discovers the COM port and completes its O1 ping.
3. A read-only camera command succeeds.
4. The final `F2` clears the camera PC indicator.
5. Stable firmware restores `VID_2341&PID_0069`.

Compatibility with unmodified Canon software is a separate phase. It requires
matching the original descriptors, endpoints, control transfers, and 64-byte
KLSI/MCCI framing rather than CDC ACM.

## Validation result (2026-09-22)

The experimental image was flashed to an UNO R4 Minima and enumerated as:

- hardware ID `USB\VID_04A9&PID_3040`;
- product `Canon EOS USB Cable (CDC test)`;
- unique RA4 serial number preserved.

Windows reused the existing Zadig whole-device WinUSB binding for the real
ES-E1 hardware ID, so it did not create a COM port. open1V was extended to
open that interface explicitly, initialize the CDC control interface, select
the associated CDC data interface, and drain stale input between processes.
O1 ping succeeded and a complete read-only camera identity session returned
`type=1 id=64 status=0x34`.

This validates the experimental identity with the current O1 bridge over both
USB enumeration and an actual EOS-1V session. It still does not establish
compatibility with the original ES-E1 KLSI/MCCI transport or unmodified Canon
software.

## Original-transport experimental image

`ra4_es_e1_klsi_bridge` implements the USB behavior recovered from original
ES-E1 captures and the host backend:

- one vendor-class interface with bulk OUT `0x02` and bulk IN `0x81`;
- fixed 64-byte blocks containing a two-byte little-endian payload length and
  at most 62 payload bytes;
- host-to-interface vendor request `1`, value `0`, carrying the five-byte serial
  configuration;
- vendor request `3`, value `3` to enable the read channel and value `2` to
  disable it;
- asynchronous forwarding between USB blocks and the validated 9600 8N1
  EOS-1V camera-side circuit.

Build it with:

```powershell
.\tools\build-es-e1-klsi-test.ps1
```

The script temporarily changes the installed Minima core to emit the recovered
USB 1.00 vendor descriptors (`bcdDevice 1.03`), then restores every modified
core file in `finally`. The original cable reports an 8-byte endpoint zero;
the Minima image keeps the RA4 core's 64-byte endpoint zero because its RUSB2
path failed configuration-descriptor enumeration at 8 bytes. This image has no CDC COM port;
returning to stable firmware requires entering the Minima DFU bootloader by
pressing RESET twice.

The build passes with Arduino Renesas core 1.6.0.

## Original-transport hardware result (2026-09-22)

The image enumerated successfully through the existing Zadig WinUSB binding as
interface `FF/00/00`, bulk OUT `0x02`, bulk IN `0x81`, with 64-byte packets.
The standalone original-transport probe then completed:

```text
request 1 (5 bytes) x3 -> OK
request 3, value 3     -> OK
FF -> F4, F4 echo      -> OK
F6 -> 17-byte response -> checksum OK
F1 -> 01 40 34 75      -> checksum OK (camera ID 64)
F2 -> F2               -> clean exit
request 3, value 2     -> OK
```

The first enumeration attempt used the original cable's 8-byte EP0 and failed
at the configuration descriptor. Keeping the RA4 core's 64-byte EP0 resolved
it. The original vendor requests and bulk framing are otherwise preserved.
The patched Canon program also completed a sustained session through this path.
Its startup `FF/F4/F6/F1` sequence and all four C.Fn blocks (`D5`, `D7`, `D9`,
`D1`) were read on one open handle. Normal Remote shutdown completed the
observed two-stage exit without reopening USB:

```text
F2 -> F4 -> echo F4 -> F2 -> F2
```

Request `3`, value `2` is also treated as a physical session boundary: the
firmware disables both camera-line drivers and discards incomplete USB/camera
input. This permits a new application connection after a probe or a clean exit.

Camera bytes may arrive immediately after the read channel is enabled. They are
buffered until the host sends its first bulk OUT block, preventing an early
bulk-IN flush from contending with the final endpoint-zero configuration request
on the RA4 USB stack.

The camera's initial `F4` is effectively a one-shot event. If the Minima is
reset while the camera already shows its PC icon, that event can be lost. For a
fresh hardware session, start or reset the Minima first, then make the camera
leave and re-enter PC mode before opening the host program.

After completing the compatibility tests, the same Minima was restored through
its DFU bootloader to the stable `ra4_camera_bridge` image. Windows enumerated
it again as an Arduino UNO R4 Minima on `COM5`, and an open1V O1 ping returned
firmware marker `RA4`. This confirms recovery from the `04A9:3040` test identity
without changing the installed Arduino core.
