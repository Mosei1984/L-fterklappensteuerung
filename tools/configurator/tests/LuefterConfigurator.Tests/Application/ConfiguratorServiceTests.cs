using LuefterConfigurator.Application;
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Tests.Application;

public sealed class ConfiguratorServiceTests
{
    [Fact]
    public async Task ScanCreatesOfflineTestControllerAndMarksItActive()
    {
        var service = new ConfiguratorService();

        var result = await service.ScanAsync(CancellationToken.None);

        Assert.True(result.Success);
        Assert.Single(result.Snapshot.Controllers);
        Assert.Equal(1, result.Snapshot.ActiveDeviceId);
        Assert.True(result.Snapshot.Controllers[0].IsOnline);
        Assert.Contains("ID 1", result.Message);
    }

    [Fact]
    public async Task ScanUsesDiscoveryResultAndSafePosition()
    {
        var service = new ConfiguratorService(new FakeDiscovery([
            new DiscoveredController(
                23,
                "USB Serial COM10",
                "pico-live",
                875,
                AutoHomeIntervalMinutes: 1440)
        ]));

        var result = await service.ScanAsync(CancellationToken.None);

        Assert.True(result.Success);
        Assert.Equal(23, result.Snapshot.ActiveDeviceId);
        Assert.Equal(875, result.Snapshot.SafePositionPromille);
        Assert.Equal(1440, result.Snapshot.AutoHomeIntervalMinutes);
        Assert.Equal("USB Serial COM10", result.Snapshot.Controllers[0].TransportName);
        Assert.Equal("pico-live", result.Snapshot.Controllers[0].FirmwareVersion);
    }

