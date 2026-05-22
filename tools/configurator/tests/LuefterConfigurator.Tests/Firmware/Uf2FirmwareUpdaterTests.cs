using LuefterConfigurator.Infrastructure.Firmware;

namespace LuefterConfigurator.Tests.Firmware;

public sealed class Uf2FirmwareUpdaterTests
{
    [Fact]
    public async Task CopiesUf2ToDetectedDrive()
    {
        using var temp = new TemporaryDirectory();
        var drive = Path.Combine(temp.Path, "RPI-RP2");
        Directory.CreateDirectory(drive);
        await File.WriteAllTextAsync(Path.Combine(drive, "INFO_UF2.TXT"), "UF2 Bootloader");

        var source = Path.Combine(temp.Path, "firmware.uf2");
        await File.WriteAllBytesAsync(source, [0x55, 0x46, 0x32]);

        var updater = new PicoUf2FirmwareUpdater(new Uf2DriveDetector([drive]));
        await updater.CopyAsync(source, CancellationToken.None);

        Assert.True(File.Exists(Path.Combine(drive, "firmware.uf2")));
    }

    [Fact]
    public async Task RejectsNonUf2File()
    {
        using var temp = new TemporaryDirectory();
        var source = Path.Combine(temp.Path, "firmware.bin");
        await File.WriteAllBytesAsync(source, [0]);

        var updater = new PicoUf2FirmwareUpdater(new Uf2DriveDetector([temp.Path]));

        var exception = await Assert.ThrowsAsync<InvalidDataException>(() => updater.CopyAsync(source, CancellationToken.None));
        Assert.Contains("UF2", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task FlashUploadUsesSafeFileNameAndReportsBytes()
    {
        using var temp = new TemporaryDirectory();
        var drive = Path.Combine(temp.Path, "RPI-RP2");
        Directory.CreateDirectory(drive);
        await File.WriteAllTextAsync(Path.Combine(drive, "INFO_UF2.TXT"), "UF2 Bootloader");
        await using var source = new MemoryStream([0x55, 0x46, 0x32, 0x0A]);

        var updater = new PicoUf2FirmwareUpdater(new Uf2DriveDetector([drive]));
        var result = await updater.FlashAsync(@"..\fanflap.uf2", source, CancellationToken.None);

        Assert.Equal("fanflap.uf2", result.FileName);
        Assert.Equal(4, result.BytesWritten);
        Assert.True(File.Exists(Path.Combine(drive, "fanflap.uf2")));
        Assert.False(File.Exists(Path.Combine(temp.Path, "fanflap.uf2")));
    }

    private sealed class TemporaryDirectory : IDisposable
    {
        public string Path { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), Guid.NewGuid().ToString("N"));

        public TemporaryDirectory() => Directory.CreateDirectory(Path);

        public void Dispose() => Directory.Delete(Path, recursive: true);
    }
}
