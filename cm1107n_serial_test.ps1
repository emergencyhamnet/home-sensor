param(
    [string]$Port = "COM6",
    [int]$Baud = 9600,
    [int]$DataBits = 8,
    [ValidateSet("None","Odd","Even","Mark","Space")]
    [string]$Parity = "None",
    [ValidateSet("One","OnePointFive","Two")]
    [string]$StopBits = "One",
    [int]$ReadSeconds = 20,
    [switch]$ListenOnly,
    [switch]$ListPorts,
    [string]$TxHex = "FF 01 86 00 00 00 00 00 79",
    [int]$InterByteTimeoutMs = 120,
    [int]$ExpectedReplyLen = 9
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Ports {
    Get-CimInstance Win32_SerialPort |
        Select-Object DeviceID, Name, Description |
        Sort-Object DeviceID
}

function HexToBytes([string]$hexString) {
    $clean = ($hexString -replace "[^0-9A-Fa-f]", "")
    if ([string]::IsNullOrWhiteSpace($clean)) {
        return @()
    }
    if (($clean.Length % 2) -ne 0) {
        throw "Hex string has odd number of nibbles: '$hexString'"
    }
    $bytes = New-Object byte[] ($clean.Length / 2)
    for ($index = 0; $index -lt $clean.Length; $index += 2) {
        $bytes[$index / 2] = [Convert]::ToByte($clean.Substring($index, 2), 16)
    }
    return $bytes
}

function BytesToHex([byte[]]$bytes) {
    if (-not $bytes -or $bytes.Length -eq 0) { return "" }
    return (($bytes | ForEach-Object { "{0:X2}" -f $_ }) -join " ")
}

function Verify-MHZ19Checksum([byte[]]$frame) {
    if (-not $frame -or $frame.Length -ne 9) {
        return $false
    }
    $sum = 0
    for ($i = 1; $i -le 7; $i++) {
        $sum = ($sum + $frame[$i]) -band 0xFF
    }
    $expected = ((0xFF - $sum + 1) -band 0xFF)
    return $frame[8] -eq $expected
}

function Try-ParseCO2([byte[]]$frame) {
    if (-not $frame -or $frame.Length -lt 4) { return $null }

    # Observed CM1107N stream format (16-byte frame), example:
    # 42 4D 07 14 08 0E 05 C1 01 2C 00 00 E2 00 06 9B
    # Field mapping below is based on common CM1107N docs and live captures.
    if ($frame.Length -eq 16 -and $frame[0] -eq 0x42 -and $frame[1] -eq 0x4D) {
        $co2 = ($frame[2] * 256) + $frame[3]
        $tempRaw = ($frame[4] * 256) + $frame[5]
        $rhRaw = ($frame[6] * 256) + $frame[7]
        $abcDays = ($frame[8] * 256) + $frame[9]
        $status = ($frame[10] * 256) + $frame[11]
        $checksumRx = ($frame[14] * 256) + $frame[15]

        $sum14 = 0
        for ($i = 0; $i -le 13; $i++) {
            $sum14 = ($sum14 + $frame[$i]) -band 0xFFFF
        }

        # Empirical check observed on CM1107N captures in this workspace:
        # checksum appears to track sum(first 14 bytes) + 0x0406 (mod 65536).
        $checksumCalc = ($sum14 + 0x0406) -band 0xFFFF

        return [pscustomobject]@{
            Protocol           = "CM1107N 16-byte"
            CO2ppm             = $co2
            TempRaw            = $tempRaw
            RHRaw              = $rhRaw
            AbcDays            = $abcDays
            Status             = $status
            ChecksumRx         = ("0x{0:X4}" -f $checksumRx)
            ChecksumCalc       = ("0x{0:X4}" -f $checksumCalc)
            ChecksumEmpiricalOk = ($checksumRx -eq $checksumCalc)
        }
    }

    # Common 9-byte format used by many UART CO2 modules:
    # FF 86 HH LL ... CS
    if ($frame.Length -eq 9 -and $frame[0] -eq 0xFF -and $frame[1] -eq 0x86) {
        $ppm = ($frame[2] * 256) + $frame[3]
        $checksumOk = Verify-MHZ19Checksum $frame
        return [pscustomobject]@{
            Protocol   = "FF 86 9-byte"
            CO2ppm     = $ppm
            ChecksumOk = $checksumOk
        }
    }

    return $null
}

if ($ListPorts) {
    Write-Host "Available serial ports:" -ForegroundColor Cyan
    Get-Ports | Format-Table -AutoSize
    return
}

$ports = Get-Ports
if (-not ($ports.DeviceID -contains $Port)) {
    Write-Warning "$Port is not currently present. Connected ports: $((($ports.DeviceID) -join ', '))"
}

$serial = New-Object System.IO.Ports.SerialPort
$serial.PortName = $Port
$serial.BaudRate = $Baud
$serial.DataBits = $DataBits
$serial.Parity = [System.IO.Ports.Parity]::$Parity
$serial.StopBits = [System.IO.Ports.StopBits]::$StopBits
$serial.ReadTimeout = 150
$serial.WriteTimeout = 500
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    Write-Host "Opened $Port @ $Baud,$DataBits,$Parity,$StopBits" -ForegroundColor Green

    # Flush stale bytes
    Start-Sleep -Milliseconds 150
    if ($serial.BytesToRead -gt 0) {
        $trash = New-Object byte[] $serial.BytesToRead
        [void]$serial.Read($trash, 0, $trash.Length)
    }

    if (-not $ListenOnly) {
        $tx = HexToBytes $TxHex
        if ($tx.Length -eq 0) {
            throw "TxHex resolved to empty byte array."
        }
        Write-Host "TX ($($tx.Length) bytes): $(BytesToHex $tx)" -ForegroundColor Yellow
        $serial.Write($tx, 0, $tx.Length)
    } else {
        Write-Host "Listen-only mode: not transmitting" -ForegroundColor Yellow
    }

    $deadline = (Get-Date).AddSeconds($ReadSeconds)
    $rx = New-Object System.Collections.Generic.List[byte]
    $lastRxAt = Get-Date

    while ((Get-Date) -lt $deadline) {
        try {
            $value = $serial.ReadByte()
            if ($value -ge 0) {
                [void]$rx.Add([byte]$value)
                $lastRxAt = Get-Date
            }
        } catch [System.TimeoutException] {
            # no byte this poll
        }

        if ($rx.Count -gt 0 -and ((Get-Date) - $lastRxAt).TotalMilliseconds -ge $InterByteTimeoutMs) {
            $frame = $rx.ToArray()
            $stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
            Write-Host "[$stamp] RX ($($frame.Length) bytes): $(BytesToHex $frame)" -ForegroundColor Cyan

            $parsed = Try-ParseCO2 $frame
            if ($null -ne $parsed) {
                if ($parsed.Protocol -eq "CM1107N 16-byte") {
                    $status = if ($parsed.ChecksumEmpiricalOk) { "OK" } else { "BAD" }
                    Write-Host "  Parsed $($parsed.Protocol): CO2=$($parsed.CO2ppm) ppm | tempRaw=$($parsed.TempRaw) | rhRaw=$($parsed.RHRaw) | abcDays=$($parsed.AbcDays) | status=$($parsed.Status)" -ForegroundColor Magenta
                    Write-Host "  Checksum rx=$($parsed.ChecksumRx) calc=$($parsed.ChecksumCalc) => $status" -ForegroundColor Magenta
                } else {
                    $status = if ($parsed.ChecksumOk) { "OK" } else { "BAD" }
                    Write-Host "  Parsed $($parsed.Protocol): CO2=$($parsed.CO2ppm) ppm | checksum=$status" -ForegroundColor Magenta
                }
            }

            if ($ExpectedReplyLen -gt 0 -and $frame.Length -ne $ExpectedReplyLen -and -not $ListenOnly) {
                Write-Warning "Reply length $($frame.Length) differs from ExpectedReplyLen=$ExpectedReplyLen"
            }

            $rx.Clear()
        }
    }

    if ($rx.Count -gt 0) {
        $frame = $rx.ToArray()
        $stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
        Write-Host "[$stamp] RX tail ($($frame.Length) bytes): $(BytesToHex $frame)" -ForegroundColor Cyan
    }
}
finally {
    if ($serial -and $serial.IsOpen) {
        $serial.Close()
        Write-Host "Closed $Port" -ForegroundColor DarkGray
    }
}
