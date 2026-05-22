namespace LuefterConfigurator.Infrastructure.Serial;

public sealed class UsbTextTransport(ISerialConnection connection)
{
    public async Task<string> SendCommandAsync(string command, CancellationToken cancellationToken)
    {
        await connection.WriteLineAsync(command, cancellationToken);
        return await connection.ReadLineAsync(TimeSpan.FromMilliseconds(500), cancellationToken);
    }
}
