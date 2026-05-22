# Lueftersteuerung Konfigurator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a local C# configurator for the 80 mm pipe-valve fan flap controller with browser UI, USB/RS485 communication, Modbus TCP gateway, UF2 flashing, multi-controller support, JSON profile import, and home automation exports.

**Architecture:** Create a .NET solution under `tools/configurator` with focused projects for domain contracts, application services, infrastructure, profile modules, adapter modules, the ASP.NET Core host, and tests. Keep the MCU firmware untouched unless a test proves a protocol gap.

**Tech Stack:** .NET 8 SDK, ASP.NET Core Razor Pages, xUnit, `System.IO.Ports`, `System.Text.Json`, TCP sockets from `System.Net.Sockets`, optional `MQTTnet` for MQTT live-test.

---

## Current status

Implementation now includes the Loxone Setup Wizard, expert USB host-test
column, UF2 flash workflow, export downloads, Windows installer scripts, EULA,
visible app icon assets and agent onboarding documentation.
Firmware, Modbus map and configurator now include `REFRESH` /
`refresh-machine` / Modbus command `5` as the preferred post-fault recovery:
it rehomes the 80 mm pipe-valve controller without requiring a Pico reset.

## Preflight

The current workspace root is not a Git repository. Execute implementation either in `artifacts/fresh-upload-20260522-184955` or in a fresh clone of `https://github.com/Mosei1984/L-fterklappensteuerung`. The current machine has .NET runtimes but no .NET SDK, so Task 1 installs or verifies the SDK before any solution work.

## File Structure

Create this structure:

```text
tools/configurator/
  LuefterConfigurator.sln
  Directory.Build.props
  src/
    LuefterConfigurator.Domain/
    LuefterConfigurator.Application/
    LuefterConfigurator.Infrastructure.Serial/
    LuefterConfigurator.Infrastructure.ModbusTcp/
    LuefterConfigurator.Infrastructure.Firmware/
    LuefterConfigurator.Infrastructure.Mqtt/
    LuefterConfigurator.Adapters.Loxone/
    LuefterConfigurator.Adapters.HomeAssistant/
    LuefterConfigurator.Adapters.OpenHab/
    LuefterConfigurator.Adapters.Modbus/
    LuefterConfigurator.Profiles.FanFlap/
    LuefterConfigurator.Profiles.Json/
    LuefterConfigurator.Host/
  tests/
    LuefterConfigurator.Tests/
```

Responsibilities:

- `Domain`: immutable records, validation results, register/setting definitions, transport/protocol/adapter interfaces.
- `Application`: orchestration services for discovery, sessions, settings, diagnostics, exports, gateway control, firmware update, profile import.
- `Infrastructure.Serial`: USB CDC and Modbus RTU serial adapters behind testable interfaces.
- `Infrastructure.ModbusTcp`: local Modbus TCP server that maps TCP Unit-IDs to controller sessions.
- `Infrastructure.Firmware`: Pico UF2 drive detection and UF2 copy workflow.
- `Infrastructure.Mqtt`: MQTT live-test adapter.
- `Adapters.*`: export adapters with no UI dependency.
- `Profiles.FanFlap`: built-in fan flap profile, text protocol parser, Modbus map.
- `Profiles.Json`: strict data-only profile importer and schema file.
- `Host`: ASP.NET Core/Blazor UI and local API.
- `Tests`: xUnit tests, fake transports, fake controller sessions, export snapshots.

## Task 1: Toolchain And Solution Scaffold

**Files:**
- Create: `tools/configurator/LuefterConfigurator.sln`
- Create: `tools/configurator/Directory.Build.props`
- Create: all project files under `tools/configurator/src/**`
- Create: `tools/configurator/tests/LuefterConfigurator.Tests/LuefterConfigurator.Tests.csproj`

- [ ] **Step 1: Verify SDK**

Run:

```powershell
dotnet --list-sdks
```

Expected before install on this machine:

```text
No SDKs were found.
```

Install .NET 8 SDK from Microsoft, then run again.

Expected after install:

```text
8.0.x [C:\Program Files\dotnet\sdk]
```

- [ ] **Step 2: Create solution and projects**

Run from repository root:

```powershell
New-Item -ItemType Directory -Force tools\configurator
dotnet new sln -n LuefterConfigurator -o tools\configurator
dotnet new classlib -n LuefterConfigurator.Domain -o tools\configurator\src\LuefterConfigurator.Domain -f net8.0
dotnet new classlib -n LuefterConfigurator.Application -o tools\configurator\src\LuefterConfigurator.Application -f net8.0
dotnet new classlib -n LuefterConfigurator.Infrastructure.Serial -o tools\configurator\src\LuefterConfigurator.Infrastructure.Serial -f net8.0
dotnet new classlib -n LuefterConfigurator.Infrastructure.ModbusTcp -o tools\configurator\src\LuefterConfigurator.Infrastructure.ModbusTcp -f net8.0
dotnet new classlib -n LuefterConfigurator.Infrastructure.Firmware -o tools\configurator\src\LuefterConfigurator.Infrastructure.Firmware -f net8.0
dotnet new classlib -n LuefterConfigurator.Infrastructure.Mqtt -o tools\configurator\src\LuefterConfigurator.Infrastructure.Mqtt -f net8.0
dotnet new classlib -n LuefterConfigurator.Adapters.Loxone -o tools\configurator\src\LuefterConfigurator.Adapters.Loxone -f net8.0
dotnet new classlib -n LuefterConfigurator.Adapters.HomeAssistant -o tools\configurator\src\LuefterConfigurator.Adapters.HomeAssistant -f net8.0
dotnet new classlib -n LuefterConfigurator.Adapters.OpenHab -o tools\configurator\src\LuefterConfigurator.Adapters.OpenHab -f net8.0
dotnet new classlib -n LuefterConfigurator.Adapters.Modbus -o tools\configurator\src\LuefterConfigurator.Adapters.Modbus -f net8.0
dotnet new classlib -n LuefterConfigurator.Profiles.FanFlap -o tools\configurator\src\LuefterConfigurator.Profiles.FanFlap -f net8.0
dotnet new classlib -n LuefterConfigurator.Profiles.Json -o tools\configurator\src\LuefterConfigurator.Profiles.Json -f net8.0
dotnet new webapp -n LuefterConfigurator.Host -o tools\configurator\src\LuefterConfigurator.Host -f net8.0
dotnet new xunit -n LuefterConfigurator.Tests -o tools\configurator\tests\LuefterConfigurator.Tests -f net8.0
```

