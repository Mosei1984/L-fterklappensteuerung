param(
  [string] $InstallDirectory = (Join-Path $env:LOCALAPPDATA 'Programs\LuefterConfigurator'),
  [switch] $KeepData,
  [switch] $Quiet,
  [switch] $Force
)

$ErrorActionPreference = 'Stop'

$installRoot = [System.IO.Path]::GetFullPath($InstallDirectory)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs'))
if (-not $installRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "InstallDirectory muss unter '$allowedRoot' liegen."
}

$statusPath = Join-Path $installRoot 'INSTALL_STATUS.json'
$desktopShortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Luefterklappen Konfigurator.lnk'
if (Test-Path $statusPath) {
  $status = Get-Content -Path $statusPath -Raw | ConvertFrom-Json
  if ($status.installDirectory) {
    $recordedRoot = [System.IO.Path]::GetFullPath([string]$status.installDirectory)
    if ($recordedRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
      $installRoot = $recordedRoot
    }
  }
  if ($status.desktopShortcut) {
    $desktopShortcut = [string]$status.desktopShortcut
  }
}

if (-not $Quiet -and -not $Force) {
  Add-Type -AssemblyName System.Windows.Forms
  $answer = [System.Windows.Forms.MessageBox]::Show(
    'Luefterklappen Konfigurator wirklich deinstallieren?',
    'Deinstallation bestaetigen',
    [System.Windows.Forms.MessageBoxButtons]::YesNo,
    [System.Windows.Forms.MessageBoxIcon]::Question)
  if ($answer -ne [System.Windows.Forms.DialogResult]::Yes) {
    Write-Host 'Deinstallation abgebrochen.'
    exit 0
  }
}

$startMenuDirectory = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Luefterklappen Konfigurator'
if (Test-Path $startMenuDirectory) {
  Remove-Item -LiteralPath $startMenuDirectory -Recurse -Force
}

if (Test-Path $desktopShortcut) {
  Remove-Item -LiteralPath $desktopShortcut -Force
}

$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\LuefterConfigurator'
if (Test-Path $uninstallKey) {
  Remove-Item -LiteralPath $uninstallKey -Recurse -Force
}

if (Test-Path $installRoot) {
  if ($KeepData) {
    Get-ChildItem -LiteralPath $installRoot -Force |
      Where-Object { $_.Name -ne 'App_Data' } |
      Remove-Item -Recurse -Force
  } else {
    Remove-Item -LiteralPath $installRoot -Recurse -Force
  }
}

if (-not $Quiet) {
  Write-Host "Deinstalliert: $installRoot"
}
