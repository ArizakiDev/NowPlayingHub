using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace NowPlayingHub;

/// <summary>Boucle principale : lit le media, l'envoie a l'ESP32, execute les commandes tactiles.</summary>
public class SyncService
{
    readonly AppConfig _cfg;
    readonly MediaService _media;
    readonly EspClient _esp;
    readonly Updater _upd;
    readonly IdleService _idle;

    public volatile bool EspOnline;
    public string LastError = "";
    public string EspVersion = "";
    public string EspIp = "";
    public Snapshot? Current;

    string _coverKey = "";
    byte[]? _coverJpeg;
    int _coverTries;
    string _iconId = "";
    byte[]? _iconJpeg;

    readonly LyricsService _lyrics;
    public LyricsService Lyrics => _lyrics;
    public readonly SpectrumService Spectrum;
    public readonly PcStatsService PcStats = new();
    DateTime _lastPcPush = DateTime.MinValue;

    // Pochette plein ecran 240x240, produite a la demande
    byte[]? _rawCover;
    string _bigKey = "";
    byte[]? _bigJpeg;
    int _bigTries;
    string _accentKey = "";
    string? _accent;

    DateTime _lastIdlePush    = DateTime.MinValue;
    List<NewsItem>? _lastPushedNews = null;  // v1.8 : pousse les actus des qu'elles changent (et plus seulement toutes les 5 min)
    WeatherData? _lastPushedWeather = null;  // detecte si la meteo a change

    public SyncService(AppConfig cfg, MediaService media, EspClient esp, Updater upd, IdleService idle)
    {
        _cfg = cfg; _media = media; _esp = esp; _upd = upd; _idle = idle;
        _lyrics = new LyricsService(cfg);
        Spectrum = new SpectrumService(cfg);
        PcStats.Start();
    }

