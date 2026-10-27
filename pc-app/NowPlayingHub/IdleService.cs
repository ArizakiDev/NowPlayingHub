using System.Globalization;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Xml.Linq;

namespace NowPlayingHub;

public record WeatherData(float Temp, float FeelsLike, float Humidity, float UvIndex, int Code, string Description, float WindSpeed, string Location, string Sunrise, string Sunset);

/// <summary>Recupere la meteo (Open-Meteo) et les news (RSS) pour l'ecran de veille.</summary>
public class IdleService
{
    readonly AppConfig _cfg;
    readonly HttpClient _http = new() { Timeout = TimeSpan.FromSeconds(15) };

    public WeatherData? Weather { get; private set; }
    public List<string> Headlines { get; private set; } = new();

    DateTime _lastWeather = DateTime.MinValue;
    DateTime _lastNews    = DateTime.MinValue;

    // Flux RSS de secours si le flux principal echoue
    static readonly string[] FallbackRssUrls =
    {
        "https://www.rfi.fr/fr/rss-podcasts-emissions",
        "https://news.google.com/rss?hl=fr&gl=FR&ceid=FR:fr",
    };

    static readonly Dictionary<int, string> WmoCodes = new()
    {
        {0,"Ciel degage"},{1,"Peu nuageux"},{2,"Partiellement nuageux"},{3,"Couvert"},
        {45,"Brouillard"},{48,"Brouillard givrant"},
        {51,"Bruine legere"},{53,"Bruine"},{55,"Bruine forte"},
        {61,"Pluie legere"},{63,"Pluie"},{65,"Pluie forte"},
        {71,"Neige legere"},{73,"Neige"},{75,"Neige forte"},{77,"Grains de neige"},
        {80,"Averses legeres"},{81,"Averses"},{82,"Averses fortes"},
        {85,"Averses de neige"},{86,"Averses de neige fortes"},
        {95,"Orage"},{96,"Orage avec grele"},{99,"Orage avec grele forte"}
    };

    static string WmoDesc(int code)
    {
        if (WmoCodes.TryGetValue(code, out var d)) return d;
        var k = WmoCodes.Keys.Where(k => k <= code).OrderByDescending(k => k).FirstOrDefault();
        return WmoCodes.TryGetValue(k, out var d2) ? d2 : "Meteo inconnue";
    }

    public IdleService(AppConfig cfg)
    {
        _cfg = cfg;
        _http.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 NowPlayingHub/1.0");
    }

    public async Task RunAsync(CancellationToken ct)
    {
        // Premier fetch immédiat au démarrage
        await TryRefreshWeather();
        await TryRefreshNews();

        while (!ct.IsCancellationRequested)
        {
            try { await Task.Delay(TimeSpan.FromMinutes(1), ct); } catch (OperationCanceledException) { break; }
            await TryRefreshWeather();
            await TryRefreshNews();
        }
    }

    Task TryRefreshWeather() => SafeRun("idle/meteo", RefreshWeatherAsync);
    Task TryRefreshNews()    => SafeRun("idle/news",  RefreshNewsAsync);

    async Task SafeRun(string tag, Func<Task> fn)
    {
        try { await fn(); }
        catch (Exception e) { Log.Write($"{tag}: {e.Message}"); }
    }

