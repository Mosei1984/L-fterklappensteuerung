using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Profiles.FanFlap;

public static class FanFlapProfile
{
    public static ControllerProfile Create() => new(
        "fanflap",
        "Luefterklappensteuerung",
        "1.0",
        [TransportKind.UsbText, TransportKind.ModbusRtu, TransportKind.ModbusTcpGateway],
        FanFlapModbusMap.Registers,
        [
            SettingDefinition.Numeric("device.id", "Device ID", 1, 247, "id"),
            SettingDefinition.Numeric("safe.position", "Safe Position", 0, 1000, "promille"),
            SettingDefinition.Numeric("soft.min.degree", "Min Winkel", 0, 90, "degree"),
            SettingDefinition.Numeric("soft.max.degree", "Max Winkel", 0, 90, "degree"),
            SettingDefinition.Numeric("stallguard.threshold", "StallGuard Threshold", 0, 255, "sgthrs"),
            SettingDefinition.Numeric("motor.normal.speed", "Normalfahrt Speed", 20, 5000, "steps/s"),
            SettingDefinition.Numeric("motor.homing.speed", "Homing Speed", 20, 5000, "steps/s"),
            SettingDefinition.Numeric("motor.run.current.ma", "Motorstrom", 100, 1000, "mA"),
            SettingDefinition.Numeric("home.min.switch", "Home Min Switch", 0, 1, "0=min 1=max"),
            SettingDefinition.Numeric("home.max.switch", "Home Max Switch", 0, 1, "0=min 1=max"),
            SettingDefinition.Numeric("home.min.direction", "Home Min Richtung", 0, 1, "0=- 1=+"),
            SettingDefinition.Numeric("home.max.direction", "Home Max Richtung", 0, 1, "0=- 1=+"),
            SettingDefinition.Numeric("stepper.direction.inverted", "Stepper Richtung invertiert", 0, 1, "bool")
        ]);
}
