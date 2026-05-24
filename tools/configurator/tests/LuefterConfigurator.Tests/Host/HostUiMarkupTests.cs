namespace LuefterConfigurator.Tests.Host;

public sealed class HostUiMarkupTests
{
    [Fact]
    public void IndexPageContainsModernServiceToolSections()
    {
        var indexPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Pages", "Index.cshtml");
        var markup = File.ReadAllText(indexPath);

        Assert.Contains("command-center", markup);
        Assert.Contains("protocol-strip", markup);
        Assert.Contains("safety-timeline", markup);
        Assert.Contains("adapter-matrix", markup);
        Assert.Contains("data-action=\"firmware-flash\"", markup, StringComparison.Ordinal);
        Assert.Contains("data-action=\"refresh-machine\"", markup, StringComparison.Ordinal);
        Assert.Contains("Fehler neu starten", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("accept=\".uf2\"", markup, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void IndexPageContainsVisibleShutdownAction()
    {
        var indexPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Pages", "Index.cshtml");
        var scriptPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "wwwroot", "js", "site.js");

        var markup = File.ReadAllText(indexPath);
        var script = File.ReadAllText(scriptPath);

        Assert.Contains("data-action=\"shutdown\"", markup, StringComparison.Ordinal);
        Assert.Contains("data-shutdown-dialog", markup, StringComparison.Ordinal);
        Assert.Contains("data-shutdown-choice=\"save\"", markup, StringComparison.Ordinal);
        Assert.Contains("data-shutdown-choice=\"discard\"", markup, StringComparison.Ordinal);
        Assert.Contains("data-shutdown-choice=\"cancel\"", markup, StringComparison.Ordinal);
        Assert.Contains("Beenden", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("/api/app/shutdown", script, StringComparison.Ordinal);
        Assert.Contains("requestShutdownChoice", script, StringComparison.Ordinal);
        Assert.Contains("Konfigurator wird beendet", script, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void IndexPageContainsRecognizableExportDownloadArea()
    {
        var indexPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Pages", "Index.cshtml");
        var markup = File.ReadAllText(indexPath);

        Assert.Contains("data-export-path", markup, StringComparison.Ordinal);
        Assert.Contains("data-export-files", markup, StringComparison.Ordinal);
        Assert.Contains("data-export-downloads", markup, StringComparison.Ordinal);
        Assert.Contains("href=\"/api/exports/", markup, StringComparison.Ordinal);
        Assert.Contains("MB_Luefterklappe_FanFlap_ID", markup, StringComparison.Ordinal);
        Assert.Contains(".json", markup, StringComparison.Ordinal);
    }

    [Fact]
    public void IndexPagePrioritizesLoxoneWizardAndSeparatesExpertHostTests()
    {
        var indexPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Pages", "Index.cshtml");
        var markup = File.ReadAllText(indexPath);

        Assert.Contains("setup-wizard", markup, StringComparison.Ordinal);
        Assert.Contains("wizard-step", markup, StringComparison.Ordinal);
        Assert.Contains("data-wizard-next", markup, StringComparison.Ordinal);
        Assert.Contains("data-wizard-back", markup, StringComparison.Ordinal);
        Assert.Contains("Loxone Setup", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Expertentest", markup, StringComparison.Ordinal);
        Assert.Contains("expert-test-column", markup, StringComparison.Ordinal);
        Assert.Contains("USB Host Test", markup, StringComparison.Ordinal);
        Assert.Contains("data-action=\"step-test\"", markup, StringComparison.Ordinal);
        Assert.Contains("Stepper testen", markup, StringComparison.Ordinal);
    }

    [Fact]
    public void IndexPageGuidesSetupWithConcreteWizardActions()
    {
        var indexPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Pages", "Index.cshtml");
        var scriptPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "wwwroot", "js", "site.js");

        var markup = File.ReadAllText(indexPath);
        var script = File.ReadAllText(scriptPath);

        Assert.Contains("data-wizard-action", markup, StringComparison.Ordinal);
        Assert.Contains("Pico suchen", script, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Werte schreiben", script, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Export erzeugen", script, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Abschluss pruefen", script, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("runWizardAction", script, StringComparison.Ordinal);
        Assert.DoesNotContain("Safe Position", script, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void IndexPageUsesPlainLanguageForDefaultSetupFlow()
    {
        var indexPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Pages", "Index.cshtml");
        var scriptPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "wwwroot", "js", "site.js");
        var markup = File.ReadAllText(indexPath);
        var script = File.ReadAllText(scriptPath);

        Assert.Contains("Luefterklappe Schritt fuer Schritt einrichten", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Pico suchen", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Sichere Stellung", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Min. Winkel", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Max. Winkel", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("StallGuard", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("data-soft-min-degree", markup, StringComparison.Ordinal);
        Assert.Contains("data-soft-max-degree", markup, StringComparison.Ordinal);
        Assert.Contains("data-stallguard-threshold", markup, StringComparison.Ordinal);
        Assert.Contains("data-normal-speed", markup, StringComparison.Ordinal);
        Assert.Contains("data-homing-speed", markup, StringComparison.Ordinal);
        Assert.Contains("data-run-current-ma", markup, StringComparison.Ordinal);
        Assert.Contains("data-home-min-switch", markup, StringComparison.Ordinal);
        Assert.Contains("data-home-max-switch", markup, StringComparison.Ordinal);
        Assert.Contains("data-home-min-direction", markup, StringComparison.Ordinal);
        Assert.Contains("data-home-max-direction", markup, StringComparison.Ordinal);
        Assert.Contains("data-stepper-direction-inverted", markup, StringComparison.Ordinal);
        Assert.Contains("Motorstrom", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("MOTORCFG", script, StringComparison.Ordinal);
        Assert.Contains("Fehler neu starten", markup, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("Refresh Machine", markup, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("Safe State", markup, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("Safe Position", markup, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("Soft Min Steps", markup, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("Soft Max Steps", markup, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void HostStylesWrapTopNavigationOnMobile()
    {
        var stylePath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "wwwroot", "css", "site.css");
        var styles = File.ReadAllText(stylePath);

        Assert.Contains("@media (max-width: 680px)", styles, StringComparison.Ordinal);
        Assert.Contains("grid-template-columns: repeat(2, minmax(0, 1fr));", styles, StringComparison.Ordinal);
        Assert.Contains("overflow: visible;", styles, StringComparison.Ordinal);
    }

    [Fact]
    public void IndexPageContainsValvePositionSimulation()
    {
        var indexPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Pages", "Index.cshtml");
        var scriptPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "wwwroot", "js", "site.js");
        var stylePath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "wwwroot", "css", "site.css");

        var markup = File.ReadAllText(indexPath);
        var script = File.ReadAllText(scriptPath);
        var styles = File.ReadAllText(stylePath);

        Assert.Contains("valve-simulation", markup, StringComparison.Ordinal);
        Assert.Contains("data-valve-blade", markup, StringComparison.Ordinal);
        Assert.Contains("data-valve-percent", markup, StringComparison.Ordinal);
        Assert.Contains("data-valve-label", markup, StringComparison.Ordinal);
        Assert.Contains("0% geschlossen", markup, StringComparison.Ordinal);
        Assert.Contains("100% offen", markup, StringComparison.Ordinal);
        Assert.Contains("Ventilweg 90 Grad", markup, StringComparison.Ordinal);
        Assert.Contains("updateValveSimulation", script, StringComparison.Ordinal);
        Assert.Contains("setValveSimulation", script, StringComparison.Ordinal);
        Assert.Contains("90 - (safePermille / 1000) * 90", script, StringComparison.Ordinal);
        Assert.Contains("--valve-angle", styles, StringComparison.Ordinal);
        Assert.Contains(".valve-blade", styles, StringComparison.Ordinal);
        Assert.Contains(".valve-scale", styles, StringComparison.Ordinal);
    }

    [Fact]
    public void HostLayoutDoesNotReferenceTemplateVendorLibraries()
    {
        var layoutPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Pages", "Shared", "_Layout.cshtml");
        var markup = File.ReadAllText(layoutPath);

        Assert.DoesNotContain("~/lib/bootstrap", markup, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("~/lib/jquery", markup, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void HostUsesVisibleBrandLogoAndRealFavicons()
    {
        var indexPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Pages", "Index.cshtml");
        var layoutPath = Path.Combine("..", "..", "..", "..", "..", "src", "LuefterConfigurator.Host", "Pages", "Shared", "_Layout.cshtml");

        var indexMarkup = File.ReadAllText(indexPath);
        var layoutMarkup = File.ReadAllText(layoutPath);

        Assert.Contains("brand-logo", indexMarkup, StringComparison.Ordinal);
        Assert.Contains("/brand/luefterklappen-logo-mark.svg", indexMarkup, StringComparison.Ordinal);
        Assert.Contains("/brand/luefterklappen-logo-mark.svg", layoutMarkup, StringComparison.Ordinal);
        Assert.Contains("/brand/luefterklappen-icon-256.png", layoutMarkup, StringComparison.Ordinal);
        Assert.DoesNotContain("data:,", layoutMarkup, StringComparison.Ordinal);
    }
}
