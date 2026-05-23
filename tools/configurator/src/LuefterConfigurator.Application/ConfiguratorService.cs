using System.Globalization;

namespace LuefterConfigurator.Application;

public sealed class ConfiguratorService(
    IControllerDiscovery? discovery = null,
    IFirmwareStatusProvider? firmwareStatusProvider = null,
    IExportArtifactWriter? exportArtifactWriter = null,
    IFirmwareFlasher? firmwareFlasher = null,
    IControllerCommandClient? commandClient = null)
{
    private readonly IControllerDiscovery discovery = discovery ?? new OfflineControllerDiscovery();
    private readonly IFirmwareStatusProvider firmwareStatusProvider = firmwareStatusProvider ?? new OfflineFirmwareStatusProvider();
    private readonly IExportArtifactWriter exportArtifactWriter = exportArtifactWriter ?? new NoopExportArtifactWriter();
    private readonly IFirmwareFlasher firmwareFlasher = firmwareFlasher ?? new UnavailableFirmwareFlasher();
    private readonly IControllerCommandClient commandClient = commandClient ?? new OfflineControllerCommandClient();
    private readonly object sync = new();
    private readonly List<ConfiguratorControllerSnapshot> controllers = [];
    private readonly List<string> log = ["> Service gestartet", "> Warte auf Controller"];
    private readonly List<string> exports = [];
    private readonly List<ExportedFile> exportFiles = [];
    private int? activeDeviceId;
    private string? activeCommandPortName;
    private int safePositionPromille = 250;
    private int softMinDegree;
    private int softMaxDegree = 90;
    private int stallGuardThreshold = 100;
    private bool gatewayRunning;
    private string lastEvent = "Warte auf Aktion";
    private string activeProfileId = "fanflap";
    private FirmwareStatus firmware = new(false, "Ungeprueft", null, null);

    public ConfiguratorSnapshot GetSnapshot()
    {
        lock (sync)
        {
            return SnapshotLocked();
        }
    }

    public async Task<ConfiguratorOperationResult> ScanAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var discoveredControllers = await discovery.ScanAsync(cancellationToken);

        lock (sync)
        {
            controllers.Clear();

            if (discoveredControllers.Count == 0)
            {
                activeDeviceId = null;
                activeCommandPortName = null;
                lastEvent = "Kein Controller erkannt";
                AddLogLocked("Scan abgeschlossen: kein USB Controller gefunden");
                return SuccessLocked("Kein Controller erkannt");
            }

            foreach (var controller in discoveredControllers)
            {
                UpsertControllerLocked(
                    controller.DeviceId,
                    controller.TransportName,
                    controller.FirmwareVersion,
                    controller.CommandPortName);
            }

            var activeController = discoveredControllers[0];
            activeDeviceId = activeController.DeviceId;
            activeCommandPortName = activeController.CommandPortName;
            if (activeController.SafePositionPromille.HasValue)
            {
                safePositionPromille = activeController.SafePositionPromille.Value;
            }

            lastEvent = "Controller erkannt";
            AddLogLocked($"Scan abgeschlossen: USB Controller ID {activeController.DeviceId.ToString(CultureInfo.InvariantCulture)} gefunden");
            return SuccessLocked($"Controller ID {activeController.DeviceId.ToString(CultureInfo.InvariantCulture)} erkannt");
        }
    }

    public Task<ConfiguratorOperationResult> ConnectUsbAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        lock (sync)
        {
            UpsertControllerLocked(activeDeviceId ?? 1, "USB Serial", "offline-test");
            activeDeviceId ??= 1;
            lastEvent = "USB verbunden";
            AddLogLocked("USB verbunden: Textprotokoll bereit");
            return Task.FromResult(SuccessLocked("USB verbunden"));
        }
    }

    public async Task<ConfiguratorOperationResult> WriteConfigAsync(ConfiguratorWriteConfigRequest request, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        string? portName;
        string[] commands;

        lock (sync)
        {
            if (request.DeviceId is < 1 or > 247)
            {
                return FailureLocked("Ungueltige Controller ID", "ID muss im Bereich 1..247 liegen.");
            }

            if (request.SafePositionPromille is < 0 or > 1000)
            {
                return FailureLocked("Ungueltige Safe Position", "Safe Position muss im Bereich 0..1000 liegen.");
            }

            if (request.SoftMinDegree is < 0 or > 90 ||
                request.SoftMaxDegree is < 0 or > 90 ||
                request.SoftMinDegree > request.SoftMaxDegree)
            {
                return FailureLocked("Ungueltige Grad-Limits", "Min/Max Grad muessen im Bereich 0..90 liegen und Min darf Max nicht ueberschreiten.");
            }

            if (request.StallGuardThreshold is < 0 or > 255)
            {
                return FailureLocked("Ungueltige StallGuard Schwelle", "StallGuard muss im Bereich 0..255 liegen.");
            }

            portName = activeCommandPortName;
            commands =
            [
                $"ID {request.DeviceId.ToString(CultureInfo.InvariantCulture)}",
                $"SAFE {request.SafePositionPromille.ToString(CultureInfo.InvariantCulture)}",
                $"STALLGUARD {request.StallGuardThreshold.ToString(CultureInfo.InvariantCulture)}",
                $"SOFTMIN_DEG {request.SoftMinDegree.ToString(CultureInfo.InvariantCulture)}",
                $"SOFTMAX_DEG {request.SoftMaxDegree.ToString(CultureInfo.InvariantCulture)}"
            ];
        }

        IReadOnlyList<ControllerCommandResult> commandResults;
        try
        {
            commandResults = await SendCommandsAsync(portName, commands, TimeSpan.FromMilliseconds(1_800), cancellationToken);
        }
        catch (Exception exception) when (IsControllerCommandFailure(exception))
        {
            lock (sync)
            {
                lastEvent = "USB Schreibfehler";
                AddLogLocked($"USB Schreibfehler: {exception.Message}");
                return FailureLocked("Konfiguration nicht geschrieben", exception.Message);
            }
        }

        lock (sync)
        {
            var rejection = FirstFirmwareRejection(commandResults);
            var previousId = activeDeviceId ?? request.DeviceId;
            UpsertControllerLocked(request.DeviceId, TransportNameForPort(portName), portName is null ? "offline-test" : "pico-live", portName);
            if (previousId != request.DeviceId)
            {
                controllers.RemoveAll(controller => controller.DeviceId == previousId);
            }

            activeDeviceId = request.DeviceId;
            activeCommandPortName = portName;
            safePositionPromille = request.SafePositionPromille;
            stallGuardThreshold = request.StallGuardThreshold;
            var softLimitRejected =
                CommandWasRejected(commandResults, "SOFTMIN_DEG") ||
                CommandWasRejected(commandResults, "SOFTMAX_DEG");
            if (!softLimitRejected)
            {
                softMinDegree = request.SoftMinDegree;
                softMaxDegree = request.SoftMaxDegree;
            }

            AddCommandResultsLocked(commandResults);

            if (rejection is not null)
            {
                lastEvent = "Konfiguration teilweise geschrieben";
                return FailureLocked(
                    "Konfiguration teilweise geschrieben",
                    $"Firmware hat '{rejection.Value.Command}' abgelehnt: {rejection.Value.Line}");
            }

            lastEvent = "Konfiguration geschrieben";
            return SuccessLocked("Konfiguration geschrieben");
        }
    }

    public async Task<ConfiguratorOperationResult> RunCommandAsync(string command, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        string? portName;
        string wireCommand;
        string message;
        TimeSpan timeout;

        lock (sync)
        {
            EnsureControllerLocked();
            var id = activeDeviceId ?? 1;

            switch (command)
            {
                case "home":
                    wireCommand = $"ID{id.ToString(CultureInfo.InvariantCulture)} HOME";
                    message = "Homing gestartet";
                    timeout = TimeSpan.FromSeconds(8);
                    break;
                case "open":
                    wireCommand = $"ID{id.ToString(CultureInfo.InvariantCulture)} GOTO_DEG 0";
                    message = "Ziel 100% gesendet";
                    timeout = TimeSpan.FromSeconds(8);
                    break;
                case "half":
                    wireCommand = $"ID{id.ToString(CultureInfo.InvariantCulture)} GOTO_DEG 45";
                    message = "Ziel 50% gesendet";
                    timeout = TimeSpan.FromSeconds(8);
                    break;
                case "safe":
                    wireCommand = $"ID{id.ToString(CultureInfo.InvariantCulture)} SAFE {safePositionPromille.ToString(CultureInfo.InvariantCulture)}";
                    message = "Safe Wert gesetzt";
                    timeout = TimeSpan.FromSeconds(2);
                    break;
                case "refresh-machine":
                    wireCommand = $"ID{id.ToString(CultureInfo.InvariantCulture)} REFRESH";
                    message = "Machine Refresh gestartet";
                    timeout = TimeSpan.FromSeconds(4);
                    break;
                case "step-test":
                    wireCommand = "STEPTEST 800";
                    message = "Stepper Test ausgefuehrt";
                    timeout = TimeSpan.FromSeconds(4);
                    break;
                default:
                    return FailureLocked("Unbekanntes Kommando", $"Kommando '{command}' wird nicht unterstuetzt.");
            }

            portName = activeCommandPortName;
        }

        ControllerCommandResult commandResult;
        try
        {
            commandResult = await commandClient.SendAsync(portName, wireCommand, timeout, cancellationToken);
        }
        catch (Exception exception) when (IsControllerCommandFailure(exception))
        {
            lock (sync)
            {
                lastEvent = "USB Befehlsfehler";
                AddLogLocked($"USB Befehlsfehler: {exception.Message}");
                return FailureLocked($"{message} fehlgeschlagen", exception.Message);
            }
        }

        lock (sync)
        {
            var rejection = FirstFirmwareRejection([commandResult]);
            lastEvent = message;
            AddCommandResultsLocked([commandResult]);
            if (rejection is not null)
            {
                return FailureLocked(
                    $"{message} abgelehnt",
                    $"Firmware hat '{rejection.Value.Command}' abgelehnt: {rejection.Value.Line}");
            }

            return SuccessLocked(message);
        }
    }

    public Task<ConfiguratorOperationResult> ImportProfileAsync(string profileId, string displayName, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        lock (sync)
        {
            activeProfileId = string.IsNullOrWhiteSpace(profileId) ? activeProfileId : profileId;
            lastEvent = "Profil validiert";
            AddLogLocked($"Profilimport geprueft: {activeProfileId} ({displayName})");
            return Task.FromResult(SuccessLocked("Profil validiert"));
        }
    }

    public Task<ConfiguratorOperationResult> SetGatewayAsync(bool enabled, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        lock (sync)
        {
            gatewayRunning = enabled;
            lastEvent = enabled ? "Gateway laeuft lokal" : "Gateway gestoppt";
            AddLogLocked(enabled
                ? "Modbus TCP Gateway gestartet: 127.0.0.1:5020"
                : "Modbus TCP Gateway gestoppt");
            return Task.FromResult(SuccessLocked(lastEvent));
        }
    }

    public async Task<ConfiguratorOperationResult> CheckFirmwareAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var status = await firmwareStatusProvider.CheckAsync(cancellationToken);

        lock (sync)
        {
            firmware = status;
            lastEvent = status.IsUf2DrivePresent ? "UF2 Laufwerk erkannt" : "UF2 Fehler";
            AddLogLocked(status.IsUf2DrivePresent
                ? $"UF2 Laufwerk erkannt: {status.DriveRoot}"
                : $"UF2 Fehler: {status.Error ?? "UF2 Laufwerk nicht gefunden."}");

            return status.IsUf2DrivePresent
                ? SuccessLocked("UF2 Laufwerk erkannt")
                : FailureLocked("UF2 Laufwerk nicht gefunden", status.Error ?? "UF2 Laufwerk nicht gefunden.");
        }
    }

    public async Task<ConfiguratorOperationResult> FlashFirmwareAsync(FirmwareFlashRequest request, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        try
        {
            var result = await firmwareFlasher.FlashAsync(request.FileName, request.Content, cancellationToken);

            lock (sync)
            {
                firmware = new FirmwareStatus(
                    true,
                    "UF2 Bootloader",
                    result.DriveRoot,
                    null,
                    result.FileName,
                    result.BytesWritten);
                lastEvent = "Firmware geflasht";
                AddLogLocked($"UF2 geflasht: {result.FileName} -> {result.DriveRoot} ({result.BytesWritten.ToString(CultureInfo.InvariantCulture)} Bytes)");
                return SuccessLocked("Firmware geflasht");
            }
        }
        catch (Exception exception) when (IsFirmwareFlashFailure(exception))
        {
            lock (sync)
            {
                firmware = new FirmwareStatus(false, "UF2 Fehler", null, exception.Message);
                lastEvent = "UF2 Flashfehler";
                AddLogLocked($"UF2 Flashfehler: {exception.Message}");
                return FailureLocked("Firmware flashen fehlgeschlagen", exception.Message);
            }
        }
    }

    public async Task<ConfiguratorOperationResult> ExportAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        byte deviceId;
        lock (sync)
        {
            deviceId = (byte)(activeDeviceId ?? 1);
        }

        var writtenFiles = await exportArtifactWriter.WriteAsync(deviceId, cancellationToken);

        lock (sync)
        {
            exports.Clear();
            exportFiles.Clear();
            exportFiles.AddRange(writtenFiles);
            exports.AddRange(writtenFiles.Count > 0
                ? writtenFiles.Select(file => ExportDisplayName(file.AdapterId))
                : ["Loxone", "Home Assistant", "MQTT", "openHAB", "Modbus TCP"]);
            lastEvent = "Exports erzeugt";
            AddLogLocked(writtenFiles.Count > 0
                ? $"Exportadapter erzeugt: {exportArtifactWriter.ExportDirectory}"
                : "Exportadapter erzeugt: Loxone, MQTT Discovery, openHAB, Modbus");
            return SuccessLocked("Exports erzeugt");
        }
    }

    private static bool IsFirmwareFlashFailure(Exception exception)
        => exception is InvalidDataException
            or DriveNotFoundException
            or IOException
            or UnauthorizedAccessException
            or InvalidOperationException;

    private static bool IsControllerCommandFailure(Exception exception)
        => exception is IOException
            or InvalidOperationException
            or TimeoutException
            or UnauthorizedAccessException;

    private void EnsureControllerLocked()
    {
        if (controllers.Count == 0)
        {
            UpsertControllerLocked(1, "USB Serial", "offline-test");
            activeDeviceId = 1;
        }
    }

    private void UpsertControllerLocked(int deviceId, string transportName, string firmwareVersion, string? commandPortName = null)
    {
        controllers.RemoveAll(controller => controller.DeviceId == deviceId);
        controllers.Add(new ConfiguratorControllerSnapshot(deviceId, activeProfileId, transportName, firmwareVersion, true));
        activeCommandPortName = commandPortName ?? activeCommandPortName;
    }

    private void AddLogLocked(string line) => log.Add($"> {line}");

    private async Task<IReadOnlyList<ControllerCommandResult>> SendCommandsAsync(
        string? portName,
        IReadOnlyList<string> commands,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var results = new List<ControllerCommandResult>(commands.Count);
        foreach (var command in commands)
        {
            cancellationToken.ThrowIfCancellationRequested();
            results.Add(await commandClient.SendAsync(portName, command, timeout, cancellationToken));
        }

        return results;
    }

    private void AddCommandResultsLocked(IReadOnlyList<ControllerCommandResult> commandResults)
    {
        foreach (var commandResult in commandResults)
        {
            AddLogLocked(commandResult.Command);
            foreach (var line in commandResult.Lines.Take(8))
            {
                AddLogLocked($"< {line}");
            }

            if (commandResult.TimedOut)
            {
                AddLogLocked("< Zeitueberschreitung ohne vollstaendige Antwort");
            }
        }
    }

    private static (string Command, string Line)? FirstFirmwareRejection(IReadOnlyList<ControllerCommandResult> commandResults)
    {
        foreach (var commandResult in commandResults)
        {
            var rejectedLine = commandResult.Lines.FirstOrDefault(IsFirmwareRejectionLine);
            if (rejectedLine is not null)
            {
                return (commandResult.Command, rejectedLine);
            }
        }

        return null;
    }

    private static bool CommandWasRejected(IReadOnlyList<ControllerCommandResult> commandResults, string commandPrefix)
        => commandResults.Any(commandResult =>
            commandResult.Command.StartsWith(commandPrefix, StringComparison.Ordinal) &&
            commandResult.Lines.Any(IsFirmwareRejectionLine));

    private static bool IsFirmwareRejectionLine(string line)
    {
        var normalized = line.ToLowerInvariant();
        return normalized.Contains("fehler", StringComparison.Ordinal) ||
            normalized.Contains("ungueltig", StringComparison.Ordinal) ||
            normalized.Contains("ungültig", StringComparison.Ordinal) ||
            normalized.Contains("nicht bereit", StringComparison.Ordinal) ||
            normalized.Contains("unbekannter befehl", StringComparison.Ordinal);
    }

    private static string TransportNameForPort(string? portName)
        => string.IsNullOrWhiteSpace(portName) ? "USB Serial" : $"USB Serial {portName}";

    private static string ExportDisplayName(string adapterId)
        => adapterId switch
        {
            "loxone" => "Loxone",
            "homeassistant-mqtt" => "Home Assistant",
            "openhab" => "openHAB",
            "modbus-registers" => "Modbus TCP",
            _ => adapterId
        };

    private ConfiguratorOperationResult SuccessLocked(string message)
        => new(true, message, SnapshotLocked());

    private ConfiguratorOperationResult FailureLocked(string message, string error)
        => new(false, message, SnapshotLocked(), error);

    private ConfiguratorSnapshot SnapshotLocked()
        => new(
            controllers.ToArray(),
            activeDeviceId,
            safePositionPromille,
            softMinDegree,
            softMaxDegree,
            stallGuardThreshold,
            gatewayRunning,
            lastEvent,
            log.ToArray(),
            exports.ToArray(),
            firmware,
            exportArtifactWriter.ExportDirectory,
            exportFiles.ToArray());
}
