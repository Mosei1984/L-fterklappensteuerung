using System.Globalization;
using System.Text;
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Adapters.Modbus;

public sealed class ModbusRegisterExportAdapter : IHomeAutomationExportAdapter
{
    public string Id => "modbus-registers";

    public ExportArtifact Export(ControllerProfile profile, byte deviceId)
    {
        var builder = new StringBuilder();
        builder.AppendLine(CultureInfo.InvariantCulture, $"# {profile.DisplayName} Modbus Registers");
        builder.AppendLine();
        builder.AppendLine(CultureInfo.InvariantCulture, $"Device ID: {deviceId}");
        builder.AppendLine();
        builder.AppendLine("| Address | Name | Type | Access |");
        builder.AppendLine("| --- | --- | --- | --- |");
        foreach (var register in profile.Registers)
        {
            builder.AppendLine(CultureInfo.InvariantCulture, $"| {register.Address} | {register.Name} | {register.ValueType} | {register.Access} |");
        }

        return new ExportArtifact(FormattableString.Invariant($"modbus-fanflap-{deviceId}.md"), builder.ToString(), "text/markdown");
    }
}