    static string Hash(string s) =>
        Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(s)))[..8].ToLowerInvariant();

    // Les polices integrees de TFT_eSPI sont ASCII : on translittere (e accent -> e, etc.)
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
        foreach (var ch in s)
            sb.Append(Map.TryGetValue(ch, out var m) ? m : ch.ToString());
        var norm = sb.ToString().Normalize(NormalizationForm.FormD);
        var res = new StringBuilder();
        foreach (var ch in norm)
        {
            if (System.Globalization.CharUnicodeInfo.GetUnicodeCategory(ch) == System.Globalization.UnicodeCategory.NonSpacingMark) continue;
            res.Append(ch >= 0x20 && ch <= 0x7E ? ch : '?');
        }
        return res.ToString();
    }

    /// <summary>Pochette 120x120 de la piste (mise en cache par piste).</summary>
    public async Task<byte[]?> GetCoverAsync(string key)
    {
        if (_coverKey != key) { _coverKey = key; _coverJpeg = null; _coverTries = 0; _rawCover = null; }
        if (_coverJpeg == null && _coverTries < 4)
        {
            _coverTries++;
            var raw = await _media.GetThumbnailAsync();
            if (raw != null)
            {
                _rawCover = raw;
                _coverJpeg = await Images.CoverJpegAsync(raw, 120, 11000); // v1.8 : 15000 -> 11000 (buffer ESP32 12 Ko, transfert plus court)
            }
        }
        return _coverJpeg;
    }

    /// <summary>Pochette 240x240 (plein ecran), a la demande ; 3 essais max par piste.</summary>
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

    public Task<bool> RunCommandAsync(string cmd) => _media.CommandAsync(cmd);

    async Task OnConnectedAsync()
    {
        try
        {
            var st = await _esp.GetStatusAsync();
            if (st != null) { EspVersion = st.Version; EspIp = st.Ip; }
            await _esp.PushConfigAsync(_cfg);
            await _esp.PostIdleConfigAsync((float)_cfg.Latitude, (float)_cfg.Longitude, _cfg.LocationName ?? "", _cfg.NewsRssUrl ?? "", _cfg.Timezone);
            Log.Write($"ESP32 connecte (firmware {EspVersion}, {EspIp})");
        }
        catch (Exception e) { Log.Write("connexion: " + e.Message); }
    }

    void SetOffline(string why)
    {
        if (EspOnline || LastError != why) Log.Write("ESP32 injoignable : " + why);
        EspOnline = false;
        LastError = why;
    }

    /// <returns>true si une commande tactile a ete executee</returns>
    async Task<bool> TickAsync()
    {
        Snapshot? snap = null;
        try { snap = await _media.ReadAsync(); }
        catch (Exception e) { Log.Write("media: " + e.Message); }
        Current = snap;

        string json, key = "", coverId = "";
        if (snap == null)
        {
            json = "{\"state\":\"stopped\"}";
        }
        else
        {
            key = snap.Key;
            coverId = Hash(key);

            // Couleur d'accent tiree de la pochette (calculee une fois par piste)
            string? accent = null;
            if (_cfg.Esp.DynamicAccent)
            {
                if (_accentKey != key)
                {
                    var jpg = await GetCoverAsync(key);
                    if (jpg != null || _coverTries >= 4)
                    {
                        _accentKey = key;
                        try { _accent = jpg == null ? null : Images.DominantColor(jpg); }
                        catch (Exception e) { Log.Write("accent: " + e.Message); _accent = null; }
                    }
                }
                accent = _accentKey == key ? _accent : null;
            }
            string lyric = "", lyricPrev = "", lyricNext = "";
            if (_cfg.Esp.ShowLyrics)
            {
                var (lp, lc, ln) = _lyrics.Context(snap);
                lyric = Clean(lc); lyricPrev = Clean(lp); lyricNext = Clean(ln);
            }

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
                lyricNext
            });
        }

        EspReply? rep;
        try { rep = await _esp.PostNowPlayingAsync(json); }
        catch (Exception e) { SetOffline(e.Message); return false; }
        if (rep == null) { SetOffline("reponse invalide"); return false; }

        if (!EspOnline) { EspOnline = true; LastError = ""; await OnConnectedAsync(); }

        // Spectre audio : la carte dit (reponse) quand elle l'affiche ; sinon la capture est arretee
        Spectrum.Request(_cfg.Visualizer && rep.Viz, EspIp);

        // Stats du PC pour l'ecran de veille (uniquement quand rien ne joue)
        if (_cfg.ShowPcStats && (snap == null || snap.State != "playing")
            && (DateTime.UtcNow - _lastPcPush).TotalSeconds >= 3 && PcStats.Latest is { } pcs)
        {
            _lastPcPush = DateTime.UtcNow;
            try { await _esp.PostPcStatsAsync(pcs); }
            catch (Exception e) { Log.Write("pcstats: " + e.Message); }
        }

        // Envoyer les donnees idle :
        //  - immediatement si la meteo vient de changer (premiere fois ou refresh)
        //  - sinon toutes les 5 min pour garder l'ESP a jour (news, etc.)
        var currentWeather = _idle.Weather;
        bool weatherChanged = currentWeather != _lastPushedWeather || !ReferenceEquals(_idle.NewsItems, _lastPushedNews);
        bool timeDue = (DateTime.UtcNow - _lastIdlePush).TotalMinutes >= 5;
        if (weatherChanged || timeDue || _lastIdlePush == DateTime.MinValue)
        {
            try
            {
                var newsNow = _idle.NewsItems;
                bool sent = await _esp.PostIdleAsync(currentWeather, _idle.Headlines, newsNow);
                if (sent) { _lastIdlePush = DateTime.UtcNow; _lastPushedWeather = currentWeather; _lastPushedNews = newsNow; }
            }
            catch (Exception e) { Log.Write("idle push: " + e.Message); }
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
        catch (Exception e) { Log.Write("envoi image: " + e.Message); }

        if (!string.IsNullOrEmpty(rep.Cmd))
        {
            try { await _media.CommandAsync(rep.Cmd); }
            catch (Exception e) { Log.Write("commande: " + e.Message); }
            return true;
        }
        return false;
    }

    public async Task RunAsync(CancellationToken ct)
    {
        // Lance le service idle (meteo + news) en parallele
        _ = Task.Run(() => _idle.RunAsync(ct));

        while (!ct.IsCancellationRequested)
        {
            int delay = Math.Max(250, _cfg.IntervalMs);
            long t0 = Environment.TickCount64;
            if (!_upd.Status.Busy)
            {
                try { if (await TickAsync()) delay = 400; }   // apres une commande : relance rapide
                catch (Exception e) { Log.Write("tick: " + e.Message); }
            }
            int spent = (int)(Environment.TickCount64 - t0);
            try { await Task.Delay(Math.Max(50, delay - spent), ct); }
            catch (OperationCanceledException) { break; }
        }
    }
}