Expected: each command ends with `Restore succeeded`.

- [ ] **Step 3: Add projects to solution**

Run:

```powershell
dotnet sln tools\configurator\LuefterConfigurator.sln add (Get-ChildItem tools\configurator\src -Recurse -Filter *.csproj).FullName
dotnet sln tools\configurator\LuefterConfigurator.sln add tools\configurator\tests\LuefterConfigurator.Tests\LuefterConfigurator.Tests.csproj
```

Expected:

```text
Project ... added to the solution.
```

- [ ] **Step 4: Add project references**

Run:

```powershell
dotnet add tools\configurator\src\LuefterConfigurator.Application reference tools\configurator\src\LuefterConfigurator.Domain
dotnet add tools\configurator\src\LuefterConfigurator.Infrastructure.Serial reference tools\configurator\src\LuefterConfigurator.Domain
dotnet add tools\configurator\src\LuefterConfigurator.Infrastructure.ModbusTcp reference tools\configurator\src\LuefterConfigurator.Domain tools\configurator\src\LuefterConfigurator.Application
dotnet add tools\configurator\src\LuefterConfigurator.Infrastructure.Firmware reference tools\configurator\src\LuefterConfigurator.Domain
dotnet add tools\configurator\src\LuefterConfigurator.Infrastructure.Mqtt reference tools\configurator\src\LuefterConfigurator.Domain
dotnet add tools\configurator\src\LuefterConfigurator.Adapters.Loxone reference tools\configurator\src\LuefterConfigurator.Domain
dotnet add tools\configurator\src\LuefterConfigurator.Adapters.HomeAssistant reference tools\configurator\src\LuefterConfigurator.Domain
dotnet add tools\configurator\src\LuefterConfigurator.Adapters.OpenHab reference tools\configurator\src\LuefterConfigurator.Domain
dotnet add tools\configurator\src\LuefterConfigurator.Adapters.Modbus reference tools\configurator\src\LuefterConfigurator.Domain
dotnet add tools\configurator\src\LuefterConfigurator.Profiles.FanFlap reference tools\configurator\src\LuefterConfigurator.Domain
dotnet add tools\configurator\src\LuefterConfigurator.Profiles.Json reference tools\configurator\src\LuefterConfigurator.Domain
dotnet add tools\configurator\src\LuefterConfigurator.Host reference tools\configurator\src\LuefterConfigurator.Domain tools\configurator\src\LuefterConfigurator.Application tools\configurator\src\LuefterConfigurator.Infrastructure.Serial tools\configurator\src\LuefterConfigurator.Infrastructure.ModbusTcp tools\configurator\src\LuefterConfigurator.Infrastructure.Firmware tools\configurator\src\LuefterConfigurator.Infrastructure.Mqtt tools\configurator\src\LuefterConfigurator.Adapters.Loxone tools\configurator\src\LuefterConfigurator.Adapters.HomeAssistant tools\configurator\src\LuefterConfigurator.Adapters.OpenHab tools\configurator\src\LuefterConfigurator.Adapters.Modbus tools\configurator\src\LuefterConfigurator.Profiles.FanFlap tools\configurator\src\LuefterConfigurator.Profiles.Json
dotnet add tools\configurator\tests\LuefterConfigurator.Tests reference tools\configurator\src\LuefterConfigurator.Domain tools\configurator\src\LuefterConfigurator.Application tools\configurator\src\LuefterConfigurator.Infrastructure.ModbusTcp tools\configurator\src\LuefterConfigurator.Infrastructure.Firmware tools\configurator\src\LuefterConfigurator.Adapters.Loxone tools\configurator\src\LuefterConfigurator.Adapters.HomeAssistant tools\configurator\src\LuefterConfigurator.Adapters.OpenHab tools\configurator\src\LuefterConfigurator.Adapters.Modbus tools\configurator\src\LuefterConfigurator.Profiles.FanFlap tools\configurator\src\LuefterConfigurator.Profiles.Json
```

- [ ] **Step 5: Add required packages**

Run:

```powershell
dotnet add tools\configurator\src\LuefterConfigurator.Infrastructure.Serial package System.IO.Ports
dotnet add tools\configurator\src\LuefterConfigurator.Infrastructure.Mqtt package MQTTnet
dotnet add tools\configurator\tests\LuefterConfigurator.Tests package FluentAssertions
```

Expected: each package is restored without warnings.

- [ ] **Step 6: Add shared build settings**

Replace `tools/configurator/Directory.Build.props` with:

```xml
<Project>
  <PropertyGroup>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
    <AnalysisLevel>latest-recommended</AnalysisLevel>
    <LangVersion>latest</LangVersion>
  </PropertyGroup>
</Project>
```

- [ ] **Step 7: Build**

Run:

```powershell
dotnet build tools\configurator\LuefterConfigurator.sln
```

Expected:

```text
Build succeeded.
    0 Warning(s)
    0 Error(s)
```

- [ ] **Step 8: Commit**

```powershell
git add tools/configurator
git commit -m "Add configurator solution scaffold"
```

## Task 2: Domain Model And Validation

**Files:**
- Create: `tools/configurator/src/LuefterConfigurator.Domain/ControllerProfile.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Domain/ControllerSession.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Domain/RegisterDefinition.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Domain/SettingDefinition.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Domain/ValidationResult.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Domain/Ports.cs`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/Domain/ProfileValidationTests.cs`

- [ ] **Step 1: Write failing validation tests**

Create `ProfileValidationTests.cs`:

```csharp
using FluentAssertions;
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Tests.Domain;

public sealed class ProfileValidationTests
{
    [Fact]
    public void DeviceIdSettingAcceptsOnlyModbusSlaveRange()
    {
        var setting = SettingDefinition.Integer("device.id", "Device ID", 1, 247, "Modbus slave id");

        setting.Validate(1).IsValid.Should().BeTrue();
        setting.Validate(247).IsValid.Should().BeTrue();
        setting.Validate(0).IsValid.Should().BeFalse();
        setting.Validate(248).IsValid.Should().BeFalse();
    }

