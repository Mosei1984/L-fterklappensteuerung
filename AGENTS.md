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
- Windows desktop shell: `tools/configurator/src/LuefterConfigurator.Desktop`
- Diagrams and documentation: `README.md`, `docs/diagrams/*.mmd`,
  `tools/configurator/README.md`, `tools/la/README.md`
- Agent guardrails: `tools/agent-hooks/luefter_repo_hook.ps1`

## Fresh clone bootstrap

Recommended Windows setup for a new PC or a new agent session:

```powershell
git clone https://github.com/Mosei1984/L-fterklappensteuerung.git
cd .\L-fterklappensteuerung

$pythonScripts = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\Scripts'
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$pythonScripts;$env:PATH"
$env:PLATFORMIO_CORE_DIR = 'C:\pio-luefter'
New-Item -ItemType Directory -Force -Path .\.dotnet-cli-home, .\.nuget | Out-Null
$env:DOTNET_CLI_HOME = (Resolve-Path .\.dotnet-cli-home).Path
$env:NUGET_PACKAGES = (Resolve-Path .\.nuget).Path
python -m pip install --upgrade pip platformio
npm install
dotnet restore .\tools\configurator\LuefterConfigurator.sln --ignore-failed-sources -p:NuGetAudit=false
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_checks.ps1
```

Use Visual Studio or VS Code with `tools/configurator/LuefterConfigurator.sln`
as the default solution. If no global .NET SDK is installed, place a local SDK
under `%USERPROFILE%\.dotnet8` or adjust `DOTNET_ROOT` before running the
configurator commands below.

## Quality gate

Run the fast project gate during normal development:

Command path for scripts and tests: `tools/run_quality_checks.ps1`.

```powershell
$pythonScripts = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\Scripts'
$env:PATH="C:\msys64\mingw64\bin;C:\msys64\usr\bin;$pythonScripts;$env:PATH"
$env:PLATFORMIO_CORE_DIR='C:\pio-luefter'
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_checks.ps1
```

Expected current coverage:

- Firmware native tests: 95 passing tests.
- Pico firmware build: `pico` environment succeeds.
- PlatformIO clang-tidy/cppcheck: native and pico pass.
- Standalone cppcheck/MISRA addon path runs.
- Configurator tests: 77 passing tests.

Run the hard all-functions gate before push, release, installer changes or
completion claims:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_hard_checks.ps1
```

Hard-gate artifacts are written under
`artifacts/quality/hard-all-functions`. The gate captures environment logs,
VS Code C#/Razor/LSP log snapshots, Test Explorer settings validation, MSBuild
binlogs, Razor compile logs, TRX test output, Cobertura coverage, Markdown lint,
repo/subagent hook smoke tests and the full firmware/configurator gate.

Documentation changes must keep `README.md` Mermaid blocks and
`docs/diagrams/architecture.mmd` / `docs/diagrams/wiring.mmd` in sync. Render
with `npm run diagrams` when diagram sources change, then run
`npm run markdownlint`.

## Configurator commands

Run tests only:

```powershell
New-Item -ItemType Directory -Force -Path .\.dotnet-cli-home | Out-Null
$env:DOTNET_CLI_HOME=(Resolve-Path .\.dotnet-cli-home).Path
dotnet test .\tools\configurator\LuefterConfigurator.sln --no-restore
```

Start local UI:

```powershell
New-Item -ItemType Directory -Force -Path .\.dotnet-cli-home | Out-Null
$env:DOTNET_CLI_HOME=(Resolve-Path .\.dotnet-cli-home).Path
dotnet run --project .\tools\configurator\src\LuefterConfigurator.Host
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
- Position commands and limits can use steps, `0..1000` permille or `0..90`
  degrees; `0` degrees is open/horizontal and `90` degrees is closed/vertical.
- Persistent device ID, safe position, StallGuard threshold, homing
  configuration and motor parameters use a two-sector flash journal with
  generation and CRC validation.
- Diagnostic Modbus registers `17..22` are read-only and append the stable
  fault reason, fault count, settings status, TMC health, boot reason and
  firmware protocol version `5`; degree/StallGuard/Homing/Motor configuration
  registers are `23..35`.
- Service text diagnostics are `DIAG?`, `FAULT?` and non-moving `SELFTEST?`.
- `REFRESH` / Modbus command `5` is the preferred recovery after a controller
  fault: it stops, clears the fault path, and rehomes without rebooting the MCU.
  Keep `RESET` for compatibility only.

Run firmware release acceptance before a release handoff:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\firmware_release_check.ps1 -NoHardware
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\firmware_release_check.ps1 -SerialPort COMx -ExpectedDeviceId 1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\firmware_release_check.ps1 -SerialPort COMx -ExpectedDeviceId 1 -RequireLogicAnalyzer
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\modbus_rtu_acceptance.ps1 -RtuPort COMx -UsbPort COM10 -DeviceId 1
```

Inspect `artifacts/release/firmware/acceptance-report.md` and
`artifacts/quality/hard-all-functions/summary.md` before claiming release
readiness. Inspect `artifacts/release/modbus-rtu/modbus-rtu-report.md` after
dedicated RTU runs. Full field acceptance still requires assembled motor,
TMC2209, endstops, RS485, a USB-RS485 RTU master adapter and logic analyzer.

## Git and generated files

- Do not commit `.pio`, `.pio-home`, `.dotnet-sdk`, `.dotnet-cli-home`,
  `node_modules`, `artifacts`, `bin`, `obj`, or `App_Data`.
- Do not commit AI-assistant co-author trailers or generated-by metadata.
  Commits should use `Airhog-Fpv <116745046+Mosei1984@users.noreply.github.com>`.
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
4. Write ID, safe position, degree limits, StallGuard threshold and motor
   speed/current limits.
5. Generate and download Loxone XML/JSON/Markdown.
6. Run final status and gateway checks.

USB host testing and raw protocol diagnostics are intentionally separated as
expert tools.