    [Fact]
    public async Task ScanWithNoHardwareDoesNotInventAControllerWhenRealDiscoveryIsEmpty()
    {
        var service = new ConfiguratorService(new FakeDiscovery([]));

        var result = await service.ScanAsync(CancellationToken.None);

        Assert.True(result.Success);
        Assert.Null(result.Snapshot.ActiveDeviceId);
        Assert.Empty(result.Snapshot.Controllers);
        Assert.Contains("Kein Controller", result.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task WriteConfigRejectsInvalidDeviceIdSafePositionAnglesStallGuardAndMotorConfig()
    {
        var service = new ConfiguratorService();

        var badId = await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(0, 250), CancellationToken.None);
        var badSafe = await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(1, 1001), CancellationToken.None);
        var badAngles = await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(1, 250, 80, 10), CancellationToken.None);
        var badStallGuard = await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(1, 250, 0, 90, 256), CancellationToken.None);
        var badAutoHome = await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(1, 250, AutoHomeIntervalMinutes: 10081), CancellationToken.None);
        var badSpeed = await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(1, 250, NormalMaxSpeedStepsPerSecond: 0), CancellationToken.None);
        var badCurrent = await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(1, 250, RunCurrentMilliamps: 1200), CancellationToken.None);

        Assert.False(badId.Success);
        Assert.Contains("ID", badId.Error, StringComparison.OrdinalIgnoreCase);
        Assert.False(badSafe.Success);
        Assert.Contains("Safe", badSafe.Error, StringComparison.OrdinalIgnoreCase);
        Assert.False(badAngles.Success);
        Assert.Contains("Grad", badAngles.Error, StringComparison.OrdinalIgnoreCase);
        Assert.False(badStallGuard.Success);
        Assert.Contains("StallGuard", badStallGuard.Error, StringComparison.OrdinalIgnoreCase);
        Assert.False(badAutoHome.Success);
        Assert.Contains("Auto-Home", badAutoHome.Error, StringComparison.OrdinalIgnoreCase);
        Assert.False(badSpeed.Success);
        Assert.Contains("Geschwindigkeit", badSpeed.Error, StringComparison.OrdinalIgnoreCase);
        Assert.False(badCurrent.Success);
        Assert.Contains("Motorstrom", badCurrent.Error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task WriteConfigPersistsValuesAndLogsSeparateFirmwareCommands()
    {
        var service = new ConfiguratorService();
        await service.ScanAsync(CancellationToken.None);

        var result = await service.WriteConfigAsync(
            new ConfiguratorWriteConfigRequest(
                12,
                640,
                10,
                80,
                64,
                NormalMaxSpeedStepsPerSecond: 900,
                HomingMaxSpeedStepsPerSecond: 150,
                RunCurrentMilliamps: 850,
                AutoHomeIntervalMinutes: 1440),
            CancellationToken.None);

        Assert.True(result.Success);
        Assert.Equal(12, result.Snapshot.ActiveDeviceId);
        Assert.Equal(640, result.Snapshot.SafePositionPromille);
        Assert.Equal(10, result.Snapshot.SoftMinDegree);
        Assert.Equal(80, result.Snapshot.SoftMaxDegree);
        Assert.Equal(64, result.Snapshot.StallGuardThreshold);
        Assert.Equal(1440, result.Snapshot.AutoHomeIntervalMinutes);
        Assert.Equal(900, result.Snapshot.NormalMaxSpeedStepsPerSecond);
        Assert.Equal(150, result.Snapshot.HomingMaxSpeedStepsPerSecond);
        Assert.Equal(850, result.Snapshot.RunCurrentMilliamps);
        Assert.Contains(result.Snapshot.Log, line => line.Contains("ID 12", StringComparison.Ordinal));
        Assert.Contains(result.Snapshot.Log, line => line.Contains("SAFE 640", StringComparison.Ordinal));
        Assert.Contains(result.Snapshot.Log, line => line.Contains("SOFTMIN_DEG 10", StringComparison.Ordinal));
        Assert.Contains(result.Snapshot.Log, line => line.Contains("SOFTMAX_DEG 80", StringComparison.Ordinal));
        Assert.Contains(result.Snapshot.Log, line => line.Contains("STALLGUARD 64", StringComparison.Ordinal));
        Assert.Contains(result.Snapshot.Log, line => line.Contains("AUTOHOME 1440", StringComparison.Ordinal));
        Assert.Contains(result.Snapshot.Log, line => line.Contains("MOTORCFG 900 150 850", StringComparison.Ordinal));
        Assert.Contains(result.Snapshot.Log, line => line.Contains("HOMECFG 0 1 0 1 0", StringComparison.Ordinal));
        Assert.DoesNotContain(result.Snapshot.Log, line => line.Contains(';', StringComparison.Ordinal));
    }

    [Fact]
    public async Task WriteConfigSendsSeparateFirmwareCommandsToDiscoveredUsbPort()
    {
        var commandClient = new FakeCommandClient();
        var service = new ConfiguratorService(
            new FakeDiscovery([
                new DiscoveredController(23, "USB Serial COM10", "pico-live", 875, "COM10")
            ]),
            commandClient: commandClient);
        await service.ScanAsync(CancellationToken.None);

        var result = await service.WriteConfigAsync(
            new ConfiguratorWriteConfigRequest(
                12,
                640,
                10,
                80,
                64,
                NormalMaxSpeedStepsPerSecond: 900,
                HomingMaxSpeedStepsPerSecond: 150,
                RunCurrentMilliamps: 850,
                AutoHomeIntervalMinutes: 1440),
            CancellationToken.None);

        Assert.True(result.Success);
        Assert.Equal(
            ["ID 12", "SAFE 640", "STALLGUARD 64", "AUTOHOME 1440", "MOTORCFG 900 150 850", "HOMECFG 0 1 0 1 0", "SOFTMIN_DEG 10", "SOFTMAX_DEG 80"],
            commandClient.Sent.Select(command => command.Command));
        Assert.All(commandClient.Sent, command => Assert.Equal("COM10", command.PortName));
        Assert.Contains(result.Snapshot.Log, line => line.Contains("< ACK STALLGUARD 64", StringComparison.Ordinal));
    }

    [Fact]
    public async Task WriteConfigSendsHomingConfigThroughUsb()
    {
        var commandClient = new FakeCommandClient();
        var service = new ConfiguratorService(
            new FakeDiscovery([
                new DiscoveredController(23, "USB Serial COM10", "pico-live", 875, "COM10")
            ]),
            commandClient: commandClient);
        await service.ScanAsync(CancellationToken.None);

        var result = await service.WriteConfigAsync(
            new ConfiguratorWriteConfigRequest(12, 640, 10, 80, 64, 1, 0, 1, 0, true),
            CancellationToken.None);

        Assert.True(result.Success);
        Assert.Equal(1, result.Snapshot.HomeMinSwitch);
        Assert.Equal(0, result.Snapshot.HomeMaxSwitch);
        Assert.Equal(1, result.Snapshot.HomeMinDirection);
        Assert.Equal(0, result.Snapshot.HomeMaxDirection);
        Assert.True(result.Snapshot.StepperDirectionInverted);
        Assert.Contains(commandClient.Sent, command => command.Command == "HOMECFG 1 0 1 0 1");
    }

    [Fact]
    public async Task WriteConfigRejectsInvalidHomingConfig()
    {
        var service = new ConfiguratorService();

        var sameSwitch = await service.WriteConfigAsync(
            new ConfiguratorWriteConfigRequest(1, 250, 0, 90, 100, 0, 0, 0, 1, false),
            CancellationToken.None);
        var badDirection = await service.WriteConfigAsync(
            new ConfiguratorWriteConfigRequest(1, 250, 0, 90, 100, 0, 1, 2, 1, false),
            CancellationToken.None);

        Assert.False(sameSwitch.Success);
        Assert.Contains("Homing", sameSwitch.Error, StringComparison.OrdinalIgnoreCase);
        Assert.False(badDirection.Success);
        Assert.Contains("Homing", badDirection.Error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task WriteConfigReportsFirmwareRejectedSoftLimitsAsPartialFailure()
    {
        var commandClient = new FakeCommandClient
        {
            Responses =
            {
                ["SOFTMIN_DEG 10"] = ["Motor nicht bereit."]
            }
        };
        var service = new ConfiguratorService(
            new FakeDiscovery([
                new DiscoveredController(1, "USB Serial COM10", "pico-live", 250, "COM10")
            ]),
            commandClient: commandClient);
        await service.ScanAsync(CancellationToken.None);

        var result = await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(1, 500, 10, 80, 64), CancellationToken.None);

        Assert.False(result.Success);
        Assert.Equal("Konfiguration teilweise geschrieben", result.Message);
        Assert.Contains("SOFTMIN_DEG 10", result.Error, StringComparison.Ordinal);
        Assert.Equal(500, result.Snapshot.SafePositionPromille);
        Assert.Equal(64, result.Snapshot.StallGuardThreshold);
        Assert.Equal(0, result.Snapshot.SoftMinDegree);
        Assert.Equal(90, result.Snapshot.SoftMaxDegree);
        Assert.Contains(result.Snapshot.Log, line => line.Contains("< Motor nicht bereit.", StringComparison.Ordinal));
    }

    [Fact]
    public async Task SafeCommandSetsSafeValueButDoesNotPretendToMoveTheFlap()
    {
        var service = new ConfiguratorService();
        await service.ScanAsync(CancellationToken.None);
        await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(1, 320), CancellationToken.None);

        var result = await service.RunCommandAsync("safe", CancellationToken.None);

        Assert.True(result.Success);
        Assert.Contains(result.Snapshot.Log, line => line.Contains("SAFE 320", StringComparison.Ordinal));
        Assert.DoesNotContain(result.Snapshot.Log, line => line.Contains("GOTO", StringComparison.OrdinalIgnoreCase));
        Assert.Contains("gesetzt", result.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task StepTestCommandSendsRawStepperPulseTestThroughUsb()
    {
        var commandClient = new FakeCommandClient();
        var service = new ConfiguratorService(
            new FakeDiscovery([
                new DiscoveredController(1, "USB Serial COM10", "pico-live", 250, "COM10")
            ]),
            commandClient: commandClient);
        await service.ScanAsync(CancellationToken.None);

        var result = await service.RunCommandAsync("step-test", CancellationToken.None);

        Assert.True(result.Success);
        var sent = Assert.Single(commandClient.Sent);
        Assert.Equal("COM10", sent.PortName);
        Assert.Equal("STEPTEST 800", sent.Command);
        Assert.Contains(result.Snapshot.Log, line => line.Contains("STEPTEST 800", StringComparison.Ordinal));
    }

    [Fact]
    public async Task RefreshMachineCommandLogsRefreshWithoutControllerReboot()
    {
        var service = new ConfiguratorService();
        await service.ScanAsync(CancellationToken.None);
        await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(9, 320), CancellationToken.None);

        var result = await service.RunCommandAsync("refresh-machine", CancellationToken.None);

        Assert.True(result.Success);
        Assert.Equal("Machine Refresh gestartet", result.Message);
        Assert.Equal("Machine Refresh gestartet", result.Snapshot.LastEvent);
        Assert.Contains(result.Snapshot.Log, line => line.Contains("ID9 REFRESH", StringComparison.Ordinal));
        Assert.DoesNotContain(result.Snapshot.Log, line => line.Contains("RESET", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public async Task MovementCommandReportsFirmwareNotReadyAsFailure()
    {
        var commandClient = new FakeCommandClient
        {
            Responses =
            {
                ["ID1 GOTO_DEG 0"] = ["Motor nicht bereit."]
            }
        };
        var service = new ConfiguratorService(
            new FakeDiscovery([
                new DiscoveredController(1, "USB Serial COM10", "pico-live", 250, "COM10")
            ]),
            commandClient: commandClient);
        await service.ScanAsync(CancellationToken.None);

        var result = await service.RunCommandAsync("open", CancellationToken.None);

        Assert.False(result.Success);
        Assert.Contains("Motor nicht bereit", result.Error, StringComparison.Ordinal);
        Assert.Contains(result.Snapshot.Log, line => line.Contains("ID1 GOTO_DEG 0", StringComparison.Ordinal));
    }

    [Fact]
    public async Task GatewayFirmwareAndExportActionsUpdateSnapshot()
    {
        var service = new ConfiguratorService();
        await service.ScanAsync(CancellationToken.None);

        var gateway = await service.SetGatewayAsync(true, CancellationToken.None);
        var firmware = await service.CheckFirmwareAsync(CancellationToken.None);
        var exports = await service.ExportAsync(CancellationToken.None);

        Assert.True(gateway.Snapshot.GatewayRunning);
        Assert.Contains("UF2", firmware.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Loxone", exports.Snapshot.Exports);
        Assert.Contains("Home Assistant", exports.Snapshot.Exports);
        Assert.Contains("openHAB", exports.Snapshot.Exports);
        Assert.Contains("Modbus TCP", exports.Snapshot.Exports);
    }

    [Fact]
    public async Task FirmwareCheckReportsMissingUf2DriveAsVisibleFailure()
    {
        var service = new ConfiguratorService(
            firmwareStatusProvider: new FakeFirmwareStatusProvider(new FirmwareStatus(false, "Normalbetrieb", null, "UF2 Laufwerk nicht gefunden.")));

        var result = await service.CheckFirmwareAsync(CancellationToken.None);

        Assert.False(result.Success);
        Assert.Equal("UF2 Laufwerk nicht gefunden.", result.Error);
        Assert.False(result.Snapshot.Firmware.IsUf2DrivePresent);
        Assert.Equal("Normalbetrieb", result.Snapshot.Firmware.Mode);
    }

    [Fact]
    public async Task FirmwareFlashCopiesUf2AndUpdatesSnapshot()
    {
        await using var content = new MemoryStream([0x55, 0x46, 0x32]);
        var flasher = new FakeFirmwareFlasher(new FirmwareFlashResult("E:\\", "fanflap.uf2", 3));
        var service = new ConfiguratorService(firmwareFlasher: flasher);

        var result = await service.FlashFirmwareAsync(
            new FirmwareFlashRequest("fanflap.uf2", content),
            CancellationToken.None);

        Assert.True(result.Success);
        Assert.Equal("fanflap.uf2", flasher.FileName);
        Assert.True(flasher.BytesRead > 0);
        Assert.Equal("UF2 Bootloader", result.Snapshot.Firmware.Mode);
        Assert.Equal("E:\\", result.Snapshot.Firmware.DriveRoot);
        Assert.Contains(result.Snapshot.Log, line => line.Contains("fanflap.uf2", StringComparison.Ordinal));
    }

    [Fact]
    public async Task FirmwareFlashReportsValidationErrorsWithoutThrowing()
    {
        var service = new ConfiguratorService(firmwareFlasher: new ThrowingFirmwareFlasher(new InvalidDataException("A readable UF2 file is required.")));

        var result = await service.FlashFirmwareAsync(
            new FirmwareFlashRequest("firmware.bin", new MemoryStream([0])),
            CancellationToken.None);

        Assert.False(result.Success);
        Assert.Equal("UF2 Fehler", result.Snapshot.Firmware.Mode);
        Assert.Contains("UF2", result.Error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task ExportWritesArtifactsToConfiguredFolderAndReportsPaths()
    {
        var exportDirectory = Path.Combine(Path.GetTempPath(), "luefter-configurator-tests", Guid.NewGuid().ToString("N"));
        var writer = new FileExportArtifactWriter(
            exportDirectory,
            new ControllerProfile("demo", "Demo", "1.0", [TransportKind.UsbText], [], []),
            [new FakeExportAdapter("demo", new ExportArtifact("demo.txt", "payload", "text/plain"))]);
        var service = new ConfiguratorService(exportArtifactWriter: writer);
        await service.ScanAsync(CancellationToken.None);
        await service.WriteConfigAsync(new ConfiguratorWriteConfigRequest(7, 250), CancellationToken.None);

        var result = await service.ExportAsync(CancellationToken.None);

        Assert.True(result.Success);
        Assert.Equal(exportDirectory, result.Snapshot.ExportDirectory);
        var exportedFile = Assert.Single(result.Snapshot.ExportFiles);
        Assert.Equal("demo.txt", exportedFile.FileName);
        Assert.Equal("demo", exportedFile.AdapterId);
        Assert.True(File.Exists(exportedFile.FullPath));
        Assert.Equal("payload", await File.ReadAllTextAsync(exportedFile.FullPath));
    }

    private sealed class FakeDiscovery(IReadOnlyList<DiscoveredController> controllers) : IControllerDiscovery
    {
        public Task<IReadOnlyList<DiscoveredController>> ScanAsync(CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(controllers);
        }
    }

    private sealed class FakeCommandClient : IControllerCommandClient
    {
        public List<(string? PortName, string Command, TimeSpan Timeout)> Sent { get; } = [];

        public Dictionary<string, IReadOnlyList<string>> Responses { get; } = [];

        public Task<ControllerCommandResult> SendAsync(
            string? portName,
            string command,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Sent.Add((portName, command, timeout));
            var response = Responses.TryGetValue(command, out var configured)
                ? configured
                : [$"ACK {command}"];
            return Task.FromResult(new ControllerCommandResult(command, response, false));
        }
    }

    private sealed class FakeFirmwareStatusProvider(FirmwareStatus status) : IFirmwareStatusProvider
    {
        public Task<FirmwareStatus> CheckAsync(CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(status);
        }
    }

    private sealed class FakeFirmwareFlasher(FirmwareFlashResult result) : IFirmwareFlasher
    {
        public string? FileName { get; private set; }

        public long BytesRead { get; private set; }

        public async Task<FirmwareFlashResult> FlashAsync(string fileName, Stream content, CancellationToken cancellationToken)
        {
            FileName = fileName;
            var buffer = new byte[128];
            while (true)
            {
                var read = await content.ReadAsync(buffer, cancellationToken);
                if (read == 0)
                {
                    break;
                }

                BytesRead += read;
            }

            return result;
        }
    }

    private sealed class ThrowingFirmwareFlasher(Exception exception) : IFirmwareFlasher
    {
        public Task<FirmwareFlashResult> FlashAsync(string fileName, Stream content, CancellationToken cancellationToken)
            => Task.FromException<FirmwareFlashResult>(exception);
    }

    private sealed class FakeExportAdapter(string id, ExportArtifact artifact) : IHomeAutomationExportAdapter
    {
        public string Id { get; } = id;

        public ExportArtifact Export(ControllerProfile profile, byte deviceId) => artifact;
    }
}
