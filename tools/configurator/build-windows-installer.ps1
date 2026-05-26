param(
  [string] $Runtime = 'win-x64',
  [string] $OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repoRoot "artifacts\configurator-installer\$Runtime"
}

$portableOutput = Join-Path $OutputDirectory 'payload'
$installerZip = Join-Path $OutputDirectory "Luefterklappen-Konfigurator-$Runtime.zip"
$publishScript = Join-Path $PSScriptRoot 'publish-portable.ps1'
$installScript = Join-Path $PSScriptRoot 'install-windows.ps1'
$setupScript = Join-Path $PSScriptRoot 'setup-windows.ps1'
$setupLauncher = Join-Path $PSScriptRoot 'Luefterklappen-Konfigurator-Setup.cmd'
$uninstallScript = Join-Path $PSScriptRoot 'uninstall-windows.ps1'
$eula = Join-Path $PSScriptRoot 'EULA.md'
$license = Join-Path $repoRoot 'LICENSE'
$thirdPartyNotices = Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md'
$icon = Join-Path $PSScriptRoot 'src\LuefterConfigurator.Host\Assets\logo\luefterklappen.ico'

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

& $publishScript -Runtime $Runtime -OutputDirectory $portableOutput
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$desktopAppExe = Join-Path $portableOutput 'Luefterklappen-Konfigurator.exe'
if (-not (Test-Path $desktopAppExe)) {
  throw "Desktop-App wurde nicht erzeugt: $desktopAppExe"
}

Copy-Item -LiteralPath $installScript -Destination (Join-Path $portableOutput 'install-windows.ps1') -Force
Copy-Item -LiteralPath $setupScript -Destination (Join-Path $portableOutput 'setup-windows.ps1') -Force
Copy-Item -LiteralPath $setupLauncher -Destination (Join-Path $portableOutput 'Luefterklappen-Konfigurator-Setup.cmd') -Force
Copy-Item -LiteralPath $uninstallScript -Destination (Join-Path $portableOutput 'uninstall-windows.ps1') -Force
Copy-Item -LiteralPath $eula -Destination (Join-Path $portableOutput 'EULA.md') -Force
Copy-Item -LiteralPath $license -Destination (Join-Path $portableOutput 'LICENSE') -Force
Copy-Item -LiteralPath $thirdPartyNotices -Destination (Join-Path $portableOutput 'THIRD_PARTY_NOTICES.md') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $portableOutput 'README.md') -Force
Copy-Item -LiteralPath $icon -Destination (Join-Path $portableOutput 'Luefterklappen-Konfigurator.ico') -Force

$installReadme = @"
Luefterklappen Konfigurator Windows Setup

1. ZIP entpacken.
2. Luefterklappen-Konfigurator-Setup.cmd doppelklicken.
3. Im Setup-Wizard EULA bestaetigen, Zielordner waehlen und Installieren.
   LICENSE und THIRD_PARTY_NOTICES.md bleiben im Installationsordner.
4. Starten:
   Start Menu > Luefterklappen Konfigurator
5. Deinstallieren:
   Windows Apps & Features oder Start Menu > Luefterklappen Konfigurator deinstallieren

Silent Install fuer Experten:
   powershell -ExecutionPolicy Bypass -File .\install-windows.ps1 -AcceptEula

Installationsstatus: INSTALL_STATUS.json
"@

Set-Content -Path (Join-Path $portableOutput 'INSTALL_README.txt') -Value $installReadme -Encoding UTF8

if (Test-Path $installerZip) {
  Remove-Item -LiteralPath $installerZip -Force
}

Compress-Archive -Path (Join-Path $portableOutput '*') -DestinationPath $installerZip -Force

Write-Host "Installer bundle: $installerZip"
Write-Host "Payload: $portableOutput"
Write-Host "Install: .\Luefterklappen-Konfigurator-Setup.cmd"
