# EOS-1V UNO R4 camera bridge

This directory contains three RA4M1 firmware paths. ESP32-S3 is not a project
target and no ESP32-S3 firmware or recovery material is kept in this repository.

| Sketch | Board | USB behavior | Status |
|---|---|---|---|
| `ra4_camera_bridge/` | UNO R4 Minima or WiFi | Normal Arduino CDC carrying O1 frames | Stable; hardware verified |
| `ra4_es_e1_id_bridge/` | UNO R4 Minima only | Experimental `04A9:3040` CDC identity carrying O1 frames | Camera session verified through open1V |
| `ra4_es_e1_klsi_bridge/` | UNO R4 Minima only | Experimental `04A9:3040` KLSI/MCCI-style vendor transport | Transport and patched Remote session verified |

All three sketches use the RA4M1 for the timing-critical camera interface. In
the two O1 variants, the host application owns the EOS-1V protocol state
machine and firmware only checks and forwards bounded requests. See
[`protocol.md`](protocol.md). The KLSI/MCCI-style image forwards the recovered
64-byte vendor blocks and does not expose CDC or O1.

## Board choice

UNO R4 Minima is the recommended development board. Its RA4M1 directly owns the USB device controller, which makes USB descriptor and transport experiments reproducible.

UNO R4 WiFi remains supported for the stable CDC/O1 camera bridge. It is not recommended for original-device identity or transport emulation because its USB-C connection is mediated by the board's ESP32-S3 bridge. This project will not replace or modify that ESP32-S3 firmware.

## Build

Stable Minima firmware:

```powershell
arduino-cli compile --fqbn arduino:renesas_uno:minima firmware/eos1v_winusb_bridge/ra4_camera_bridge
```

Stable UNO R4 WiFi bridge:

```powershell
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi firmware/eos1v_winusb_bridge/ra4_camera_bridge
```

Experimental Minima ES-E1-ID CDC image:

```powershell
.\tools\build-es-e1-id-test.ps1
```

Experimental Minima KLSI/MCCI-style image:

```powershell
.\tools\build-es-e1-klsi-test.ps1
```

The ID image remains CDC ACM with O1 framing and is not a replacement for the
legacy Canon driver. The KLSI/MCCI-style image has no CDC port and requires the
documented Windows binding and DFU recovery procedure. Read
[`docs/es-e1-usb-identity-test.md`](../../docs/es-e1-usb-identity-test.md) before
building either experimental image.

The stable and ES-E1-ID sketches document the hardware-validated 1N4007
high-assist diode. The KLSI experiment records the 1N4148 used for that test.
Do not change diode type merely because the USB transport changes; use the
component actually installed and re-check polarity and DATA-A high level.

## Safety behavior

- Both camera drivers idle released.
- `EXCHANGE` limits host payload and expected camera reply sizes.
- `RELEASE` immediately releases the line and drains pending camera input.
- A 30-second host-idle timeout releases an active high-assist state.

Disconnect the camera-side DATA-A and DATA-B wiring before changing firmware.
