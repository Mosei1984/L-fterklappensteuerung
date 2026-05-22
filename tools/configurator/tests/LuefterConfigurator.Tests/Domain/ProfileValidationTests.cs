using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Tests.Domain;

public sealed class ProfileValidationTests
{
    [Fact]
    public void DeviceIdSettingAcceptsOnlyModbusSlaveRange()
    {
        var setting = SettingDefinition.Numeric("device.id", "Device ID", 1, 247, "id");

        Assert.True(setting.Validate(1).IsValid);
        Assert.True(setting.Validate(247).IsValid);
        Assert.False(setting.Validate(0).IsValid);
        Assert.False(setting.Validate(248).IsValid);
    }

    [Fact]
    public void SafePositionAcceptsOnlyPromilleRange()
    {
        var setting = SettingDefinition.Numeric("safe.position", "Safe Position", 0, 1000, "promille");

        Assert.True(setting.Validate(0).IsValid);
        Assert.True(setting.Validate(1000).IsValid);
        Assert.False(setting.Validate(-1).IsValid);
        Assert.False(setting.Validate(1001).IsValid);
    }

    [Fact]
    public void ProfileRejectsDuplicateRegisters()
    {
        var profile = new ControllerProfile(
            "fanflap",
            "Fan Flap",
            "1.0",
            [TransportKind.UsbText],
            [
                RegisterDefinition.Holding(0, "state", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly),
                RegisterDefinition.Holding(0, "state_copy", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly)
            ],
            []);

        Assert.Contains(profile.Validate().Errors, error => error.Contains("Duplicate register address 0", StringComparison.Ordinal));
    }
}
