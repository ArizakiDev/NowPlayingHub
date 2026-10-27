using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using Microsoft.Win32;

namespace NowPlayingHub;

public static class Autostart
{
    const string KeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    const string ValueName = "NowPlayingHub";

    public static bool Get()
    {
        try { using var k = Registry.CurrentUser.OpenSubKey(KeyPath); return k?.GetValue(ValueName) != null; }
        catch { return false; }
    }

    public static void Set(bool on)
    {
        try
        {
            using var k = Registry.CurrentUser.OpenSubKey(KeyPath, true);
            if (k == null) return;
            if (on) k.SetValue(ValueName, $"\"{Environment.ProcessPath}\"");
            else k.DeleteValue(ValueName, false);
        }
        catch (Exception e) { Log.Write("autostart: " + e.Message); }
    }
}

/// <summary>Icone de la zone de notification + menu.</summary>
public sealed class TrayApp : IDisposable
{
    readonly NotifyIcon _ni;
    readonly ToolStripMenuItem _status;
    readonly ToolStripMenuItem _auto;
    readonly System.Windows.Forms.Timer _timer;
    readonly WebPanel _panel;

    public TrayApp(SyncService sync, WebPanel panel)
    {
        _panel = panel;
        _ni = new NotifyIcon { Icon = MakeIcon(), Visible = true, Text = "Now Playing Hub" };

        var menu = new ContextMenuStrip();
        menu.Items.Add("Ouvrir le panel", null, (s, e) => OpenPanel());
        _status = new ToolStripMenuItem("ESP32 : ...") { Enabled = false };
        menu.Items.Add(_status);
        _auto = new ToolStripMenuItem("Lancer au demarrage de Windows") { Checked = Autostart.Get(), CheckOnClick = true };
        _auto.CheckedChanged += (s, e) => Autostart.Set(_auto.Checked);
        menu.Items.Add(_auto);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Quitter", null, (s, e) => { _ni.Visible = false; Application.ExitThread(); });
        _ni.ContextMenuStrip = menu;
        _ni.DoubleClick += (s, e) => OpenPanel();

        _timer = new System.Windows.Forms.Timer { Interval = 1000 };
        _timer.Tick += (s, e) =>
        {
            _status.Text = sync.EspOnline ? $"ESP32 : connecte ({sync.EspIp})" : "ESP32 : injoignable";
            var t = sync.EspOnline ? "Now Playing Hub - connecte" : "Now Playing Hub - ESP32 injoignable";
            _ni.Text = t.Length > 63 ? t[..63] : t;
        };
        _timer.Start();
    }

    public void OpenPanel()
    {
        try { Process.Start(new ProcessStartInfo(_panel.Url) { UseShellExecute = true }); }
        catch (Exception e) { Log.Write("ouverture panel: " + e.Message); }
    }

    static Icon MakeIcon()
    {
        using var b = new Bitmap(32, 32);
        using (var g = Graphics.FromImage(b))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.Transparent);
            using var br = new SolidBrush(Color.FromArgb(250, 45, 72));
            g.FillEllipse(br, 1, 1, 30, 30);
            g.FillPolygon(Brushes.White, new[] { new Point(12, 9), new Point(12, 23), new Point(23, 16) });
        }
        return Icon.FromHandle(b.GetHicon());
    }

    public void Dispose() { _timer.Dispose(); _ni.Dispose(); }
}

static class Program
{
    [STAThread]
    static void Main()
    {
        using var mutex = new Mutex(true, "NowPlayingHub.Singleton", out bool created);
        var cfg = AppConfig.Load();
        if (!created)
        {
            // deja lance : on ouvre simplement le panel
            try { Process.Start(new ProcessStartInfo($"http://localhost:{cfg.Port}/") { UseShellExecute = true }); } catch { }
            return;
        }

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        AppDomain.CurrentDomain.UnhandledException += (s, e) => Log.Write("FATAL " + e.ExceptionObject);
        TaskScheduler.UnobservedTaskException += (s, e) => { Log.Write("TASK " + e.Exception); e.SetObserved(); };
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (s, e) => Log.Write("UI " + e.Exception);

        var media = new MediaService(cfg);
        var esp = new EspClient(cfg);
        var upd = new Updater(cfg, esp);
        var idle = new IdleService(cfg);
        var sync = new SyncService(cfg, media, esp, upd, idle);
        var panel = new WebPanel(cfg, media, esp, upd, sync, idle);

        try { panel.Start(); }
        catch (Exception e)
        {
            MessageBox.Show("Impossible de demarrer le panel : " + e.Message, "Now Playing Hub");
            return;
        }

        var cts = new CancellationTokenSource();
        _ = Task.Run(() => sync.RunAsync(cts.Token));

        using var tray = new TrayApp(sync, panel);
        tray.OpenPanel();                 // premier lancement : ouvre directement le panel
        Application.Run();
        cts.Cancel();
    }
}
