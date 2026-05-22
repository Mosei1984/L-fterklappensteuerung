using LuefterConfigurator.Adapters.HomeAssistant;
using LuefterConfigurator.Adapters.Loxone;
using LuefterConfigurator.Adapters.Modbus;
using LuefterConfigurator.Adapters.OpenHab;
using LuefterConfigurator.Application;
using LuefterConfigurator.Profiles.FanFlap;
using System.Text.Json;
using System.Xml.Linq;

namespace LuefterConfigurator.Tests.Adapters;

public sealed class ExportAdapterTests
{
    [Fact]
    public void LoxoneExportContainsDeviceIdAndRegisters()
    {
        var export = new LoxoneExportAdapter().Export(FanFlapProfile.Create(), 17);

        Assert.Equal("loxone-fanflap-17.md", export.FileName);
        Assert.Contains("Modbus ID: 17", export.Content, StringComparison.Ordinal);
        Assert.Contains("safe_position_promille", export.Content, StringComparison.Ordinal);
    }

    [Fact]
    public async Task LoxoneExportWritesMarkdownXmlTemplateAndMachineReadableJson()
    {
        var exportDirectory = Path.Combine(Path.GetTempPath(), "luefter-configurator-tests", Guid.NewGuid().ToString("N"));
        var writer = new FileExportArtifactWriter(exportDirectory, FanFlapProfile.Create(), [new LoxoneExportAdapter()]);

        var files = await writer.WriteAsync(17, CancellationToken.None);

        Assert.Collection(
            files.OrderBy(file => file.FileName, StringComparer.Ordinal),
            file => Assert.Equal("MB_Luefterklappe_FanFlap_ID17.xml", file.FileName),
            file => Assert.Equal("loxone-fanflap-17.json", file.FileName),
            file => Assert.Equal("loxone-fanflap-17.md", file.FileName));

        var xmlPath = Path.Combine(exportDirectory, "MB_Luefterklappe_FanFlap_ID17.xml");
        var jsonPath = Path.Combine(exportDirectory, "loxone-fanflap-17.json");

        var xml = XDocument.Load(xmlPath);
        Assert.Contains("Modbus", xml.Root?.Name.LocalName ?? string.Empty, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(xml.Descendants().Select(element => element.Value), value => value.Contains("17", StringComparison.Ordinal));
        Assert.Contains(xml.Descendants().Select(element => element.Value), value => value.Contains("safe_position", StringComparison.OrdinalIgnoreCase));

        using var json = JsonDocument.Parse(await File.ReadAllTextAsync(jsonPath));
        Assert.Equal("fanflap", json.RootElement.GetProperty("profileId").GetString());
        Assert.Equal(17, json.RootElement.GetProperty("deviceId").GetInt32());
        Assert.Equal(5, json.RootElement.GetProperty("commands").GetProperty("refreshMachine").GetInt32());
        Assert.True(json.RootElement.GetProperty("registers").GetArrayLength() > 0);
    }

    [Fact]
    public void HomeAssistantExportContainsDiscoveryTopic()
    {
        var export = new HomeAssistantMqttDiscoveryAdapter().Export(FanFlapProfile.Create(), 17);

        Assert.Contains("homeassistant", export.Content, StringComparison.Ordinal);
        Assert.Contains("fanflap_17_position_promille", export.Content, StringComparison.Ordinal);
    }

    [Fact]
    public void OpenHabExportContainsThingAndItems()
    {
        var export = new OpenHabExportAdapter().Export(FanFlapProfile.Create(), 17);

        Assert.Contains("Thing modbus:data:fanflap17", export.Content, StringComparison.Ordinal);
        Assert.Contains("Number FanFlap17_Position", export.Content, StringComparison.Ordinal);
    }

    [Fact]
    public void ModbusExportContainsRegisterTable()
    {
        var export = new ModbusRegisterExportAdapter().Export(FanFlapProfile.Create(), 17);

        Assert.Contains("| Address | Name | Type | Access |", export.Content, StringComparison.Ordinal);
    }
}
