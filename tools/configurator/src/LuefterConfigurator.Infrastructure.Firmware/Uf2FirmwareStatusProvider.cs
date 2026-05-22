using LuefterConfigurator.Application;

namespace LuefterConfigurator.Infrastructure.Firmware;

public sealed class Uf2FirmwareStatusProvider(Uf2DriveDetector detector) : IFirmwareStatusProvider
{
    public Task<FirmwareStatus> CheckAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        try
        {
            var driveRoot = detector.Detect();
            return Task.FromResult(new FirmwareStatus(true, "UF2 Bootloader", driveRoot, null));
        }
        catch (DriveNotFoundException exception)
        {
            return Task.FromResult(new FirmwareStatus(false, "Normalbetrieb", null, exception.Message));
        }
        catch (IOException exception)
        {
            return Task.FromResult(new FirmwareStatus(false, "UF2 Fehler", null, exception.Message));
        }
        catch (UnauthorizedAccessException exception)
        {
            return Task.FromResult(new FirmwareStatus(false, "UF2 Fehler", null, exception.Message));
        }
    }
}
