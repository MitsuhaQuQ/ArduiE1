# EOS-1V UNO R4 camera bridge

> **ESP32-S3 STATUS: UNUSABLE EXPERIMENTAL DRAFT.** This warning applies only
> to `esp32s3_winusb_bridge/`. The RA4 camera bridge in
> `ra4_camera_bridge/ra4_camera_bridge.ino` is the working USB-CDC/UART bridge
> and is the firmware to compile for UNO R4 WiFi or UNO R4 Minima.

This contains the validated RA4 camera bridge for Arduino UNO R4 WiFi and UNO
R4 Minima. The WiFi USB bridge prototype is separate:

- `ra4_camera_bridge/` runs on the RA4M1 and owns the timing-critical camera
  line driver.
- `esp32s3_winusb_bridge/` runs on the on-board ESP32-S3 and exposes a USB
  vendor interface that Windows can bind to WinUSB. It transparently forwards
  framed bytes to the RA4M1 over the board's internal UART.

The original diagnostic sketch is archived under
`tests/hardware/eos1v_interface`. This project
uses a deliberately small binary transport so the PC application owns the
camera protocol state machine and both the CLI and GUI can share it.

## Important hardware fact

On UNO R4 Minima, the RA4M1 USB CDC port is the host-side USB-UART endpoint,
so `ra4_camera_bridge.ino` is self-contained. On UNO R4 WiFi, the USB-C port
is handled by the on-board ESP32-S3 bridge; the RA4 sketch still owns the
camera UART and must use the normal WiFi board path. A custom WinUSB interface
on WiFi would require replacing the ESP32-S3 firmware, which is the separate
experimental path documented below.

## Warning before flashing

Flashing `esp32s3_winusb_bridge` replaces the official Arduino USB/Wi-Fi bridge
firmware. Normal sketch upload, debugger and Wi-Fi bridge behavior may be
unavailable until the official firmware is restored. Keep a tested restore
package before changing the ESP32-S3 firmware.

Official bridge and restore source:

https://github.com/arduino/uno-r4-wifi-usb-bridge

中文恢复步骤见 [`RESTORE_OFFICIAL_FIRMWARE.zh-CN.md`](RESTORE_OFFICIAL_FIRMWARE.zh-CN.md)。
实机枚举基线见 [`HARDWARE_BASELINE.zh-CN.md`](HARDWARE_BASELINE.zh-CN.md)。

Do not flash either processor while a camera is connected. Disconnect the
camera-side DATA-A/DATA-B wiring first.

## Build targets

### RA4M1

- Board: Arduino UNO R4 WiFi (`arduino:renesas_uno:unor4wifi`) or UNO R4 Minima
  (`arduino:renesas_uno:minima`)
- Sketch: `ra4_camera_bridge/ra4_camera_bridge.ino`
- Internal link: `Serial` at 115200 baud
- Camera receive: `Serial1` at 9600 baud

### ESP32-S3

- Target: ESP32-S3 with native USB OTG
- USB mode: USB-OTG (TinyUSB), `ARDUINO_USB_MODE=0`
- USB CDC on boot: disabled
- Sketch: `esp32s3_winusb_bridge/esp32s3_winusb_bridge.ino`
- Internal RA4 link: UART0 RX GPIO44, TX GPIO43, matching Arduino's official
  UNO R4 WiFi bridge firmware

The ESP32-S3 sketch uses the official Arduino-ESP32 `USBVendor` class. That
class supplies a vendor interface and Microsoft OS 2.0 descriptors for WinUSB.
On Arduino-ESP32 3.3.x, `USB.webUSB(true)` must remain enabled because the core
uses that same switch when serving the Microsoft descriptor request. The PC
application itself still communicates through WinUSB, not the browser API.
The prototype deliberately does not claim a new USB VID/PID. A distributed
product must use identifiers the distributor is authorized to use.

## Wire protocol

See `protocol.md`. Every message is framed and protected by CRC-16/CCITT-FALSE.
The ESP32-S3 does not interpret frames. This makes framing identical on USB and
the internal UART and keeps the USB processor outside the camera state machine.

The initial RA4 commands are intentionally limited:

- `PING`: confirm firmware and protocol version;
- `GET_STATUS`: read line and counter state;
- `EXCHANGE`: send zero or more camera bytes and collect a bounded reply;
- `RELEASE`: immediately release both camera line drivers and drain input.

There is no erase or settings-specific command in this firmware. Destructive
policy remains in the PC application.

As a hardware fail-safe, the RA4 releases both camera-line drivers after 30
seconds without a valid host frame. The PC application should still send
`RELEASE` during orderly shutdown; the timer covers a crashed or unplugged
host.

## Reproducible compile checks

With the required cores already installed:

```powershell
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi `
  firmware/eos1v_winusb_bridge/ra4_camera_bridge
# For UNO R4 Minima use: arduino:renesas_uno:minima

arduino-cli compile `
  --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,FlashSize=4M,PartitionScheme=default,PSRAM=disabled" `
  firmware/eos1v_winusb_bridge/esp32s3_winusb_bridge
```

The generic ESP32-S3 target is currently used for compile validation. Before
flashing the on-board module, the image layout and restore procedure must be
matched to Arduino's official UNO R4 WiFi bridge package.

## Current verification level

The ESP32-S3 source is currently unusable. It compiles with the listed generic
target, but compilation is the only positive result and does not establish a
correct image layout, boot behavior, USB enumeration, internal UART routing,
or recovery path on the UNO R4 WiFi. Do not flash it. The RA4 bridge and frame
format are retained as development material for a future corrected transport.
