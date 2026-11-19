using System.Reflection;
using System.Windows.Threading;
using Drawing = System.Drawing;
using Forms = System.Windows.Forms;

namespace NowPlayingHub;

public sealed class TrayIcon : IDisposable
{
    readonly Forms.NotifyIcon _ni;
    readonly Forms.ToolStripMenuItem _status;
    readonly Forms.ToolStripMenuItem _auto;
    readonly DispatcherTimer _timer;

    public TrayIcon(AppServices svc, Action open, Action quit)
    {
        _ni = new Forms.NotifyIcon { Icon = LoadIcon(), Visible = true, Text = "Now Playing Hub" };

        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add("Ouvrir Now Playing Hub", null, (s, e) => open());
        _status = new Forms.ToolStripMenuItem("Carte : …") { Enabled = false };
        menu.Items.Add(_status);
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add("Précédent", null, (s, e) => _ = svc.Sync.RunCommandAsync("prev"));
        menu.Items.Add("Lecture / Pause", null, (s, e) => _ = svc.Sync.RunCommandAsync("toggle"));
        menu.Items.Add("Suivant", null, (s, e) => _ = svc.Sync.RunCommandAsync("next"));
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add("Volume +", null, (s, e) => _ = svc.Sync.RunCommandAsync("volup"));
        menu.Items.Add("Volume −", null, (s, e) => _ = svc.Sync.RunCommandAsync("voldown"));
        menu.Items.Add("Muet", null, (s, e) => _ = svc.Sync.RunCommandAsync("mute"));
        menu.Items.Add(new Forms.ToolStripSeparator());
        _auto = new Forms.ToolStripMenuItem("Lancer au démarrage de Windows") { Checked = Autostart.Get(), CheckOnClick = true };
        _auto.CheckedChanged += (s, e) => Autostart.Set(_auto.Checked);
        menu.Items.Add(_auto);
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add("Quitter", null, (s, e) => quit());
        _ni.ContextMenuStrip = menu;
        _ni.DoubleClick += (s, e) => open();

        _timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _timer.Tick += (s, e) =>
        {
            var s2 = svc.Sync;
            _status.Text = s2.EspOnline ? $"Carte : connectée ({s2.EspIp})" : "Carte : injoignable";
            var cur = s2.Current;
            var t = cur != null ? $"{cur.Title} — {cur.Artist}" : (s2.EspOnline ? "Now Playing Hub" : "Now Playing Hub · carte injoignable");
            _ni.Text = t.Length > 63 ? t[..63] : t;
        };
        _timer.Start();
    }

    public void Hint(string text)
    {
        try { _ni.ShowBalloonTip(3000, "Now Playing Hub", text, Forms.ToolTipIcon.Info); } catch { }
    }

    static Drawing.Icon LoadIcon()
    {
        try
        {
            using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("NowPlayingHub.app.ico");
            if (s != null) return new Drawing.Icon(s);
        }
        catch (Exception e) { Log.Write("icône : " + e.Message); }
        return Drawing.SystemIcons.Application;
    }

    public void Dispose()
    {
        _timer.Stop();
        _ni.Visible = false;
        _ni.Dispose();
    }
}
