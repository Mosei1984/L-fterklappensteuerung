namespace LuefterConfigurator.Tests.Documentation;

public sealed class AgentOnboardingDocumentationTests
{
    [Fact]
    public void RepositoryProvidesAgentOnboardingAndCurrentConfiguratorDocs()
    {
        var root = FindRepoRoot();
        var agentsPath = Path.Combine(root, "AGENTS.md");
        var readmePath = Path.Combine(root, "README.md");
        var configuratorReadmePath = Path.Combine(root, "tools", "configurator", "README.md");

        Assert.True(File.Exists(agentsPath), "AGENTS.md is required so new agents can orient quickly.");

        var agents = File.ReadAllText(agentsPath);
        var readme = File.ReadAllText(readmePath);
        var configuratorReadme = File.ReadAllText(configuratorReadmePath);

        Assert.Contains("Quality gate", agents, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("tools/run_quality_checks.ps1", agents, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("tools/configurator", agents, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("106 passing tests", agents, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Loxone Setup Wizard", readme, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Register `0..36`", readme, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("current_degree", readme, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Firmware-Protokollversion, aktuell `6`", readme, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Windows Installation", configuratorReadme, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Luefterklappen-Konfigurator.ico", configuratorReadme, StringComparison.OrdinalIgnoreCase);
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
