namespace LuefterConfigurator.Domain;

public sealed record SettingDefinition(
    string Key,
    string DisplayName,
    int Minimum,
    int Maximum,
    string Unit)
{
    public static SettingDefinition Numeric(string key, string displayName, int minimum, int maximum, string unit)
        => new(key, displayName, minimum, maximum, unit);

    public ValidationResult Validate(int value)
        => value < Minimum || value > Maximum
            ? ValidationResult.Fail($"{Key} must be between {Minimum} and {Maximum}.")
            : ValidationResult.Ok();
}
