using System.Globalization;
using System.Text;
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Adapters.OpenHab;

public sealed class OpenHabExportAdapter : IHomeAutomationExportAdapter
{
    public string Id => "openhab";

    public ExportArtifact Export(ControllerProfile profile, byte deviceId)
    {
        var builder = new StringBuilder();
        builder.AppendLine(CultureInfo.InvariantCulture, $"Thing modbus:data:fanflap{deviceId} \"{profile.DisplayName}\" [ readStart=\"0\", readValueType=\"uint16\" ]");
        builder.AppendLine(CultureInfo.InvariantCulture, $"Number FanFlap{deviceId}_Position \"Position\" {{ channel=\"modbus:data:fanflap{deviceId}:number\" }}");
        return new ExportArtifact(FormattableString.Invariant($"openhab-fanflap-{deviceId}.things"), builder.ToString(), "text/plain");
    }
}
