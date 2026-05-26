namespace LuefterConfigurator.Application;

using LuefterConfigurator.Domain;

public sealed record ConfiguratorControllerSnapshot(
    int DeviceId,
    string ProfileId,
    string TransportName,
    string FirmwareVersion,
    bool IsOnline);

public sealed record ConfiguratorSnapshot(
    IReadOnlyList<ConfiguratorControllerSnapshot> Controllers,
    int? ActiveDeviceId,
    int SafePositionPromille,
    int SoftMinDegree,
    int SoftMaxDegree,
    int StallGuardThreshold,
    int AutoHomeIntervalMinutes,
    int NormalMaxSpeedStepsPerSecond,
    int HomingMaxSpeedStepsPerSecond,
    int RunCurrentMilliamps,
    int HomeMinSwitch,
    int HomeMaxSwitch,
    int HomeMinDirection,
    int HomeMaxDirection,
    bool StepperDirectionInverted,
    bool GatewayRunning,
    string LastEvent,
    IReadOnlyList<string> Log,
    IReadOnlyList<string> Exports,
    FirmwareStatus Firmware,
    string ExportDirectory,
    IReadOnlyList<ExportedFile> ExportFiles);

public sealed record ConfiguratorOperationResult(
    bool Success,
    string Message,
    ConfiguratorSnapshot Snapshot,
    string? Error = null);

public sealed record ConfiguratorWriteConfigRequest(
    int DeviceId,
    int SafePositionPromille,
    int SoftMinDegree = 0,
    int SoftMaxDegree = 90,
    int StallGuardThreshold = 100,
    int HomeMinSwitch = 0,
    int HomeMaxSwitch = 1,
    int HomeMinDirection = 0,
    int HomeMaxDirection = 1,
    bool StepperDirectionInverted = false,
    int NormalMaxSpeedStepsPerSecond = 400,
    int HomingMaxSpeedStepsPerSecond = 200,
    int RunCurrentMilliamps = 650,
    int AutoHomeIntervalMinutes = 0);

public sealed record ConfiguratorProfileImportRequest(string Json);

public sealed record FirmwareStatus(
    bool IsUf2DrivePresent,
    string Mode,
    string? DriveRoot,
    string? Error,
    string? LastFlashedFile = null,
    long? BytesWritten = null);

public interface IFirmwareStatusProvider
{
    Task<FirmwareStatus> CheckAsync(CancellationToken cancellationToken);
}

public sealed class OfflineFirmwareStatusProvider : IFirmwareStatusProvider
{
    public Task<FirmwareStatus> CheckAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(new FirmwareStatus(false, "Normalbetrieb", null, "UF2 Laufwerk nicht gefunden."));
    }
}

public sealed record FirmwareFlashRequest(string FileName, Stream Content);

public sealed record FirmwareFlashResult(string DriveRoot, string FileName, long BytesWritten);

public interface IFirmwareFlasher
{
    Task<FirmwareFlashResult> FlashAsync(string fileName, Stream content, CancellationToken cancellationToken);
}

public interface IControllerConnectionCloser
{
    void CloseActiveConnections();
}

public sealed class NoopControllerConnectionCloser : IControllerConnectionCloser
{
    public void CloseActiveConnections()
    {
    }
}

public sealed class UnavailableFirmwareFlasher : IFirmwareFlasher
{
    public Task<FirmwareFlashResult> FlashAsync(string fileName, Stream content, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromException<FirmwareFlashResult>(new InvalidOperationException("UF2 Flasher ist nicht konfiguriert."));
    }
}

