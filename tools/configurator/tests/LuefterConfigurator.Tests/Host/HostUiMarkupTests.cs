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
        Assert.Contains("Refresh Machine", markup, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("accept=\".uf2\"", markup, StringComparison.OrdinalIgnoreCase);
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
