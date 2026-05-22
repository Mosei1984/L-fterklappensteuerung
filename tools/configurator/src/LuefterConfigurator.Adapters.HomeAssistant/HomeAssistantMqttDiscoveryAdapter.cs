using System.Globalization;
using System.Text;
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Adapters.HomeAssistant;

public sealed class HomeAssistantMqttDiscoveryAdapter : IHomeAutomationExportAdapter
{
    public string Id => "homeassistant-mqtt";

    public ExportArtifact Export(ControllerProfile profile, byte deviceId)
    {
        var builder = new StringBuilder();
        foreach (var register in profile.Registers.Where(register => register.Access != RegisterAccess.WriteOnly))
        {
            var objectId = FormattableString.Invariant($"fanflap_{deviceId}_{register.Name}");
            builder.AppendLine(CultureInfo.InvariantCulture, $"homeassistant/sensor/{objectId}/config");
            builder.AppendLine(CultureInfo.InvariantCulture, $$"""{"name":"{{profile.DisplayName}} {{register.Name}}","unique_id":"{{objectId}}","state_topic":"fanflap/{{deviceId}}/{{register.Name}}"}""");
        }

        return new ExportArtifact(FormattableString.Invariant($"homeassistant-fanflap-{deviceId}.txt"), builder.ToString(), "text/plain");
    }
}
