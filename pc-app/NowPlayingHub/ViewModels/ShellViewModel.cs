using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;

namespace NowPlayingHub;

public sealed class ShellViewModel : ObservableObject
{
    readonly AppServices _svc;
    readonly DispatcherTimer _timer;
    readonly FirmwareViewModel _firmware;
    PageViewModel _current;
    string _accentHex = "";
    bool _windowVisible = true;

    public IReadOnlyList<PageViewModel> Pages { get; }
    public event Action? PageChanged;

    public PageViewModel Current
    {
        get => _current;
        set
        {
            if (value == null || ReferenceEquals(value, _current)) return;
            _current.OnHidden();
            _current = value;
            Raise();
            _current.OnShown();
            PageChanged?.Invoke();
        }
    }

    bool _connOnline;
    public bool ConnOnline { get => _connOnline; private set => Set(ref _connOnline, value); }

    string _connText = "Recherche de la carte…";
    public string ConnText { get => _connText; private set => Set(ref _connText, value); }

    string _syncText = "";
    public string SyncText { get => _syncText; private set => Set(ref _syncText, value); }

    public bool WindowVisible
    {
        get => _windowVisible;
        set { _windowVisible = value; if (value) { Tick(); _current.OnShown(); } }
    }

    public ShellViewModel(AppServices svc)
    {
        _svc = svc;
        _firmware = new FirmwareViewModel(svc);
        SystemViewModel? system = null;
        var home = new HomeViewModel(svc, () => { if (system != null) Current = system; });
        system = new SystemViewModel(svc);
        Pages = new PageViewModel[]
        {
            home, new DisplayViewModel(svc), new IdleViewModel(svc), new MusicViewModel(svc),
            new HardwareViewModel(svc), new StatusViewModel(svc), _firmware, system
        };
        _current = Pages[0];

        _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _timer.Tick += (s, e) => Tick();
        _timer.Start();
        Tick();
    }

    void Tick()
    {
        var sync = _svc.Sync;
        ConnOnline = sync.EspOnline;
        ConnText = sync.EspOnline
            ? $"Carte connectée · {sync.EspIp}"
            : (string.IsNullOrEmpty(sync.LastError) ? "Recherche de la carte…" : "Carte injoignable");
        SyncText = _svc.Settings.Status;
        UpdateAccent();
        _firmware.BackgroundCheck();

        if (_windowVisible) _current.OnTick();
    }

    void UpdateAccent()
    {
        var hex = _svc.Sync.EffectiveAccent;
        if (hex == _accentHex) return;
        _accentHex = hex;
        try
        {
            var c = (Color)ColorConverter.ConvertFromString(hex);
            double lum = (0.299 * c.R + 0.587 * c.G + 0.114 * c.B) / 255.0;
            var res = Application.Current.Resources;
            res["AccentBrush"] = new SolidColorBrush(c);
            res["OnAccentBrush"] = new SolidColorBrush(lum > 0.62 ? Color.FromRgb(16, 16, 20) : Colors.White);
        }
        catch (Exception e) { Log.Write("accent interface : " + e.Message); }
    }
}
