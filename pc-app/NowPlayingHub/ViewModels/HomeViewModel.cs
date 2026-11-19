using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace NowPlayingHub;

public sealed class HomeViewModel : PageViewModel
{
    readonly AppServices _svc;
    readonly Action _goConnection;
    string _shownCoverKey = "";

    public override string Title => "Accueil";
    public override string Glyph => "\uE80F";

    public HomeViewModel(AppServices svc, Action goConnection)
    {
        _svc = svc; _goConnection = goConnection;
        PrevCommand = new RelayCommand(_ => Send("prev"));
        ToggleCommand = new RelayCommand(_ => Send("toggle"));
        NextCommand = new RelayCommand(_ => Send("next"));
        GoConnectionCommand = new RelayCommand(_ => _goConnection());
        VolUpCommand = new RelayCommand(_ => Send("volup"));
        VolDownCommand = new RelayCommand(_ => Send("voldown"));
        MuteCommand = new RelayCommand(_ => Send("mute"));
    }

    public RelayCommand VolUpCommand { get; }
    public RelayCommand VolDownCommand { get; }
    public RelayCommand MuteCommand { get; }

    public RelayCommand PrevCommand { get; }
    public RelayCommand ToggleCommand { get; }
    public RelayCommand NextCommand { get; }
    public RelayCommand GoConnectionCommand { get; }

    bool _hasTrack; public bool HasTrack { get => _hasTrack; private set { if (Set(ref _hasTrack, value)) Raise(nameof(NoTrack)); } }
    public bool NoTrack => !_hasTrack;
    string _title = ""; public string TrackTitle { get => _title; private set => Set(ref _title, value); }
    string _artist = ""; public string Artist { get => _artist; private set => Set(ref _artist, value); }
    string _album = ""; public string Album { get => _album; private set => Set(ref _album, value); }
    string _app = ""; public string AppName { get => _app; private set => Set(ref _app, value); }
    string _lyric = ""; public string Lyric { get => _lyric; private set => Set(ref _lyric, value); }
    ImageSource? _cover; public ImageSource? Cover { get => _cover; private set => Set(ref _cover, value); }
    double _progress; public double Progress { get => _progress; private set => Set(ref _progress, value); }
    string _pos = "0:00"; public string PositionText { get => _pos; private set => Set(ref _pos, value); }
    string _dur = "0:00"; public string DurationText { get => _dur; private set => Set(ref _dur, value); }
    string _glyph = "\uE102"; public string PlayGlyph { get => _glyph; private set => Set(ref _glyph, value); }

    bool _online; public bool ConnOnline { get => _online; private set { if (Set(ref _online, value)) Raise(nameof(IsOffline)); } }
    public bool IsOffline => !_online;
    string _cardState = ""; public string CardState { get => _cardState; private set => Set(ref _cardState, value); }
    string _cardLine = ""; public string CardLine { get => _cardLine; private set => Set(ref _cardLine, value); }
    string _screen = "—"; public string ScreenState { get => _screen; private set => Set(ref _screen, value); }
    string _screenLine = ""; public string ScreenLine { get => _screenLine; private set => Set(ref _screenLine, value); }
    string _source = ""; public string SourceState { get => _source; private set => Set(ref _source, value); }
    string _sourceLine = ""; public string SourceLine { get => _sourceLine; private set => Set(ref _sourceLine, value); }

    void Send(string cmd) => Fire(() => _svc.Sync.RunCommandAsync(cmd), "commande " + cmd);

    public void SeekFraction(double f)
    {
        var snap = _svc.Sync.Current;
        if (snap == null || snap.Duration <= 0) return;
        double secs = Math.Clamp(f, 0, 1) * snap.Duration;
        Fire(() => _svc.Sync.RunCommandAsync("seek:" + secs.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture)), "déplacement");
    }

    static string Mmss(double s)
    {
        if (s < 0) s = 0;
        var t = TimeSpan.FromSeconds(s);
        return t.TotalHours >= 1 ? $"{(int)t.TotalHours}:{t.Minutes:00}:{t.Seconds:00}" : $"{t.Minutes}:{t.Seconds:00}";
    }

    static string ScreenName(string s) => s switch
    {
        "play" => "Lecture", "cover" => "Pochette plein écran", "viz" => "Spectre audio", "lyrics" => "Paroles",
        "idle" => "Horloge", "weather" => "Météo", "news" => "Actualités", "pc" => "Mon PC",
        "system" => "Système", "menu" => "Menu", "wifi" => "Configuration Wi-Fi", _ => "—"
    };

    public override void OnShown() => OnTick();

    public override void OnTick()
    {
        var sync = _svc.Sync;
        var snap = sync.Current;
        var info = sync.Info;

        HasTrack = snap != null;
        if (snap != null)
        {
            TrackTitle = snap.Title; Artist = snap.Artist; Album = snap.Album; AppName = snap.AppName;
            double pos = snap.Position;
            if (snap.State == "playing") pos += Math.Max(0, (DateTime.UtcNow - sync.SnapshotAt).TotalSeconds);
            if (snap.Duration > 0) pos = Math.Min(pos, snap.Duration);
            Progress = snap.Duration > 0 ? pos / snap.Duration : 0;
            PositionText = Mmss(pos);
            DurationText = Mmss(snap.Duration);
            PlayGlyph = snap.State == "playing" ? "\uE103" : "\uE102";
            Lyric = sync.LyricNow;
            RefreshCover(snap.Key);
        }
        else { Cover = null; _shownCoverKey = ""; Lyric = ""; }

        ConnOnline = sync.EspOnline;
        if (sync.EspOnline && info != null)
        {
            CardState = "Connectée";
            CardLine = $"Firmware {info.Version} · Wi-Fi {info.Rssi} dBm · {info.Heap / 1024} Ko libres";
            ScreenState = ScreenName(info.Screen);
            ScreenLine = info.Night ? "Mode nuit actif" : $"Luminosité {info.Brightness} %";
        }
        else
        {
            CardState = "Injoignable";
            CardLine = string.IsNullOrEmpty(sync.LastError) ? "Recherche en cours…" : sync.LastError;
            ScreenState = "—"; ScreenLine = "";
        }

        var sel = _svc.Cfg.SelectedAppId;
        bool follow = sel == MediaService.FollowPlaying;
        SourceState = snap != null ? snap.AppName : (sel.Length == 0 ? "Automatique" : follow ? "Celui qui joue" : MediaService.FriendlyName(sel));
        SourceLine = sel.Length == 0 ? "Cider, Apple Music ou iTunes" : follow ? "Suit le lecteur en cours" : "Application choisie";
    }

    void RefreshCover(string key)
    {
        if (_shownCoverKey == key) return;
        var sync = _svc.Sync;
        if (sync.CoverKey != key) return;
        var data = sync.RawCover ?? sync.CoverJpeg;
        if (data == null) return;
        _shownCoverKey = key;
        Cover = Decode(data) ?? (sync.CoverJpeg != null ? Decode(sync.CoverJpeg) : null);
    }

    static ImageSource? Decode(byte[] data)
    {
        try
        {
            using var ms = new MemoryStream(data);
            var bmp = new BitmapImage();
            bmp.BeginInit();
            bmp.CacheOption = BitmapCacheOption.OnLoad;
            bmp.DecodePixelWidth = 480;
            bmp.StreamSource = ms;
            bmp.EndInit();
            bmp.Freeze();
            return bmp;
        }
        catch { return null; }
    }
}
