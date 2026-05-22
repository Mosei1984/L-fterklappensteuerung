param(
  [string] $SourceDirectory = $PSScriptRoot,
  [string] $InstallDirectory = (Join-Path $env:LOCALAPPDATA 'Programs\LuefterConfigurator'),
  [switch] $AcceptEula,
  [switch] $CreateDesktopShortcut
)

$ErrorActionPreference = 'Stop'

if (-not $AcceptEula) {
  throw 'Installation abgebrochen: Bitte EULA.md lesen und mit -AcceptEula akzeptieren.'
}

$sourceRoot = (Resolve-Path $SourceDirectory).Path
$appExePath = Join-Path $sourceRoot 'Luefterklappen-Konfigurator.exe'
$hostExePath = Join-Path $sourceRoot 'LuefterConfigurator.Host.exe'
if (-not (Test-Path $appExePath)) {
  throw "Luefterklappen-Konfigurator.exe wurde in '$sourceRoot' nicht gefunden. Erst build-windows-installer.ps1 ausfuehren."
}

if (-not (Test-Path $hostExePath)) {
  throw "LuefterConfigurator.Host.exe wurde in '$sourceRoot' nicht gefunden. Erst build-windows-installer.ps1 ausfuehren."
}

$installRoot = [System.IO.Path]::GetFullPath($InstallDirectory)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs'))
if (-not $installRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "InstallDirectory muss unter '$allowedRoot' liegen."
}

New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
Copy-Item -Path (Join-Path $sourceRoot '*') -Destination $installRoot -Recurse -Force

$installedExe = Join-Path $installRoot 'Luefterklappen-Konfigurator.exe'
$installedHostExe = Join-Path $installRoot 'LuefterConfigurator.Host.exe'
$iconPath = Join-Path $installRoot 'Luefterklappen-Konfigurator.ico'
$uninstallScript = Join-Path $installRoot 'uninstall-windows.ps1'
$statusPath = Join-Path $installRoot 'INSTALL_STATUS.json'
$startMenuDirectory = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Luefterklappen Konfigurator'
$shortcutPath = Join-Path $startMenuDirectory 'Luefterklappen Konfigurator.lnk'
$uninstallShortcutPath = Join-Path $startMenuDirectory 'Luefterklappen Konfigurator deinstallieren.lnk'
$desktopShortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Luefterklappen Konfigurator.lnk'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\LuefterConfigurator'
$uninstallCommand = "powershell -ExecutionPolicy Bypass -File `"$uninstallScript`""
$quietUninstallCommand = "powershell -ExecutionPolicy Bypass -File `"$uninstallScript`" -Quiet -Force"

New-Item -ItemType Directory -Force -Path $startMenuDirectory | Out-Null
$shell = New-Object -ComObject WScript.Shell

$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $installedExe
$shortcut.WorkingDirectory = $installRoot
$shortcut.IconLocation = if (Test-Path $iconPath) { $iconPath } else { $installedExe }
$shortcut.Description = 'Luefterklappen Konfigurator starten'
$shortcut.Save()

$uninstallShortcut = $shell.CreateShortcut($uninstallShortcutPath)
$uninstallShortcut.TargetPath = 'powershell.exe'
$uninstallShortcut.Arguments = "-ExecutionPolicy Bypass -File `"$uninstallScript`""
$uninstallShortcut.WorkingDirectory = $installRoot
$uninstallShortcut.IconLocation = if (Test-Path $iconPath) { $iconPath } else { $installedExe }
$uninstallShortcut.Description = 'Luefterklappen Konfigurator deinstallieren'
$uninstallShortcut.Save()

if ($CreateDesktopShortcut) {
  $desktopShortcut = $shell.CreateShortcut($desktopShortcutPath)
  $desktopShortcut.TargetPath = $installedExe
  $desktopShortcut.WorkingDirectory = $installRoot
  $desktopShortcut.IconLocation = if (Test-Path $iconPath) { $iconPath } else { $installedExe }
  $desktopShortcut.Description = 'Luefterklappen Konfigurator starten'
  $desktopShortcut.Save()
}

$estimatedSizeKb = [int]([Math]::Ceiling(((Get-ChildItem -LiteralPath $installRoot -Recurse -File | Measure-Object -Property Length -Sum).Sum) / 1KB))

$status = [ordered]@{
  application = 'Luefterklappen Konfigurator'
  installedAt = (Get-Date).ToString('o')
  installDirectory = $installRoot
  sourceDirectory = $sourceRoot
  executable = $installedExe
  hostExecutable = $installedHostExe
  icon = $iconPath
  url = 'http://127.0.0.1:5184'
  startMenuShortcut = $shortcutPath
  uninstallShortcut = $uninstallShortcutPath
  desktopShortcut = if ($CreateDesktopShortcut) { $desktopShortcutPath } else { $null }
  uninstallCommand = $uninstallCommand
}

$status | ConvertTo-Json -Depth 4 | Set-Content -Path $statusPath -Encoding UTF8

New-Item -Path $uninstallKey -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name DisplayName -Value 'Luefterklappen Konfigurator' -PropertyType String -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name Publisher -Value 'Airhog-Fpv' -PropertyType String -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value '1.0.0' -PropertyType String -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $installRoot -PropertyType String -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name DisplayIcon -Value $(if (Test-Path $iconPath) { $iconPath } else { $installedExe }) -PropertyType String -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name UninstallString -Value $uninstallCommand -PropertyType String -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name QuietUninstallString -Value $quietUninstallCommand -PropertyType String -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name EstimatedSize -Value $estimatedSizeKb -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null

Write-Host "Installiert: $installRoot"
Write-Host "Start Menu Shortcut: $shortcutPath"
Write-Host "Status: $statusPath"
