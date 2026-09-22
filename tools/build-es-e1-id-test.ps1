[CmdletBinding()]
param(
    [string]$CoreVersion = "1.6.0",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
$sketch = Join-Path $repository "firmware\eos1v_winusb_bridge\ra4_es_e1_id_bridge"
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repository "build\ra4_es_e1_id_bridge"
}

$core = Join-Path $env:LOCALAPPDATA "Arduino15\packages\arduino\hardware\renesas_uno\$CoreVersion"
$pins = Join-Path $core "variants\MINIMA\pins_arduino.h"
$usb = Join-Path $core "cores\arduino\usb\USB.cpp"
if (-not (Test-Path $pins) -or -not (Test-Path $usb)) {
    throw "Arduino Renesas core $CoreVersion was not found under $core"
}

$arduinoCli = Get-Command arduino-cli -ErrorAction SilentlyContinue
if ($arduinoCli) {
    $arduinoCliPath = $arduinoCli.Source
} else {
    $arduinoCliPath = Join-Path $env:LOCALAPPDATA "Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe"
}
if (-not (Test-Path $arduinoCliPath)) {
    throw "arduino-cli was not found in PATH or in the standard Arduino IDE installation"
}

$pinsOriginal = [IO.File]::ReadAllText($pins)
$usbOriginal = [IO.File]::ReadAllText($usb)

try {
    $pinsPatched = $pinsOriginal.Replace("#define USB_VID           (0x2341)", "#define USB_VID           (0x04A9)")
    $pinsPatched = $pinsPatched.Replace("#define USB_PID           (0x0069)", "#define USB_PID           (0x3040)")
    if ($pinsPatched -eq $pinsOriginal) {
        throw "Expected Minima USB_VID/USB_PID definitions were not found; core layout may have changed"
    }

    $usbPatched = $usbOriginal.Replace('[USBD_STR_MANUF] = "Arduino"', '[USBD_STR_MANUF] = "Canon"')
    $usbPatched = $usbPatched.Replace('[USBD_STR_PRODUCT] = USB_NAME', '[USBD_STR_PRODUCT] = "Canon EOS USB Cable (CDC test)"')
    $usbPatched = $usbPatched.Replace('[USBD_STR_CDC] = "CDC Port"', '[USBD_STR_CDC] = "Canon EOS Port (CDC test)"')
    if ($usbPatched -eq $usbOriginal) {
        throw "Expected USB string table was not found; core layout may have changed"
    }

    [IO.File]::WriteAllText($pins, $pinsPatched)
    [IO.File]::WriteAllText($usb, $usbPatched)
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    & $arduinoCliPath compile --fqbn arduino:renesas_uno:minima --clean --output-dir $OutputDirectory $sketch
    if ($LASTEXITCODE -ne 0) {
        throw "arduino-cli compile failed with exit code $LASTEXITCODE"
    }
}
finally {
    [IO.File]::WriteAllText($pins, $pinsOriginal)
    [IO.File]::WriteAllText($usb, $usbOriginal)
}

Write-Host "Built experimental CDC image with USB VID_04A9&PID_3040: $OutputDirectory"
Write-Host "The installed Arduino core was restored."