    [Fact]
    public void SafePositionAcceptsOnlyPromilleRange()
    {
        var setting = SettingDefinition.Integer("safe.position", "Safe Position", 0, 1000, "promille");

        setting.Validate(0).IsValid.Should().BeTrue();
        setting.Validate(1000).IsValid.Should().BeTrue();
        setting.Validate(-1).IsValid.Should().BeFalse();
        setting.Validate(1001).IsValid.Should().BeFalse();
    }

    [Fact]
    public void ProfileRejectsDuplicateRegisters()
    {
        var profile = new ControllerProfile(
            "fanflap",
            "Fan Flap",
            "1.0",
            [TransportKind.UsbText],
            [RegisterDefinition.Holding(0, "state", RegisterValueType.UInt16, RegisterAccess.ReadOnly),
             RegisterDefinition.Holding(0, "state_copy", RegisterValueType.UInt16, RegisterAccess.ReadOnly)],
            []);

        profile.Validate().Errors.Should().Contain(e => e.Contains("Duplicate register address 0"));
    }
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter ProfileValidationTests
```

Expected: FAIL because `ControllerProfile`, `SettingDefinition`, and `RegisterDefinition` do not exist.

- [ ] **Step 3: Implement domain records**

Create the domain files with these public APIs:

```csharp
namespace LuefterConfigurator.Domain;

public enum TransportKind { UsbText, ModbusRtu, ModbusTcpGateway }
public enum RegisterKind { Coil, DiscreteInput, Holding, Input }
public enum RegisterValueType { UInt16, Int16, UInt32, Int32, Boolean }
public enum RegisterAccess { ReadOnly, WriteOnly, ReadWrite }

public sealed record ValidationResult(bool IsValid, IReadOnlyList<string> Errors)
{
    public static ValidationResult Ok() => new(true, Array.Empty<string>());
    public static ValidationResult Fail(params string[] errors) => new(false, errors);
}

public sealed record SettingDefinition(
    string Key,
    string DisplayName,
    int Minimum,
    int Maximum,
    string Unit)
{
    public static SettingDefinition Integer(string key, string displayName, int minimum, int maximum, string unit)
        => new(key, displayName, minimum, maximum, unit);

    public ValidationResult Validate(int value)
        => value < Minimum || value > Maximum
            ? ValidationResult.Fail($"{Key} must be between {Minimum} and {Maximum}.")
            : ValidationResult.Ok();
}

public sealed record RegisterDefinition(
    RegisterKind Kind,
    ushort Address,
    string Name,
    RegisterValueType ValueType,
    RegisterAccess Access)
{
    public static RegisterDefinition Holding(ushort address, string name, RegisterValueType type, RegisterAccess access)
        => new(RegisterKind.Holding, address, name, type, access);
}

public sealed record ControllerProfile(
    string Id,
    string DisplayName,
    string SchemaVersion,
    IReadOnlyList<TransportKind> SupportedTransports,
    IReadOnlyList<RegisterDefinition> Registers,
    IReadOnlyList<SettingDefinition> Settings)
{
    public ValidationResult Validate()
    {
        var errors = new List<string>();
        foreach (var group in Registers.GroupBy(register => (register.Kind, register.Address)))
        {
            if (group.Count() > 1)
            {
                errors.Add($"Duplicate register address {group.Key.Address} for {group.Key.Kind}.");
            }
        }

        return errors.Count == 0 ? ValidationResult.Ok() : new ValidationResult(false, errors);
    }
}

public sealed record ControllerSessionId(string Value)
{
    public static ControllerSessionId New() => new(Guid.NewGuid().ToString("N"));
}

public sealed record ControllerIdentity(string ProfileId, byte DeviceId, string FirmwareVersion, string TransportName);
```

- [ ] **Step 4: Run tests**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter ProfileValidationTests
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add tools/configurator/src/LuefterConfigurator.Domain tools/configurator/tests/LuefterConfigurator.Tests/Domain
git commit -m "Add configurator domain model"
```

## Task 3: FanFlap Built-In Profile And Protocol Parsing

**Files:**
- Create: `tools/configurator/src/LuefterConfigurator.Profiles.FanFlap/FanFlapProfile.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Profiles.FanFlap/FanFlapTextProtocol.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Profiles.FanFlap/FanFlapModbusMap.cs`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/FanFlap/FanFlapProfileTests.cs`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/FanFlap/FanFlapTextProtocolTests.cs`

- [ ] **Step 1: Write failing profile tests**

Create `FanFlapProfileTests.cs`:

```csharp
using FluentAssertions;
using LuefterConfigurator.Profiles.FanFlap;

namespace LuefterConfigurator.Tests.FanFlap;

public sealed class FanFlapProfileTests
{
    [Fact]
    public void BuiltInProfileContainsRequiredSettings()
    {
        var profile = FanFlapProfile.Create();

        profile.Validate().IsValid.Should().BeTrue();
        profile.Settings.Select(s => s.Key).Should().Contain(["device.id", "safe.position"]);
    }

    [Fact]
    public void BuiltInProfileContainsModbusRegisters()
    {
        var profile = FanFlapProfile.Create();

        profile.Registers.Should().Contain(r => r.Name == "state");
        profile.Registers.Should().Contain(r => r.Name == "position_promille");
        profile.Registers.Should().Contain(r => r.Name == "safe_position_promille");
    }
}
```

- [ ] **Step 2: Write failing text protocol parser tests**

Create `FanFlapTextProtocolTests.cs`:

```csharp
using FluentAssertions;
using LuefterConfigurator.Profiles.FanFlap;

namespace LuefterConfigurator.Tests.FanFlap;

public sealed class FanFlapTextProtocolTests
{
    [Fact]
    public void ParsesIdResponse()
    {
        FanFlapTextProtocol.ParseDeviceId("ID: 17").Should().Be(17);
    }

    [Fact]
    public void RejectsMalformedIdResponse()
    {
        Action act = () => FanFlapTextProtocol.ParseDeviceId("ID = x");
        act.Should().Throw<FormatException>();
    }

    [Fact]
    public void BuildsAddressedCommand()
    {
        FanFlapTextProtocol.AddressCommand(7, "SAFE?").Should().Be("ID7 SAFE?");
    }
}
```

- [ ] **Step 3: Run tests and verify failure**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter FanFlap
```

Expected: FAIL because `FanFlapProfile` and `FanFlapTextProtocol` do not exist.

- [ ] **Step 4: Implement profile and parser**

Add:

```csharp
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Profiles.FanFlap;

public static class FanFlapProfile
{
    public static ControllerProfile Create() => new(
        "fanflap",
        "Luefterklappensteuerung",
        "1.0",
        [TransportKind.UsbText, TransportKind.ModbusRtu, TransportKind.ModbusTcpGateway],
        FanFlapModbusMap.Registers,
        [
            SettingDefinition.Integer("device.id", "Device ID", 1, 247, "id"),
            SettingDefinition.Integer("safe.position", "Safe Position", 0, 1000, "promille")
        ]);
}

public static class FanFlapModbusMap
{
    public static IReadOnlyList<RegisterDefinition> Registers { get; } =
    [
        RegisterDefinition.Holding(0, "state", RegisterValueType.UInt16, RegisterAccess.ReadOnly),
        RegisterDefinition.Holding(1, "position_promille", RegisterValueType.Int16, RegisterAccess.ReadOnly),
        RegisterDefinition.Holding(2, "target_promille", RegisterValueType.Int16, RegisterAccess.ReadWrite),
        RegisterDefinition.Holding(3, "safe_position_promille", RegisterValueType.UInt16, RegisterAccess.ReadWrite),
        RegisterDefinition.Holding(4, "fault_code", RegisterValueType.UInt16, RegisterAccess.ReadOnly),
        RegisterDefinition.Holding(5, "device_id", RegisterValueType.UInt16, RegisterAccess.ReadWrite)
    ];
}

public static class FanFlapTextProtocol
{
    public static string AddressCommand(byte deviceId, string command) => $"ID{deviceId} {command}";

