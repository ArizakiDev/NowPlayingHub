namespace NowPlayingHub;

[Flags]
public enum Push { None = 0, Card = 1, Idle = 2 }

public sealed class SettingsCoordinator
{
    const int DelayMs = 800;

    readonly AppConfig _cfg;
    readonly EspClient _esp;
    readonly IdleService _idle;
    readonly SyncService _sync;
    readonly object _gate = new();
    readonly System.Threading.Timer _timer;
    Push _pending;
    volatile string _status = "";

    public string Status => _status;

    public SettingsCoordinator(AppConfig cfg, EspClient esp, IdleService idle, SyncService sync)
    {
        _cfg = cfg; _esp = esp; _idle = idle; _sync = sync;
        _timer = new System.Threading.Timer(_ => _ = FlushAsync(), null, Timeout.Infinite, Timeout.Infinite);
    }

    public void Changed(Push what)
    {
        lock (_gate) { _pending |= what; }
        _status = what == Push.None ? "" : "Modification en attente…";
        _timer.Change(DelayMs, Timeout.Infinite);
    }

    public async Task FlushAsync()
    {
        Push what;
        lock (_gate) { what = _pending; _pending = Push.None; }
        _cfg.Save();
        if (what == Push.None) { _status = ""; return; }

        if ((what & Push.Idle) != 0) _idle.Invalidate();
        if (!_sync.EspOnline) { _status = "Carte hors ligne : les réglages seront envoyés à la reconnexion."; return; }

        try
        {
            bool ok = true;
            if ((what & Push.Idle) != 0) ok &= await _esp.PostIdleConfigAsync(_cfg);
            if ((what & Push.Card) != 0) ok &= await _esp.PushConfigAsync(_cfg);
            _status = ok ? "Réglages envoyés à la carte." : "La carte a refusé les réglages.";
        }
        catch (Exception e)
        {
            Log.Write("envoi des réglages : " + e.Message);
            _status = "Envoi impossible : " + e.Message;
        }
    }

    public void SaveNow()
    {
        _timer.Change(Timeout.Infinite, Timeout.Infinite);
        _cfg.Save();
    }
}

public sealed class AppServices : IDisposable
{
    readonly CancellationTokenSource _cts = new();

    public AppConfig Cfg { get; }
    public MediaService Media { get; }
    public EspClient Esp { get; }
    public Updater Updater { get; }
    public IdleService Idle { get; }
    public SyncService Sync { get; }
    public SettingsCoordinator Settings { get; }
    public StatusTracker Tracker { get; }
    public StatusServer StatusWeb { get; }

    public AppServices()
    {
        Cfg = AppConfig.Load();
        Media = new MediaService(Cfg);
        Esp = new EspClient(Cfg);
        Updater = new Updater(Cfg, Esp);
        Idle = new IdleService(Cfg);
        Sync = new SyncService(Cfg, Media, Esp, Updater, Idle);
        Settings = new SettingsCoordinator(Cfg, Esp, Idle, Sync);
        Tracker = new StatusTracker(Sync, Idle);
        StatusWeb = new StatusServer(Cfg, () => StatusJson.Build(Tracker.Snapshot(), Sync.Info, Sync.EspOnline));
    }

    public void Start()
    {
        _ = Task.Run(() => Sync.RunAsync(_cts.Token));
        Tracker.Start();
        StatusWeb.Restart();
    }

    public void Dispose()
    {
        _cts.Cancel();
        Settings.SaveNow();
        try { StatusWeb.Dispose(); } catch { }
        try { Tracker.Dispose(); } catch { }
        try { Sync.Spectrum.Dispose(); } catch { }
    }
}
