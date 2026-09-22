# UNO R4 Minima ES-E1 USB identity test

This repository deliberately keeps two firmware paths:

| Path | USB identity | Host transport | Status |
|---|---|---|---|
| `ra4_camera_bridge` | Arduino UNO R4 (`2341:0069` on Minima) | CDC ACM carrying O1 frames | Stable and hardware verified |
| `ra4_es_e1_id_bridge` | Canon ES-E1 test identity (`04A9:3040`) | CDC ACM carrying the same O1 frames | Experimental; descriptor and build verification only |

The experimental image changes only the USB identity and strings. It does not
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