    public static byte ParseDeviceId(string response)
    {
        const string prefix = "ID:";
        if (!response.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            throw new FormatException($"Invalid ID response: {response}");
        }

        var valueText = response[prefix.Length..].Trim();
        if (!byte.TryParse(valueText, out var value) || value is < 1 or > 247)
        {
            throw new FormatException($"Invalid ID value: {response}");
        }

        return value;
    }
}
```

- [ ] **Step 5: Run tests**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter FanFlap
```

Expected: PASS.

- [ ] **Step 6: Commit**

```powershell
git add tools/configurator/src/LuefterConfigurator.Profiles.FanFlap tools/configurator/tests/LuefterConfigurator.Tests/FanFlap
git commit -m "Add fan flap profile"
```

## Task 4: JSON Profile Import

**Files:**
- Create: `tools/configurator/src/LuefterConfigurator.Profiles.Json/controller-profile.schema.json`
- Create: `tools/configurator/src/LuefterConfigurator.Profiles.Json/JsonProfileImporter.cs`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/Profiles/JsonProfileImporterTests.cs`

- [ ] **Step 1: Write failing tests**

Create `JsonProfileImporterTests.cs`:

```csharp
using FluentAssertions;
using LuefterConfigurator.Profiles.Json;

namespace LuefterConfigurator.Tests.Profiles;

public sealed class JsonProfileImporterTests
{
    [Fact]
    public void ImportsValidProfile()
    {
        var json = """
        {
          "id": "demo",
          "displayName": "Demo Controller",
          "schemaVersion": "1.0",
          "transports": ["UsbText"],
          "settings": [{"key":"device.id","displayName":"Device ID","minimum":1,"maximum":247,"unit":"id"}],
          "registers": [{"kind":"Holding","address":0,"name":"state","valueType":"UInt16","access":"ReadOnly"}]
        }
        """;

        var profile = JsonProfileImporter.Import(json);

        profile.Id.Should().Be("demo");
        profile.Validate().IsValid.Should().BeTrue();
    }

    [Fact]
    public void RejectsDuplicateRegisters()
    {
        var json = """
        {
          "id": "bad",
          "displayName": "Bad",
          "schemaVersion": "1.0",
          "transports": ["UsbText"],
          "settings": [],
          "registers": [
            {"kind":"Holding","address":0,"name":"a","valueType":"UInt16","access":"ReadOnly"},
            {"kind":"Holding","address":0,"name":"b","valueType":"UInt16","access":"ReadOnly"}
          ]
        }
        """;

        Action act = () => JsonProfileImporter.Import(json);
        act.Should().Throw<InvalidDataException>().WithMessage("*Duplicate register address 0*");
    }
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter JsonProfileImporterTests
```

Expected: FAIL because importer does not exist.

- [ ] **Step 3: Add schema and importer**

Create `controller-profile.schema.json`:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "Controller Profile",
  "type": "object",
  "required": ["id", "displayName", "schemaVersion", "transports", "settings", "registers"],
  "additionalProperties": false
}
```

Create `JsonProfileImporter.cs` with strict enum parsing and post-import validation:

```csharp
using System.Text.Json;
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Profiles.Json;

public static class JsonProfileImporter
{
    public static ControllerProfile Import(string json)
    {
        var dto = JsonSerializer.Deserialize<JsonProfileDto>(json, new JsonSerializerOptions
        {
            PropertyNameCaseInsensitive = true
        }) ?? throw new InvalidDataException("Profile JSON is empty.");

        var profile = new ControllerProfile(
            Require(dto.Id, "id"),
            Require(dto.DisplayName, "displayName"),
            Require(dto.SchemaVersion, "schemaVersion"),
            dto.Transports.Select(ParseEnum<TransportKind>).ToArray(),
            dto.Registers.Select(r => RegisterDefinition.Holding(
                checked((ushort)r.Address),
                Require(r.Name, "register.name"),
                ParseEnum<RegisterValueType>(r.ValueType),
                ParseEnum<RegisterAccess>(r.Access))).ToArray(),
            dto.Settings.Select(s => SettingDefinition.Integer(
                Require(s.Key, "setting.key"),
                Require(s.DisplayName, "setting.displayName"),
                s.Minimum,
                s.Maximum,
                Require(s.Unit, "setting.unit"))).ToArray());

        var validation = profile.Validate();
        if (!validation.IsValid)
        {
            throw new InvalidDataException(string.Join(Environment.NewLine, validation.Errors));
        }

        return profile;
    }

    private static string Require(string? value, string field)
        => string.IsNullOrWhiteSpace(value) ? throw new InvalidDataException($"{field} is required.") : value;

    private static T ParseEnum<T>(string value) where T : struct
        => Enum.TryParse<T>(value, ignoreCase: true, out var result)
            ? result
            : throw new InvalidDataException($"Invalid {typeof(T).Name}: {value}");

    private sealed record JsonProfileDto(string? Id, string? DisplayName, string? SchemaVersion, string[] Transports, JsonSettingDto[] Settings, JsonRegisterDto[] Registers);
    private sealed record JsonSettingDto(string? Key, string? DisplayName, int Minimum, int Maximum, string? Unit);
    private sealed record JsonRegisterDto(string Kind, int Address, string? Name, string ValueType, string Access);
}
```

- [ ] **Step 4: Run tests**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter JsonProfileImporterTests
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add tools/configurator/src/LuefterConfigurator.Profiles.Json tools/configurator/tests/LuefterConfigurator.Tests/Profiles
git commit -m "Add JSON controller profile import"
```

## Task 5: USB Text And Modbus RTU Transport Foundation

**Files:**
- Create: `tools/configurator/src/LuefterConfigurator.Infrastructure.Serial/ISerialConnection.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Infrastructure.Serial/UsbTextTransport.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Infrastructure.Serial/ModbusRtuCrc.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Infrastructure.Serial/ModbusRtuTransport.cs`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/Serial/UsbTextTransportTests.cs`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/Serial/ModbusRtuCrcTests.cs`

- [ ] **Step 1: Write failing CRC tests**

Create `ModbusRtuCrcTests.cs`:

```csharp
using FluentAssertions;
using LuefterConfigurator.Infrastructure.Serial;

namespace LuefterConfigurator.Tests.Serial;

public sealed class ModbusRtuCrcTests
{
    [Fact]
    public void ComputesKnownReadHoldingRegistersCrc()
    {
        var payload = Convert.FromHexString("010300000002");

        var crc = ModbusRtuCrc.Compute(payload);

        crc.Should().Be(0x0BC4);
    }

