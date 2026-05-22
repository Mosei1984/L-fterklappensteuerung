namespace LuefterConfigurator.Domain;

public enum RegisterKind
{
    Coil,
    DiscreteInput,
    Holding,
    Input
}

public enum RegisterValueType
{
    Unsigned16,
    Signed16,
    Unsigned32,
    Signed32,
    Boolean
}

public enum RegisterAccess
{
    ReadOnly,
    WriteOnly,
    ReadWrite
}

public sealed record RegisterDefinition(
    RegisterKind Kind,
    ushort Address,
    string Name,
    RegisterValueType ValueType,
    RegisterAccess Access,
    string Description = "")
{
    public static RegisterDefinition Holding(
        ushort address,
        string name,
        RegisterValueType type,
        RegisterAccess access,
        string description = "")
        => new(RegisterKind.Holding, address, name, type, access, description);
}
