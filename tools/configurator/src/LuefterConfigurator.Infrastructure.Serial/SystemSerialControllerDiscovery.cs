using System.Globalization;
using System.IO.Ports;
using System.Text.RegularExpressions;
using LuefterConfigurator.Application;

namespace LuefterConfigurator.Infrastructure.Serial;

public sealed class SystemSerialControllerDiscovery : IControllerDiscovery
{
    private const int BaudRate = 115200;
    private static readonly TimeSpan ProbeBootDelay = TimeSpan.FromMilliseconds(1_800);
    private static readonly TimeSpan LineTimeout = TimeSpan.FromMilliseconds(700);

    public async Task<IReadOnlyList<DiscoveredController>> ScanAsync(CancellationToken cancellationToken)
        => await Task.Run(() => ScanPorts(cancellationToken), cancellationToken);

    private static List<DiscoveredController> ScanPorts(CancellationToken cancellationToken)
    {
        var controllers = new List<DiscoveredController>();

        foreach (var portName in SerialPort.GetPortNames().Order(StringComparer.OrdinalIgnoreCase))
        {
            cancellationToken.ThrowIfCancellationRequested();

            var controller = TryProbePort(portName, cancellationToken);
            if (controller is not null)
            {
                controllers.Add(controller);
            }
        }

        return controllers;
    }

    private static DiscoveredController? TryProbePort(string portName, CancellationToken cancellationToken)
    {
        try
        {
            using var serialPort = new SerialPort(portName, BaudRate, Parity.None, 8, StopBits.One)
            {
                DtrEnable = true,
                RtsEnable = false,
                NewLine = "\n",
                ReadTimeout = 100,
                WriteTimeout = 300
            };

            serialPort.Open();
            Thread.Sleep(ProbeBootDelay);
            cancellationToken.ThrowIfCancellationRequested();
            serialPort.DiscardInBuffer();

            var idLine = QueryLine(serialPort, "ID?", cancellationToken);
            var deviceId = ParseFirstInt(idLine);
            if (!deviceId.HasValue || deviceId.Value is < 1 or > 247)
            {
                return null;
            }

            var safeLine = QueryLine(serialPort, "SAFE?", cancellationToken);
            var safePosition = ParseFirstInt(safeLine);
            return new DiscoveredController(
                deviceId.Value,
                $"USB Serial {portName}",
                "pico-live",
                safePosition is >= 0 and <= 1000 ? safePosition : null,
                portName);
        }
        catch (Exception exception) when (IsRecoverableSerialError(exception))
        {
            return null;
        }
    }

    private static string QueryLine(SerialPort serialPort, string command, CancellationToken cancellationToken)
    {
        serialPort.WriteLine(command);
        var deadline = DateTimeOffset.UtcNow + LineTimeout;

        while (DateTimeOffset.UtcNow < deadline)
        {
            cancellationToken.ThrowIfCancellationRequested();

            try
            {
                var line = serialPort.ReadLine();
                if (!string.IsNullOrWhiteSpace(line))
                {
                    return line.Trim();
                }
            }
            catch (TimeoutException)
            {
            }
        }

        return string.Empty;
    }

    private static int? ParseFirstInt(string value)
    {
        var match = Regex.Match(value, @"-?\d+");
        return match.Success && int.TryParse(match.Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed)
            ? parsed
            : null;
    }

    private static bool IsRecoverableSerialError(Exception exception)
        => exception is IOException
            or InvalidOperationException
            or TimeoutException
            or UnauthorizedAccessException;
}
