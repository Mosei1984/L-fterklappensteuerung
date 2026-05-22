using System.Diagnostics;
using Microsoft.Web.WebView2.WinForms;

namespace LuefterConfigurator.Desktop;

public sealed class MainForm : Form
{
    private const string LocalUrl = "http://127.0.0.1:5184";
    private readonly WebView2 webView = new() { Dock = DockStyle.Fill, Visible = false };
    private readonly Icon? applicationIcon = LoadApplicationIcon();
    private readonly Label statusLabel = new()
    {
        Dock = DockStyle.Fill,
        Font = new Font("Segoe UI", 11F),
        Text = "Luefterklappen Konfigurator wird gestartet...",
        TextAlign = ContentAlignment.MiddleCenter
    };
    private Process? hostProcess;
    private bool startedHost;

    public MainForm()
    {
        Text = "Luefterklappen Konfigurator";
        MinimumSize = new Size(1040, 720);
        Size = new Size(1280, 820);
        StartPosition = FormStartPosition.CenterScreen;
        if (applicationIcon is not null)
        {
            Icon = applicationIcon;
        }

        Controls.Add(webView);
        Controls.Add(statusLabel);
    }

    protected override async void OnShown(EventArgs e)
    {
        base.OnShown(e);

        try
        {
            await StartHostAndNavigateAsync();
        }
        catch (InvalidOperationException exception)
        {
            ShowStartupError(exception.Message);
        }
        catch (FileNotFoundException exception)
        {
            ShowStartupError(exception.Message);
        }
        catch (HttpRequestException exception)
        {
            ShowStartupError(exception.Message);
        }
        catch (TaskCanceledException exception)
        {
            ShowStartupError(exception.Message);
        }
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        base.OnFormClosing(e);

        if (hostProcess is null)
        {
            applicationIcon?.Dispose();
            return;
        }

        if (startedHost && !hostProcess.HasExited)
        {
            hostProcess.Kill(entireProcessTree: true);
        }

        hostProcess.Dispose();
        applicationIcon?.Dispose();
    }

    private async Task StartHostAndNavigateAsync()
    {
        if (!await IsHostHealthyAsync())
        {
            StartHostProcess();
            if (!await WaitForHostAsync())
            {
                throw new InvalidOperationException("Der lokale Konfigurator-Dienst konnte nicht gestartet werden.");
            }
        }

        statusLabel.Text = "Oberflaeche wird geladen...";
        await webView.EnsureCoreWebView2Async();
        webView.Source = new Uri(LocalUrl);
        webView.Visible = true;
        statusLabel.Visible = false;
    }

    private void StartHostProcess()
    {
        var hostPath = Path.Combine(AppContext.BaseDirectory, "LuefterConfigurator.Host.exe");
        if (!File.Exists(hostPath))
        {
            throw new FileNotFoundException("LuefterConfigurator.Host.exe wurde neben der Windows-App nicht gefunden.", hostPath);
        }

        var startInfo = new ProcessStartInfo
        {
            FileName = hostPath,
            WorkingDirectory = AppContext.BaseDirectory,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        startInfo.Environment["LUEFTER_CONFIGURATOR_NO_BROWSER"] = "1";

        hostProcess = Process.Start(startInfo)
            ?? throw new InvalidOperationException("LuefterConfigurator.Host.exe konnte nicht gestartet werden.");
        startedHost = true;
    }

    private static async Task<bool> WaitForHostAsync()
    {
        for (var attempt = 0; attempt < 40; attempt++)
        {
            if (await IsHostHealthyAsync())
            {
                return true;
            }

            await Task.Delay(250);
        }

        return false;
    }

    private static async Task<bool> IsHostHealthyAsync()
    {
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(2) };

        try
        {
            using var response = await client.GetAsync(new Uri($"{LocalUrl}/api/health"));
            return response.IsSuccessStatusCode;
        }
        catch (HttpRequestException)
        {
            return false;
        }
        catch (TaskCanceledException)
        {
            return false;
        }
    }

    private void ShowStartupError(string message)
    {
        statusLabel.Text = "Start fehlgeschlagen";
        _ = MessageBox.Show(
            this,
            $"{message}{Environment.NewLine}{Environment.NewLine}Falls WebView2 fehlt, installiere die Microsoft Edge WebView2 Runtime.",
            "Luefterklappen Konfigurator",
            MessageBoxButtons.OK,
            MessageBoxIcon.Error);
    }

    private static Icon? LoadApplicationIcon()
    {
        var iconPath = Path.Combine(AppContext.BaseDirectory, "Luefterklappen-Konfigurator.ico");
        return File.Exists(iconPath) ? new Icon(iconPath) : null;
    }
}
