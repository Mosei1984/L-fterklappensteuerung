namespace LuefterConfigurator.Domain;

public sealed record ValidationResult(bool IsValid, IReadOnlyList<string> Errors)
{
    public static ValidationResult Ok() => new(true, Array.Empty<string>());

    public static ValidationResult Fail(params string[] errors) => new(false, errors);
}
