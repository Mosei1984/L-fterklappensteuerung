using System.IO.Ports;
using LuefterConfigurator.Application;

namespace LuefterConfigurator.Infrastructure.Serial;

public sealed class SystemSerialControllerCommandClient(SystemSerialConnectionCloser connectionCloser) : IControllerCommandClient
{
    private const int BaudRate = 115200;
    private static readonly TimeSpan OpenSettleDelay = TimeSpan.FromMilliseconds(300);
    private static readonly TimeSpan ReadIdleDelay = TimeSpan.FromMilliseconds(180);

    public async Task<ControllerCommandResult> SendAsync(
        string? portName,
        string command,
        TimeSpan timeout,
        CancellationToken cancellationToken)
        => await Task.Run(() => SendBlocking(portName, command, timeout, cancellationToken), cancellationToken);

    private ControllerCommandResult SendBlocking(
        string? portName,
        string command,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(portName))
        {
            throw new InvalidOperationException("Kein USB-Port aktiv. Bitte Pico suchen und erneut versuchen.");
        }

        using var serialPort = new SerialPort(portName, BaudRate, Parity.None, 8, StopBits.One)
        {
            DtrEnable = true,
            RtsEnable = false,
            NewLine = "\n",
            ReadTimeout = 100,
            WriteTimeout = 500
        };

        using var lease = connectionCloser.Track(serialPort);
        serialPort.Open();
        Thread.Sleep(OpenSettleDelay);
        cancellationToken.ThrowIfCancellationRequested();
        serialPort.DiscardInBuffer();
        serialPort.WriteLine(command);

        var lines = new List<string>();
        var deadline = DateTimeOffset.UtcNow + timeout;
        var lastLineAt = DateTimeOffset.MinValue;

        while (DateTimeOffset.UtcNow < deadline)
        {
            cancellationToken.ThrowIfCancellationRequested();

            try
            {
                var line = serialPort.ReadLine().Trim();
                if (line.Length == 0)
                {
                    continue;
                }

                lines.Add(line);
                lastLineAt = DateTimeOffset.UtcNow;
            }
            catch (TimeoutException)
            {
                if (lines.Count > 0 && DateTimeOffset.UtcNow - lastLineAt >= ReadIdleDelay)
                {
                    break;
                }
            }
        }

        return new ControllerCommandResult(command, lines, lines.Count == 0 && DateTimeOffset.UtcNow >= deadline);
    }
}
