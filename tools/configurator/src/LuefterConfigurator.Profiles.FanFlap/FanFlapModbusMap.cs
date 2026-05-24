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
        RegisterDefinition.Holding(9, "flags", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Bit0 ready, bit1 fault, bit2 soft endstops active, bit3 moving"),
        RegisterDefinition.Holding(10, "current_steps_hi", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Current steps int32 high word"),
        RegisterDefinition.Holding(11, "current_steps_lo", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Current steps int32 low word"),
        RegisterDefinition.Holding(12, "homing_max_hi", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Homing maximum int32 high word"),
        RegisterDefinition.Holding(13, "homing_max_lo", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Homing maximum int32 low word"),
        RegisterDefinition.Holding(14, "target_promille", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Target position 0..1000 promille"),
        RegisterDefinition.Holding(15, "position_promille", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Current position 0..1000 promille"),
        RegisterDefinition.Holding(16, "safe_position_promille", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Persisted safe position 0..1000 promille"),
        RegisterDefinition.Holding(17, "last_fault_reason", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Stable fault reason enum"),
        RegisterDefinition.Holding(18, "fault_count", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Saturating fault counter"),
        RegisterDefinition.Holding(19, "settings_status", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Settings status code"),
        RegisterDefinition.Holding(20, "tmc_health", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "TMC health code"),
        RegisterDefinition.Holding(21, "boot_reason", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Boot reason code"),
        RegisterDefinition.Holding(22, "firmware_protocol_version", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Firmware protocol version"),
        RegisterDefinition.Holding(23, "target_degree", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Target angle 0 open to 90 closed"),
        RegisterDefinition.Holding(24, "current_degree", RegisterValueType.Unsigned16, RegisterAccess.ReadOnly, "Current angle 0 open to 90 closed"),
        RegisterDefinition.Holding(25, "soft_min_degree", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Minimum allowed angle in degrees"),
        RegisterDefinition.Holding(26, "soft_max_degree", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Maximum allowed angle in degrees"),
        RegisterDefinition.Holding(27, "stallguard_threshold", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Persisted TMC2209 SGTHRS threshold 0..255"),
        RegisterDefinition.Holding(28, "home_min_switch", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Logical min endpoint switch: 0 min input, 1 max input"),
        RegisterDefinition.Holding(29, "home_max_switch", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Logical max endpoint switch: 0 min input, 1 max input"),
        RegisterDefinition.Holding(30, "home_min_direction", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Home-min search direction: 0 negative, 1 positive"),
        RegisterDefinition.Holding(31, "home_max_direction", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Home-max search direction: 0 negative, 1 positive"),
        RegisterDefinition.Holding(32, "stepper_direction_inverted", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Invert stepper direction: 0 normal, 1 inverted"),
        RegisterDefinition.Holding(33, "normal_max_speed_steps_per_second", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Normal movement maximum speed in steps per second"),
        RegisterDefinition.Holding(34, "homing_max_speed_steps_per_second", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Homing movement maximum speed in steps per second"),
        RegisterDefinition.Holding(35, "run_current_milliamps", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "TMC2209 run current limit in milliamps"),
        RegisterDefinition.Holding(36, "auto_home_interval_minutes", RegisterValueType.Unsigned16, RegisterAccess.ReadWrite, "Periodic re-homing interval in minutes; 0 disables auto-home")
    ];
}
