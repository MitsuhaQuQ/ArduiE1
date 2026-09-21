param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$Seconds = 30,
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
if (-not $Output) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $Output = Join-Path $PSScriptRoot "eos_edges_$stamp.bin"
}

$serial = [System.IO.Ports.SerialPort]::new(
    $Port,
    1000000,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000
$serial.DtrEnable = $true
$stream = $null

try {
    $serial.Open()
    Start-Sleep -Milliseconds 1500
    $serial.DiscardInBuffer()
    $stream = [System.IO.File]::Open($Output, [System.IO.FileMode]::Create)
    $serial.Write("S")
    Write-Host "Recording $Port for $Seconds seconds. Perform the Remote read operation now."

    $buffer = New-Object byte[] 4096
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $count = $serial.Read($buffer, 0, $buffer.Length)
            if ($count -gt 0) { $stream.Write($buffer, 0, $count) }
        }
        catch [System.TimeoutException] {
        }
    }
    $serial.Write("X")
    Write-Host "Saved raw edge capture to $Output"
}
finally {
    if ($stream) { $stream.Dispose() }
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
