namespace LuefterConfigurator.Infrastructure.Serial;

public interface ISerialConnection
{
    Task WriteLineAsync(string line, CancellationToken cancellationToken);

    Task<string> ReadLineAsync(TimeSpan timeout, CancellationToken cancellationToken);
}
