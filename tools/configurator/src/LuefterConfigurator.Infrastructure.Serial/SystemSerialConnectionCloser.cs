using System.Collections.Concurrent;
using System.IO;
using System.IO.Ports;
using LuefterConfigurator.Application;

namespace LuefterConfigurator.Infrastructure.Serial;

public sealed class SystemSerialConnectionCloser : IControllerConnectionCloser
{
    private readonly ConcurrentDictionary<SerialPort, byte> activePorts = [];

    public int ActiveCount => activePorts.Count;

    public IDisposable Track(SerialPort serialPort)
    {
        activePorts.TryAdd(serialPort, 0);
        return new Lease(this, serialPort);
    }

    public void CloseActiveConnections()
    {
        foreach (var serialPort in activePorts.Keys)
        {
            ClosePort(serialPort);
            activePorts.TryRemove(serialPort, out _);
        }
    }

    private static void ClosePort(SerialPort serialPort)
    {
        try
        {
            if (serialPort.IsOpen)
            {
                TryDiscardBuffers(serialPort);
                serialPort.Close();
            }
        }
        catch (IOException)
        {
        }
        catch (InvalidOperationException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    private static void TryDiscardBuffers(SerialPort serialPort)
    {
        try
        {
            serialPort.DiscardOutBuffer();
            serialPort.DiscardInBuffer();
        }
        catch (IOException)
        {
        }
        catch (InvalidOperationException)
        {
        }
    }

    private sealed class Lease(SystemSerialConnectionCloser owner, SerialPort serialPort) : IDisposable
    {
        public void Dispose() => owner.activePorts.TryRemove(serialPort, out _);
    }
}
