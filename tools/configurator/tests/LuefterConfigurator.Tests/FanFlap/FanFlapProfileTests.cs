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
        Assert.Contains(profile.Settings, setting => setting.Key == "auto.home.interval.minutes");
        Assert.Contains(profile.Settings, setting => setting.Key == "motor.normal.speed");
        Assert.Contains(profile.Settings, setting => setting.Key == "motor.homing.speed");
        Assert.Contains(profile.Settings, setting => setting.Key == "motor.run.current.ma");
        Assert.Contains(profile.Settings, setting => setting.Key == "home.min.switch");
        Assert.Contains(profile.Settings, setting => setting.Key == "stepper.direction.inverted");
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
        Assert.Contains(profile.Registers, register => register.Name == "home_min_switch" && register.Address == 28);
        Assert.Contains(profile.Registers, register => register.Name == "stepper_direction_inverted" && register.Address == 32);
        Assert.Contains(profile.Registers, register => register.Name == "normal_max_speed_steps_per_second" && register.Address == 33);
        Assert.Contains(profile.Registers, register => register.Name == "homing_max_speed_steps_per_second" && register.Address == 34);
        Assert.Contains(profile.Registers, register => register.Name == "run_current_milliamps" && register.Address == 35);
        Assert.Contains(profile.Registers, register => register.Name == "auto_home_interval_minutes" && register.Address == 36);
        Assert.Contains(profile.Registers, register => register.Name == "firmware_protocol_version" && register.Address == 22);
        Assert.Equal(37, profile.Registers.Count);
    }
}
