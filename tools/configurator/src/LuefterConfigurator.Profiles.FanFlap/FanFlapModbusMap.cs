using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Profiles.FanFlap;

public static class FanFlapModbusMap
{
    public static IReadOnlyList<RegisterDefinition> Registers { get; } =
    [
        RegisterDefinition.Holding(0, "command", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "0 none, 1 home, 2 reset, 3 soft endstops on, 4 off, 5 refresh machine"),
        RegisterDefinition.Holding(1, "target_steps_hi", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Target steps int32 high word"),
        RegisterDefinition.Holding(2, "target_steps_lo", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Target steps int32 low word"),
        RegisterDefinition.Holding(3, "soft_min_hi", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Soft minimum int32 high word"),
        RegisterDefinition.Holding(4, "soft_min_lo", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Soft minimum int32 low word"),
        RegisterDefinition.Holding(5, "soft_max_hi", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Soft maximum int32 high word"),
        RegisterDefinition.Holding(6, "soft_max_lo", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Soft maximum int32 low word"),
        RegisterDefinition.Holding(7, "device_id", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Persisted Modbus slave id 1..247"),
        RegisterDefinition.Holding(8, "state", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Controller state enum"),
        RegisterDefinition.Holding(9, "flags", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Bit0 ready, bit1 fault, bit2 soft endstops active"),
        RegisterDefinition.Holding(10, "current_steps_hi", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Current steps int32 high word"),
        RegisterDefinition.Holding(11, "current_steps_lo", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Current steps int32 low word"),
        RegisterDefinition.Holding(12, "homing_max_hi", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Homing maximum int32 high word"),
        RegisterDefinition.Holding(13, "homing_max_lo", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Homing maximum int32 low word"),
        RegisterDefinition.Holding(14, "target_promille", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Target position 0..1000 promille"),
        RegisterDefinition.Holding(15, "position_promille", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Current position 0..1000 promille"),
        RegisterDefinition.Holding(16, "safe_position_promille", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Persisted safe position 0..1000 promille")
    ];
}
