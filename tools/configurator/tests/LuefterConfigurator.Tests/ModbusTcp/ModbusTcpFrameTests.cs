using LuefterConfigurator.Infrastructure.ModbusTcp;

namespace LuefterConfigurator.Tests.ModbusTcp;

public sealed class ModbusTcpFrameTests
{
    [Fact]
    public void ParsesReadHoldingRegistersRequest()
    {
        var bytes = Convert.FromHexString("000100000006010300000002");

        var frame = ModbusTcpFrame.Parse(bytes);

        Assert.Equal(1, frame.TransactionId);
        Assert.Equal(1, frame.UnitId);
        Assert.Equal(3, frame.FunctionCode);
        Assert.Equal([0x03, 0x00, 0x00, 0x00, 0x02], frame.Pdu);
    }

    [Fact]
    public void BuildsExceptionResponse()
    {
        var response = ModbusTcpFrame.Exception(1, 1, 3, 2).ToArray();

        Assert.Equal("000100000003018302", Convert.ToHexString(response));
    }
}
