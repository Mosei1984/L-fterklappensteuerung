param(
    [switch] $NoHardware,
    [switch] $RequireLogicAnalyzer,
    [string] $SerialPort = "",
    [string] $ExpectedDeviceId = "1",
    [string] $ArtifactRoot = "artifacts\release\firmware"
)

$ErrorActionPreference = 'Stop'

function New-Directory {
    param([string] $Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Add-ReportLine {
    param([string] $Line)
    $script:ReportLines.Add($Line) | Out-Null
}

function Add-ReportBlock {
    param(
        [string] $Title,
        [string[]] $Lines
    )
    Add-ReportLine ""
    Add-ReportLine "## $Title"
    if ($Lines.Count -eq 0) {
        Add-ReportLine "- no output"
        return
    }
    Add-ReportLine '```text'
    foreach ($line in $Lines) {
        Add-ReportLine $line
    }
    Add-ReportLine '```'
}

function Invoke-LoggedCommand {
    param(
        [string] $Title,
        [scriptblock] $Command
    )

    Add-ReportLine ""
    Add-ReportLine "## $Title"
    $output = & $Command 2>&1
    $exitCode = if ($LASTEXITCODE -ne $null) { $LASTEXITCODE } else { 0 }
    Add-ReportLine '```text'
    foreach ($line in $output) {
        Add-ReportLine ([string] $line)
    }
    Add-ReportLine '```'

    if ($exitCode -ne 0) {
        throw "$Title failed with exit code $exitCode"
    }

    return [string[]] $output
}

function Get-ModbusCrc {
    param([byte[]] $Data)

    [uint16] $crc = 0xFFFF
    foreach ($byte in $Data) {
        $crc = [uint16] ($crc -bxor $byte)
        for ($bit = 0; $bit -lt 8; $bit++) {
            $lsbSet = (($crc -band 0x0001) -ne 0)
            $crc = [uint16] ($crc -shr 1)
            if ($lsbSet) {
                $crc = [uint16] ($crc -bxor 0xA001)
            }
        }
    }
    return $crc
}

function New-ModbusFrame {
    param([byte[]] $Payload)

    $crc = Get-ModbusCrc -Data $Payload
    $frame = New-Object byte[] ($Payload.Length + 2)
    [Array]::Copy($Payload, $frame, $Payload.Length)
    $frame[$Payload.Length] = [byte] ($crc -band 0x00FF)
    $frame[$Payload.Length + 1] = [byte] (($crc -shr 8) -band 0x00FF)
    return $frame
}

function Test-ModbusCrc {
    param([byte[]] $Frame)

    if ($Frame.Length -lt 3) {
        return $false
    }

    $payloadLength = $Frame.Length - 2
    $payload = New-Object byte[] $payloadLength
    [Array]::Copy($Frame, $payload, $payloadLength)
    $expected = Get-ModbusCrc -Data $payload
    $actual = [uint16] ([int] $Frame[$payloadLength] -bor
        ([int] $Frame[$payloadLength + 1] -shl 8))
    return $expected -eq $actual
}

function Read-SerialLines {
    param(
        [System.IO.Ports.SerialPort] $Port,
        [int] $TimeoutMs = 1500
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $lines = New-Object System.Collections.Generic.List[string]
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $Port.ReadLine()
            if ($line.Length -gt 0) {
                $lines.Add($line.Trim()) | Out-Null
            }
        } catch [TimeoutException] {
            Start-Sleep -Milliseconds 20
        }
    }
    return [string[]] $lines
}

function Send-TextCommand {
    param(
        [System.IO.Ports.SerialPort] $Port,
        [string] $Command
    )

    $Port.DiscardInBuffer()
    $Port.WriteLine($Command)
    return Read-SerialLines -Port $Port
}

function Send-ModbusFrame {
    param(
        [System.IO.Ports.SerialPort] $Port,
        [byte[]] $Frame,
        [int] $TimeoutMs = 500
    )

    $Port.DiscardInBuffer()
    $Port.Write($Frame, 0, $Frame.Length)
    Start-Sleep -Milliseconds $TimeoutMs
    $count = $Port.BytesToRead
    $response = New-Object byte[] $count
    if ($count -gt 0) {
        [void] $Port.Read($response, 0, $count)
    }
    return $response
}

function Assert-SerialDiagnostics {
    param(
        [System.IO.Ports.SerialPort] $Port,
        [byte] $DeviceId
    )

    $textCommands = @(
        "ID$DeviceId DIAG?",
        "ID$DeviceId FAULT?",
        "ID$DeviceId SELFTEST?"
    )

    foreach ($command in $textCommands) {
        $lines = Send-TextCommand -Port $Port -Command $command
        Add-ReportBlock -Title "Serial $command" -Lines $lines
        if ($lines.Count -eq 0) {
            throw "$command returned no lines"
        }
        if ($lines | Where-Object { $_ -like 'FEHLER:*' }) {
            throw "$command returned an idle FEHLER line"
        }
    }
}

function Assert-ModbusDiagnostics {
    param(
        [System.IO.Ports.SerialPort] $Port,
        [byte] $DeviceId
    )

    $readAll = New-ModbusFrame -Payload ([byte[]] @($DeviceId, 0x03, 0x00, 0x00, 0x00, 0x24))
    $readDiagnostics = New-ModbusFrame -Payload ([byte[]] @($DeviceId, 0x03, 0x00, 0x11, 0x00, 0x06))
    $readPastEnd = New-ModbusFrame -Payload ([byte[]] @($DeviceId, 0x03, 0x00, 0x23, 0x00, 0x02))
    $writeDiagnostic = New-ModbusFrame -Payload ([byte[]] @($DeviceId, 0x06, 0x00, 0x11, 0x00, 0x01))
    $broadcastWrite = New-ModbusFrame -Payload ([byte[]] @(0x00, 0x06, 0x00, 0x00, 0x00, 0x00))

    $readAllResponse = Send-ModbusFrame -Port $Port -Frame $readAll
    Add-ReportBlock -Title "Modbus read 0..35" -Lines @([BitConverter]::ToString($readAllResponse))
    if (($readAllResponse.Length -lt 5) -or (-not (Test-ModbusCrc -Frame $readAllResponse))) {
        throw 'Modbus read 0..35 returned no valid CRC frame'
    }

    $readDiagnosticsResponse = Send-ModbusFrame -Port $Port -Frame $readDiagnostics
    Add-ReportBlock -Title "Modbus read 17..22" -Lines @([BitConverter]::ToString($readDiagnosticsResponse))
    if (($readDiagnosticsResponse.Length -lt 5) -or (-not (Test-ModbusCrc -Frame $readDiagnosticsResponse))) {
        throw 'Modbus read 17..22 returned no valid CRC frame'
    }

    $readPastEndResponse = Send-ModbusFrame -Port $Port -Frame $readPastEnd
    Add-ReportBlock -Title "Modbus read past 35" -Lines @([BitConverter]::ToString($readPastEndResponse))
    if (($readPastEndResponse.Length -lt 5) -or (-not (Test-ModbusCrc -Frame $readPastEndResponse)) -or
        ($readPastEndResponse[1] -ne 0x83) -or ($readPastEndResponse[2] -ne 0x02)) {
        throw 'Modbus read past 35 did not return illegal-address exception'
    }

    $writeDiagnosticResponse = Send-ModbusFrame -Port $Port -Frame $writeDiagnostic
    Add-ReportBlock -Title "Modbus diagnostic write" -Lines @([BitConverter]::ToString($writeDiagnosticResponse))
    if (($writeDiagnosticResponse.Length -lt 5) -or (-not (Test-ModbusCrc -Frame $writeDiagnosticResponse)) -or
        ($writeDiagnosticResponse[1] -ne 0x86) -or ($writeDiagnosticResponse[2] -ne 0x02)) {
        throw 'Modbus diagnostic write did not return illegal-address exception'
    }

    $broadcastResponse = Send-ModbusFrame -Port $Port -Frame $broadcastWrite
    Add-ReportBlock -Title 'Modbus broadcast write' -Lines @([BitConverter]::ToString($broadcastResponse))
    if ($broadcastResponse.Length -ne 0) {
        throw 'Modbus broadcast write produced a response'
    }
}

function Assert-DocumentationSafety {
    $docs = @('README.md', 'tools/la/README.md', 'AGENTS.md') |
        Where-Object { Test-Path -LiteralPath $_ }
    $unsafePattern = '(?i)\b(fire|smoke|co|combustion|life-safety)\b.{0,32}\b(certified|rated|approved|detector|alarm|protection|equipment)\b'
    $negativeSafetyPattern = '(?i)\b(not|kein|keine|nicht)\b.{0,80}\b(fire|smoke|co|combustion|life-safety|detector|alarm|protection|equipment)\b'
    $findings = New-Object System.Collections.Generic.List[string]
    foreach ($doc in $docs) {
        $lineNumber = 0
        Get-Content -LiteralPath $doc | ForEach-Object {
            $lineNumber += 1
            $line = [string] $_
            if (($line -match $unsafePattern) -and ($line -notmatch $negativeSafetyPattern)) {
                $resolvedPath = (Resolve-Path -LiteralPath $doc).Path
                $findings.Add(('{0}:{1}: {2}' -f $resolvedPath, $lineNumber, $line.Trim())) | Out-Null
            }
        }
    }

    Add-ReportBlock -Title 'Documentation safety scan' -Lines ([string[]] $findings)
    if ($findings.Count -gt 0) {
        throw 'Documentation safety scan found unsafe marketing language'
    }
}

New-Directory -Path $ArtifactRoot
$script:ReportLines = New-Object System.Collections.Generic.List[string]
Add-ReportLine '# Firmware Acceptance Report'
Add-ReportLine ""
Add-ReportLine "- Timestamp UTC: $([DateTime]::UtcNow.ToString('s'))Z"
Add-ReportLine "- NoHardware: $NoHardware"
Add-ReportLine "- RequireLogicAnalyzer: $RequireLogicAnalyzer"
Add-ReportLine "- SerialPort: $SerialPort"
Add-ReportLine "- ExpectedDeviceId: $ExpectedDeviceId"

$reportPath = Join-Path $ArtifactRoot 'acceptance-report.md'

try {
    Invoke-LoggedCommand -Title 'Hard checks' -Command {
        powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_hard_checks.ps1
    } | Out-Null

    $uf2Source = Join-Path '.pio\build\pico' 'firmware.uf2'
    if (-not (Test-Path -LiteralPath $uf2Source)) {
        throw "Firmware UF2 not found at $uf2Source"
    }

    $uf2Target = Join-Path $ArtifactRoot 'firmware.uf2'
    Copy-Item -LiteralPath $uf2Source -Destination $uf2Target -Force
    $hash = Get-FileHash -LiteralPath $uf2Target -Algorithm SHA256
    $hashLine = "$($hash.Hash)  firmware.uf2"
    $hashPath = Join-Path $ArtifactRoot 'firmware.uf2.sha256'
    Set-Content -LiteralPath $hashPath -Value $hashLine -Encoding ascii
    Add-ReportLine ""
    Add-ReportLine '## Firmware artifact'
    Add-ReportLine "- UF2: $uf2Target"
    Add-ReportLine "- SHA256: $hashLine"

    Assert-DocumentationSafety

    if (-not $NoHardware) {
        if ([string]::IsNullOrWhiteSpace($SerialPort)) {
            throw 'SerialPort is required unless -NoHardware is set'
        }

        [byte] $deviceId = [byte]::Parse($ExpectedDeviceId)
        $port = [System.IO.Ports.SerialPort]::new($SerialPort, 38400, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
        $port.NewLine = "`n"
        $port.ReadTimeout = 250
        $port.WriteTimeout = 250
        try {
            $port.Open()
            Assert-SerialDiagnostics -Port $port -DeviceId $deviceId
            Assert-ModbusDiagnostics -Port $port -DeviceId $deviceId
        } finally {
            if ($port.IsOpen) {
                $port.Close()
            }
        }

        if ($RequireLogicAnalyzer) {
            $laOutput = & .\tools\la\capture_luefterklappe.ps1 -ExpectedId $ExpectedDeviceId 2>&1
            Add-ReportBlock -Title 'Logic analyzer' -Lines ([string[]] $laOutput)
            if ($LASTEXITCODE -ne 0 -or ($laOutput | Where-Object { $_ -like 'FAIL:*' })) {
                throw 'Logic analyzer acceptance failed'
            }
        }
    } else {
        Add-ReportLine ""
        Add-ReportLine '## Hardware diagnostics'
        Add-ReportLine "- Skipped because -NoHardware was set."
    }

    if (-not (Test-Path -LiteralPath $uf2Target)) {
        throw 'NoHardware artifact check failed: firmware.uf2 missing'
    }
    if (-not (Test-Path -LiteralPath $hashPath)) {
        throw 'NoHardware artifact check failed: firmware.uf2.sha256 missing'
    }

    Add-ReportLine ""
    Add-ReportLine '## Result'
    Add-ReportLine 'PASS'
    Set-Content -LiteralPath $reportPath -Value $ReportLines -Encoding utf8
} catch {
    Add-ReportLine ""
    Add-ReportLine '## Result'
    Add-ReportLine "FAIL: $($_.Exception.Message)"
    Set-Content -LiteralPath $reportPath -Value $ReportLines -Encoding utf8
    throw
}

if (-not (Test-Path -LiteralPath $reportPath)) {
    throw 'Acceptance report was not written'
}

Write-Host "Firmware acceptance report: $reportPath"
