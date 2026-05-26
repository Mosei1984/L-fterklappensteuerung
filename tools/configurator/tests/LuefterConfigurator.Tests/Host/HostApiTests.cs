using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using LuefterConfigurator.Application;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;

namespace LuefterConfigurator.Tests.Host;

public sealed class HostApiTests : IClassFixture<WebApplicationFactory<Program>>
{
    private readonly WebApplicationFactory<Program> factory;

    public HostApiTests(WebApplicationFactory<Program> factory)
    {
        this.factory = factory.WithWebHostBuilder(builder => builder.UseEnvironment("Testing"));
    }

    [Fact]
    public async Task IndexAndStateApiRenderWithoutHardware()
    {
        using var client = factory.CreateClient();

        var page = await client.GetStringAsync("/");
        var state = await client.GetFromJsonAsync<ConfiguratorSnapshot>("/api/controllers/state");

        Assert.Contains("command-center", page, StringComparison.Ordinal);
        Assert.NotNull(state);
        Assert.Equal(250, state.SafePositionPromille);
        Assert.Equal(0, state.SoftMinDegree);
        Assert.Equal(90, state.SoftMaxDegree);
        Assert.Equal(100, state.StallGuardThreshold);
        Assert.Equal(0, state.AutoHomeIntervalMinutes);
        Assert.Equal(400, state.NormalMaxSpeedStepsPerSecond);
        Assert.Equal(200, state.HomingMaxSpeedStepsPerSecond);
        Assert.Equal(650, state.RunCurrentMilliamps);
        Assert.Empty(state.Controllers);
    }

    [Fact]
    public async Task ControllerApiSupportsOfflineScanConfigSafeAndGatewayFlow()
    {
        using var client = factory.CreateClient();

        var scan = await PostAsync(client, "/api/controllers/scan");
        var config = await client.PutAsJsonAsync(
            "/api/controllers/config",
            new ConfiguratorWriteConfigRequest(
                13,
                640,
                10,
                80,
                64,
                NormalMaxSpeedStepsPerSecond: 900,
                HomingMaxSpeedStepsPerSecond: 150,
                RunCurrentMilliamps: 850,
                AutoHomeIntervalMinutes: 1440));
        var configured = await config.Content.ReadFromJsonAsync<ConfiguratorOperationResult>();
        var safe = await PostAsync(client, "/api/commands/safe");
        var refresh = await PostAsync(client, "/api/commands/refresh-machine");
        var gateway = await PostAsync(client, "/api/gateway/start");
        var exports = await PostAsync(client, "/api/exports");

        Assert.Equal(HttpStatusCode.OK, config.StatusCode);
        Assert.True(scan.Success);
        Assert.NotNull(configured);
        Assert.True(configured.Success);
        Assert.Equal(13, configured.Snapshot.ActiveDeviceId);
        Assert.Equal(640, configured.Snapshot.SafePositionPromille);
        Assert.Equal(10, configured.Snapshot.SoftMinDegree);
        Assert.Equal(80, configured.Snapshot.SoftMaxDegree);
        Assert.Equal(64, configured.Snapshot.StallGuardThreshold);
        Assert.Equal(1440, configured.Snapshot.AutoHomeIntervalMinutes);
        Assert.Equal(900, configured.Snapshot.NormalMaxSpeedStepsPerSecond);
        Assert.Equal(150, configured.Snapshot.HomingMaxSpeedStepsPerSecond);
        Assert.Equal(850, configured.Snapshot.RunCurrentMilliamps);
        Assert.Contains(configured.Snapshot.Log, line => line.Contains("ID 13", StringComparison.Ordinal));
        Assert.Contains(configured.Snapshot.Log, line => line.Contains("SAFE 640", StringComparison.Ordinal));
        Assert.Contains(configured.Snapshot.Log, line => line.Contains("SOFTMIN_DEG 10", StringComparison.Ordinal));
        Assert.Contains(configured.Snapshot.Log, line => line.Contains("STALLGUARD 64", StringComparison.Ordinal));
        Assert.Contains(configured.Snapshot.Log, line => line.Contains("AUTOHOME 1440", StringComparison.Ordinal));
        Assert.Contains(configured.Snapshot.Log, line => line.Contains("MOTORCFG 900 150 850", StringComparison.Ordinal));
        Assert.True(safe.Success);
        Assert.DoesNotContain(safe.Snapshot.Log, line => line.Contains("GOTO", StringComparison.OrdinalIgnoreCase));
        Assert.True(refresh.Success);
        Assert.Contains(refresh.Snapshot.Log, line => line.Contains("REFRESH", StringComparison.Ordinal));
        Assert.True(gateway.Snapshot.GatewayRunning);
        Assert.Contains("Loxone", exports.Snapshot.Exports);
        Assert.False(string.IsNullOrWhiteSpace(exports.Snapshot.ExportDirectory));
        Assert.NotEmpty(exports.Snapshot.ExportFiles);
    }

