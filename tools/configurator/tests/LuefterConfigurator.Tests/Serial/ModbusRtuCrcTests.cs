using LuefterConfigurator.Infrastructure.Serial;

namespace LuefterConfigurator.Tests.Serial;

public sealed class ModbusRtuCrcTests
{
    [Fact]
    public void ComputesKnownReadHoldingRegistersCrc()
    {
        var payload = Convert.FromHexString("010300000002");

        var crc = ModbusRtuCrc.Compute(payload);

        Assert.Equal(0x0BC4, crc);
    }

    [Fact]
    public void AppendsCrcLowByteFirst()
    {
        var frame = ModbusRtuCrc.Append(Convert.FromHexString("010300000002"));

        Assert.Equal("010300000002C40B", Convert.ToHexString(frame));
    }
}
