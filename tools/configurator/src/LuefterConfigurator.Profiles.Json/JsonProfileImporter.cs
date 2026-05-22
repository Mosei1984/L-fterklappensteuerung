using System.Text.Json;
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Profiles.Json;

public static class JsonProfileImporter
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public static ControllerProfile Import(string json)
    {
        var dto = JsonSerializer.Deserialize<JsonProfileDto>(json, SerializerOptions)
            ?? throw new InvalidDataException("Profile JSON is empty.");

        var registers = (dto.Registers ?? []).Select(register => new RegisterDefinition(
            ParseEnum<RegisterKind>(Require(register.Kind, "register.kind")),
            ParseAddress(register.Address),
            Require(register.Name, "register.name"),
            ParseEnum<RegisterValueType>(Require(register.ValueType, "register.valueType")),
            ParseEnum<RegisterAccess>(Require(register.Access, "register.access")))).ToArray();

        var profile = new ControllerProfile(
            Require(dto.Id, "id"),
            Require(dto.DisplayName, "displayName"),
            Require(dto.SchemaVersion, "schemaVersion"),
            (dto.Transports ?? []).Select(ParseEnum<TransportKind>).ToArray(),
            registers,
            (dto.Settings ?? []).Select(setting => SettingDefinition.Numeric(
                Require(setting.Key, "setting.key"),
                Require(setting.DisplayName, "setting.displayName"),
                setting.Minimum,
                setting.Maximum,
                Require(setting.Unit, "setting.unit"))).ToArray());

        var validation = profile.Validate();
        if (!validation.IsValid)
        {
            throw new InvalidDataException(string.Join(Environment.NewLine, validation.Errors));
        }

        return profile;
    }

    private static string Require(string? value, string field)
        => string.IsNullOrWhiteSpace(value) ? throw new InvalidDataException($"{field} is required.") : value;

    private static T ParseEnum<T>(string value)
        where T : struct
        => Enum.TryParse<T>(NormalizeEnumValue<T>(value), ignoreCase: true, out var result)
            ? result
            : throw new InvalidDataException($"Invalid {typeof(T).Name}: {value}");

    private static ushort ParseAddress(int address)
        => address is < ushort.MinValue or > ushort.MaxValue
            ? throw new InvalidDataException($"Register address {address} is outside UInt16 range.")
            : (ushort)address;

    private static string NormalizeEnumValue<T>(string value)
        => typeof(T) == typeof(RegisterValueType)
            ? value switch
            {
                "UInt16" => nameof(RegisterValueType.Unsigned16),
                "Int16" => nameof(RegisterValueType.Signed16),
                "UInt32" => nameof(RegisterValueType.Unsigned32),
                "Int32" => nameof(RegisterValueType.Signed32),
                _ => value
            }
            : value;

    private sealed record JsonProfileDto(
        string? Id,
        string? DisplayName,
        string? SchemaVersion,
        string[]? Transports,
        JsonSettingDto[]? Settings,
        JsonRegisterDto[]? Registers);

    private sealed record JsonSettingDto(string? Key, string? DisplayName, int Minimum, int Maximum, string? Unit);

    private sealed record JsonRegisterDto(string? Kind, int Address, string? Name, string? ValueType, string? Access);
}
