using LuefterConfigurator.Application;

namespace LuefterConfigurator.Infrastructure.Firmware;

public sealed class PicoUf2FirmwareUpdater(Uf2DriveDetector detector) : IFirmwareFlasher
{
    public async Task CopyAsync(string uf2Path, CancellationToken cancellationToken)
    {
        if (!File.Exists(uf2Path))
        {
            throw new InvalidDataException("A readable UF2 file is required.");
        }

        await using var source = File.OpenRead(uf2Path);
        await FlashAsync(Path.GetFileName(uf2Path), source, cancellationToken);
    }

    public async Task<FirmwareFlashResult> FlashAsync(string fileName, Stream content, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var safeFileName = SafeUf2FileName(fileName, content);
        var drive = detector.Detect();
        var destination = Path.Combine(drive, safeFileName);
        await using var target = new FileStream(destination, FileMode.Create, FileAccess.Write, FileShare.None, 81920, useAsync: true);
        await content.CopyToAsync(target, cancellationToken);
        await target.FlushAsync(cancellationToken);

        if (target.Position == 0)
        {
            throw new InvalidDataException("The UF2 file is empty.");
        }

        return new FirmwareFlashResult(drive, safeFileName, target.Position);
    }

    private static string SafeUf2FileName(string fileName, Stream content)
    {
        var safeFileName = Path.GetFileName(fileName);
        if (string.IsNullOrWhiteSpace(safeFileName) ||
            !string.Equals(Path.GetExtension(safeFileName), ".uf2", StringComparison.OrdinalIgnoreCase) ||
            !content.CanRead)
        {
            throw new InvalidDataException("A readable UF2 file is required.");
        }

        return safeFileName;
    }
}
