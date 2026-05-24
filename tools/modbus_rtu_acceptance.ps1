param(
  [string] $RtuPort = "",
  [int] $BaudRate = 38400,
  [byte] $DeviceId = 1,
  [string] $UsbPort = "",
  [switch] $ExerciseMotion,
  [switch] $PrintOnly,
  [switch] $SelfTest,
  [string] $ArtifactRoot = "artifacts\release\modbus-rtu"
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$artifactPath = if ([System.IO.Path]::IsPathRooted($ArtifactRoot)) {
  $ArtifactRoot
} else {
  Join-Path $repoRoot $ArtifactRoot
}
$reportPath = Join-Path $artifactPath 'modbus-rtu-report.md'
$script:ReportLines = New-Object System.Collections.Generic.List[string]

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
  if (($null -eq $Lines) -or ($Lines.Count -eq 0)) {
    Add-ReportLine "- no output"
    return
  }
  Add-ReportLine '```text'
  foreach ($line in $Lines) {
    Add-ReportLine $line
  }
  Add-ReportLine '```'
}

function Format-Bytes {
  param([byte[]] $Data)
  if (($null -eq $Data) -or ($Data.Length -eq 0)) {
    return '<none>'
  }
  return [BitConverter]::ToString($Data)
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

function New-ReadHoldingFrame {
  param(
    [byte] $Address,
    [uint16] $Start,
    [uint16] $Quantity
  )

  return New-ModbusFrame -Payload ([byte[]] @(
      $Address, 0x03,
      (($Start -shr 8) -band 0xFF), ($Start -band 0xFF),
      (($Quantity -shr 8) -band 0xFF), ($Quantity -band 0xFF)
    ))
}

function New-WriteSingleFrame {
  param(
    [byte] $Address,
    [uint16] $Register,
    [uint16] $Value
  )

  return New-ModbusFrame -Payload ([byte[]] @(
      $Address, 0x06,
      (($Register -shr 8) -band 0xFF), ($Register -band 0xFF),
      (($Value -shr 8) -band 0xFF), ($Value -band 0xFF)
    ))
}

function Test-ModbusCrc {
  param([byte[]] $Frame)

  if (($null -eq $Frame) -or ($Frame.Length -lt 3)) {
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

function Read-UInt16BigEndian {
  param(
    [byte[]] $Data,
    [int] $Offset
  )

  return [uint16] ((([int] $Data[$Offset]) -shl 8) -bor
      ([int] $Data[$Offset + 1]))
}

function Read-SerialResponse {
  param(
    [System.IO.Ports.SerialPort] $Port,
    [int] $ExpectedLength = 0,
    [int] $TimeoutMs = 500
  )

  $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
  $bytes = New-Object System.Collections.Generic.List[byte]
  while ([DateTime]::UtcNow -lt $deadline) {
    $count = $Port.BytesToRead
    if ($count -gt 0) {
      $buffer = New-Object byte[] $count
      [void] $Port.Read($buffer, 0, $count)
      foreach ($byte in $buffer) {
        $bytes.Add($byte) | Out-Null
      }
      if (($ExpectedLength -gt 0) -and ($bytes.Count -ge $ExpectedLength)) {
        break
      }
    } else {
      Start-Sleep -Milliseconds 10
    }
  }
  return [byte[]] $bytes.ToArray()
}

function Invoke-ModbusFrame {
  param(
    [System.IO.Ports.SerialPort] $Port,
    [byte[]] $Frame,
    [int] $ExpectedLength,
    [int] $TimeoutMs = 500
  )

  $Port.DiscardInBuffer()
  $Port.Write($Frame, 0, $Frame.Length)
  return Read-SerialResponse -Port $Port -ExpectedLength $ExpectedLength -TimeoutMs $TimeoutMs
}

function Assert-FrameBase {
  param(
    [byte[]] $Response,
    [byte] $ExpectedAddress
  )

  if (($null -eq $Response) -or ($Response.Length -lt 5)) {
    throw "Expected Modbus response from ID $ExpectedAddress, got: $(Format-Bytes $Response)"
  }
  if (-not (Test-ModbusCrc -Frame $Response)) {
    throw "Modbus response has invalid CRC: $(Format-Bytes $Response)"
  }
  if ($Response[0] -ne $ExpectedAddress) {
    throw "Modbus response used ID $($Response[0]), expected $ExpectedAddress"
  }
}

function Assert-ExceptionResponse {
  param(
    [byte[]] $Response,
    [byte] $ExpectedAddress,
    [byte] $Function,
    [byte] $ExceptionCode,
    [string] $Title
  )

  Assert-FrameBase -Response $Response -ExpectedAddress $ExpectedAddress
  $expectedFunction = [byte] ($Function -bor 0x80)
  if (($Response.Length -ne 5) -or ($Response[1] -ne $expectedFunction) -or
      ($Response[2] -ne $ExceptionCode)) {
    throw "$Title returned unexpected exception frame: $(Format-Bytes $Response)"
  }
}

function Invoke-ReadHolding {
  param(
    [System.IO.Ports.SerialPort] $Port,
    [byte] $Address,
    [uint16] $Start,
    [uint16] $Quantity,
    [string] $Title
  )

  $frame = New-ReadHoldingFrame -Address $Address -Start $Start -Quantity $Quantity
  $expectedLength = 5 + (2 * $Quantity)
  $response = Invoke-ModbusFrame -Port $Port -Frame $frame -ExpectedLength $expectedLength
  Add-ReportBlock -Title $Title -Lines @(
    "TX: $(Format-Bytes $frame)",
    "RX: $(Format-Bytes $response)"
  )

  Assert-FrameBase -Response $response -ExpectedAddress $Address
  if (($response.Length -ne $expectedLength) -or ($response[1] -ne 0x03) -or
      ($response[2] -ne (2 * $Quantity))) {
    throw "$Title returned unexpected read frame: $(Format-Bytes $response)"
  }

  $values = New-Object uint16[] $Quantity
  for ($index = 0; $index -lt $Quantity; $index++) {
    $values[$index] = Read-UInt16BigEndian -Data $response -Offset (3 + (2 * $index))
  }
  return ,$values
}

function Invoke-WriteSingle {
  param(
    [System.IO.Ports.SerialPort] $Port,
    [byte] $Address,
    [uint16] $Register,
    [uint16] $Value,
    [string] $Title
  )

  $frame = New-WriteSingleFrame -Address $Address -Register $Register -Value $Value
  $response = Invoke-ModbusFrame -Port $Port -Frame $frame -ExpectedLength 8
  Add-ReportBlock -Title $Title -Lines @(
    "TX: $(Format-Bytes $frame)",
    "RX: $(Format-Bytes $response)"
  )

  Assert-FrameBase -Response $response -ExpectedAddress $Address
  if (($response.Length -ne 8) -or ($response[1] -ne 0x06) -or
      ((Format-Bytes $response) -ne (Format-Bytes $frame))) {
    throw "$Title did not echo the write-single request"
  }
}

function Invoke-ExpectException {
  param(
    [System.IO.Ports.SerialPort] $Port,
    [byte[]] $Frame,
    [byte] $Address,
    [byte] $Function,
    [byte] $ExceptionCode,
    [string] $Title
  )

  $response = Invoke-ModbusFrame -Port $Port -Frame $Frame -ExpectedLength 5
  Add-ReportBlock -Title $Title -Lines @(
    "TX: $(Format-Bytes $Frame)",
    "RX: $(Format-Bytes $response)"
  )
  Assert-ExceptionResponse -Response $response -ExpectedAddress $Address `
    -Function $Function -ExceptionCode $ExceptionCode -Title $Title
}

function Invoke-ExpectNoResponse {
  param(
    [System.IO.Ports.SerialPort] $Port,
    [byte[]] $Frame,
    [string] $Title
  )

  $response = Invoke-ModbusFrame -Port $Port -Frame $Frame -ExpectedLength 0 -TimeoutMs 300
  Add-ReportBlock -Title $Title -Lines @(
    "TX: $(Format-Bytes $Frame)",
    "RX: $(Format-Bytes $response)"
  )
  if ($response.Length -ne 0) {
    throw "$Title produced a response: $(Format-Bytes $response)"
  }
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

function Invoke-UsbDiagnostics {
  param(
    [string] $PortName,
    [byte] $Address
  )

  $port = [System.IO.Ports.SerialPort]::new($PortName, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
  $port.NewLine = "`n"
  $port.ReadTimeout = 250
  $port.WriteTimeout = 250
  try {
    $port.Open()
    foreach ($command in @("ID$Address DIAG?", "ID$Address FAULT?", "ID$Address SELFTEST?", "ID$Address MOTORCFG?")) {
      $port.DiscardInBuffer()
      $port.WriteLine($command)
      $lines = Read-SerialLines -Port $port
      Add-ReportBlock -Title "USB $command" -Lines $lines
      if ($lines.Count -eq 0) {
        throw "$command returned no USB diagnostic lines"
      }
      if ($lines | Where-Object { $_ -like 'FEHLER:*' }) {
        throw "$command returned an error line"
      }
    }
  } finally {
    if ($port.IsOpen) {
      $port.Close()
    }
  }
}

function Invoke-SelfTest {
  Add-ReportBlock -Title 'CRC known vector' -Lines @(
    "request: 01-03-00-00-00-0A",
    "expected: C5-CD",
    "actual: $(Format-Bytes (New-ReadHoldingFrame -Address 1 -Start 0 -Quantity 10))"
  )
  $frame = New-ReadHoldingFrame -Address 1 -Start 0 -Quantity 10
  if ((Format-Bytes $frame) -ne '01-03-00-00-00-0A-C5-CD') {
    throw 'CRC known vector failed'
  }

  $exception = New-ModbusFrame -Payload ([byte[]] @(1, 0x83, 0x02))
  if (-not (Test-ModbusCrc -Frame $exception)) {
    throw 'Exception CRC self-test failed'
  }

  $sampleRead = New-ModbusFrame -Payload ([byte[]] @(1, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1B))
  if (-not (Test-ModbusCrc -Frame $sampleRead)) {
    throw 'Read response CRC self-test failed'
  }
}

function Invoke-PrintOnly {
  $steps = @(
    'Read holding 0..35',
    'Read diagnostics 17..22',
    'Read degree/StallGuard/Homing/Motor 23..35',
    'Reject read past register 35',
    'Reject diagnostic write 17',
    'Reject invalid safe-position value',
    'Reject invalid StallGuard threshold',
    'Reject invalid homing value',
    'Reject invalid motor speed/current values',
    'Ignore wrong device ID',
    'Broadcast safe-position write',
    'Write and restore safe-position register 16',
    'Write and restore StallGuard threshold register 27',
    'Write and restore motor speed/current registers 33..35',
    'Frame-timeout resync with partial request',
    'Optional target-degree movement when -ExerciseMotion is set'
  )

  foreach ($step in $steps) {
    Write-Host "- $step"
  }

  Add-ReportBlock -Title 'Planned Modbus RTU steps' -Lines $steps
}

function Invoke-HardwareAcceptance {
  if ([string]::IsNullOrWhiteSpace($RtuPort)) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    Add-ReportBlock -Title 'Detected COM ports' -Lines ([string[]] $ports)
    throw 'RtuPort is required. Use a USB-RS485 master adapter on the RS485 A/B bus; the Pico USB CDC port is not the RTU bus.'
  }

  if (-not [string]::IsNullOrWhiteSpace($UsbPort)) {
    Invoke-UsbDiagnostics -PortName $UsbPort -Address $DeviceId
  }

  $port = [System.IO.Ports.SerialPort]::new($RtuPort, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
  $port.ReadTimeout = 250
  $port.WriteTimeout = 250
  try {
    $port.Open()

    $all = Invoke-ReadHolding -Port $port -Address $DeviceId -Start 0 -Quantity 36 -Title 'Read holding 0..35'
    if ($all[7] -ne $DeviceId) {
      throw "Register 7 reported device ID $($all[7]), expected $DeviceId"
    }
    if ($all[22] -ne 5) {
      throw "Protocol register 22 reported $($all[22]), expected 5"
    }

    $diagnostics = Invoke-ReadHolding -Port $port -Address $DeviceId -Start 17 -Quantity 6 -Title 'Read diagnostics 17..22'
    if ($diagnostics[5] -ne 5) {
      throw "Diagnostics protocol version reported $($diagnostics[5]), expected 5"
    }

    $degreeConfig = Invoke-ReadHolding -Port $port -Address $DeviceId -Start 23 -Quantity 13 -Title 'Read degree/StallGuard/Homing/Motor 23..35'
    foreach ($index in 0..3) {
      if ($degreeConfig[$index] -gt 90) {
        throw "Degree register $($index + 23) out of range: $($degreeConfig[$index])"
      }
    }
    if ($degreeConfig[4] -gt 255) {
      throw "StallGuard threshold out of range: $($degreeConfig[4])"
    }
    foreach ($index in 5..9) {
      if ($degreeConfig[$index] -gt 1) {
        throw "Homing register $($index + 23) out of range: $($degreeConfig[$index])"
      }
    }
    foreach ($index in 10..11) {
      if (($degreeConfig[$index] -lt 20) -or ($degreeConfig[$index] -gt 5000)) {
        throw "Motor speed register $($index + 23) out of range: $($degreeConfig[$index])"
      }
    }
    if (($degreeConfig[12] -lt 100) -or ($degreeConfig[12] -gt 1000)) {
      throw "Motor current register 35 out of range: $($degreeConfig[12])"
    }

    Invoke-ExpectException -Port $port `
      -Frame (New-ReadHoldingFrame -Address $DeviceId -Start 35 -Quantity 2) `
      -Address $DeviceId -Function 0x03 -ExceptionCode 0x02 `
      -Title 'Reject read past register 35'

    Invoke-ExpectException -Port $port `
      -Frame (New-WriteSingleFrame -Address $DeviceId -Register 17 -Value 1) `
      -Address $DeviceId -Function 0x06 -ExceptionCode 0x02 `
      -Title 'Reject diagnostic write 17'

    Invoke-ExpectException -Port $port `
      -Frame (New-WriteSingleFrame -Address $DeviceId -Register 16 -Value 1001) `
      -Address $DeviceId -Function 0x06 -ExceptionCode 0x03 `
      -Title 'Reject invalid safe-position value'

    Invoke-ExpectException -Port $port `
      -Frame (New-WriteSingleFrame -Address $DeviceId -Register 27 -Value 256) `
      -Address $DeviceId -Function 0x06 -ExceptionCode 0x03 `
      -Title 'Reject invalid StallGuard threshold'

    Invoke-ExpectException -Port $port `
      -Frame (New-WriteSingleFrame -Address $DeviceId -Register 28 -Value 2) `
      -Address $DeviceId -Function 0x06 -ExceptionCode 0x03 `
      -Title 'Reject invalid homing value'

    Invoke-ExpectException -Port $port `
      -Frame (New-WriteSingleFrame -Address $DeviceId -Register 33 -Value 19) `
      -Address $DeviceId -Function 0x06 -ExceptionCode 0x03 `
      -Title 'Reject invalid normal speed register 33'

    Invoke-ExpectException -Port $port `
      -Frame (New-WriteSingleFrame -Address $DeviceId -Register 35 -Value 1001) `
      -Address $DeviceId -Function 0x06 -ExceptionCode 0x03 `
      -Title 'Reject invalid motor current register 35'

    $wrongAddress = if ($DeviceId -eq 247) { [byte] 1 } else { [byte] 247 }
    Invoke-ExpectNoResponse -Port $port `
      -Frame (New-ReadHoldingFrame -Address $wrongAddress -Start 8 -Quantity 2) `
      -Title 'Ignore wrong device ID'

    $originalSafe = $all[16]
    $broadcastSafe = if ($originalSafe -eq 321) { [uint16] 654 } else { [uint16] 321 }
    Invoke-ExpectNoResponse -Port $port `
      -Frame (New-WriteSingleFrame -Address 0 -Register 16 -Value $broadcastSafe) `
      -Title 'Broadcast safe-position write'
    $safeAfterBroadcast = Invoke-ReadHolding -Port $port -Address $DeviceId -Start 16 -Quantity 1 -Title 'Read safe-position after broadcast'
    if ($safeAfterBroadcast[0] -ne $originalSafe) {
      throw "Broadcast write changed safe position from $originalSafe to $($safeAfterBroadcast[0])"
    }

    $testSafe = if ($originalSafe -eq 333) { [uint16] 666 } else { [uint16] 333 }
    Invoke-WriteSingle -Port $port -Address $DeviceId -Register 16 -Value $testSafe -Title 'Write test safe-position register 16'
    $safeAfterWrite = Invoke-ReadHolding -Port $port -Address $DeviceId -Start 16 -Quantity 1 -Title 'Read test safe-position register 16'
    if ($safeAfterWrite[0] -ne $testSafe) {
      throw "Safe-position write read back $($safeAfterWrite[0]), expected $testSafe"
    }
    Invoke-WriteSingle -Port $port -Address $DeviceId -Register 16 -Value $originalSafe -Title 'Restore safe-position register 16'

    $originalStallGuard = $degreeConfig[4]
    $testStallGuard = if ($originalStallGuard -eq 42) { [uint16] 43 } else { [uint16] 42 }
    Invoke-WriteSingle -Port $port -Address $DeviceId -Register 27 -Value $testStallGuard -Title 'Write test StallGuard threshold register 27'
    $stallAfterWrite = Invoke-ReadHolding -Port $port -Address $DeviceId -Start 27 -Quantity 1 -Title 'Read test StallGuard threshold register 27'
    if ($stallAfterWrite[0] -ne $testStallGuard) {
      throw "StallGuard write read back $($stallAfterWrite[0]), expected $testStallGuard"
    }
    Invoke-WriteSingle -Port $port -Address $DeviceId -Register 27 -Value $originalStallGuard -Title 'Restore StallGuard threshold register 27'

    $originalNormalSpeed = $degreeConfig[10]
    $testNormalSpeed = if ($originalNormalSpeed -eq 777) { [uint16] 888 } else { [uint16] 777 }
    Invoke-WriteSingle -Port $port -Address $DeviceId -Register 33 -Value $testNormalSpeed -Title 'Write test normal speed register 33'
    $normalSpeedAfterWrite = Invoke-ReadHolding -Port $port -Address $DeviceId -Start 33 -Quantity 1 -Title 'Read test normal speed register 33'
    if ($normalSpeedAfterWrite[0] -ne $testNormalSpeed) {
      throw "Normal speed write read back $($normalSpeedAfterWrite[0]), expected $testNormalSpeed"
    }
    Invoke-WriteSingle -Port $port -Address $DeviceId -Register 33 -Value $originalNormalSpeed -Title 'Restore normal speed register 33'

    $originalHomingSpeed = $degreeConfig[11]
    $testHomingSpeed = if ($originalHomingSpeed -eq 123) { [uint16] 124 } else { [uint16] 123 }
    Invoke-WriteSingle -Port $port -Address $DeviceId -Register 34 -Value $testHomingSpeed -Title 'Write test homing speed register 34'
    $homingSpeedAfterWrite = Invoke-ReadHolding -Port $port -Address $DeviceId -Start 34 -Quantity 1 -Title 'Read test homing speed register 34'
    if ($homingSpeedAfterWrite[0] -ne $testHomingSpeed) {
      throw "Homing speed write read back $($homingSpeedAfterWrite[0]), expected $testHomingSpeed"
    }
    Invoke-WriteSingle -Port $port -Address $DeviceId -Register 34 -Value $originalHomingSpeed -Title 'Restore homing speed register 34'

    $originalRunCurrent = $degreeConfig[12]
    $testRunCurrent = if ($originalRunCurrent -eq 850) { [uint16] 851 } else { [uint16] 850 }
    Invoke-WriteSingle -Port $port -Address $DeviceId -Register 35 -Value $testRunCurrent -Title 'Write test motor current register 35'
    $runCurrentAfterWrite = Invoke-ReadHolding -Port $port -Address $DeviceId -Start 35 -Quantity 1 -Title 'Read test motor current register 35'
    if ($runCurrentAfterWrite[0] -ne $testRunCurrent) {
      throw "Motor current write read back $($runCurrentAfterWrite[0]), expected $testRunCurrent"
    }
    Invoke-WriteSingle -Port $port -Address $DeviceId -Register 35 -Value $originalRunCurrent -Title 'Restore motor current register 35'

    $partialFrame = [byte[]] @($DeviceId, 0x03, 0x00)
    $port.DiscardInBuffer()
    $port.Write($partialFrame, 0, $partialFrame.Length)
    Start-Sleep -Milliseconds 80
    [void] (Invoke-ReadHolding -Port $port -Address $DeviceId -Start 8 -Quantity 2 -Title 'Frame-timeout resync with valid status read')

    if ($ExerciseMotion) {
      $currentDegree = (Invoke-ReadHolding -Port $port -Address $DeviceId -Start 24 -Quantity 1 -Title 'Read current degree before optional movement')[0]
      Invoke-WriteSingle -Port $port -Address $DeviceId -Register 23 -Value $currentDegree -Title 'Optional no-distance target-degree write'
    } else {
      Add-ReportLine ""
      Add-ReportLine '## Motion exercise'
      Add-ReportLine '- Skipped. Use -ExerciseMotion only when the valve mechanics are safely assembled.'
    }
  } finally {
    if ($port.IsOpen) {
      $port.Close()
    }
  }
}

New-Directory -Path $artifactPath
Add-ReportLine '# Modbus RTU Acceptance Report'
Add-ReportLine ""
Add-ReportLine "- Timestamp UTC: $([DateTime]::UtcNow.ToString('s'))Z"
Add-ReportLine "- RtuPort: $RtuPort"
Add-ReportLine "- BaudRate: $BaudRate"
Add-ReportLine "- DeviceId: $DeviceId"
Add-ReportLine "- UsbPort: $UsbPort"
Add-ReportLine "- ExerciseMotion: $ExerciseMotion"
Add-ReportLine "- PrintOnly: $PrintOnly"
Add-ReportLine "- SelfTest: $SelfTest"

try {
  if ($SelfTest) {
    Invoke-SelfTest
  } elseif ($PrintOnly) {
    Invoke-PrintOnly
  } else {
    Invoke-HardwareAcceptance
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

Write-Host "Modbus RTU acceptance report: $reportPath"