    [Fact]
    public void AppendsCrcLowByteFirst()
    {
        var frame = ModbusRtuCrc.Append(Convert.FromHexString("010300000002"));

        Convert.ToHexString(frame).Should().Be("010300000002C40B");
    }
}
```

- [ ] **Step 2: Write failing USB text transport tests**

Create `UsbTextTransportTests.cs`:

```csharp
using FluentAssertions;
using LuefterConfigurator.Infrastructure.Serial;

namespace LuefterConfigurator.Tests.Serial;

public sealed class UsbTextTransportTests
{
    [Fact]
    public async Task SendsCommandAndReturnsLine()
    {
        var fake = new FakeSerialConnection("ID: 17");
        var transport = new UsbTextTransport(fake);

        var response = await transport.SendCommandAsync("ID?", CancellationToken.None);

        fake.Written.Should().Equal("ID?");
        response.Should().Be("ID: 17");
    }

    private sealed class FakeSerialConnection : ISerialConnection
    {
        private readonly Queue<string> responses;
        public List<string> Written { get; } = [];

        public FakeSerialConnection(params string[] responses) => this.responses = new Queue<string>(responses);
        public Task WriteLineAsync(string line, CancellationToken cancellationToken)
        {
            Written.Add(line);
            return Task.CompletedTask;
        }

