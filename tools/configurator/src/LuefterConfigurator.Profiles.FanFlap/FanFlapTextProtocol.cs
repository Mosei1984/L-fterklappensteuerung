using System.Globalization;

namespace LuefterConfigurator.Profiles.FanFlap;

public static class FanFlapTextProtocol
{
    public static string AddressCommand(byte deviceId, string command) => $"ID{deviceId.ToString(CultureInfo.InvariantCulture)} {command}";

    public static byte ParseDeviceId(string response)
    {
        const string prefix = "ID:";
        if (!response.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            throw new FormatException($"Invalid ID response: {response}");
        }

        var valueText = response[prefix.Length..].Trim();
        if (!byte.TryParse(valueText, NumberStyles.None, CultureInfo.InvariantCulture, out var value) || value is < 1 or > 247)
        {
            throw new FormatException($"Invalid ID value: {response}");
        }

        return value;
    }
}
