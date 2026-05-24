using Microsoft.AspNetCore.DataProtection;
using System.Text.Json;
using LuefterConfigurator.Adapters.HomeAssistant;
using LuefterConfigurator.Adapters.Loxone;
using LuefterConfigurator.Adapters.Modbus;
using LuefterConfigurator.Adapters.OpenHab;
using LuefterConfigurator.Application;
using LuefterConfigurator.Domain;
using LuefterConfigurator.Infrastructure.Firmware;
using LuefterConfigurator.Infrastructure.Serial;
using LuefterConfigurator.Profiles.FanFlap;
using LuefterConfigurator.Profiles.Json;

var builder = WebApplication.CreateBuilder(args);

var configuredUrls = builder.Configuration["urls"] ?? Environment.GetEnvironmentVariable("ASPNETCORE_URLS");
if (!builder.Environment.IsEnvironment("Testing") && string.IsNullOrWhiteSpace(configuredUrls))
{
    builder.WebHost.UseUrls("http://127.0.0.1:5184");
}

builder.Logging.ClearProviders();
builder.Logging.AddConsole();
builder.Services.AddDataProtection()
    .PersistKeysToFileSystem(new DirectoryInfo(Path.Combine(builder.Environment.ContentRootPath, "App_Data", "DataProtectionKeys")));
builder.Services.AddRazorPages();
if (builder.Environment.IsEnvironment("Testing"))
{
    builder.Services.AddSingleton<IControllerDiscovery, OfflineControllerDiscovery>();
    builder.Services.AddSingleton<IControllerCommandClient, OfflineControllerCommandClient>();
    builder.Services.AddSingleton<IFirmwareStatusProvider, OfflineFirmwareStatusProvider>();
    builder.Services.AddSingleton<IFirmwareFlasher, OfflineFirmwareFlasher>();
    builder.Services.AddSingleton<IControllerConnectionCloser, NoopControllerConnectionCloser>();
    builder.Services.AddSingleton<IConfiguratorShutdown, NoopConfiguratorShutdown>();
}
else
{
    builder.Services.AddSingleton<SystemSerialConnectionCloser>();
    builder.Services.AddSingleton<IControllerConnectionCloser>(serviceProvider =>
        serviceProvider.GetRequiredService<SystemSerialConnectionCloser>());
    builder.Services.AddSingleton<IControllerDiscovery>(serviceProvider =>
        new SystemSerialControllerDiscovery(serviceProvider.GetRequiredService<SystemSerialConnectionCloser>()));
    builder.Services.AddSingleton<IControllerCommandClient>(serviceProvider =>
        new SystemSerialControllerCommandClient(serviceProvider.GetRequiredService<SystemSerialConnectionCloser>()));
    builder.Services.AddSingleton(new Uf2DriveDetector());
    builder.Services.AddSingleton<IFirmwareStatusProvider, Uf2FirmwareStatusProvider>();
    builder.Services.AddSingleton<IFirmwareFlasher, PicoUf2FirmwareUpdater>();
    builder.Services.AddSingleton<IConfiguratorShutdown, HostConfiguratorShutdown>();
}

builder.Services.AddSingleton<IHomeAutomationExportAdapter, LoxoneExportAdapter>();
builder.Services.AddSingleton<IHomeAutomationExportAdapter, HomeAssistantMqttDiscoveryAdapter>();
builder.Services.AddSingleton<IHomeAutomationExportAdapter, OpenHabExportAdapter>();
builder.Services.AddSingleton<IHomeAutomationExportAdapter, ModbusRegisterExportAdapter>();
builder.Services.AddSingleton(FanFlapProfile.Create());
builder.Services.AddSingleton<IExportArtifactWriter>(serviceProvider => new FileExportArtifactWriter(
    Path.Combine(builder.Environment.ContentRootPath, "App_Data", "exports"),
    serviceProvider.GetRequiredService<ControllerProfile>(),
    serviceProvider.GetServices<IHomeAutomationExportAdapter>()));
builder.Services.AddSingleton<ConfiguratorService>();

var app = builder.Build();

if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Error");
}

app.UseStaticFiles();
app.UseRouting();
app.MapRazorPages();
app.MapGet("/api/health", () => Results.Ok(new { status = "ok" }));
app.MapPost("/api/app/serial/close", (IControllerConnectionCloser connections) =>
{
    connections.CloseActiveConnections();
    return Results.Ok(new { message = "Serial-Verbindungen beendet." });
});
app.MapPost("/api/app/shutdown", (
    HttpResponse response,
    IConfiguratorShutdown shutdown,
    IControllerConnectionCloser connections) =>
{
    connections.CloseActiveConnections();
    response.OnCompleted(() =>
    {
        shutdown.RequestShutdown();
        return Task.CompletedTask;
    });
    return Results.Ok(new { message = "Konfigurator wird beendet." });
});
app.MapGet("/api/controllers/state", (ConfiguratorService service) => Results.Ok(service.GetSnapshot()));
app.MapPost("/api/controllers/scan", async (ConfiguratorService service, CancellationToken cancellationToken)
    => Results.Ok(await service.ScanAsync(cancellationToken)));
app.MapPost("/api/controllers/connect", async (ConfiguratorService service, CancellationToken cancellationToken)
    => Results.Ok(await service.ConnectUsbAsync(cancellationToken)));
app.MapPut("/api/controllers/config", async (
    ConfiguratorWriteConfigRequest request,
    ConfiguratorService service,
    CancellationToken cancellationToken) => ToHttpResult(await service.WriteConfigAsync(request, cancellationToken)));
