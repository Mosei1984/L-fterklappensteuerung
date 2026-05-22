namespace LuefterConfigurator.Domain;

public enum TransportKind
{
    UsbText,
    ModbusRtu,
    ModbusTcpGateway
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

        if (string.IsNullOrWhiteSpace(Id))
        {
            errors.Add("Profile id is required.");
        }

        if (string.IsNullOrWhiteSpace(DisplayName))
        {
            errors.Add("Display name is required.");
        }

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
