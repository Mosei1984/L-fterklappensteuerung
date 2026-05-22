# Agent Quickstart

This repository contains a Raspberry Pi Pico controller firmware for an 80 mm
ventilation pipe valve and a local Windows/C# configurator. Use this file first
when joining the project.

## Scope

- Firmware core: `lib/Luefterklappe/src`
- Pico/Arduino HAL: `src/main.cpp`
- Native firmware tests: `test/test_controller/test_main.cpp`
- Logic-analyzer workflow: `tools/la`
- Configurator solution: `tools/configurator/LuefterConfigurator.sln`
- Configurator host/UI: `tools/configurator/src/LuefterConfigurator.Host`
- Home automation adapters: `tools/configurator/src/LuefterConfigurator.Adapters.*`

## Quality gate

Run the full gate before claiming completion:

Command path for scripts and tests: `tools/run_quality_checks.ps1`.

```powershell
$env:PATH='C:\msys64\mingw64\bin;C:\msys64\usr\bin;C:\Users\mosei\AppData\Local\Programs\Python\Python312\Scripts;' + $env:PATH
$env:PLATFORMIO_CORE_DIR='C:\pio-luefter'
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_checks.ps1
```

Expected current coverage:

- Firmware native tests: 42 passing tests.
- Pico firmware build: `pico` environment succeeds.
- PlatformIO clang-tidy/cppcheck: native and pico pass.
- Standalone cppcheck/MISRA addon path runs.
- Configurator tests: 59 passing tests.

## Configurator commands

Run tests only:

```powershell
$env:DOTNET_CLI_HOME=(Resolve-Path .\.dotnet-cli-home).Path
& 'C:\Users\mosei\.dotnet8\dotnet.exe' test .\tools\configurator\LuefterConfigurator.sln --no-restore
```

Start local UI:

```powershell
$env:DOTNET_ROOT='C:\Users\mosei\.dotnet8'
$env:DOTNET_CLI_HOME=(Resolve-Path .\.dotnet-cli-home).Path
& 'C:\Users\mosei\.dotnet8\dotnet.exe' run --project .\tools\configurator\src\LuefterConfigurator.Host
```

Open: <http://127.0.0.1:5184>

Build portable/installable Windows package:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\configurator\build-windows-installer.ps1
```

Output ZIP:

```text
artifacts/configurator-installer/win-x64/Luefterklappen-Konfigurator-win-x64.zip
```

## Firmware notes

- The core is MCU-independent. Keep Arduino/RP2040-specific code in
  `src/main.cpp` or HAL adapters only.
- Endstops are the primary homing reference. StallGuard is fallback during
  homing and overload/blockage detection during normal movement.
- Modbus broadcast ID `0` must not execute writes for this home-use controller.
- Safe position is configurable in `0..1000` permille and persisted with CRC.
- `REFRESH` / Modbus command `5` is the preferred recovery after a controller
  fault: it stops, clears the fault path, and rehomes without rebooting the MCU.
  Keep `RESET` for compatibility only.

## Git and generated files

- Do not commit `.pio`, `.pio-home`, `.dotnet-sdk`, `.dotnet-cli-home`,
  `node_modules`, `artifacts`, `bin`, `obj`, or `App_Data`.
- Generated configurator assets that belong in source live under
  `tools/configurator/src/LuefterConfigurator.Host/wwwroot/brand` and
  `tools/configurator/src/LuefterConfigurator.Host/Assets/logo`.
- Generated Loxone/export files under `App_Data/exports` are runtime output and
  ignored.

## Current user-facing direction

The target device is an 80 mm pipe valve for residential ventilation. The
configurator should guide normal users through Loxone setup first:

1. Prepare Loxone/Modbus TCP settings.
2. Detect Pico over USB.
3. Flash UF2 if needed.
4. Write ID, safe position and limits.
5. Generate and download Loxone XML/JSON/Markdown.
6. Run final status and gateway checks.

USB host testing and raw protocol diagnostics are intentionally separated as
expert tools.
