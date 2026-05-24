param(
  [string] $Runtime = 'win-x64',
  [string] $OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$hostProject = Join-Path $PSScriptRoot 'src\LuefterConfigurator.Host\LuefterConfigurator.Host.csproj'
$desktopProject = Join-Path $PSScriptRoot 'src\LuefterConfigurator.Desktop\LuefterConfigurator.Desktop.csproj'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repoRoot "artifacts\configurator-portable\$Runtime"
}

$repoRootPath = [System.IO.Path]::GetFullPath($repoRoot)
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
if (-not $outputPath.StartsWith($repoRootPath + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Refusing to clean unsafe publish directory outside repository: $outputPath"
}

if (Test-Path $outputPath) {
  Remove-Item -LiteralPath $outputPath -Recurse -Force
}

$dotnet = Join-Path $env:USERPROFILE '.dotnet8\dotnet.exe'
if (-not (Test-Path $dotnet)) {
  $dotnetCommand = Get-Command dotnet -ErrorAction Stop
  $dotnet = $dotnetCommand.Source
}

if ([string]::IsNullOrWhiteSpace($env:DOTNET_CLI_HOME)) {
  $env:DOTNET_CLI_HOME = $repoRoot
}

if ([string]::IsNullOrWhiteSpace($env:NUGET_PACKAGES)) {
  $env:NUGET_PACKAGES = Join-Path $env:LOCALAPPDATA 'LuefterConfigurator\nuget'
}

& $dotnet publish $hostProject `
  -c Release `
  -r $Runtime `
  --ignore-failed-sources `
  --self-contained true `
  -p:PublishSingleFile=true `
  -p:EnableCompressionInSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true `
  -p:NuGetAudit=false `
  -o $OutputDirectory

if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

& $dotnet publish $desktopProject `
  -c Release `
  -r $Runtime `
  --ignore-failed-sources `
  --self-contained true `
  -p:PublishSingleFile=true `
  -p:EnableCompressionInSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true `
  -p:NuGetAudit=false `
  -o $OutputDirectory

if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$hostExe = Join-Path $OutputDirectory 'LuefterConfigurator.Host.exe'
$desktopExe = Join-Path $OutputDirectory 'Luefterklappen-Konfigurator.exe'
if (-not (Test-Path $hostExe)) {
  throw "Interner Host wurde nicht erzeugt: $hostExe"
}

if (-not (Test-Path $desktopExe)) {
  throw "Windows-App wurde nicht erzeugt: $desktopExe"
}

Write-Host "Portable Windows-App: $OutputDirectory"
Write-Host "Start: .\Luefterklappen-Konfigurator.exe"
Write-Host "Expert host: .\LuefterConfigurator.Host.exe"
