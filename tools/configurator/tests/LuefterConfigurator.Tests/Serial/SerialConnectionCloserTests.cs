using System.IO.Ports;
using LuefterConfigurator.Infrastructure.Serial;

namespace LuefterConfigurator.Tests.Serial;

public sealed class SerialConnectionCloserTests
{
    [Fact]
    public void CloseActiveConnectionsReleasesTrackedSerialPorts()
    {
        var closer = new SystemSerialConnectionCloser();
        using var serialPort = new SerialPort("COM_DOES_NOT_EXIST");
        using var lease = closer.Track(serialPort);

        Assert.Equal(1, closer.ActiveCount);

        closer.CloseActiveConnections();

        Assert.Equal(0, closer.ActiveCount);
    }
}
