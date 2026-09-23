# ArduiE1 — EOS-1V UNO R4 camera interface

ArduiE1 is an independently implemented Arduino UNO R4 interface for the Canon
EOS-1V service/data channel exposed through the N3 three-pin remote connector.
It provides the camera-side hardware and bridge firmware used by the separate
`open1V-cli` and `open1v-filmdb` host applications.

The repository contains no Canon executables, drivers, firmware, manuals, or
copied source code.

## Current firmware

Three firmware paths are retained:

| Firmware | Board | USB transport | Status |
|---|---|---|---|
| `ra4_camera_bridge` | UNO R4 Minima or WiFi | Arduino CDC carrying O1 frames | Stable; camera hardware verified |
| `ra4_es_e1_id_bridge` | UNO R4 Minima only | Experimental `04A9:3040` CDC identity carrying O1 frames | Experimental; open1V camera session verified |
| `ra4_es_e1_klsi_bridge` | UNO R4 Minima only | Experimental KLSI/MCCI-style vendor transport | Experimental; transport and patched Remote session verified |

The stable `ra4_camera_bridge` is the recommended firmware. It is a bounded O1
transport bridge, not an interactive protocol console: the host application
owns the EOS-1V session, read, write, and delete logic. The bridge accepts
`PING`, `GET_STATUS`, `EXCHANGE`, and `RELEASE` frames at 115200 baud and
forwards camera bytes at 9600 baud without interpreting or retrying them.

The historical command-driven research firmware remains under
`tests/hardware/eos1v_interface`. Its single-character console commands,
including the guarded `Z` then `!` delete sequence, do not apply to the stable
bridge.

## Verified scope

The electrical interface and host/bridge stack have completed these operations
on a real EOS-1V:

- 9600 baud, 8-N-1, LSB-first, non-inverted camera transport;
- `FF/F4/F6/F1` session establishment and both observed `F2` exit forms;
- C.Fn and P.Fn reads and selected verified writes;
- camera ID and clock reads, writes, and read-back verification;
- shooting-field mask reads, controlled writes, and restoration;
- variable-length `E3/E4` film-record download;
- continuous multi-action sessions with one final exit;
- explicit delete-all with post-operation verification;
- recovery after MCU/USB power loss;
- stable CDC/O1 operation on UNO R4 WiFi and Minima;
- experimental ES-E1 identity and KLSI/MCCI-style transport on Minima.

Protocol operations are implemented by the host applications. This list does
not mean the stable bridge exposes matching serial-console commands.

The firmware owns all board-specific behavior: bit timing, D5 high-level
assist, stale-RX draining, line release, and concrete receive windows. O1
profiled exchange (`0x12`) lets the host choose a neutral transport class while
keeping hardware timing on the Arduino. Legacy exchange (`0x10`) is retained
for compatibility with existing host builds.

## Hardware

UNO R4 Minima is recommended. UNO R4 WiFi is supported for the stable CDC/O1
bridge, but its USB-C port is mediated by the on-board ESP32-S3, so USB identity
and original-transport experiments target Minima only.

The verified active circuit uses two isolated DATA-A transmit paths because the
camera-side pull-up can disappear during a long low pulse:

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

UNO D1/TX and D2 remain disconnected in the final active circuit. Read the
[complete wiring notes](docs/wiring.md) and identify N3 conductors by continuity
at the actual plug. Connector drawings and cable colors are not reliable.

| Canon line | Normal shutter function | EOS-1V PC-mode function |
|---|---|---|
| `COMMON` | Remote common/reference | Signal reference and return |
| `FOCUS` | Half-press/focus contact | Bidirectional `DATA-A` |
| `SHUTTER` | Full-press/shutter contact | Bidirectional `DATA-B` |

## Build the stable bridge

Install Arduino CLI and the Renesas UNO core, then compile for the selected
board:

```sh
arduino-cli core install arduino:renesas_uno
arduino-cli compile --fqbn arduino:renesas_uno:minima firmware/eos1v_winusb_bridge/ra4_camera_bridge
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi firmware/eos1v_winusb_bridge/ra4_camera_bridge
```

Upload the matching image, connect the verified circuit, put the camera into PC
mode, then use `open1V-cli` or `open1v-filmdb`. A serial monitor is not a user
interface for the stable bridge; arbitrary text is ignored until an O1 frame is
recognized. Host applications may auto-detect the board or accept an explicit
serial device such as `COM3`, `/dev/cu.usbmodem...`, or `/dev/ttyACM0`.

The experimental Minima images require the PowerShell build scripts documented
in [the ES-E1 USB experiment guide](docs/es-e1-usb-identity-test.md). Those
scripts temporarily patch Arduino core files and restore them afterward.

## Safety

- Both camera drivers idle released.
- Stable bridge communication begins only after a valid O1 `EXCHANGE` request.
- The bridge limits host payload, camera transmit, expected reply, and timeout.
- `RELEASE` disables both drivers and drains pending camera input.
- A 30-second host-idle timeout releases an active high-assist state.
- The bridge never retries camera commands; write/delete retry policy belongs to
  the host application.
- Disconnect DATA-A and DATA-B before changing firmware.
- Back up film records before testing any write or delete operation.

## Repository layout

```text
firmware/eos1v_winusb_bridge/   Stable and experimental bridge firmware
tests/hardware/eos1v_interface/ Archived command-driven diagnostic firmware
experiments/                    Earlier electrical and UART probes
tools/                          Build, capture, and decoding tools
docs/                           Wiring, protocol, and validation records
```

## Documentation

- [Bridge firmware and build matrix](firmware/eos1v_winusb_bridge/README.md)
- [O1 bridge protocol](firmware/eos1v_winusb_bridge/protocol.md)
- [Wiring](docs/wiring.md)
- [Communication behavior manual](docs/communication-manual.md)
- [Hardware validation](docs/hardware-validation.md)
- [Active interface validation](docs/active-interface-validation.md)
- [Protocol validation](docs/protocol-validation.md)
- [ES-E1 USB identity and transport experiments](docs/es-e1-usb-identity-test.md)
- [Archived diagnostic console commands](docs/command-reference.md)

Chinese translations and detailed research notes are retained alongside the
English documents.

## Legal and license

Canon and EOS are trademarks of Canon Inc. Source code and original
documentation are available under the [MIT License](LICENSE).
