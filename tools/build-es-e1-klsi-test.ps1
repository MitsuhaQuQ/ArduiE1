[CmdletBinding()]
param(
    [string]$CoreVersion = "1.6.0",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
$sketch = Join-Path $repository "firmware\eos1v_winusb_bridge\ra4_es_e1_klsi_bridge"
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repository "build\ra4_es_e1_klsi_bridge"
}

$core = Join-Path $env:LOCALAPPDATA "Arduino15\packages\arduino\hardware\renesas_uno\$CoreVersion"
$pins = Join-Path $core "variants\MINIMA\pins_arduino.h"
$tusb = Join-Path $core "variants\MINIMA\tusb_config.h"
$usb = Join-Path $core "cores\arduino\usb\USB.cpp"
foreach ($path in @($pins, $tusb, $usb)) {
    if (-not (Test-Path $path)) { throw "Required Arduino core file not found: $path" }
}

$arduinoCli = Get-Command arduino-cli -ErrorAction SilentlyContinue
if ($arduinoCli) {
    $arduinoCliPath = $arduinoCli.Source
} else {
    $arduinoCliPath = Join-Path $env:LOCALAPPDATA "Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe"
}
if (-not (Test-Path $arduinoCliPath)) { throw "arduino-cli was not found" }

$original = @{}
foreach ($path in @($pins, $tusb, $usb)) { $original[$path] = [IO.File]::ReadAllText($path) }

try {
    $pinsPatched = $original[$pins].Replace("#define USB_VID           (0x2341)", "#define USB_VID           (0x04A9)")
    $pinsPatched = $pinsPatched.Replace("#define USB_PID           (0x0069)", "#define USB_PID           (0x3040)")
    if ($pinsPatched -eq $original[$pins]) { throw "Minima VID/PID definitions not found" }

    $tusbPatched = $original[$tusb]
    # The original ES-E1 advertises an 8-byte endpoint zero. The RA4M1
    # TinyUSB/RUSB2 path fails configuration-descriptor enumeration at that
    # size, so the Minima-compatible image keeps the core's 64-byte EP0.
    # Keep the CDC class compiled because the Arduino core always builds
    # SerialUSB.cpp. It is deliberately omitted from the USB descriptors.
    $tusbPatched = $tusbPatched.Replace('#define CFG_TUD_HID              1', '#define CFG_TUD_HID              0')
    $tusbPatched = $tusbPatched.Replace('#define CFG_TUD_VENDOR           0', '#define CFG_TUD_VENDOR           1')
    $tusbPatched = $tusbPatched.Replace('#define CFG_TUD_DFU_RUNTIME      1', '#define CFG_TUD_DFU_RUNTIME      0')
    $tusbPatched += "`r`n#define CFG_TUD_VENDOR_RX_BUFSIZE 256`r`n#define CFG_TUD_VENDOR_TX_BUFSIZE 256`r`n#define CFG_TUD_VENDOR_EPSIZE 64`r`n"

    $usbPatched = $original[$usb]
    $usbPatched = $usbPatched.Replace('.bcdUSB = 0x0200', '.bcdUSB = 0x0100')
    $usbPatched = $usbPatched.Replace('.bDeviceClass = TUSB_CLASS_CDC', '.bDeviceClass = 0xFF')
    $usbPatched = $usbPatched.Replace('.bDeviceSubClass = MISC_SUBCLASS_COMMON', '.bDeviceSubClass = 0x00')
    $usbPatched = $usbPatched.Replace('.bDeviceProtocol = MISC_PROTOCOL_IAD', '.bDeviceProtocol = 0x00')
    $usbPatched = $usbPatched.Replace('.bcdDevice = 0x0100', '.bcdDevice = 0x0103')
    $usbPatched = [regex]::Replace($usbPatched,
        '(?ms)\s*// Descriptors are always composite\s+usbd_desc_device\.bDeviceClass = 0;\s+usbd_desc_device\.bDeviceSubClass = 0;\s+usbd_desc_device\.bDeviceProtocol = 0;\s+',
        "`r`n    ")
    $usbPatched = $usbPatched.Replace('[USBD_STR_MANUF] = "Arduino"', '[USBD_STR_MANUF] = "Canon"')
    $usbPatched = $usbPatched.Replace('[USBD_STR_PRODUCT] = USB_NAME', '[USBD_STR_PRODUCT] = "Canon EOS USB Cable"')

    $start = $usbPatched.IndexOf('void __SetupUSBDescriptor() {')
    $finish = $usbPatched.IndexOf('static void utox8', $start)
    if ($start -lt 0 -or $finish -lt 0) { throw "USB descriptor builder was not found" }
    $vendorBuilder = @'
void __SetupUSBDescriptor() {
    if (!usbd_desc_cfg) {
        const uint8_t interface_count = 1;
        const int usbd_desc_len = TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN;
        uint8_t tud_cfg_desc[TUD_CONFIG_DESC_LEN] = {
            TUD_CONFIG_DESCRIPTOR(1, interface_count, 0, usbd_desc_len,
                                  TUSB_DESC_CONFIG_ATT_SELF_POWERED, 500)
        };
        uint8_t vendor_desc[TUD_VENDOR_DESC_LEN] = {
            TUD_VENDOR_DESCRIPTOR(0, 0, 0x02, 0x81, 64)
        };
        usbd_desc_cfg = (uint8_t *)malloc(usbd_desc_len);
        if (usbd_desc_cfg) {
            memcpy(usbd_desc_cfg, tud_cfg_desc, sizeof(tud_cfg_desc));
            memcpy(usbd_desc_cfg + sizeof(tud_cfg_desc), vendor_desc,
                   sizeof(vendor_desc));
        }
    }
}

'@
    $usbPatched = $usbPatched.Substring(0, $start) + $vendorBuilder + $usbPatched.Substring($finish)

    [IO.File]::WriteAllText($pins, $pinsPatched)
    [IO.File]::WriteAllText($tusb, $tusbPatched)
    [IO.File]::WriteAllText($usb, $usbPatched)
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    & $arduinoCliPath compile --fqbn arduino:renesas_uno:minima --clean --output-dir $OutputDirectory $sketch
    if ($LASTEXITCODE -ne 0) { throw "arduino-cli compile failed with exit code $LASTEXITCODE" }
}
finally {
    foreach ($path in @($pins, $tusb, $usb)) { [IO.File]::WriteAllText($path, $original[$path]) }
}

Write-Host "Built experimental KLSI/MCCI-compatible image: $OutputDirectory"
Write-Host "The installed Arduino core was restored. Flash only to UNO R4 Minima."
