using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Adapters.Loxone;

public sealed class LoxoneExportAdapter : IHomeAutomationMultiExportAdapter
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public string Id => "loxone";

    public ExportArtifact Export(ControllerProfile profile, byte deviceId)
        => ExportMarkdown(profile, deviceId);

    public IReadOnlyList<ExportArtifact> ExportAll(ControllerProfile profile, byte deviceId)
        => [ExportMarkdown(profile, deviceId), ExportXmlTemplate(profile, deviceId), ExportJsonContract(profile, deviceId)];

    private static ExportArtifact ExportMarkdown(ControllerProfile profile, byte deviceId)
    {
        var builder = new StringBuilder();
        builder.AppendLine(CultureInfo.InvariantCulture, $"# {profile.DisplayName} Loxone Modbus");
        builder.AppendLine();
        builder.AppendLine(CultureInfo.InvariantCulture, $"Modbus ID: {deviceId}");
        builder.AppendLine("Loxone IO-Adressen entsprechen der 0-basierten Holding-Register-Adresse der Firmware.");
        builder.AppendLine("32-bit Positionswerte werden High-Word zuerst und Low-Word danach uebertragen.");
        builder.AppendLine();
        builder.AppendLine("Empfohlene Loxone Punkte:");
        builder.AppendLine("- Analogaktor: `target_degree`, Register 23, Wertebereich 0..90; 0 ist offen, 90 geschlossen");
        builder.AppendLine("- Optionaler Legacy-Aktor: `target_promille`, Register 14, Wertebereich 0..1000");
        builder.AppendLine("- Parameter: `safe_position_promille`, Register 16, Wertebereich 0..1000");
        builder.AppendLine("- Parameter: `soft_min_degree`, `soft_max_degree`, Register 25..26");
        builder.AppendLine("- Expertenparameter: `stallguard_threshold`, Register 27, Wertebereich 0..255");
        builder.AppendLine("- Statussensoren: `state`, `flags`, `current_degree`, `position_promille`, Polling 2..5 s; `flags` Bit3 meldet laufende Bewegung");
        builder.AppendLine();
        builder.AppendLine("| Address | Name | Type | Access |");
        builder.AppendLine("| --- | --- | --- | --- |");
        foreach (var register in profile.Registers)
        {
            builder.AppendLine(CultureInfo.InvariantCulture, $"| {register.Address} | {register.Name} | {register.ValueType} | {register.Access} |");
        }

        return new ExportArtifact(FormattableString.Invariant($"loxone-fanflap-{deviceId}.md"), builder.ToString(), "text/markdown");
    }

    private static ExportArtifact ExportXmlTemplate(ControllerProfile profile, byte deviceId)
    {
        var document = new XDocument(
            new XDeclaration("1.0", "utf-8", null),
            new XElement(
                "ModbusTemplate",
                new XAttribute("name", "Luefterklappe FanFlap"),
                new XAttribute("deviceId", deviceId.ToString(CultureInfo.InvariantCulture)),
                new XAttribute("profileId", profile.Id),
                new XAttribute("schemaVersion", profile.SchemaVersion),
                new XElement(
                    "Loxone",
                    new XElement("TemplateFile", TemplateFileName(deviceId)),
                    new XElement("TemplateFolder", @"C:\ProgramData\Loxone\Loxone Config <version>\ENG\Comm"),
                    new XElement("Communication", "Modbus RTU 38400 8N1 via RS485 oder Modbus TCP Gateway 127.0.0.1:5020"),
                    new XElement("Addressing", "IO address equals firmware holding-register address, zero based"),
                    new XElement("WordOrder", "32-bit signed step values use high word first, low word second")),
                new XElement(
                    "Device",
                    new XAttribute("modbusAddress", deviceId.ToString(CultureInfo.InvariantCulture)),
                    new XAttribute("pollingSeconds", "2"),
                    profile.Registers.Select(ToTemplateElement))));

        return new ExportArtifact(TemplateFileName(deviceId), document.ToString(SaveOptions.DisableFormatting), "application/xml");
    }

    private static ExportArtifact ExportJsonContract(ControllerProfile profile, byte deviceId)
    {
        var payload = new
        {
            profileId = profile.Id,
            displayName = profile.DisplayName,
            schemaVersion = profile.SchemaVersion,
            deviceId,
            loxone = new
            {
                templateFile = TemplateFileName(deviceId),
                templateFolder = @"C:\ProgramData\Loxone\Loxone Config <version>\ENG\Comm",
                modbusRtu = new { baudRate = 38400, dataBits = 8, parity = "None", stopBits = 1 },
                modbusTcpGateway = new { host = "127.0.0.1", port = 5020, unitId = deviceId },
                pollingSeconds = 2,
                addressing = "zero-based IO address equals firmware holding register address"
            },
            commands = new
            {
                home = 1,
                reset = 2,
                softEndstopsOn = 3,
                softEndstopsOff = 4,
                refreshMachine = 5
            },
            registers = profile.Registers.Select(register => new
            {
                kind = register.Kind.ToString(),
                address = register.Address,
                name = register.Name,
                valueType = register.ValueType.ToString(),
                access = register.Access.ToString(),
                description = register.Description
            }).ToArray()
        };

        var json = JsonSerializer.Serialize(payload, JsonOptions);
        return new ExportArtifact(FormattableString.Invariant($"loxone-fanflap-{deviceId}.json"), json, "application/json");
    }

    private static XElement ToTemplateElement(RegisterDefinition register)
    {
        var elementName = register.Access == RegisterAccess.ReadOnly ? "Sensor" : "Actuator";
        return new XElement(
            elementName,
            new XAttribute("name", register.Name),
            new XAttribute("ioAddress", register.Address.ToString(CultureInfo.InvariantCulture)),
            new XAttribute("command", "3"),
            new XAttribute("registerKind", register.Kind.ToString()),
            new XAttribute("dataType", ToLoxoneDataType(register.ValueType)),
            new XAttribute("access", register.Access.ToString()),
            new XAttribute("description", register.Description),
            new XElement("Name", register.Name),
            new XElement("IOAddress", register.Address.ToString(CultureInfo.InvariantCulture)),
            new XElement("Command", "3 - Read holding register (4x)"),
            new XElement("DataType", ToLoxoneDataType(register.ValueType)),
            new XElement("Access", register.Access.ToString()),
            new XElement("Description", register.Description));
    }

    private static string ToLoxoneDataType(RegisterValueType valueType)
        => valueType switch
        {
            RegisterValueType.Boolean => "Digital",
            RegisterValueType.Signed16 => "16-bit signed integer",
            RegisterValueType.Signed32 => "32-bit signed integer, high word first",
            RegisterValueType.Unsigned32 => "32-bit unsigned integer, high word first",
            _ => "16-bit unsigned integer"
        };

    private static string TemplateFileName(byte deviceId)
        => FormattableString.Invariant($"MB_Luefterklappe_FanFlap_ID{deviceId}.xml");
}
