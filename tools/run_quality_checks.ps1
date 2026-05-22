$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($env:PLATFORMIO_CORE_DIR)) {
  $env:PLATFORMIO_CORE_DIR = Join-Path $env:USERPROFILE '.pio-lf'
}

$mingwBin = 'C:\msys64\mingw64\bin'
if ((Test-Path $mingwBin) -and (($env:PATH -split ';') -notcontains $mingwBin)) {
  $env:PATH = "$mingwBin;$env:PATH"
}

function Invoke-QualityStep {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Name,
    [Parameter(Mandatory = $true)]
    [scriptblock] $Step
  )

  Write-Host ""
  Write-Host "== $Name =="
  & $Step
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

Invoke-QualityStep "Native unit tests" {
  platformio test -e native
}

Invoke-QualityStep "Pico firmware build" {
  platformio run -e pico
}

Invoke-QualityStep "PlatformIO static analysis, native" {
  platformio check -e native --skip-packages
}

Invoke-QualityStep "PlatformIO static analysis, pico" {
  platformio check -e pico --skip-packages
}

$cppcheck = Get-Command cppcheck -ErrorAction SilentlyContinue
$platformioCppcheckPath = Join-Path $env:PLATFORMIO_CORE_DIR 'packages\tool-cppcheck\cppcheck.exe'
$cppcheckPath = if (Test-Path $platformioCppcheckPath) { $platformioCppcheckPath } elseif ($null -ne $cppcheck) { $cppcheck.Source } else { $platformioCppcheckPath }
$misraAddon = Join-Path $env:PLATFORMIO_CORE_DIR 'packages\tool-cppcheck\addons\misra.py'
if ((Test-Path $cppcheckPath) -and (Test-Path $misraAddon)) {
  Invoke-QualityStep "Standalone cppcheck with MISRA addon when available" {
    $savedPath = $env:PATH
    try {
      $env:PATH = (($env:PATH -split ';') | Where-Object { $_ -notlike 'C:\msys64*' }) -join ';'
      & $cppcheckPath `
        --enable=all `
        --platform=win64 `
        --std=c++14 `
        --language=c++ `
        --inline-suppr `
        --suppress=unusedFunction `
        --suppressions-list=.cppcheck-suppressions `
        "--addon=$misraAddon" `
        -Ilib/Luefterklappe/src `
        lib/Luefterklappe/src
    } finally {
      $env:PATH = $savedPath
    }
  }
} else {
  Write-Warning "cppcheck MISRA addon was not found; PlatformIO check remains configured."
}

$configuratorSolution = '.\tools\configurator\LuefterConfigurator.sln'
if (Test-Path $configuratorSolution) {
  Invoke-QualityStep "C# configurator tests" {
    $localDotnet = Join-Path $env:USERPROFILE '.dotnet8\dotnet.exe'
    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    $dotnetPath = if (Test-Path $localDotnet) { $localDotnet } elseif ($null -ne $dotnet) { $dotnet.Source } else { $localDotnet }
    if (Test-Path (Split-Path $dotnetPath -Parent)) {
      $env:DOTNET_ROOT = Split-Path $dotnetPath -Parent
    }
    if ([string]::IsNullOrWhiteSpace($env:DOTNET_CLI_HOME)) {
      $env:DOTNET_CLI_HOME = (Resolve-Path .).Path
    }
    & $dotnetPath test $configuratorSolution --no-restore
  }
}
