namespace LuefterConfigurator.Tests.Host;

public sealed class HostSmokeTests
{
    [Fact]
    public void HostProjectMarkerExists()
    {
        var path = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Program.cs");

        Assert.True(File.Exists(path), path);
    }

    [Fact]
    public void HostDoesNotOpenExternalBrowserOnStartup()
    {
        var path = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Program.cs");
        var source = File.ReadAllText(path);

        Assert.DoesNotContain("ApplicationStarted.Register", source, StringComparison.Ordinal);
        Assert.DoesNotContain("OpenBrowser", source, StringComparison.Ordinal);
        Assert.DoesNotContain("UseShellExecute = true", source, StringComparison.Ordinal);
    }

    [Fact]
    public void DesktopLauncherHostsUiInNativeWindowsWindow()
    {
        var projectPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Desktop", "LuefterConfigurator.Desktop.csproj");
        var formPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Desktop", "MainForm.cs");

        Assert.True(File.Exists(projectPath), projectPath);
        Assert.True(File.Exists(formPath), formPath);

        var project = File.ReadAllText(projectPath);
        var form = File.ReadAllText(formPath);

        Assert.Contains("<UseWindowsForms>true</UseWindowsForms>", project, StringComparison.Ordinal);
        Assert.Contains("Microsoft.Web.WebView2", project, StringComparison.Ordinal);
        Assert.Contains("WebView2", form, StringComparison.Ordinal);
        Assert.Contains("LuefterConfigurator.Host.exe", form, StringComparison.Ordinal);
        Assert.Contains("LUEFTER_CONFIGURATOR_NO_BROWSER", form, StringComparison.Ordinal);
        Assert.Contains("CreateNoWindow = true", form, StringComparison.Ordinal);
        Assert.Contains("EnsureCoreWebView2Async", form, StringComparison.Ordinal);
    }
}