public sealed class OfflineFirmwareFlasher : IFirmwareFlasher
{
    public async Task<FirmwareFlashResult> FlashAsync(string fileName, Stream content, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var safeFileName = Path.GetFileName(fileName);
        if (string.IsNullOrWhiteSpace(safeFileName) ||
            !string.Equals(Path.GetExtension(safeFileName), ".uf2", StringComparison.OrdinalIgnoreCase) ||
            !content.CanRead)
        {
            throw new InvalidDataException("Eine lesbare UF2 Datei ist erforderlich.");
        }

        var buffer = new byte[8192];
        long bytesWritten = 0;
        while (true)
        {
            var read = await content.ReadAsync(buffer, cancellationToken);
            if (read == 0)
            {
                break;
            }

            bytesWritten += read;
        }

        if (bytesWritten == 0)
        {
            throw new InvalidDataException("Die UF2 Datei ist leer.");
        }

        return new FirmwareFlashResult("TEST-UF2:\\", safeFileName, bytesWritten);
    }
}

public sealed record ExportedFile(string AdapterId, string FileName, string FullPath, string ContentType);

public interface IExportArtifactWriter
{
    string ExportDirectory { get; }

    Task<IReadOnlyList<ExportedFile>> WriteAsync(byte deviceId, CancellationToken cancellationToken);
}

public sealed class NoopExportArtifactWriter : IExportArtifactWriter
{
    public string ExportDirectory => string.Empty;

    public Task<IReadOnlyList<ExportedFile>> WriteAsync(byte deviceId, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult<IReadOnlyList<ExportedFile>>([]);
    }
}

public sealed class FileExportArtifactWriter(
    string exportDirectory,
    ControllerProfile profile,
    IEnumerable<IHomeAutomationExportAdapter> adapters) : IExportArtifactWriter
{
    private readonly IReadOnlyList<IHomeAutomationExportAdapter> adapters = adapters.ToArray();

    public string ExportDirectory { get; } = exportDirectory;

    public async Task<IReadOnlyList<ExportedFile>> WriteAsync(byte deviceId, CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(ExportDirectory);
        var files = new List<ExportedFile>();

        foreach (var adapter in adapters)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var artifacts = adapter is IHomeAutomationMultiExportAdapter multiExportAdapter
                ? multiExportAdapter.ExportAll(profile, deviceId)
                : [adapter.Export(profile, deviceId)];

            foreach (var artifact in artifacts)
            {
                var fileName = Path.GetFileName(artifact.FileName);
                var fullPath = Path.Combine(ExportDirectory, fileName);
                await File.WriteAllTextAsync(fullPath, artifact.Content, cancellationToken);
                files.Add(new ExportedFile(adapter.Id, fileName, fullPath, artifact.ContentType));
            }
        }

        return files;
    }
}

public sealed record DiscoveredController(
    int DeviceId,
    string TransportName,
    string FirmwareVersion,
    int? SafePositionPromille = null,
    string? CommandPortName = null,
    int? HomeMinSwitch = null,
    int? HomeMaxSwitch = null,
    int? HomeMinDirection = null,
    int? HomeMaxDirection = null,
    bool? StepperDirectionInverted = null,
    int? NormalMaxSpeedStepsPerSecond = null,
    int? HomingMaxSpeedStepsPerSecond = null,
    int? RunCurrentMilliamps = null,
    int? AutoHomeIntervalMinutes = null);

public interface IControllerDiscovery
{
    Task<IReadOnlyList<DiscoveredController>> ScanAsync(CancellationToken cancellationToken);
}

public sealed class OfflineControllerDiscovery : IControllerDiscovery
{
    private static readonly IReadOnlyList<DiscoveredController> Controllers =
    [
        new DiscoveredController(1, "USB Serial", "offline-test")
    ];

    public Task<IReadOnlyList<DiscoveredController>> ScanAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(Controllers);
    }
}

public sealed record ControllerCommandResult(
    string Command,
    IReadOnlyList<string> Lines,
    bool TimedOut);

public interface IControllerCommandClient
{
    Task<ControllerCommandResult> SendAsync(
        string? portName,
        string command,
        TimeSpan timeout,
        CancellationToken cancellationToken);
}

public sealed class OfflineControllerCommandClient : IControllerCommandClient
{
    public Task<ControllerCommandResult> SendAsync(
        string? portName,
        string command,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(new ControllerCommandResult(command, [$"OFFLINE {command}"], false));
    }
}
