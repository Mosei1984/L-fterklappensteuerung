namespace LuefterConfigurator.Domain;

public sealed record ControllerSessionId(string Value)
{
    public static ControllerSessionId New() => new(Guid.NewGuid().ToString("N"));
}

public sealed record ControllerIdentity(string ProfileId, byte DeviceId, string FirmwareVersion, string TransportName);
