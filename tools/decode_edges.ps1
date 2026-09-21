param(
    [Parameter(Mandatory = $true)]
    [string]$Capture,
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
if (-not $Output) { $Output = [System.IO.Path]::ChangeExtension($Capture, ".csv") }

$raw = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Capture))
$magic = [System.Text.Encoding]::ASCII.GetBytes("EOS1EDGE")
$start = -1
for ($i = 0; $i -le $raw.Length - $magic.Length; $i++) {
    $match = $true
    for ($j = 0; $j -lt $magic.Length; $j++) {
        if ($raw[$i + $j] -ne $magic[$j]) { $match = $false; break }
    }
    if ($match) { $start = $i + $magic.Length; break }
}
if ($start -lt 0) { throw "EOS1EDGE header not found; verify the UNO sketch and COM port." }

$writer = [System.IO.StreamWriter]::new($Output, $false, [System.Text.UTF8Encoding]::new($false))
$writer.WriteLine("time_us,channel,line,level,delta_same_line_us")
$previousRaw = -1L
$wrapBase = 0L
$previousA = -1L
$previousB = -1L
$dropped = 0L
$events = 0L

try {
    for ($offset = $start; $offset + 3 -lt $raw.Length; $offset += 4) {
        $word = [System.BitConverter]::ToUInt32($raw, $offset)
        if ($word -eq [uint32]::MaxValue -and $offset + 7 -lt $raw.Length) {
            $dropped += [System.BitConverter]::ToUInt32($raw, $offset + 4)
            $offset += 4
            continue
        }

        $channel = if (($word -band [uint32]2147483648) -ne 0) { 1 } else { 0 }
        $level = if (($word -band [uint32]1073741824) -ne 0) { 1 } else { 0 }
        $rawTime = [long]($word -band [uint32]1073741823)
        if ($previousRaw -ge 0 -and $rawTime -lt $previousRaw -and ($previousRaw - $rawTime) -gt 536870912) {
            $wrapBase += 1073741824
        }
        $time = $wrapBase + $rawTime
        $previousRaw = $rawTime

        if ($channel -eq 0) {
            $delta = if ($previousA -lt 0) { "" } else { ($time - $previousA).ToString() }
            $previousA = $time
            $line = "FOCUS_DATA_A"
        }
        else {
            $delta = if ($previousB -lt 0) { "" } else { ($time - $previousB).ToString() }
            $previousB = $time
            $line = "SHUTTER_DATA_B"
        }
        $writer.WriteLine("$time,$channel,$line,$level,$delta")
        $events++
    }
}
finally {
    $writer.Dispose()
}

Write-Host "Decoded $events edges to $Output"
Write-Host "Dropped events reported by recorder: $dropped"
