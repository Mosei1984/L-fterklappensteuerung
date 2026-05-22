using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Application;

public sealed record ControllerSession(ControllerSessionId Id, ControllerIdentity Identity);

public sealed class MultiControllerSessionService
{
    private readonly List<ControllerSession> sessions = [];

    public Task<ControllerSessionId> AddAsync(ControllerIdentity identity)
    {
        var id = ControllerSessionId.New();
        sessions.Add(new ControllerSession(id, identity));
        return Task.FromResult(id);
    }

    public IReadOnlyList<ControllerSession> GetAll() => sessions.ToArray();

    public IReadOnlyList<string> GetGatewayBlockingProblems()
        => sessions
            .GroupBy(session => session.Identity.DeviceId)
            .Where(group => group.Count() > 1)
            .Select(group => $"duplicate device id {group.Key}")
            .ToArray();
}
