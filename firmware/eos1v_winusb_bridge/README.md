# EOS-1V UNO R4 camera bridge

This directory contains the two supported RA4M1 firmware paths. ESP32-S3 is not a project target and no ESP32-S3 firmware or recovery material is kept in this repository.

| Sketch | Board | USB behavior | Status |
|---|---|---|---|
| `ra4_camera_bridge/` | UNO R4 Minima or WiFi | Normal Arduino CDC carrying O1 frames | Stable; hardware verified |
| `ra4_es_e1_id_bridge/` | UNO R4 Minima only | Experimental `04A9:3040` CDC identity carrying O1 frames | Build verified; hardware validation required |

Both sketches use the RA4M1 for the timing-critical camera interface. The host application owns the EOS-1V protocol state machine; firmware only checks and forwards bounded O1 requests. See [`protocol.md`](protocol.md).

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

The experimental image is CDC ACM with O1 framing. It does not yet emulate the original KLSI/MCCI USB transport and is not a drop-in replacement for the legacy Canon driver.

## Safety behavior

- Both camera drivers idle released.
- `EXCHANGE` limits host payload and expected camera reply sizes.
- `RELEASE` immediately releases the line and drains pending camera input.
- A 30-second host-idle timeout releases an active high-assist state.

Disconnect the camera-side DATA-A and DATA-B wiring before changing firmware.
