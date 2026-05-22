namespace LuefterConfigurator.Tests.Host;

public sealed class HostSmokeTests
{
    [Fact]
    public void HostProjectMarkerExists()
    {
        var path = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Program.cs");

        Assert.True(File.Exists(path), path);
    }
}
