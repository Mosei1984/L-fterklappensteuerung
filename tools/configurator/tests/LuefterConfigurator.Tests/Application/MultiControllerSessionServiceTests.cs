using LuefterConfigurator.Application;
using LuefterConfigurator.Domain;

namespace LuefterConfigurator.Tests.Application;

public sealed class MultiControllerSessionServiceTests
{
    [Fact]
    public async Task AddsIndependentSessions()
    {
        var service = new MultiControllerSessionService();

        var first = await service.AddAsync(new ControllerIdentity("fanflap", 1, "1.0", "COM10"));
        var second = await service.AddAsync(new ControllerIdentity("fanflap", 2, "1.0", "COM11"));

        Assert.NotEqual(first, second);
        Assert.Equal(2, service.GetAll().Count);
    }

    [Fact]
    public async Task DetectsDeviceIdCollision()
    {
        var service = new MultiControllerSessionService();

        await service.AddAsync(new ControllerIdentity("fanflap", 1, "1.0", "COM10"));
        await service.AddAsync(new ControllerIdentity("fanflap", 1, "1.0", "COM11"));

        Assert.Contains(service.GetGatewayBlockingProblems(), problem => problem.Contains("duplicate device id 1", StringComparison.Ordinal));
    }
}