    async Task RefreshWeatherAsync()
    {
        if ((DateTime.UtcNow - _lastWeather).TotalMinutes < 10) return;

        // IMPORTANT : InvariantCulture pour le point décimal (50.85 et non 50,85)
        var lat = _cfg.Latitude.ToString("F4", CultureInfo.InvariantCulture);
        var lon = _cfg.Longitude.ToString("F4", CultureInfo.InvariantCulture);

        var url = $"https://api.open-meteo.com/v1/forecast" +
                  $"?latitude={lat}&longitude={lon}" +
                  $"&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m,relative_humidity_2m,uv_index" +
                  $"&daily=sunrise,sunset" +
                  $"&timezone=auto&forecast_days=1";

        Log.Write($"Meteo fetch: lat={lat} lon={lon}");
        using var r = await _http.GetAsync(url);
        if (!r.IsSuccessStatusCode)
        {
            var body = await r.Content.ReadAsStringAsync();
            Log.Write($"Meteo HTTP {(int)r.StatusCode}: {body[..Math.Min(200, body.Length)]}");
            return;
        }
        var json = await r.Content.ReadAsStringAsync();
        using var doc = JsonDocument.Parse(json);
        var cur   = doc.RootElement.GetProperty("current");
        var daily = doc.RootElement.GetProperty("daily");

        float temp  = cur.GetProperty("temperature_2m").GetSingle();
        float feels = cur.GetProperty("apparent_temperature").GetSingle();
        int   code  = cur.GetProperty("weather_code").GetInt32();
        float wind  = cur.GetProperty("wind_speed_10m").GetSingle();
        float hum   = cur.TryGetProperty("relative_humidity_2m", out var hv) ? hv.GetSingle() : 0;
        float uv    = cur.TryGetProperty("uv_index", out var uvi) ? uvi.GetSingle() : -1;

        string sunrise = "", sunset = "";
        if (daily.TryGetProperty("sunrise", out var srArr) && srArr.GetArrayLength() > 0)
        {
            var raw = srArr[0].GetString() ?? "";
            sunrise = raw.Length >= 16 ? raw[11..16] : raw; // "2026-10-27T07:24" -> "07:24"
        }
        if (daily.TryGetProperty("sunset", out var ssArr) && ssArr.GetArrayLength() > 0)
        {
            var raw = ssArr[0].GetString() ?? "";
            sunset = raw.Length >= 16 ? raw[11..16] : raw;
        }

        Weather = new WeatherData((float)temp, (float)feels, (float)hum, (float)uv,
                                  code, WmoDesc(code), (float)wind, _cfg.LocationName, sunrise, sunset);
        _lastWeather = DateTime.UtcNow;
        Log.Write($"Meteo OK: {temp:F1}C {WmoDesc(code)} hum={hum:F0}% uv={uv:F1} sr={sunrise} ss={sunset}");
    }

    async Task RefreshNewsAsync()
    {
        if ((DateTime.UtcNow - _lastNews).TotalMinutes < 30) return;
        if (string.IsNullOrWhiteSpace(_cfg.NewsRssUrl)) return;

        // Essaie le flux configuré puis les fallbacks
        var urls = new[] { _cfg.NewsRssUrl }.Concat(FallbackRssUrls).Distinct();
        foreach (var url in urls)
        {
            try
            {
                var titles = await FetchRssTitlesAsync(url, _cfg.NewsCount);
                if (titles.Count > 0)
                {
                    Headlines = titles;
                    _lastNews = DateTime.UtcNow;
                    Log.Write($"News OK: {titles.Count} titres depuis {url}");
                    return;
                }
            }
            catch (Exception e)
            {
                Log.Write($"idle/news ({url}): {e.Message}");
            }
        }
        Log.Write("idle/news: aucun flux RSS fonctionnel");
    }

    async Task<List<string>> FetchRssTitlesAsync(string url, int maxCount)
    {
        using var resp = await _http.GetAsync(url);
        if (!resp.IsSuccessStatusCode) return new();

        var body = await resp.Content.ReadAsStringAsync();

        // Détection HTML : si le corps commence par < !doctype ou <html, c'est pas du RSS
        var trimmed = body.TrimStart();
        if (trimmed.StartsWith("<!") || trimmed.StartsWith("<html", StringComparison.OrdinalIgnoreCase))
        {
            Log.Write($"idle/news ({url}): reponse HTML (redirection/erreur), ignore");
            return new();
        }

        var doc   = XDocument.Parse(body);
        var ns    = XNamespace.Get("http://www.w3.org/2005/Atom");
        var items = doc.Descendants("item").Concat(doc.Descendants(ns + "entry")).Take(maxCount);
        return items
            .Select(i => (string?)(i.Element("title") ?? i.Element(ns + "title")))
            .Where(t => !string.IsNullOrWhiteSpace(t))
            .Select(t => t!.Trim())
            .ToList();
    }
}
