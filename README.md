# EOS-1V UNO R4 Serial Interface

An open, independently implemented Arduino UNO R4 WiFi interface for the Canon EOS-1V service/data channel exposed through the N3 three-pin remote connector.

The project currently provides a verified electrical interface, session handshake, settings reads, C.Fn/P.Fn reads and selected writes, clock read/write, shooting-data configuration, film-record download, and an explicitly armed delete operation. It does not contain Canon executables, drivers, firmware, manuals, or copied source code.

## Status

The hardware and protocol paths have been tested against a real EOS-1V:

- 9600 baud, 8-N-1, LSB-first serial transport
- `FF/F4`, `F6`, and `F1` session establishment
- single-stage `F2/F2` session exit
- C.Fn and P.Fn reads
- camera clock read and write
- camera ID read and write
- shooting-field mask read, validated write, and restore
- variable-length `E3/E4` film-record download
- continuous multi-action session followed by one final exit
- explicit two-step film-data deletion
- recovery after MCU/USB power loss

The main firmware remains a protocol research console. Commands that write camera state are included for reproducibility and are guarded by exact baseline checks where practical.

## Hardware

Target board: **Arduino UNO R4 WiFi**.

The tested interface uses two isolated transmit paths because the camera-side pull-up can disappear during a long low pulse:

```text
LOW path
UNO D4 -- 10k -- NPN base
NPN base -- 100k -- emitter
NPN emitter -- COMMON
NPN collector -- 330R -- DATA-A

HIGH assist
UNO D5 -- 1k -- 1N4007 anode
1N4007 cathode/stripe -- DATA-A

Receive
DATA-B -- 5.1k -- UNO D0/RX

Ground
EOS COMMON -- UNO GND
```

UNO D1/TX and D2 must remain disconnected. See [the complete wiring notes](docs/wiring.zh-CN.md) before connecting a camera.

## Build

Install the Arduino Renesas core, then compile:

```powershell
arduino-cli core install arduino:renesas_uno
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi firmware/eos1v_interface
```

Open the serial monitor at 115200 baud after upload. Put the camera into PC mode before starting a protocol command. A successful `F2` exit deliberately returns the camera to normal metering mode, so PC mode must be re-entered before the next independent command.

## Repository layout

```text
firmware/eos1v_interface/  Verified research console firmware
experiments/               Earlier electrical and UART probe sketches
tools/                     Capture and offline UART decoding tools
docs/                      Wiring, protocol, and validation notes
```

The sketches under `experiments/` document the development path. They are not the recommended camera interface.

## Safety model

- Both output drivers idle disabled.
- Communication starts only after an explicit serial-console command.
- The firmware does not repeatedly probe the normal shutter connector.
- Write tests check known baseline values before changing state.
- Restore operations require a fresh PC-mode session.
- Film-data deletion requires uppercase `Z`, followed by `!` within ten seconds.

Always keep an independent backup of film records before testing write or delete commands.

## Documentation

- [Wiring](docs/wiring.zh-CN.md)
- [Hardware validation](docs/hardware-validation.zh-CN.md)
- [Active interface validation](docs/active-interface-validation.zh-CN.md)
- [Protocol validation](docs/protocol-validation.zh-CN.md)
- [Console command reference](docs/command-reference.md)

## Legal note

This is an independent interoperability and preservation project. Canon and EOS are trademarks of Canon Inc. No Canon software or copyrighted binary is distributed here.

## License

Source code and original documentation in this repository are available under the MIT License. See [LICENSE](LICENSE).
