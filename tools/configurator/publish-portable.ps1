param(
  [string] $Runtime = 'win-x64',
  [string] $OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$project = Join-Path $PSScriptRoot 'src\LuefterConfigurator.Host\LuefterConfigurator.Host.csproj'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repoRoot "artifacts\configurator-portable\$Runtime"
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

& $dotnet publish $project `
  -c Release `
  -r $Runtime `
  --self-contained true `
  -p:PublishSingleFile=true `
  -p:EnableCompressionInSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true `
  -o $OutputDirectory

if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

Write-Host "Portable build: $OutputDirectory"
Write-Host "Start: .\LuefterConfigurator.Host.exe"
Write-Host "Open: http://127.0.0.1:5184"
