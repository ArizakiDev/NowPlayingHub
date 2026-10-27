using System.Net;
using System.Reflection;
using System.Text;
using System.Text.Json;

namespace NowPlayingHub;

/// <summary>Panel web local (http://localhost:8123) : etat, reglages de l'ecran, choix de l'appli, mises a jour.</summary>
public class WebPanel
{
    readonly AppConfig _cfg;
    readonly MediaService _media;
    readonly EspClient _esp;
    readonly Updater _upd;
    readonly SyncService _sync;
    readonly IdleService _idle;
    HttpListener? _listener;
    string _html = "";
    static readonly JsonSerializerOptions J = new(JsonSerializerDefaults.Web);

    public int Port { get; private set; }
    public string Url => $"http://localhost:{Port}/";
    public const string AppVersion = "1.1.0";

    public WebPanel(AppConfig cfg, MediaService media, EspClient esp, Updater upd, SyncService sync, IdleService idle)
    {
        _cfg = cfg; _media = media; _esp = esp; _upd = upd; _sync = sync; _idle = idle;
    }

    public void Start()
    {
        var asm = Assembly.GetExecutingAssembly();
        var res = asm.GetManifestResourceNames().First(n => n.EndsWith("panel.html"));
        using (var s = asm.GetManifestResourceStream(res)!)
        using (var r = new StreamReader(s, Encoding.UTF8)) _html = r.ReadToEnd();

        for (int p = _cfg.Port; p < _cfg.Port + 10; p++)
        {
            try
            {
                var l = new HttpListener();
                l.Prefixes.Add($"http://localhost:{p}/");
                l.Start();
                _listener = l; Port = p;
                break;
            }
            catch (Exception e) { Log.Write($"port {p}: {e.Message}"); }
        }
        if (_listener == null) throw new InvalidOperationException("Aucun port libre pour le panel (8123-8132).");
        Log.Write("Panel sur " + Url);
        _ = Task.Run(LoopAsync);
    }

    async Task LoopAsync()
    {
        while (_listener != null && _listener.IsListening)
        {
            HttpListenerContext ctx;
            try { ctx = await _listener.GetContextAsync(); }
            catch { break; }
            _ = Task.Run(() => HandleAsync(ctx));
        }
    }

    // Protection : seul le panel servi par cette appli peut l'appeler (pas un autre site web)
    bool Allowed(HttpListenerRequest r)
    {
        var host = r.Headers["Host"] ?? "";
        if (host != $"localhost:{Port}" && host != $"127.0.0.1:{Port}") return false;
        var origin = r.Headers["Origin"];
        if (!string.IsNullOrEmpty(origin) && origin != $"http://localhost:{Port}" && origin != $"http://127.0.0.1:{Port}") return false;
        return true;
    }

    static async Task WriteAsync(HttpListenerContext c, int status, string type, byte[] data)
    {
        c.Response.StatusCode = status;
        c.Response.ContentType = type;
        c.Response.Headers["Cache-Control"] = "no-store";
        c.Response.ContentLength64 = data.Length;
        await c.Response.OutputStream.WriteAsync(data);
        c.Response.Close();
    }

    static Task JsonAsync(HttpListenerContext c, object o, int status = 200) =>
        WriteAsync(c, status, "application/json; charset=utf-8", Encoding.UTF8.GetBytes(JsonSerializer.Serialize(o, J)));

    static async Task<JsonElement> BodyJsonAsync(HttpListenerRequest r)
    {
        using var sr = new StreamReader(r.InputStream, Encoding.UTF8);
        var text = await sr.ReadToEndAsync();
        using var doc = JsonDocument.Parse(string.IsNullOrWhiteSpace(text) ? "{}" : text);
        return doc.RootElement.Clone();
    }

