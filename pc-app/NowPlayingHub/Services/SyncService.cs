using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace NowPlayingHub;

public class SyncService
{
    readonly AppConfig _cfg;
    readonly MediaService _media;
    readonly EspClient _esp;
    readonly Updater _upd;
    readonly IdleService _idle;
    readonly LyricsService _lyrics;

    public volatile bool EspOnline;
    public string LastError = "";
    public string EspVersion = "";
    public string EspIp = "";
    public volatile EspInfo? Info;
    public volatile Snapshot? Current;
    public DateTime SnapshotAt = DateTime.MinValue;
    public string LyricNow = "";
    public LyricsService Lyrics => _lyrics;
    public readonly SpectrumService Spectrum;

    public byte[]? RawCover => _rawCover;
    public byte[]? CoverJpeg => _coverJpeg;
    public string CoverKey => _coverKey;

    public string EffectiveAccent
    {
        get
        {
            var cur = Current;
            if (_cfg.Esp.DynamicAccent && cur != null && _accentKey == cur.Key && _accent != null) return _accent;
            return _cfg.Esp.Accent;
        }
    }

    string _coverKey = "";
    byte[]? _coverJpeg, _rawCover;
    int _coverTries;
    DateTime _coverTryAt = DateTime.MinValue;
    string _iconId = "";
    byte[]? _iconJpeg;
    string _bigKey = "";
    byte[]? _bigJpeg;
    int _bigTries;
    string _accentKey = "";
    string? _accent;

    DateTime _lastIdlePush = DateTime.MinValue;
    DateTime _lastInfoAt = DateTime.MinValue;
    List<NewsItem>? _lastPushedNews;
    WeatherData? _lastPushedWeather;
    bool _configSynced;

    public SyncService(AppConfig cfg, MediaService media, EspClient esp, Updater upd, IdleService idle)
    {
        _cfg = cfg; _media = media; _esp = esp; _upd = upd; _idle = idle;
        _lyrics = new LyricsService(cfg);
        Spectrum = new SpectrumService(cfg);
    }

