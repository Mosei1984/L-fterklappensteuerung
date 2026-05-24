param(
  [string] $ArtifactRoot = "artifacts\tests\modbus-rtu-acceptance"
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$scriptPath = Join-Path $PSScriptRoot 'modbus_rtu_acceptance.ps1'
$artifactPath = Join-Path $repoRoot $ArtifactRoot

if (-not (Test-Path -LiteralPath $scriptPath)) {
  throw "Missing Modbus RTU acceptance script: $scriptPath"
}

$selfTestOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath `
  -SelfTest `
  -ArtifactRoot $ArtifactRoot 2>&1
if ($LASTEXITCODE -ne 0) {
  $selfTestOutput | ForEach-Object { Write-Host $_ }
  throw "Modbus RTU self-test failed with exit code $LASTEXITCODE"
}

$reportPath = Join-Path $artifactPath 'modbus-rtu-report.md'
if (-not (Test-Path -LiteralPath $reportPath)) {
  throw "Modbus RTU self-test did not write report: $reportPath"
}

$report = Get-Content -LiteralPath $reportPath -Raw
if ($report -notmatch '## Result\s+PASS') {
  throw 'Modbus RTU self-test report did not contain PASS result'
}

$printOnlyOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath `
  -PrintOnly `
  -RtuPort COM99 `
  -DeviceId 1 `
  -ArtifactRoot $ArtifactRoot 2>&1
if ($LASTEXITCODE -ne 0) {
  $printOnlyOutput | ForEach-Object { Write-Host $_ }
  throw "Modbus RTU print-only mode failed with exit code $LASTEXITCODE"
}

$printOnlyText = [string]::Join("`n", [string[]] $printOnlyOutput)
foreach ($expected in @('Read holding 0..36', 'Read diagnostics 17..22', 'Broadcast safe-position write', 'Write and restore Auto-Home interval register 36')) {
  if ($printOnlyText -notmatch [regex]::Escape($expected)) {
    throw "Print-only output missed expected step: $expected"
  }
}

Write-Host 'Modbus RTU acceptance script self-test passed.'
