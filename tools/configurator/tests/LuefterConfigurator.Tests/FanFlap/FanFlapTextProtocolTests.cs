using LuefterConfigurator.Profiles.FanFlap;

namespace LuefterConfigurator.Tests.FanFlap;

public sealed class FanFlapTextProtocolTests
{
    [Fact]
    public void ParsesIdResponse()
    {
        Assert.Equal(17, FanFlapTextProtocol.ParseDeviceId("ID: 17"));
    }

    [Fact]
    public void RejectsMalformedIdResponse()
    {
        Assert.Throws<FormatException>(() => FanFlapTextProtocol.ParseDeviceId("ID = x"));
    }

    [Fact]
    public void BuildsAddressedCommand()
    {
        Assert.Equal("ID7 SAFE?", FanFlapTextProtocol.AddressCommand(7, "SAFE?"));
    }
}