app.MapPost("/api/profiles/import", async (
    ConfiguratorProfileImportRequest request,
    ConfiguratorService service,
    CancellationToken cancellationToken) =>
{
    try
    {
        var profile = JsonProfileImporter.Import(request.Json);
        return ToHttpResult(await service.ImportProfileAsync(profile.Id, profile.DisplayName, cancellationToken));
    }
    catch (InvalidDataException exception)
    {
        return Results.BadRequest(new ConfiguratorOperationResult(
            false,
            "Profil ungueltig",
            service.GetSnapshot(),
            exception.Message));
    }
    catch (JsonException exception)
    {
        return Results.BadRequest(new ConfiguratorOperationResult(
            false,
            "Profil ungueltig",
            service.GetSnapshot(),
            exception.Message));
    }
});
app.MapPost("/api/commands/{command}", async (
    string command,
    ConfiguratorService service,
    CancellationToken cancellationToken) => ToHttpResult(await service.RunCommandAsync(command, cancellationToken)));
app.MapPost("/api/gateway/start", async (ConfiguratorService service, CancellationToken cancellationToken)
    => Results.Ok(await service.SetGatewayAsync(true, cancellationToken)));
app.MapPost("/api/gateway/stop", async (ConfiguratorService service, CancellationToken cancellationToken)
    => Results.Ok(await service.SetGatewayAsync(false, cancellationToken)));
app.MapPost("/api/firmware/check", async (ConfiguratorService service, CancellationToken cancellationToken)
    => Results.Ok(await service.CheckFirmwareAsync(cancellationToken)));
app.MapPost("/api/firmware/flash", async (
    HttpRequest request,
    ConfiguratorService service,
    CancellationToken cancellationToken) =>
{
    if (!request.HasFormContentType)
    {
        return BadFirmwareUpload(service, "UF2 Datei fehlt.");
    }

    var form = await request.ReadFormAsync(cancellationToken);
    var file = form.Files.GetFile("file") ?? (form.Files.Count > 0 ? form.Files[0] : null);
    if (file is null || file.Length == 0)
    {
        return BadFirmwareUpload(service, "UF2 Datei fehlt.");
    }

    await using var stream = file.OpenReadStream();
    return ToHttpResult(await service.FlashFirmwareAsync(new FirmwareFlashRequest(file.FileName, stream), cancellationToken));
});
app.MapPost("/api/exports", async (ConfiguratorService service, CancellationToken cancellationToken)
    => Results.Ok(await service.ExportAsync(cancellationToken)));
app.MapGet("/api/exports", (ConfiguratorService service)
    => Results.Ok(new
    {
        exportDirectory = service.GetSnapshot().ExportDirectory,
        files = service.GetSnapshot().ExportFiles.Select(file => new
        {
            file.AdapterId,
            file.FileName,
            file.ContentType,
            downloadUrl = $"/api/exports/{Uri.EscapeDataString(file.FileName)}"
        })
    }));
app.MapGet("/api/exports/{*fileName}", (string fileName, ConfiguratorService service) =>
{
    var decodedFileName = Uri.UnescapeDataString(fileName);
    if (!IsSafeExportFileName(decodedFileName))
    {
        return Results.BadRequest(new { error = "Ungueltiger Exportdateiname." });
    }

    var snapshot = service.GetSnapshot();
    var file = snapshot.ExportFiles.FirstOrDefault(candidate =>
        string.Equals(candidate.FileName, decodedFileName, StringComparison.OrdinalIgnoreCase));
    if (file is null)
    {
        return Results.NotFound(new { error = "Exportdatei nicht gefunden." });
    }

    if (!IsPathInsideDirectory(file.FullPath, snapshot.ExportDirectory) || !File.Exists(file.FullPath))
    {
        return Results.NotFound(new { error = "Exportdatei nicht gefunden." });
    }

    return Results.File(file.FullPath, file.ContentType, file.FileName);
});

app.Run();

static IResult ToHttpResult(ConfiguratorOperationResult result)
    => result.Success ? Results.Ok(result) : Results.BadRequest(result);

static IResult BadFirmwareUpload(ConfiguratorService service, string error)
    => Results.BadRequest(new ConfiguratorOperationResult(
        false,
        "Firmware flashen fehlgeschlagen",
        service.GetSnapshot(),
        error));

static bool IsSafeExportFileName(string fileName)
    => !string.IsNullOrWhiteSpace(fileName)
        && string.Equals(fileName, Path.GetFileName(fileName), StringComparison.Ordinal)
        && !fileName.Contains(Path.DirectorySeparatorChar, StringComparison.Ordinal)
        && !fileName.Contains(Path.AltDirectorySeparatorChar, StringComparison.Ordinal);

static bool IsPathInsideDirectory(string filePath, string directoryPath)
{
    if (string.IsNullOrWhiteSpace(directoryPath))
    {
        return false;
    }

    var root = Path.GetFullPath(directoryPath);
    var fullPath = Path.GetFullPath(filePath);
    return fullPath.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
}

public partial class Program
{
}

internal interface IConfiguratorShutdown
{
    void RequestShutdown();
}

internal sealed class NoopConfiguratorShutdown : IConfiguratorShutdown
{
    public void RequestShutdown()
    {
    }
}

internal sealed class HostConfiguratorShutdown(IHostApplicationLifetime lifetime) : IConfiguratorShutdown
{
    public void RequestShutdown()
    {
        lifetime.StopApplication();
    }
}
