using System.Globalization;
using System.Net;
using System.Text.RegularExpressions;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Xml.Linq;

namespace NowPlayingHub;

/// <summary>Une actualite : titre + resume nettoye + heure de publication + source (v1.8, pour l'ecran detail de l'ESP32).</summary>
public record NewsItem(string Title, string Desc, string When, string Source);

public record WeatherData(float Temp, float FeelsLike, float Humidity, float UvIndex, int Code, string Description, float WindSpeed, string Location, string Sunrise, string Sunset);

/// <summary>Recupere la meteo (Open-Meteo) et les news (RSS) pour l'ecran de veille.</summary>
public class IdleService
{
    readonly AppConfig _cfg;
    readonly HttpClient _http = new() { Timeout = TimeSpan.FromSeconds(15) };

    public WeatherData? Weather { get; private set; }
    public List<string> Headlines { get; private set; } = new();
    public List<NewsItem> NewsItems { get; private set; } = new();   // meme ordre que Headlines

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
                var items = await FetchRssItemsAsync(url, _cfg.NewsCount);
                if (items.Count > 0)
                {
                    NewsItems = items;
                    Headlines = items.Select(i => i.Title).ToList();
                    _lastNews = DateTime.UtcNow;
                    Log.Write($"News OK: {items.Count} titres ({items.Count(i => i.Desc.Length > 0)} avec resume) depuis {url}");
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

    async Task<List<NewsItem>> FetchRssItemsAsync(string url, int maxCount)
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
        var dc    = XNamespace.Get("http://purl.org/dc/elements/1.1/");
        // Source par defaut = titre du flux (ex. "Le Monde - A la une")
        var channelTitle = Clip(CleanText((string?)(doc.Descendants("channel").Elements("title").FirstOrDefault()
                                                  ?? doc.Root?.Element(ns + "title")) ?? ""), 28);
        var nodes = doc.Descendants("item").Concat(doc.Descendants(ns + "entry")).Take(maxCount);

        var list = new List<NewsItem>();
        foreach (var n in nodes)
        {
            var title = CleanText((string?)(n.Element("title") ?? n.Element(ns + "title")) ?? "");
            if (title.Length == 0) continue;

            // Resume : <description> (RSS) ou <summary>/<content> (Atom) ; HTML retire, entites decodees
            var rawDesc = (string?)(n.Element("description") ?? n.Element(ns + "summary") ?? n.Element(ns + "content")) ?? "";
            var desc = CleanText(rawDesc);
            // Beaucoup de flux (Google News...) repetent le titre dans le resume : on le retire, s'il ne reste rien on n'envoie rien
            if (desc.StartsWith(title, StringComparison.OrdinalIgnoreCase)) desc = desc.Substring(title.Length).TrimStart(' ', '-', '–', ':', '.');
            if (desc.Length < 15) desc = "";
            desc = Clip(desc, 280);

            // Heure de publication, en heure locale : "HH:mm" si aujourd'hui, sinon "dd/MM HH:mm"
            var pub = (string?)(n.Element("pubDate") ?? n.Element(ns + "published") ?? n.Element(ns + "updated") ?? n.Element(dc + "date"));
            var when = "";
            if (!string.IsNullOrWhiteSpace(pub) &&
                DateTimeOffset.TryParse(pub, CultureInfo.InvariantCulture, DateTimeStyles.AssumeUniversal, out var dto))
            {
                var local = dto.ToLocalTime();
                when = local.Date == DateTime.Now.Date ? local.ToString("HH:mm", CultureInfo.InvariantCulture)
                                                       : local.ToString("dd/MM HH:mm", CultureInfo.InvariantCulture);
            }

            // Source : <source> (Google News, RSS 2.0) sinon titre du flux
            var src = Clip(CleanText((string?)n.Element("source") ?? ""), 28);
            if (src.Length == 0) src = channelTitle;

            list.Add(new NewsItem(title, desc, when, src));
        }
        return list;
    }

    static readonly Regex _tags = new("<[^>]+>", RegexOptions.Compiled);
    static readonly Regex _ws   = new("\\s+", RegexOptions.Compiled);

    /// <summary>Retire le HTML, decode les entites (&amp;eacute; ...), compacte les espaces.</summary>
    static string CleanText(string s)
    {
        if (string.IsNullOrEmpty(s)) return "";
        s = _tags.Replace(s, " ");
        s = WebUtility.HtmlDecode(WebUtility.HtmlDecode(s)).Replace('\u00A0', ' ');   // double decode : certains flux echappent deux fois
        return _ws.Replace(s, " ").Trim();
    }

    /// <summary>Coupe a max caracteres sur une frontiere de mot, ajoute "..." si coupe.</summary>
    static string Clip(string s, int max)
    {
        if (s.Length <= max) return s;
        var cut = s.LastIndexOf(' ', max - 3);
        if (cut < max / 2) cut = max - 3;
        return s.Substring(0, cut).TrimEnd(' ', ',', ';', ':', '-') + "...";
    }
}