        public Task<string> ReadLineAsync(TimeSpan timeout, CancellationToken cancellationToken)
            => Task.FromResult(responses.Dequeue());
    }
}
```

- [ ] **Step 3: Run tests and verify failure**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter "UsbTextTransportTests|ModbusRtuCrcTests"
```

Expected: FAIL because transport and CRC classes do not exist.

- [ ] **Step 4: Implement transport contracts and CRC**

Create:

```csharp
namespace LuefterConfigurator.Infrastructure.Serial;

public interface ISerialConnection
{
    Task WriteLineAsync(string line, CancellationToken cancellationToken);
    Task<string> ReadLineAsync(TimeSpan timeout, CancellationToken cancellationToken);
}

public sealed class UsbTextTransport
{
    private readonly ISerialConnection connection;

    public UsbTextTransport(ISerialConnection connection) => this.connection = connection;

    public async Task<string> SendCommandAsync(string command, CancellationToken cancellationToken)
    {
        await connection.WriteLineAsync(command, cancellationToken);
        return await connection.ReadLineAsync(TimeSpan.FromMilliseconds(500), cancellationToken);
    }
}

public static class ModbusRtuCrc
{
    public static ushort Compute(ReadOnlySpan<byte> payload)
    {
        ushort crc = 0xFFFF;
        foreach (var value in payload)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
            {
                crc = (crc & 0x0001) != 0 ? (ushort)((crc >> 1) ^ 0xA001) : (ushort)(crc >> 1);
            }
        }

        return crc;
    }

    public static byte[] Append(ReadOnlySpan<byte> payload)
    {
        var crc = Compute(payload);
        var result = new byte[payload.Length + 2];
        payload.CopyTo(result);
        result[^2] = (byte)(crc & 0xFF);
        result[^1] = (byte)(crc >> 8);
        return result;
    }
}
```

- [ ] **Step 5: Add first Modbus RTU request builder**

Create `ModbusRtuTransport.cs`:

```csharp
namespace LuefterConfigurator.Infrastructure.Serial;

public static class ModbusRtuTransport
{
    public static byte[] BuildReadHoldingRegisters(byte slaveId, ushort startAddress, ushort count)
    {
        if (slaveId is < 1 or > 247)
        {
            throw new ArgumentOutOfRangeException(nameof(slaveId), "Modbus slave id must be 1..247.");
        }

        Span<byte> payload = stackalloc byte[6];
        payload[0] = slaveId;
        payload[1] = 0x03;
        payload[2] = (byte)(startAddress >> 8);
        payload[3] = (byte)startAddress;
        payload[4] = (byte)(count >> 8);
        payload[5] = (byte)count;
        return ModbusRtuCrc.Append(payload);
    }
}
```

- [ ] **Step 6: Run tests**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter "UsbTextTransportTests|ModbusRtuCrcTests"
```

Expected: PASS.

- [ ] **Step 7: Commit**

```powershell
git add tools/configurator/src/LuefterConfigurator.Infrastructure.Serial tools/configurator/tests/LuefterConfigurator.Tests/Serial
git commit -m "Add configurator serial transport foundation"
```

## Task 6: Multi-Controller Sessions

**Files:**
- Create: `tools/configurator/src/LuefterConfigurator.Application/MultiControllerSessionService.cs`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/Application/MultiControllerSessionServiceTests.cs`

- [ ] **Step 1: Write failing tests**

Create `MultiControllerSessionServiceTests.cs`:

```csharp
using FluentAssertions;
using LuefterConfigurator.Application;
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Tests.Application;

public sealed class MultiControllerSessionServiceTests
{
    [Fact]
    public async Task AddsIndependentSessions()
    {
        var service = new MultiControllerSessionService();

        var first = await service.AddAsync(new ControllerIdentity("fanflap", 1, "1.0", "COM10"));
        var second = await service.AddAsync(new ControllerIdentity("fanflap", 2, "1.0", "COM11"));

        first.Should().NotBe(second);
        service.GetAll().Should().HaveCount(2);
    }

    [Fact]
    public async Task DetectsDeviceIdCollision()
    {
        var service = new MultiControllerSessionService();

        await service.AddAsync(new ControllerIdentity("fanflap", 1, "1.0", "COM10"));
        await service.AddAsync(new ControllerIdentity("fanflap", 1, "1.0", "COM11"));

        service.GetGatewayBlockingProblems().Should().Contain(p => p.Contains("duplicate device id 1"));
    }
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter MultiControllerSessionServiceTests
```

Expected: FAIL because service does not exist.

- [ ] **Step 3: Implement service**

Create records and service:

```csharp
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Application;

public sealed record ControllerSession(ControllerSessionId Id, ControllerIdentity Identity);

public sealed class MultiControllerSessionService
{
    private readonly List<ControllerSession> sessions = [];

    public Task<ControllerSessionId> AddAsync(ControllerIdentity identity)
    {
        var id = ControllerSessionId.New();
        sessions.Add(new ControllerSession(id, identity));
        return Task.FromResult(id);
    }

    public IReadOnlyList<ControllerSession> GetAll() => sessions.ToArray();

    public IReadOnlyList<string> GetGatewayBlockingProblems()
        => sessions
            .GroupBy(session => session.Identity.DeviceId)
            .Where(group => group.Count() > 1)
            .Select(group => $"duplicate device id {group.Key}")
            .ToArray();
}
```

- [ ] **Step 4: Run tests**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter MultiControllerSessionServiceTests
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add tools/configurator/src/LuefterConfigurator.Application tools/configurator/tests/LuefterConfigurator.Tests/Application
git commit -m "Add multi-controller session service"
```

## Task 7: Modbus TCP Gateway Core

**Files:**
- Create: `tools/configurator/src/LuefterConfigurator.Infrastructure.ModbusTcp/ModbusTcpFrame.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Infrastructure.ModbusTcp/ModbusTcpGateway.cs`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/ModbusTcp/ModbusTcpFrameTests.cs`

- [ ] **Step 1: Write failing frame tests**

Create `ModbusTcpFrameTests.cs`:

```csharp
using FluentAssertions;
using LuefterConfigurator.Infrastructure.ModbusTcp;

namespace LuefterConfigurator.Tests.ModbusTcp;

public sealed class ModbusTcpFrameTests
{
    [Fact]
    public void ParsesReadHoldingRegistersRequest()
    {
        var bytes = Convert.FromHexString("000100000006010300000002");

        var frame = ModbusTcpFrame.Parse(bytes);

        frame.TransactionId.Should().Be(1);
        frame.UnitId.Should().Be(1);
        frame.FunctionCode.Should().Be(3);
        frame.Pdu.Should().Equal([0x03, 0x00, 0x00, 0x00, 0x02]);
    }

    [Fact]
    public void BuildsExceptionResponse()
    {
        var response = ModbusTcpFrame.Exception(1, 1, 3, 2).ToArray();

        Convert.ToHexString(response).Should().Be("000100000003018302");
    }
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter ModbusTcpFrameTests
```

Expected: FAIL because frame parser does not exist.

- [ ] **Step 3: Implement frame parser**

Create:

```csharp
namespace LuefterConfigurator.Infrastructure.ModbusTcp;

public sealed record ModbusTcpFrame(ushort TransactionId, byte UnitId, byte FunctionCode, byte[] Pdu)
{
    public static ModbusTcpFrame Parse(ReadOnlySpan<byte> buffer)
    {
        if (buffer.Length < 8)
        {
            throw new InvalidDataException("Modbus TCP frame too short.");
        }

        var transactionId = ReadUInt16(buffer[0..2]);
        var protocolId = ReadUInt16(buffer[2..4]);
        var length = ReadUInt16(buffer[4..6]);
        if (protocolId != 0)
        {
            throw new InvalidDataException("Unsupported Modbus TCP protocol id.");
        }
        if (length < 2 || buffer.Length != 6 + length)
        {
            throw new InvalidDataException("Invalid Modbus TCP length.");
        }

        var unitId = buffer[6];
        var pdu = buffer[7..].ToArray();
        return new ModbusTcpFrame(transactionId, unitId, pdu[0], pdu);
    }

    public static byte[] Exception(ushort transactionId, byte unitId, byte functionCode, byte exceptionCode)
        => Build(transactionId, unitId, [(byte)(functionCode | 0x80), exceptionCode]);

    public static byte[] Build(ushort transactionId, byte unitId, byte[] pdu)
    {
        var length = checked((ushort)(pdu.Length + 1));
        var result = new byte[7 + pdu.Length];
        WriteUInt16(result.AsSpan(0, 2), transactionId);
        WriteUInt16(result.AsSpan(2, 2), 0);
        WriteUInt16(result.AsSpan(4, 2), length);
        result[6] = unitId;
        pdu.CopyTo(result.AsSpan(7));
        return result;
    }

    private static ushort ReadUInt16(ReadOnlySpan<byte> bytes) => (ushort)((bytes[0] << 8) | bytes[1]);
    private static void WriteUInt16(Span<byte> target, ushort value)
    {
        target[0] = (byte)(value >> 8);
        target[1] = (byte)value;
    }
}
```

- [ ] **Step 4: Run tests**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter ModbusTcpFrameTests
```

Expected: PASS.

- [ ] **Step 5: Implement gateway after frame tests pass**

Add `ModbusTcpGateway` with constructor arguments `(IPAddress bindAddress, int port, Func<byte, byte[], Task<byte[]>> pduHandler)`. The first version handles TCP accept/read/write and delegates PDU handling. Keep controller mapping in Application service, not in the socket class.

- [ ] **Step 6: Commit**

```powershell
git add tools/configurator/src/LuefterConfigurator.Infrastructure.ModbusTcp tools/configurator/tests/LuefterConfigurator.Tests/ModbusTcp
git commit -m "Add Modbus TCP gateway core"
```

## Task 8: UF2 Firmware Update Workflow

**Files:**
- Create: `tools/configurator/src/LuefterConfigurator.Infrastructure.Firmware/Uf2DriveDetector.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Infrastructure.Firmware/PicoUf2FirmwareUpdater.cs`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/Firmware/Uf2FirmwareUpdaterTests.cs`

- [ ] **Step 1: Write failing tests**

Create `Uf2FirmwareUpdaterTests.cs`:

```csharp
using FluentAssertions;
using LuefterConfigurator.Infrastructure.Firmware;

namespace LuefterConfigurator.Tests.Firmware;

public sealed class Uf2FirmwareUpdaterTests
{
    [Fact]
    public async Task CopiesUf2ToDetectedDrive()
    {
        using var temp = new TemporaryDirectory();
        var drive = Path.Combine(temp.Path, "RPI-RP2");
        Directory.CreateDirectory(drive);
        await File.WriteAllTextAsync(Path.Combine(drive, "INFO_UF2.TXT"), "UF2 Bootloader");

        var source = Path.Combine(temp.Path, "firmware.uf2");
        await File.WriteAllBytesAsync(source, [0x55, 0x46, 0x32]);

        var updater = new PicoUf2FirmwareUpdater(new Uf2DriveDetector([drive]));
        await updater.CopyAsync(source, CancellationToken.None);

        File.Exists(Path.Combine(drive, "firmware.uf2")).Should().BeTrue();
    }

    [Fact]
    public async Task RejectsNonUf2File()
    {
        using var temp = new TemporaryDirectory();
        var source = Path.Combine(temp.Path, "firmware.bin");
        await File.WriteAllBytesAsync(source, [0]);

        var updater = new PicoUf2FirmwareUpdater(new Uf2DriveDetector([temp.Path]));
        var act = () => updater.CopyAsync(source, CancellationToken.None);

        await act.Should().ThrowAsync<InvalidDataException>().WithMessage("*UF2*");
    }

    private sealed class TemporaryDirectory : IDisposable
    {
        public string Path { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        public TemporaryDirectory() => Directory.CreateDirectory(Path);
        public void Dispose() => Directory.Delete(Path, recursive: true);
    }
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter Uf2FirmwareUpdaterTests
```

Expected: FAIL because updater does not exist.

- [ ] **Step 3: Implement UF2 detector and updater**

Create:

```csharp
namespace LuefterConfigurator.Infrastructure.Firmware;

public sealed class Uf2DriveDetector
{
    private readonly IReadOnlyList<string> roots;

    public Uf2DriveDetector(IReadOnlyList<string>? roots = null)
        => this.roots = roots ?? DriveInfo.GetDrives().Where(d => d.IsReady).Select(d => d.RootDirectory.FullName).ToArray();

    public string Detect()
    {
        var drive = roots.FirstOrDefault(root => File.Exists(Path.Combine(root, "INFO_UF2.TXT")));
        return drive ?? throw new DriveNotFoundException("UF2 bootloader drive not found.");
    }
}

public sealed class PicoUf2FirmwareUpdater
{
    private readonly Uf2DriveDetector detector;

    public PicoUf2FirmwareUpdater(Uf2DriveDetector detector) => this.detector = detector;

    public async Task CopyAsync(string uf2Path, CancellationToken cancellationToken)
    {
        if (!File.Exists(uf2Path) || !string.Equals(Path.GetExtension(uf2Path), ".uf2", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("A readable UF2 file is required.");
        }

        var drive = detector.Detect();
        var destination = Path.Combine(drive, Path.GetFileName(uf2Path));
        await using var source = File.OpenRead(uf2Path);
        await using var target = File.Create(destination);
        await source.CopyToAsync(target, cancellationToken);
    }
}
```

- [ ] **Step 4: Run tests**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter Uf2FirmwareUpdaterTests
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add tools/configurator/src/LuefterConfigurator.Infrastructure.Firmware tools/configurator/tests/LuefterConfigurator.Tests/Firmware
git commit -m "Add UF2 firmware update workflow"
```

## Task 9: Export Adapters

**Files:**
- Create: `tools/configurator/src/LuefterConfigurator.Domain/ExportContracts.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Adapters.Loxone/LoxoneExportAdapter.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Adapters.HomeAssistant/HomeAssistantMqttDiscoveryAdapter.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Adapters.OpenHab/OpenHabExportAdapter.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Adapters.Modbus/ModbusRegisterExportAdapter.cs`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/Adapters/ExportAdapterTests.cs`

- [ ] **Step 1: Write failing export tests**

Create `ExportAdapterTests.cs`:

```csharp
using FluentAssertions;
using LuefterConfigurator.Adapters.HomeAssistant;
using LuefterConfigurator.Adapters.Loxone;
using LuefterConfigurator.Adapters.Modbus;
using LuefterConfigurator.Adapters.OpenHab;
using LuefterConfigurator.Profiles.FanFlap;

namespace LuefterConfigurator.Tests.Adapters;

public sealed class ExportAdapterTests
{
    [Fact]
    public void LoxoneExportContainsDeviceIdAndRegisters()
    {
        var export = new LoxoneExportAdapter().Export(FanFlapProfile.Create(), 17);

        export.FileName.Should().Be("loxone-fanflap-17.md");
        export.Content.Should().Contain("Modbus ID: 17");
        export.Content.Should().Contain("safe_position_promille");
    }

    [Fact]
    public void HomeAssistantExportContainsDiscoveryTopic()
    {
        var export = new HomeAssistantMqttDiscoveryAdapter().Export(FanFlapProfile.Create(), 17);

        export.Content.Should().Contain("homeassistant");
        export.Content.Should().Contain("fanflap_17_position_promille");
    }

    [Fact]
    public void OpenHabExportContainsThingAndItems()
    {
        var export = new OpenHabExportAdapter().Export(FanFlapProfile.Create(), 17);

        export.Content.Should().Contain("Thing modbus:data:fanflap17");
        export.Content.Should().Contain("Number FanFlap17_Position");
    }

    [Fact]
    public void ModbusExportContainsRegisterTable()
    {
        var export = new ModbusRegisterExportAdapter().Export(FanFlapProfile.Create(), 17);

        export.Content.Should().Contain("| Address | Name | Type | Access |");
    }
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter ExportAdapterTests
```

Expected: FAIL because adapters do not exist.

- [ ] **Step 3: Implement export contracts and adapters**

Create contract:

```csharp
namespace LuefterConfigurator.Domain;

public sealed record ExportArtifact(string FileName, string Content, string ContentType);

public interface IHomeAutomationExportAdapter
{
    string Id { get; }
    ExportArtifact Export(ControllerProfile profile, byte deviceId);
}
```

Each adapter returns deterministic UTF-8 text from the active profile and device ID. Do not read hardware inside export adapters.

- [ ] **Step 4: Run tests**

Run:

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln --filter ExportAdapterTests
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add tools/configurator/src/LuefterConfigurator.Adapters.* tools/configurator/src/LuefterConfigurator.Domain/ExportContracts.cs tools/configurator/tests/LuefterConfigurator.Tests/Adapters
git commit -m "Add home automation export adapters"
```

## Task 10: Local Host And Browser UI

**Files:**
- Modify: `tools/configurator/src/LuefterConfigurator.Host/Program.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Host/Pages/Index.cshtml`
- Create: `tools/configurator/src/LuefterConfigurator.Host/Pages/Index.cshtml.cs`
- Create: `tools/configurator/src/LuefterConfigurator.Host/wwwroot/css/site.css`
- Test: `tools/configurator/tests/LuefterConfigurator.Tests/Host/HostSmokeTests.cs`

- [ ] **Step 1: Write host smoke test**

Create `HostSmokeTests.cs`:

```csharp
using FluentAssertions;

namespace LuefterConfigurator.Tests.Host;

public sealed class HostSmokeTests
{
    [Fact]
    public void HostProjectMarkerExists()
    {
        File.Exists(Path.Combine("..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Program.cs"))
            .Should().BeTrue();
    }
}
```

- [ ] **Step 2: Implement local-only host binding**

In `Program.cs`, ensure:

```csharp
var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls("http://127.0.0.1:5184");
builder.Services.AddRazorPages();

var app = builder.Build();
app.UseStaticFiles();
app.MapRazorPages();
app.MapGet("/api/health", () => Results.Ok(new { status = "ok" }));
app.Run();
```

- [ ] **Step 3: Add first UI page**

Replace `Index.cshtml` with a dense service dashboard layout containing sections with these labels exactly:

```html
@page
@model IndexModel
<main class="shell">
  <section class="toolbar">
    <h1>Lueftersteuerung Konfigurator</h1>
    <button type="button">Ports scannen</button>
  </section>
  <section class="grid">
    <article><h2>Geraete</h2><p>Keine Verbindung</p></article>
    <article><h2>Status</h2><p>Warte auf Controller</p></article>
    <article><h2>Konfiguration</h2><p>ID und Safe-Position</p></article>
    <article><h2>Diagnose</h2><p>Textprotokoll und Modbus RTU</p></article>
    <article><h2>Gateway</h2><p>Modbus TCP lokal</p></article>
    <article><h2>Firmware</h2><p>UF2 Flashing</p></article>
    <article><h2>Integrationen</h2><p>Loxone, Home Assistant, MQTT, openHAB</p></article>
    <article><h2>Profile</h2><p>FanFlap und JSON Import</p></article>
  </section>
</main>
```

- [ ] **Step 4: Run host**

Run:

```powershell
dotnet run --project tools\configurator\src\LuefterConfigurator.Host
```

Expected:

```text
Now listening on: http://127.0.0.1:5184
```

- [ ] **Step 5: Verify health endpoint**

Run in a second shell:

```powershell
Invoke-RestMethod http://127.0.0.1:5184/api/health
```

Expected:

```text
status
------
ok
```

- [ ] **Step 6: Commit**

```powershell
git add tools/configurator/src/LuefterConfigurator.Host tools/configurator/tests/LuefterConfigurator.Tests/Host
git commit -m "Add local configurator host UI"
```

## Task 11: Documentation And Verification

**Files:**
- Modify: `README.md`
- Create: `tools/configurator/README.md`
- Modify: `tools/run_quality_checks.ps1`

- [ ] **Step 1: Document configurator commands**

Create `tools/configurator/README.md`:

```markdown
# Lueftersteuerung Konfigurator

Lokaler C# Service mit Browser-UI fuer die Luefterklappensteuerung.

## Start

```powershell
dotnet run --project tools\configurator\src\LuefterConfigurator.Host
```

Oeffnen: http://127.0.0.1:5184

## Test

```powershell
dotnet test tools\configurator\LuefterConfigurator.sln
```

## Release-1 Umfang

- USB CDC Textprotokoll
- Modbus RTU Testpfad
- mehrere Controller-Sessions
- Modbus TCP Gateway
- UF2 Firmware Update
- JSON Profilimport
- Loxone/Home Assistant/openHAB/Modbus Exporte
```

- [ ] **Step 2: Add configurator check to quality script**

Append to `tools/run_quality_checks.ps1` after existing firmware checks:

```powershell
if (Test-Path ".\tools\configurator\LuefterConfigurator.sln") {
    dotnet test .\tools\configurator\LuefterConfigurator.sln
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

- [ ] **Step 3: Run full checks**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_checks.ps1
dotnet test .\tools\configurator\LuefterConfigurator.sln
```

Expected:

```text
Build succeeded.
Passed!
```

- [ ] **Step 4: Commit**

```powershell
git add README.md tools/run_quality_checks.ps1 tools/configurator/README.md
git commit -m "Document configurator workflow"
```

## Self-Review Checklist

- Spec coverage:
  - Browser UI: Task 10.
  - USB/RS485 communication foundation: Tasks 3 and 5.
  - Multiple controllers: Task 6.
  - Modbus TCP Gateway: Task 7.
  - UF2 flashing: Task 8.
  - JSON profile import: Task 4.
  - Home automation exports: Task 9.
  - Safety rules: Tasks 2, 4, 5, 6, 7.
- Placeholder scan: this plan contains no placeholder markers or unbounded edge-case steps.
- Type consistency:
  - `ControllerProfile`, `RegisterDefinition`, `SettingDefinition`, `TransportKind`, and `ControllerIdentity` are introduced before use.
  - Adapter contracts are introduced before adapter tests.
  - Gateway frame parser is introduced before gateway service.