    static string Hash(string s) =>
        Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(s)))[..8].ToLowerInvariant();

    static readonly Dictionary<char, string> Map = new()
    {
        ['œ'] = "oe", ['Œ'] = "OE", ['æ'] = "ae", ['Æ'] = "AE", ['ß'] = "ss", ['ø'] = "o", ['Ø'] = "O",
        ['ł'] = "l", ['Ł'] = "L", ['đ'] = "d", ['’'] = "'", ['‘'] = "'", ['“'] = "\"", ['”'] = "\"",
        ['–'] = "-", ['—'] = "-", ['…'] = "..."
    };

    string Clean(string s)
    {
        if (!_cfg.AsciiOnly) return s;
        var sb = new StringBuilder();
        foreach (var ch in s) sb.Append(Map.TryGetValue(ch, out var m) ? m : ch.ToString());
        var norm = sb.ToString().Normalize(NormalizationForm.FormD);
        var res = new StringBuilder();
        foreach (var ch in norm)
        {
            if (System.Globalization.CharUnicodeInfo.GetUnicodeCategory(ch) == System.Globalization.UnicodeCategory.NonSpacingMark) continue;
            res.Append(ch >= 0x20 && ch <= 0x7E ? ch : '?');
        }
        return res.ToString();
    }

    public async Task<byte[]?> GetCoverAsync(string key)
    {
        if (_coverKey != key) { _coverKey = key; _coverJpeg = null; _rawCover = null; _coverTries = 0; _coverTryAt = DateTime.MinValue; }
        if (_coverJpeg == null && _coverTries < 8 && (DateTime.UtcNow - _coverTryAt).TotalMilliseconds > 900)
        {
            _coverTries++;
            _coverTryAt = DateTime.UtcNow;
            var raw = await _media.GetThumbnailAsync();
            if (raw != null)
            {
                _rawCover = raw;
                _coverJpeg = await Images.CoverJpegAsync(raw, 120, 11000);
            }
        }
        return _coverJpeg;
    }

    async Task<byte[]?> GetBigCoverAsync(string key)
    {
        if (_bigKey != key) { _bigKey = key; _bigJpeg = null; _bigTries = 0; }
        if (_bigJpeg == null && _bigTries < 3)
        {
            _bigTries++;
            var raw = (_coverKey == key ? _rawCover : null) ?? await _media.GetThumbnailAsync();
            if (raw != null) _bigJpeg = await Images.CoverJpegAsync(raw, 240, 20000);
        }
        return _bigJpeg;
    }

    public Task<bool> RunCommandAsync(string cmd) => cmd switch
    {
        "volup"   => Task.FromResult(Volume.Step(+0.05f)),
        "voldown" => Task.FromResult(Volume.Step(-0.05f)),
        "mute"    => Task.FromResult(Volume.ToggleMute()),
        _         => _media.CommandAsync(cmd)
    };

    public void ForceReconnect() { EspOnline = false; _configSynced = false; }

    public async Task<EspInfo?> RefreshInfoAsync()
    {
        try
        {
            var i = await _esp.GetInfoAsync();
            if (i != null) { Info = i; EspVersion = i.Version; EspIp = i.Ip; }
            _lastInfoAt = DateTime.UtcNow;
            return i;
        }
        catch (Exception e) { Log.Write("statut carte : " + e.Message); return null; }
    }

    async Task<bool> PushAllConfigAsync()
    {
        try
        {
            bool a = await _esp.PushConfigAsync(_cfg);
            bool b = await _esp.PostIdleConfigAsync(_cfg);
            return a && b;
        }
        catch (Exception e) { Log.Write("envoi des réglages : " + e.Message); return false; }
    }

    int _fails;

    static string Friendly(Exception e)
    {
        if (e is TaskCanceledException or TimeoutException || e.InnerException is TimeoutException)
            return "Pas de réponse de la carte (délai dépassé).";
        if (e is HttpRequestException h)
        {
            var inner = h.InnerException as System.Net.Sockets.SocketException;
            return inner?.SocketErrorCode switch
            {
                System.Net.Sockets.SocketError.HostNotFound or System.Net.Sockets.SocketError.TryAgain => "Adresse introuvable : vérifie l'adresse de la carte (nowplaying.local ou son IP).",
                System.Net.Sockets.SocketError.ConnectionRefused => "Connexion refusée par la carte.",
                System.Net.Sockets.SocketError.HostUnreachable or System.Net.Sockets.SocketError.NetworkUnreachable => "Carte inaccessible sur le réseau.",
                _ => "Connexion impossible : " + (inner?.Message ?? h.Message)
            };
        }
        return e.Message;
    }

    bool Tolerate(string why)
    {
        if (EspOnline && ++_fails < 3) { LastError = why; return true; }
        return false;
    }

    void SetOffline(string why)
    {
        _fails = 0;
        if (EspOnline || LastError != why) Log.Write("ESP32 injoignable : " + why);
        EspOnline = false;
        _configSynced = false;
        LastError = why;
    }

    async Task WarmCoverAsync(Snapshot? snap)
    {
        if (snap == null) return;
        try { await GetCoverAsync(snap.Key); }
        catch (Exception e) { Log.Write("pochette : " + e.Message); }
    }

    async Task<bool> TickAsync()
    {
        Snapshot? snap = null;
        try { snap = await _media.ReadAsync(); }
        catch (Exception e) { Log.Write("média : " + e.Message); }
        Current = snap;
        SnapshotAt = DateTime.UtcNow;

        string json, key = "", coverId = "";
        if (snap == null)
        {
            json = "{\"state\":\"stopped\"}";
            LyricNow = "";
            History.Enabled = _cfg.ListenHistory;
            if (History.Enabled) History.Track(null);
        }
        else
        {
            key = snap.Key;
            coverId = Hash(key);

            string? accent = null;
            if (_cfg.Esp.DynamicAccent)
            {
                if (_accentKey != key)
                {
                    var jpg = await GetCoverAsync(key);
                    if (jpg != null || _coverTries >= 8)
                    {
                        _accentKey = key;
                        try { _accent = jpg == null ? null : Images.DominantColor(jpg); }
                        catch (Exception e) { Log.Write("accent : " + e.Message); _accent = null; }
                    }
                }
                accent = _accentKey == key ? _accent : null;
            }

            string lyric = "", lyricPrev = "", lyricNext = "";
            int lyricIn = 0;
            LyricNow = "";
            if (_cfg.Esp.ShowLyrics)
            {
                var (lp, lc, ln, inMs) = _lyrics.ContextTimed(snap);
                LyricNow = lc;
                lyric = Clean(lc); lyricPrev = Clean(lp); lyricNext = Clean(ln);
                lyricIn = inMs;
            }
            History.Enabled = _cfg.ListenHistory;
            if (History.Enabled) History.Track(snap);

            json = JsonSerializer.Serialize(new
            {
                state = snap.State,
                title = Clean(snap.Title),
                artist = Clean(snap.Artist),
                album = Clean(snap.Album),
                position = Math.Round(snap.Position, 1),
                duration = (int)Math.Round(snap.Duration),
                coverId,
                app = Clean(snap.AppName),
                appId = Hash(snap.AppId),
                accent,
                lyric,
                lyricPrev,
                lyricNext,
                lyricIn,
                vol = Volume.Read()?.Pct ?? -1,
                mute = Volume.Read()?.Mute ?? false
            });
        }

        EspReply? rep;
        try { rep = await _esp.PostNowPlayingAsync(json); }
        catch (Exception e) { var why = Friendly(e); if (!Tolerate(why)) SetOffline(why); await WarmCoverAsync(snap); return false; }
        if (rep == null) { if (!Tolerate("réponse invalide")) SetOffline("réponse invalide"); await WarmCoverAsync(snap); return false; }
        _fails = 0;

        if (!EspOnline)
        {
            EspOnline = true; LastError = "";
            await RefreshInfoAsync();
            Log.Write($"ESP32 connecté (firmware {EspVersion}, {EspIp})");
            _configSynced = false;
        }

        if (!_configSynced)
        {
            _configSynced = await PushAllConfigAsync();
            if (_configSynced) Log.Write("réglages envoyés à la carte");
        }

        if ((DateTime.UtcNow - _lastInfoAt).TotalSeconds >= 10) await RefreshInfoAsync();

        Spectrum.Request(_cfg.Visualizer && rep.Viz, EspIp);

        var weather = _idle.Weather;
        bool changed = weather != _lastPushedWeather || !ReferenceEquals(_idle.NewsItems, _lastPushedNews);
        bool due = (DateTime.UtcNow - _lastIdlePush).TotalMinutes >= 5;
        if (changed || due || _lastIdlePush == DateTime.MinValue)
        {
            try
            {
                var news = _idle.NewsItems;
                if (await _esp.PostIdleAsync(weather, _idle.Headlines, news))
                { _lastIdlePush = DateTime.UtcNow; _lastPushedWeather = weather; _lastPushedNews = news; }
            }
            catch (Exception e) { Log.Write("données de veille : " + e.Message); }
        }

        try
        {
            if (rep.NeedCover && snap != null)
            {
                var jpg = await GetCoverAsync(key);
                if (jpg != null) await _esp.PostCoverAsync(coverId, jpg);
            }
            if (rep.NeedBig && snap != null)
            {
                var big = await GetBigCoverAsync(key);
                if (big != null) await _esp.PostBigCoverAsync(coverId, big);
            }
            if (rep.NeedIcon && snap != null)
            {
                var iconId = Hash(snap.AppId);
                if (_iconId != iconId || _iconJpeg == null)
                {
                    _iconJpeg = Images.IconJpeg(snap.AppId, snap.AppName);
                    _iconId = iconId;
                }
                await _esp.PostIconAsync(iconId, _iconJpeg);
            }
        }
        catch (Exception e) { Log.Write("envoi d'image : " + e.Message); }

        await WarmCoverAsync(snap);

        if (!string.IsNullOrEmpty(rep.Cmd))
        {
            try { await RunCommandAsync(rep.Cmd); }
            catch (Exception e) { Log.Write("commande : " + e.Message); }
            return true;
        }
        return false;
    }

    public async Task RunAsync(CancellationToken ct)
    {
        _ = Task.Run(() => _idle.RunAsync(ct));

        while (!ct.IsCancellationRequested)
        {
            int delay = Math.Max(250, _cfg.IntervalMs);
            long t0 = Environment.TickCount64;
            if (!_upd.Status.Busy)
            {
                try { if (await TickAsync()) delay = 400; }
                catch (Exception e) { Log.Write("cycle : " + e.Message); }
            }
            int spent = (int)(Environment.TickCount64 - t0);
            try { await Task.Delay(Math.Max(50, delay - spent), ct); }
            catch (OperationCanceledException) { break; }
        }
    }
}
