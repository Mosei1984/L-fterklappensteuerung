using LuefterConfigurator.Profiles.FanFlap;

namespace LuefterConfigurator.Tests.FanFlap;

public sealed class FanFlapProfileTests
{
    [Fact]
    public void BuiltInProfileContainsRequiredSettings()
    {
        var profile = FanFlapProfile.Create();

        Assert.True(profile.Validate().IsValid);
        Assert.Contains(profile.Settings, setting => setting.Key == "device.id");
        Assert.Contains(profile.Settings, setting => setting.Key == "safe.position");
    }

    [Fact]
    public void BuiltInProfileContainsModbusRegisters()
    {
        var profile = FanFlapProfile.Create();

        Assert.Contains(profile.Registers, register => register.Name == "state");
        Assert.Contains(profile.Registers, register => register.Name == "position_promille");
        Assert.Contains(profile.Registers, register => register.Name == "safe_position_promille");
    }
}