    static string Str(JsonElement e, string n, string def = "") =>
        e.TryGetProperty(n, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? def : def;

    async Task HandleAsync(HttpListenerContext c)
    {
        try
        {
            if (!Allowed(c.Request)) { await WriteAsync(c, 403, "text/plain", Encoding.UTF8.GetBytes("forbidden")); return; }
            var path = c.Request.Url!.AbsolutePath;
            var route = c.Request.HttpMethod + " " + path;

            switch (route)
            {
                case "GET /":
                    await WriteAsync(c, 200, "text/html; charset=utf-8", Encoding.UTF8.GetBytes(_html));
                    break;

                case "GET /api/state":
                    await JsonAsync(c, await BuildStateAsync());
                    break;

                case "GET /cover.jpg":
                {
                    var snap = _sync.Current;
                    var jpg = snap == null ? null : await _sync.GetCoverAsync(snap.Key);
                    if (jpg == null) await WriteAsync(c, 404, "text/plain", Array.Empty<byte>());
                    else await WriteAsync(c, 200, "image/jpeg", jpg);
                    break;
                }

                case "GET /icon.png":
                {
                    var snap = _sync.Current;
                    if (snap == null) { await WriteAsync(c, 404, "text/plain", Array.Empty<byte>()); break; }
                    await WriteAsync(c, 200, "image/png", Images.IconPng(snap.AppId, snap.AppName));
                    break;
                }

                case "POST /api/cmd":
                {
                    var b = await BodyJsonAsync(c.Request);
                    bool ok = await _sync.RunCommandAsync(Str(b, "cmd"));
                    await JsonAsync(c, new { ok });
                    break;
                }

                case "POST /api/select-app":
                {
                    var b = await BodyJsonAsync(c.Request);
                    _cfg.SelectedAppId = Str(b, "id");
                    _cfg.Save();
                    // Propager vers l'ESP32 pour que celui-ci fetch meteo/RSS de lui-meme
                    _ = _esp.PostIdleConfigAsync(
                        (float)_cfg.Latitude, (float)_cfg.Longitude,
                        _cfg.LocationName ?? "",
                        _cfg.NewsRssUrl ?? "", _cfg.Timezone);
                    await JsonAsync(c, new { ok = true });
                    break;
                }

                case "POST /api/settings":
                {
                    var b = await BodyJsonAsync(c.Request);
                    var host = Str(b, "espHost", _cfg.EspHost).Trim();
                    if (host.Length > 0) _cfg.EspHost = host;
                    _cfg.EspToken = Str(b, "espToken", _cfg.EspToken);
                    _cfg.GithubRepo = Str(b, "githubRepo", _cfg.GithubRepo).Trim();
                    _cfg.Save();
                    _sync.EspOnline = false;      // force une reconnexion (et un renvoi des reglages)
                    await JsonAsync(c, new { ok = true });
                    break;
                }

                case "POST /api/lyrics-config":
                {
                    var b = await BodyJsonAsync(c.Request);
                    if (b.TryGetProperty("lrclib", out var lb) && (lb.ValueKind == JsonValueKind.True || lb.ValueKind == JsonValueKind.False)) _cfg.LyricsLrclib = lb.GetBoolean();
                    if (b.TryGetProperty("netease", out var nb) && (nb.ValueKind == JsonValueKind.True || nb.ValueKind == JsonValueKind.False)) _cfg.LyricsNetease = nb.GetBoolean();
                    _cfg.MusixmatchToken = Str(b, "musixmatchToken", _cfg.MusixmatchToken).Trim();
                    _cfg.Save();
                    await JsonAsync(c, new { ok = true });
                    break;
                }

                case "POST /api/esp-config":
                {
                    var b = await BodyJsonAsync(c.Request);
                    var s = _cfg.Esp;
                    if (b.TryGetProperty("brightness", out var br) && br.TryGetInt32(out var bri)) s.Brightness = Math.Clamp(bri, 5, 100);
                    if (b.TryGetProperty("scrollSpeed", out var sc) && sc.TryGetInt32(out var spd)) s.ScrollSpeed = Math.Clamp(spd, 10, 150);
                    if (b.TryGetProperty("showApp", out var sa) && (sa.ValueKind == JsonValueKind.True || sa.ValueKind == JsonValueKind.False)) s.ShowApp = sa.GetBoolean();
                    if (b.TryGetProperty("dynamicAccent", out var da) && (da.ValueKind == JsonValueKind.True || da.ValueKind == JsonValueKind.False)) s.DynamicAccent = da.GetBoolean();
                    if (b.TryGetProperty("showLyrics", out var sl) && (sl.ValueKind == JsonValueKind.True || sl.ValueKind == JsonValueKind.False)) s.ShowLyrics = sl.GetBoolean();
                    var acc = Str(b, "accent");
                    if (System.Text.RegularExpressions.Regex.IsMatch(acc, "^#[0-9A-Fa-f]{6}$")) s.Accent = acc.ToUpperInvariant();
                    _cfg.Save();
                    bool pushed = false;
                    try { pushed = await _esp.PushConfigAsync(_cfg); } catch (Exception e) { Log.Write("config: " + e.Message); }
                    await JsonAsync(c, new { ok = true, pushed });
                    break;
                }

                case "GET /api/geocode":
                {
                    // Recherche d'une ville (Open-Meteo geocoding) pour remplir lat/lon sans les connaitre
                    var q = c.Request.QueryString["q"] ?? "";
                    if (q.Trim().Length < 2) { await JsonAsync(c, new { results = Array.Empty<object>() }); break; }
                    using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(8) };
                    var gj = await http.GetStringAsync($"https://geocoding-api.open-meteo.com/v1/search?name={Uri.EscapeDataString(q.Trim())}&count=6&language=fr");
                    using var gd = JsonDocument.Parse(gj);
                    var list = new List<object>();
                    if (gd.RootElement.TryGetProperty("results", out var arr))
                        foreach (var it in arr.EnumerateArray())
                        {
                            string G(string n) => it.TryGetProperty(n, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";
                            double D(string n) => it.TryGetProperty(n, out var v) && v.TryGetDouble(out var d) ? d : 0;
                            list.Add(new { name = G("name"), admin = G("admin1"), country = G("country"), latitude = D("latitude"), longitude = D("longitude"), timezone = G("timezone") });
                        }
                    await JsonAsync(c, new { results = list });
                    break;
                }

                case "POST /api/wifi-reset":
                {
                    bool ok = await _esp.ResetWifiAsync();
                    _sync.EspOnline = false;
                    await JsonAsync(c, new { ok });
                    break;
                }

                case "POST /api/autostart":
                {
                    var b = await BodyJsonAsync(c.Request);
                    bool on = b.TryGetProperty("enabled", out var en) && en.ValueKind == JsonValueKind.True;
                    Autostart.Set(on);
                    await JsonAsync(c, new { ok = true });
                    break;
                }

                case "GET /api/idle-config":
                    await JsonAsync(c, new {
                        latitude      = _cfg.Latitude,
                        longitude     = _cfg.Longitude,
                        locationName  = _cfg.LocationName,
                        timezone      = _cfg.Timezone,
                        newsRssUrl    = _cfg.NewsRssUrl,
                        newsCount     = _cfg.NewsCount,
                        nightModeEnabled = _cfg.NightModeEnabled,
                        nightModeStart= _cfg.NightModeStart,
                        nightModeEnd  = _cfg.NightModeEnd,
                        nightBrightness = _cfg.NightBrightness,
                        dimAfterMinutes = _cfg.DimAfterMinutes,
                        dimBrightness = _cfg.DimBrightness,
                        weather = _idle.Weather == null ? null : (object)new {
                            temp        = _idle.Weather.Temp,
                            feelsLike   = _idle.Weather.FeelsLike,
                            humidity    = _idle.Weather.Humidity,
                            uvIndex     = _idle.Weather.UvIndex,
                            weatherCode = _idle.Weather.Code,
                description = _idle.Weather.Description,
                            windSpeed   = _idle.Weather.WindSpeed,
                            location    = _idle.Weather.Location,
                            sunrise     = _idle.Weather.Sunrise,
                            sunset      = _idle.Weather.Sunset
                        },
                        headlines = _idle.Headlines
                    });
                    break;

                case "POST /api/idle-config":
                {
                    var b = await BodyJsonAsync(c.Request);
                    if (b.TryGetProperty("latitude",       out var lat) && lat.TryGetDouble(out var latV))  _cfg.Latitude       = latV;
                    if (b.TryGetProperty("longitude",      out var lon) && lon.TryGetDouble(out var lonV))  _cfg.Longitude      = lonV;
                    if (b.TryGetProperty("locationName",   out var loc) && loc.ValueKind == JsonValueKind.String) _cfg.LocationName = loc.GetString()!;
                    if (b.TryGetProperty("newsRssUrl",     out var rss) && rss.ValueKind == JsonValueKind.String) _cfg.NewsRssUrl   = rss.GetString()!;
                    if (b.TryGetProperty("newsCount",      out var nc)  && nc.TryGetInt32(out var ncV))     _cfg.NewsCount      = Math.Clamp(ncV, 1, 10);
                    var tzIn = Str(b, "timezone");
                    if (System.Text.RegularExpressions.Regex.IsMatch(tzIn, @"^[A-Za-z0-9+\-:,./]{2,64}$")) _cfg.Timezone = tzIn;
                    if (b.TryGetProperty("nightModeEnabled", out var nen) && (nen.ValueKind == JsonValueKind.True || nen.ValueKind == JsonValueKind.False)) _cfg.NightModeEnabled = nen.GetBoolean();
                    if (b.TryGetProperty("nightBrightness",out var nb)  && nb.TryGetInt32(out var nbV))     _cfg.NightBrightness = Math.Clamp(nbV, 1, 100);
                    if (b.TryGetProperty("dimBrightness",  out var db)  && db.TryGetInt32(out var dbV))     _cfg.DimBrightness  = Math.Clamp(dbV, 1, 100);
                    if (b.TryGetProperty("nightModeStart", out var ns)  && ns.TryGetInt32(out var nsV))     _cfg.NightModeStart = Math.Clamp(nsV, 0, 23);
                    if (b.TryGetProperty("nightModeEnd",   out var ne)  && ne.TryGetInt32(out var neV))     _cfg.NightModeEnd   = Math.Clamp(neV, 0, 23);
                    if (b.TryGetProperty("dimAfterMinutes",out var dm)  && dm.TryGetInt32(out var dmV))     _cfg.DimAfterMinutes= Math.Clamp(dmV, 1, 60);
                    _cfg.Save();
                    // Tout envoyer a la carte (lieu / RSS + mode nuit / veille)
                    bool pushed = false;
                    try
                    {
                        bool a = await _esp.PostIdleConfigAsync(
                            (float)_cfg.Latitude, (float)_cfg.Longitude,
                            _cfg.LocationName ?? "", _cfg.NewsRssUrl ?? "", _cfg.Timezone);
                        bool b2 = await _esp.PushConfigAsync(_cfg);
                        pushed = a && b2;
                    }
                    catch (Exception e) { Log.Write("idle-config: " + e.Message); }
                    await JsonAsync(c, new { ok = true, pushed });
                    break;
                }

                case "GET /api/update/check":
                {
                    var rel = await _upd.CheckLatestAsync();
                    bool newer = rel.Tag != null && Updater.IsNewer(rel.Tag, _sync.EspVersion);
                    await JsonAsync(c, new { tag = rel.Tag, assetUrl = rel.AssetUrl, notes = rel.Notes, error = rel.Error, newer, current = _sync.EspVersion });
                    break;
                }

                case "POST /api/update/github":
                {
                    var b = await BodyJsonAsync(c.Request);
                    var url = Str(b, "url");
                    if (_upd.Status.Busy) { await JsonAsync(c, new { ok = false, error = "Une mise a jour est deja en cours." }, 409); break; }
                    _ = Task.Run(() => _upd.FlashFromGithubAsync(url));
                    await JsonAsync(c, new { ok = true });
                    break;
                }

                case "POST /api/update/file":
                {
                    if (_upd.Status.Busy) { await JsonAsync(c, new { ok = false, error = "Une mise a jour est deja en cours." }, 409); break; }
                    if (c.Request.ContentLength64 > 4_000_000) { await JsonAsync(c, new { ok = false, error = "Fichier trop gros." }, 413); break; }
                    using var ms = new MemoryStream();
                    await c.Request.InputStream.CopyToAsync(ms);
                    var fw = ms.ToArray();
                    _ = Task.Run(() => _upd.FlashBytesAsync(fw));
                    await JsonAsync(c, new { ok = true });
                    break;
                }

                default:
                    await WriteAsync(c, 404, "text/plain", Encoding.UTF8.GetBytes("not found"));
                    break;
            }
        }
        catch (Exception e)
        {
            Log.Write("panel: " + e.Message);
            try { await JsonAsync(c, new { ok = false, error = e.Message }, 500); } catch { }
        }
    }

    async Task<object> BuildStateAsync()
    {
        List<SessionInfo> sessions;
        try { sessions = await _media.ListAsync(); } catch { sessions = new(); }
        var n = _sync.Current;
        return new
        {
            appVersion = AppVersion,
            esp = new { online = _sync.EspOnline, host = _cfg.EspHost, version = _sync.EspVersion, ip = _sync.EspIp, error = _sync.LastError },
            now = n == null ? (object?)null : new { n.State, n.Title, n.Artist, n.Album, n.Position, n.Duration, app = n.AppName, key = n.Key },
            sessions,
            selectedAppId = _cfg.SelectedAppId,
            settings = new { espHost = _cfg.EspHost, espToken = _cfg.EspToken, githubRepo = _cfg.GithubRepo, lyricsLrclib = _cfg.LyricsLrclib, lyricsNetease = _cfg.LyricsNetease, musixmatchToken = _cfg.MusixmatchToken },
            espConfig = _cfg.Esp,
            update = new { busy = _upd.Status.Busy, percent = _upd.Status.Percent, message = _upd.Status.Message },
            autostart = Autostart.Get(),
            idleConfig = new {
                latitude       = _cfg.Latitude,
                longitude      = _cfg.Longitude,
                locationName   = _cfg.LocationName,
                timezone       = _cfg.Timezone,
                newsRssUrl     = _cfg.NewsRssUrl,
                newsCount      = _cfg.NewsCount,
                nightModeEnabled = _cfg.NightModeEnabled,
                nightModeStart = _cfg.NightModeStart,
                nightModeEnd   = _cfg.NightModeEnd,
                nightBrightness = _cfg.NightBrightness,
                dimAfterMinutes= _cfg.DimAfterMinutes,
                dimBrightness  = _cfg.DimBrightness
            },
            weather = _idle.Weather == null ? null : (object)new {
                temp        = _idle.Weather.Temp,
                feelsLike   = _idle.Weather.FeelsLike,
                humidity    = _idle.Weather.Humidity,
                uvIndex     = _idle.Weather.UvIndex,
                weatherCode = _idle.Weather.Code,
                description = _idle.Weather.Description,
                windSpeed   = _idle.Weather.WindSpeed,
                location    = _idle.Weather.Location,
                sunrise     = _idle.Weather.Sunrise,
                sunset      = _idle.Weather.Sunset
            }
        };
    }
}
