using LuefterConfigurator.Domain;
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
        Assert.Contains(profile.Settings, setting => setting.Key == "soft.min.degree");
        Assert.Contains(profile.Settings, setting => setting.Key == "soft.max.degree");
        Assert.Contains(profile.Settings, setting => setting.Key == "stallguard.threshold");
    }

    [Fact]
    public void BuiltInProfileContainsModbusRegisters()
    {
        var profile = FanFlapProfile.Create();

        Assert.Contains(profile.Registers, register => register.Name == "state");
        Assert.Contains(profile.Registers, register => register.Name == "position_promille");
        Assert.Contains(profile.Registers, register => register.Name == "safe_position_promille");
        Assert.Contains(profile.Registers, register => register.Name == "target_degree" && register.Address == 23);
        Assert.Contains(profile.Registers, register => register.Name == "current_degree" && register.Address == 24 && register.Access == RegisterAccess.ReadOnly);
        Assert.Contains(profile.Registers, register => register.Name == "soft_min_degree" && register.Address == 25 && register.Access == RegisterAccess.ReadWrite);
        Assert.Contains(profile.Registers, register => register.Name == "soft_max_degree" && register.Address == 26 && register.Access == RegisterAccess.ReadWrite);
        Assert.Contains(profile.Registers, register => register.Name == "stallguard_threshold" && register.Address == 27);
        Assert.Contains(profile.Registers, register => register.Name == "firmware_protocol_version" && register.Address == 22);
        Assert.Equal(28, profile.Registers.Count);
    }
}
