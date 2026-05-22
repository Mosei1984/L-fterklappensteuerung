$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($env:PLATFORMIO_CORE_DIR)) {
  $env:PLATFORMIO_CORE_DIR = Join-Path $env:USERPROFILE '.platformio-luefter'
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
