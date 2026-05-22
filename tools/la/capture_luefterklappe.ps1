param(
  [string]$SigrokCli = "sigrok-cli",
  [string]$Python = "python",
  [string]$Driver = "fx2lafw",
  [string]$SampleRate = "4000000",
  [double]$SampleRateHz = 4000000,
  [string]$Duration = "10s",
  [int]$ExpectedId = 1,
  [string]$Channels = "D0,D1,D2,D3,D4,D5,D6,D7",
  [string]$Rs485DeviceTxChannel = "D0",
  [string]$Rs485DeviceRxChannel = "D1",
  [string]$StepChannel = "D2",
  [string]$DirChannel = "D3",
  [string]$EnableChannel = "D6",
  [string]$TmcTxChannel = "D7",
  [string]$TmcRxChannel = "",
  [string]$OutputDir = "artifacts/la",
  [string]$RunId = (Get-Date -Format "yyyyMMdd-HHmmss"),
  [string]$InputCapture = "",
  [switch]$SkipCapture,
  [switch]$SkipDecode,
  [switch]$SkipAnalyze,
  [switch]$EnableActiveHigh,
  [switch]$PrintOnly
)

$ErrorActionPreference = "Stop"

function Resolve-Tool {
  param(
    [string]$Name,
    [string]$Fallback
  )

  $command = Get-Command $Name -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }
  if ($Fallback -and (Test-Path -LiteralPath $Fallback)) {
    return $Fallback
  }
  throw "Tool not found: $Name"
}

function Normalize-OptionalChannel {
  param([string]$Value)

  if ([string]::IsNullOrWhiteSpace($Value)) {
    return ""
  }
  $normalized = $Value.Trim()
  if ($normalized -in @("-", "off", "none", "disabled")) {
    return ""
  }
  return $normalized
}

function Format-CommandLine {
  param(
    [string]$Exe,
    [string[]]$ArgumentList
  )

  $parts = @($Exe) + $ArgumentList
  return ($parts | ForEach-Object {
      if ($_ -match "\s") {
        '"' + $_ + '"'
      } else {
        $_
      }
    }) -join " "
}

function Invoke-Tool {
  param(
    [string]$Exe,
    [string[]]$ArgumentList
  )

  Write-Host ("+ " + (Format-CommandLine -Exe $Exe -ArgumentList $ArgumentList))
  if (-not $PrintOnly) {
    & $Exe @ArgumentList
  }
}

function Export-UartBytes {
  param(
    [string]$CapturePath,
    [string]$OutputPath,
    [string]$LineRole,
    [string]$Channel,
    [int]$Baud
  )

  $decoder = "uart:${LineRole}=${Channel}:baudrate=${Baud}:data_bits=8:parity=none:stop_bits=1"
  Invoke-Tool -Exe $script:SigrokExe -ArgumentList @(
    "-i", $CapturePath,
    "-P", $decoder,
    "-B", "uart=$LineRole",
    "-o", $OutputPath
  )
}

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Resolve-Path (Join-Path $scriptDir "..\..")
$TmcTxChannel = Normalize-OptionalChannel -Value $TmcTxChannel
$TmcRxChannel = Normalize-OptionalChannel -Value $TmcRxChannel
$runDir = Join-Path (Resolve-Path $repoRoot) (Join-Path $OutputDir $RunId)
$capturePath = if ($InputCapture) {
  (Resolve-Path -LiteralPath $InputCapture).Path
} else {
  Join-Path $runDir "capture.sr"
}

if (-not $SkipCapture -or -not $SkipDecode) {
  $fallbackSigrok = Join-Path $env:LOCALAPPDATA "sigrok\sigrok-cli\sigrok-cli.exe"
  $script:SigrokExe = Resolve-Tool -Name $SigrokCli -Fallback $fallbackSigrok
}

if (-not $SkipAnalyze) {
  $script:PythonExe = Resolve-Tool -Name $Python -Fallback ""
}

if (-not $PrintOnly) {
  New-Item -ItemType Directory -Force -Path $runDir | Out-Null
}

if (-not $SkipCapture) {
  Invoke-Tool -Exe $script:SigrokExe -ArgumentList @(
    "-d", $Driver,
    "-C", $Channels,
    "-c", "samplerate=$SampleRate",
    "--time", $Duration,
    "-O", "srzip",
    "-o", $capturePath
  )
}

$rs485TxPath = Join-Path $runDir "rs485_device_tx.bin"
$rs485RxPath = Join-Path $runDir "rs485_device_rx.bin"
$tmcTxPath = if ($TmcTxChannel) { Join-Path $runDir "tmc_tx.bin" } else { "" }
$tmcRxPath = if ($TmcRxChannel) { Join-Path $runDir "tmc_rx.bin" } else { "" }
$csvPath = Join-Path $runDir "samples.csv"

if (-not $SkipDecode) {
  Export-UartBytes -CapturePath $capturePath -OutputPath $rs485TxPath -LineRole "tx" -Channel $Rs485DeviceTxChannel -Baud 38400
  Export-UartBytes -CapturePath $capturePath -OutputPath $rs485RxPath -LineRole "rx" -Channel $Rs485DeviceRxChannel -Baud 38400
  if ($TmcTxChannel) {
    Export-UartBytes -CapturePath $capturePath -OutputPath $tmcTxPath -LineRole "tx" -Channel $TmcTxChannel -Baud 115200
  }
  if ($TmcRxChannel) {
    Export-UartBytes -CapturePath $capturePath -OutputPath $tmcRxPath -LineRole "rx" -Channel $TmcRxChannel -Baud 115200
  }

  Invoke-Tool -Exe $script:SigrokExe -ArgumentList @(
    "-i", $capturePath,
    "-O", "csv:header=true",
    "-o", $csvPath
  )
}

if (-not $SkipAnalyze) {
  $analyzer = Join-Path $scriptDir "analyze_la_capture.py"
  $enablePolarity = if ($EnableActiveHigh) { "--enable-active-high" } else { "--enable-active-low" }
  $analyzerArgs = @(
    $analyzer,
    "--expected-id", "$ExpectedId",
    "--rs485-device-rx-bin", $rs485RxPath,
    "--rs485-device-tx-bin", $rs485TxPath,
    "--csv", $csvPath,
    "--samplerate-hz", "$SampleRateHz",
    "--step-channel", $StepChannel,
    "--dir-channel", $DirChannel,
    "--enable-channel", $EnableChannel,
    $enablePolarity
  )
  if ($TmcRxChannel) {
    $analyzerArgs += @("--tmc-rx-bin", $tmcRxPath)
  }
  if ($TmcTxChannel) {
    $analyzerArgs += @("--tmc-tx-bin", $tmcTxPath)
  }
  Invoke-Tool -Exe $script:PythonExe -ArgumentList $analyzerArgs
}

Write-Host "LA artifacts: $runDir"
