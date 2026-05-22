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
            SettingDefinition.Numeric("safe.position", "Safe Position", 0, 1000, "promille")
        ]);
}