    [Fact]
    public async Task ExportApiListsGeneratedFilesWithDownloadUrls()
    {
        using var client = factory.CreateClient();

        await PostAsync(client, "/api/controllers/scan");
        await client.PutAsJsonAsync("/api/controllers/config", new ConfiguratorWriteConfigRequest(13, 640));
        await PostAsync(client, "/api/exports");

        var response = await client.GetAsync("/api/exports");

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        var payload = await response.Content.ReadFromJsonAsync<JsonDocument>();
        Assert.NotNull(payload);
        var files = payload.RootElement.GetProperty("files").EnumerateArray().ToArray();
        Assert.Contains(files, file =>
            file.GetProperty("fileName").GetString() == "MB_Luefterklappe_FanFlap_ID13.xml" &&
            file.GetProperty("contentType").GetString() == "application/xml" &&
            file.GetProperty("downloadUrl").GetString() == "/api/exports/MB_Luefterklappe_FanFlap_ID13.xml");
        Assert.Contains(files, file =>
            file.GetProperty("fileName").GetString() == "loxone-fanflap-13.json" &&
            file.GetProperty("contentType").GetString() == "application/json");
    }

    [Fact]
    public async Task ExportApiDownloadsGeneratedFileBySafeFileName()
    {
        using var client = factory.CreateClient();

        await PostAsync(client, "/api/controllers/scan");
        await client.PutAsJsonAsync("/api/controllers/config", new ConfiguratorWriteConfigRequest(13, 640));
        await PostAsync(client, "/api/exports");

        var response = await client.GetAsync("/api/exports/MB_Luefterklappe_FanFlap_ID13.xml");
        var content = await response.Content.ReadAsStringAsync();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("application/xml", response.Content.Headers.ContentType?.MediaType);
        Assert.Contains("safe_position", content, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("13", content, StringComparison.Ordinal);
    }

    [Fact]
    public async Task ExportApiRejectsPathTraversalDownloads()
    {
        using var client = factory.CreateClient();

        var response = await client.GetAsync("/api/exports/%2e%2e%2fappsettings.json");

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
    }

    [Fact]
    public async Task ProfileImportApiValidatesJsonAndUpdatesNextScannedController()
    {
        using var client = factory.CreateClient();
        var import = await client.PostAsJsonAsync("/api/profiles/import", new ConfiguratorProfileImportRequest("""
        {
          "id": "custom-fanflap",
          "displayName": "Custom FanFlap",
          "schemaVersion": "1.0",
          "transports": ["UsbText"],
          "settings": [{"key":"device.id","displayName":"Device ID","minimum":1,"maximum":247,"unit":"id"}],
          "registers": [{"kind":"Holding","address":0,"name":"state","valueType":"UInt16","access":"ReadOnly"}]
        }
        """));
        var imported = await import.Content.ReadFromJsonAsync<ConfiguratorOperationResult>();
        var scanned = await PostAsync(client, "/api/controllers/scan");

        Assert.Equal(HttpStatusCode.OK, import.StatusCode);
        Assert.NotNull(imported);
        Assert.True(imported.Success);
        Assert.Equal("custom-fanflap", scanned.Snapshot.Controllers.Single().ProfileId);
        Assert.Contains(scanned.Snapshot.Log, line => line.Contains("custom-fanflap", StringComparison.Ordinal));
    }

    [Fact]
    public async Task InvalidProfileImportReturnsBadRequestWithoutControllerMutation()
    {
        using var client = factory.CreateClient();

        var response = await client.PostAsJsonAsync("/api/profiles/import", new ConfiguratorProfileImportRequest("""{"id":""}"""));
        var result = await response.Content.ReadFromJsonAsync<ConfiguratorOperationResult>();

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.NotNull(result);
        Assert.False(result.Success);
        Assert.Empty(result.Snapshot.Controllers);
        Assert.Contains("required", result.Error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task InvalidConfigReturnsBadRequestWithCurrentSnapshot()
    {
        using var client = factory.CreateClient();

        var response = await client.PutAsJsonAsync("/api/controllers/config", new ConfiguratorWriteConfigRequest(248, 250));
        var result = await response.Content.ReadFromJsonAsync<ConfiguratorOperationResult>();

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.NotNull(result);
        Assert.False(result.Success);
        Assert.NotNull(result.Snapshot);
        Assert.Contains("ID", result.Error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task FirmwareCheckReturnsVisibleErrorWhenUf2DriveIsMissing()
    {
        using var client = factory.CreateClient();

        var response = await client.PostAsync("/api/firmware/check", null);
        var result = await response.Content.ReadFromJsonAsync<ConfiguratorOperationResult>();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.NotNull(result);
        Assert.False(result.Success);
        Assert.False(result.Snapshot.Firmware.IsUf2DrivePresent);
        Assert.Contains("UF2", result.Error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task FirmwareFlashEndpointAcceptsUf2Upload()
    {
        using var client = factory.CreateClient();
        using var form = new MultipartFormDataContent();
        form.Add(new ByteArrayContent([0x55, 0x46, 0x32]), "file", "fanflap.uf2");

        var response = await client.PostAsync("/api/firmware/flash", form);
        var result = await response.Content.ReadFromJsonAsync<ConfiguratorOperationResult>();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.NotNull(result);
        Assert.True(result.Success);
        Assert.Equal("UF2 Bootloader", result.Snapshot.Firmware.Mode);
        Assert.Contains("fanflap.uf2", result.Snapshot.Log[^1], StringComparison.Ordinal);
    }

    [Fact]
    public async Task FirmwareFlashEndpointRejectsMissingFile()
    {
        using var client = factory.CreateClient();
        using var form = new MultipartFormDataContent();
        form.Add(new StringContent("missing"), "marker");

        var response = await client.PostAsync("/api/firmware/flash", form);
        var result = await response.Content.ReadFromJsonAsync<ConfiguratorOperationResult>();

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.NotNull(result);
        Assert.False(result.Success);
        Assert.Contains("UF2", result.Error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task ShutdownEndpointReturnsVisibleShutdownMessage()
    {
        using var client = factory.CreateClient();

        var response = await client.PostAsync("/api/app/shutdown", null);
        var payload = await response.Content.ReadFromJsonAsync<JsonDocument>();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.NotNull(payload);
        Assert.Equal("Konfigurator wird beendet.", payload.RootElement.GetProperty("message").GetString());
    }

    [Fact]
    public async Task SerialCloseEndpointReturnsVisibleCloseMessage()
    {
        using var client = factory.CreateClient();

        var response = await client.PostAsync("/api/app/serial/close", null);
        var payload = await response.Content.ReadFromJsonAsync<JsonDocument>();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.NotNull(payload);
        Assert.Equal("Serial-Verbindungen beendet.", payload.RootElement.GetProperty("message").GetString());
    }

    private static async Task<ConfiguratorOperationResult> PostAsync(HttpClient client, string route)
    {
        var response = await client.PostAsync(route, null);
        response.EnsureSuccessStatusCode();
        var result = await response.Content.ReadFromJsonAsync<ConfiguratorOperationResult>();
        Assert.NotNull(result);
        return result;
    }
}
