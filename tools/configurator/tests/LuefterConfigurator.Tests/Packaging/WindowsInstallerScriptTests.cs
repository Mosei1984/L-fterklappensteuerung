namespace LuefterConfigurator.Tests.Packaging;

public sealed class WindowsInstallerScriptTests
{
    [Fact]
    public void WindowsInstallerBundleContainsInstallUninstallEulaAndStatusContracts()
    {
        var root = FindRepoRoot();
        var eulaPath = Path.Combine(root, "tools", "configurator", "EULA.md");
        var buildScriptPath = Path.Combine(root, "tools", "configurator", "build-windows-installer.ps1");
        var installScriptPath = Path.Combine(root, "tools", "configurator", "install-windows.ps1");
        var uninstallScriptPath = Path.Combine(root, "tools", "configurator", "uninstall-windows.ps1");

        Assert.True(File.Exists(eulaPath), "EULA.md is required for an end-user installable package.");
        Assert.True(File.Exists(buildScriptPath), "build-windows-installer.ps1 is required.");
        Assert.True(File.Exists(installScriptPath), "install-windows.ps1 is required.");
        Assert.True(File.Exists(uninstallScriptPath), "uninstall-windows.ps1 is required.");

        var eula = File.ReadAllText(eulaPath);
        var buildScript = File.ReadAllText(buildScriptPath);
        var installScript = File.ReadAllText(installScriptPath);
        var uninstallScript = File.ReadAllText(uninstallScriptPath);

        Assert.Contains("Endbenutzer", eula, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Luefterklappen Konfigurator", eula, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("publish-portable.ps1", buildScript, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("install-windows.ps1", buildScript, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("uninstall-windows.ps1", buildScript, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("EULA.md", buildScript, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("INSTALL_STATUS.json", installScript, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(@"HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall", installScript, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Start Menu", installScript, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("INSTALL_STATUS.json", uninstallScript, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(@"HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall", uninstallScript, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Remove-Item", uninstallScript, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void WindowsInstallerBundleContainsApplicationIconContract()
    {
        var root = FindRepoRoot();
        var iconPath = Path.Combine(root, "tools", "configurator", "src", "LuefterConfigurator.Host", "Assets", "logo", "luefterklappen.ico");
        var svgPath = Path.Combine(root, "tools", "configurator", "src", "LuefterConfigurator.Host", "wwwroot", "brand", "luefterklappen-logo-mark.svg");
        var pngPath = Path.Combine(root, "tools", "configurator", "src", "LuefterConfigurator.Host", "wwwroot", "brand", "luefterklappen-icon-256.png");
        var projectPath = Path.Combine(root, "tools", "configurator", "src", "LuefterConfigurator.Host", "LuefterConfigurator.Host.csproj");
        var buildScriptPath = Path.Combine(root, "tools", "configurator", "build-windows-installer.ps1");
        var installScriptPath = Path.Combine(root, "tools", "configurator", "install-windows.ps1");

        Assert.True(File.Exists(iconPath), "Windows .ico is required for Start Menu and folder visibility.");
        Assert.True(File.Exists(svgPath), "SVG mark is required for the browser UI.");
        Assert.True(File.Exists(pngPath), "PNG icon is required for browser and packaged asset visibility.");

        var project = File.ReadAllText(projectPath);
        var buildScript = File.ReadAllText(buildScriptPath);
        var installScript = File.ReadAllText(installScriptPath);

        Assert.Contains("<ApplicationIcon>Assets\\logo\\luefterklappen.ico</ApplicationIcon>", project, StringComparison.Ordinal);
        Assert.Contains("Luefterklappen-Konfigurator.ico", project, StringComparison.Ordinal);
        Assert.Contains("src\\LuefterConfigurator.Host\\Assets\\logo\\luefterklappen.ico", buildScript, StringComparison.Ordinal);
        Assert.Contains("Luefterklappen-Konfigurator.ico", buildScript, StringComparison.Ordinal);
        Assert.Contains("IconLocation", installScript, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Luefterklappen-Konfigurator.ico", installScript, StringComparison.Ordinal);
    }

    private static string FindRepoRoot()
    {
        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "platformio.ini")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Repository root with platformio.ini was not found.");
    }
}
