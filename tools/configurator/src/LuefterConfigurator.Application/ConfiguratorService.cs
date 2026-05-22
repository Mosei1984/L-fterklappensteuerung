using System.Globalization;

namespace LuefterConfigurator.Application;

public sealed class ConfiguratorService(
    IControllerDiscovery? discovery = null,
    IFirmwareStatusProvider? firmwareStatusProvider = null,
    IExportArtifactWriter? exportArtifactWriter = null,
    IFirmwareFlasher? firmwareFlasher = null)
{
    private readonly IControllerDiscovery discovery = discovery ?? new OfflineControllerDiscovery();
    private readonly IFirmwareStatusProvider firmwareStatusProvider = firmwareStatusProvider ?? new OfflineFirmwareStatusProvider();
    private readonly IExportArtifactWriter exportArtifactWriter = exportArtifactWriter ?? new NoopExportArtifactWriter();
    private readonly IFirmwareFlasher firmwareFlasher = firmwareFlasher ?? new UnavailableFirmwareFlasher();
    private readonly object sync = new();
    private readonly List<ConfiguratorControllerSnapshot> controllers = [];
    private readonly List<string> log = ["> Service gestartet", "> Warte auf Controller"];
    private readonly List<string> exports = [];
    private readonly List<ExportedFile> exportFiles = [];
    private int? activeDeviceId;
    private int safePositionPromille = 250;
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
                lastEvent = "Kein Controller erkannt";
                AddLogLocked("Scan abgeschlossen: kein USB Controller gefunden");
                return SuccessLocked("Kein Controller erkannt");
            }

            foreach (var controller in discoveredControllers)
            {
                UpsertControllerLocked(controller.DeviceId, controller.TransportName, controller.FirmwareVersion);
            }

            var activeController = discoveredControllers[0];
            activeDeviceId = activeController.DeviceId;
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

    public Task<ConfiguratorOperationResult> WriteConfigAsync(ConfiguratorWriteConfigRequest request, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        lock (sync)
        {
            if (request.DeviceId is < 1 or > 247)
            {
                return Task.FromResult(FailureLocked("Ungueltige Controller ID", "ID muss im Bereich 1..247 liegen."));
            }

            if (request.SafePositionPromille is < 0 or > 1000)
            {
                return Task.FromResult(FailureLocked("Ungueltige Safe Position", "Safe Position muss im Bereich 0..1000 liegen."));
            }

            var previousId = activeDeviceId ?? request.DeviceId;
            UpsertControllerLocked(request.DeviceId, "USB Serial", "offline-test");
            if (previousId != request.DeviceId)
            {
                controllers.RemoveAll(controller => controller.DeviceId == previousId);
            }

            activeDeviceId = request.DeviceId;
            safePositionPromille = request.SafePositionPromille;
            lastEvent = "Konfiguration geschrieben";
            AddLogLocked($"ID {request.DeviceId.ToString(CultureInfo.InvariantCulture)}");
            AddLogLocked($"SAFE {request.SafePositionPromille.ToString(CultureInfo.InvariantCulture)}");
            return Task.FromResult(SuccessLocked("Konfiguration geschrieben"));
        }
    }

    public Task<ConfiguratorOperationResult> RunCommandAsync(string command, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        lock (sync)
        {
            EnsureControllerLocked();
            var id = activeDeviceId ?? 1;

            switch (command)
            {
                case "home":
                    lastEvent = "Homing gestartet";
                    AddLogLocked($"ID{id.ToString(CultureInfo.InvariantCulture)} HOME");
                    return Task.FromResult(SuccessLocked("Homing gestartet"));
                case "open":
                    lastEvent = "Ziel 100% gesendet";
                    AddLogLocked($"ID{id.ToString(CultureInfo.InvariantCulture)} GOTO 12000");
                    return Task.FromResult(SuccessLocked("Ziel 100% gesendet"));
                case "half":
                    lastEvent = "Ziel 50% gesendet";
                    AddLogLocked($"ID{id.ToString(CultureInfo.InvariantCulture)} GOTO 0");
                    return Task.FromResult(SuccessLocked("Ziel 50% gesendet"));
                case "safe":
                    lastEvent = "Safe Wert gesetzt";
                    AddLogLocked($"ID{id.ToString(CultureInfo.InvariantCulture)} SAFE {safePositionPromille.ToString(CultureInfo.InvariantCulture)} gesetzt; Anfahrt erfolgt ueber Zielposition");
                    return Task.FromResult(SuccessLocked("Safe Wert gesetzt"));
                case "refresh-machine":
                    lastEvent = "Machine Refresh gestartet";
                    AddLogLocked($"ID{id.ToString(CultureInfo.InvariantCulture)} REFRESH");
                    return Task.FromResult(SuccessLocked("Machine Refresh gestartet"));
                default:
                    return Task.FromResult(FailureLocked("Unbekanntes Kommando", $"Kommando '{command}' wird nicht unterstuetzt."));
            }
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

    private void EnsureControllerLocked()
    {
        if (controllers.Count == 0)
        {
            UpsertControllerLocked(1, "USB Serial", "offline-test");
            activeDeviceId = 1;
        }
    }

    private void UpsertControllerLocked(int deviceId, string transportName, string firmwareVersion)
    {
        controllers.RemoveAll(controller => controller.DeviceId == deviceId);
        controllers.Add(new ConfiguratorControllerSnapshot(deviceId, activeProfileId, transportName, firmwareVersion, true));
    }

    private void AddLogLocked(string line) => log.Add($"> {line}");

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
            gatewayRunning,
            lastEvent,
            log.ToArray(),
            exports.ToArray(),
            firmware,
            exportArtifactWriter.ExportDirectory,
            exportFiles.ToArray());
}
