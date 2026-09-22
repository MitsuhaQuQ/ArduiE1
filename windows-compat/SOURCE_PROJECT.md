# EOS-1V compatibility source project

This source directory rebuilds every project-owned executable component in the clean release. It does not require or contain Canon binaries. The compatibility DLL supports both the original ES-E1 cable and the UNO R4 bridge firmware in this repository.

## Transport selection

`EOSHOOKX.dll` keeps the COM-style API expected by the original Canon program and selects one of two backends:

- **Arduino** finds an UNO R4 WiFi USB serial port with hardware ID `VID_2341&PID_1002` and sends the repository's framed `O1` exchange protocol at 115200 baud.
- **ES-E1** uses the established WinUSB/KLSI backend for the original Canon cable (`VID_04A9&PID_3040`).
- **Auto** tries Arduino first and falls back to ES-E1 if the Arduino is absent
  or cannot be opened. The Arduino bridge has been verified to operate while
  the ES-E1 is also enumerated on the host.

Auto is the default. To make the choice deterministic when both devices are connected, create `EOSBRIDGE.INI` beside `EOSHOOKX.dll`:

```ini
[Bridge]
Backend=Arduino
```

Use `Backend=ES-E1` to force the original cable, or `Backend=Auto` to restore automatic selection. No UMDF replacement is installed and neither device's system driver is changed.

When both devices are plugged in, select the intended backend explicitly:

```ini
[Bridge]
Backend=Arduino
```

or:

```ini
[Bridge]
Backend=ES-E1
```

The setting is read beside `EOSHOOKX.dll`, so it applies to the patched Canon
process without changing the machine-wide COM or USB configuration. The
Arduino bridge and the ES-E1 have separate host transports; the bridge keeps
the selected backend for the whole Remote session and logs when both devices
are present. If both N3 cables are physically attached to one camera, avoid
simultaneous active drivers unless the specific wiring setup has been verified.

The Canon driver issues a write and a later read, while an `O1` exchange carries the outgoing bytes and expected reply length together. For the Arduino backend, `EOSHOOKX.dll` therefore holds the latest write until the corresponding read. A following write flushes the older pending write as a zero-reply exchange. This preserves delays inserted by the original program between commands without hard-coding individual camera commands.

## Canon application session model

The original Remote application keeps one pseudo-COM connection open across C.Fn, P.Fn, shooting-data, and idle UI activity. In-session `FF/F4/F1` or `F4/F6` traffic performs synchronization; it is not a physical disconnect. The bridge must therefore preserve its backend, pending-write state, receive queue, and camera electrical state between UI operations.

Remote also emits an intermediate one-byte `F2` while closing its COM-style layer. The compatibility DLL acknowledges that byte locally so the camera remains in PC mode until Remote's final shutdown path. Only that final path performs the real one- or two-stage `F2` sequence and releases the hardware drivers. Sending a real `F2` after each high-level action would be incompatible with the original program.

## Windows 11 hardware validation

The Arduino backend has been exercised with the patched original Canon Remote application, an UNO R4 WiFi running the RA4 bridge firmware, and an EOS-1V. The verified startup exchange was:

```text
FF -> F4
F4 acknowledgement
F6 -> 17-byte identification block
F1 -> 6-byte camera identity block
```

The original application's C.Fn and P.Fn screens then completed reads and setting writes successfully, including the application's normal write verification. Shooting-data download was also completed after adding variable-packet read-ahead: five roll segments were transferred, converted from temporary files to EFD files, and handed to Memory through the original application path. Normal Remote exit completed the final `F2` sequence and the camera visibly left PC mode. This validates bidirectional application-to-DLL, DLL-to-UNO, and UNO-to-camera traffic. Camera-clock/ID writes and an ES-E1 regression run remain separate validation items.

## Prerequisites

- Windows 10 or Windows 11, x64;
- Visual Studio or Visual Studio Build Tools with **Desktop development with C++**;
- a Windows SDK providing SetupAPI, WinUSB, BCrypt, and User32 import libraries.

The build scripts locate the newest installed Visual Studio C++ toolchain through `vswhere.exe`.

## One-command build

Run `build_all.cmd` from a normal Command Prompt. It produces:

- `EOSHOOKX.dll` — PE32/x86 bridge loaded by the original 32-bit Canon application;
- `EOS1V_Patcher.exe` — PE32+/x64 local patch installer with the static C runtime;
- `smoke_test.exe` and `bridge_device_test.exe` — x86 bridge diagnostics;
- `eos_probe.exe` — x64 standalone WinUSB transport probe.

## Individual builds

`build.cmd` builds the x86 bridge DLL and its tests from `eosbridge.c` plus `eosbridge.def`.

`smoke_test.exe` validates the bridge's required legacy exports and ordinary non-COM file forwarding without requiring any Canon binary. `bridge_device_test.exe` is the hardware-facing bridge diagnostic. It sends real camera commands; confirm that the camera is in PC mode before running it.

`build_patcher.cmd` builds the x64 native local patcher from `eos1v_patcher.c`. The patcher uses Windows CNG (`bcrypt.dll`) for SHA-256, supports a folder passed by drag-and-drop, and otherwise operates on its own directory.

`build_patcher.cmd` requires the already built `EOSHOOKX.dll`, calculates its SHA-256, regenerates `bridge_hash.h`, compiles `eos1v_patcher.rc`, and embeds the DLL as an `RCDATA` resource in `EOS1V_Patcher.exe`. Thus a source-built patcher carries and accepts the bridge DLL produced in the same build even when PE linker metadata changes the binary hash.

`build_probe.cmd` builds the standalone WinUSB transport probe.

The WinUSB path opens the device synchronously and discovers the bulk IN and
OUT endpoint addresses from the active interface descriptor. It no longer
assumes `0x81/0x02`, which avoids failures with a different endpoint ordering
or a WinUSB stack that rejects synchronous calls on an overlapped handle.

If `eos_probe.exe` reports that the device is found but `WinUsb_Initialize`
returns error 6 (`ERROR_INVALID_HANDLE`), the USB device is still using the
Canon/vendor driver. Use Zadig to select the `Canon EOS USB Cable` / ES-E1
device, choose **Microsoft WinUSB**, install or replace the driver, and then
physically reconnect the cable. The DLL cannot turn a vendor-driver handle into
a WinUSB handle in user mode. This project does not ship or install an INF.

## Clean-distribution rule

Do not add Canon `Remote.exe`, `Eos1v.drv`, `Memory.exe`, locally patched copies, or binary deltas to this project. `EOS1V_Patcher.exe` accepts only the three exact original hashes recorded in its source and creates the equal-length import-name modifications on the user's machine.
