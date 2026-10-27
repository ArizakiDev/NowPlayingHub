using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;

namespace NowPlayingHub;

public record EspReply(bool NeedCover, bool NeedIcon, string? Cmd);
public record EspStatus(string Version, string Ip);

/// <summary>Contenu HTTP envoye par blocs pour suivre la progression de l'upload du firmware.</summary>
sealed class ProgressContent : HttpContent
{
    readonly byte[] _data;
    readonly Action<int> _progress;
    public int Sent { get; private set; }
    public ProgressContent(byte[] data, Action<int> progress) { _data = data; _progress = progress; }

    protected override async Task SerializeToStreamAsync(Stream stream, TransportContext? context)
    {
        const int chunk = 4096;
        for (int off = 0; off < _data.Length; off += chunk)
        {
            int n = Math.Min(chunk, _data.Length - off);
            await stream.WriteAsync(_data.AsMemory(off, n));
            Sent = (int)((long)(off + n) * 100 / _data.Length);
            _progress(Sent);
        }
    }

    protected override bool TryComputeLength(out long length) { length = _data.Length; return true; }
}

/// <summary>Client HTTP de l'ESP32.</summary>
public class EspClient
{
    readonly AppConfig _cfg;
    readonly HttpClient _http = new() { Timeout = TimeSpan.FromSeconds(3) };
    readonly HttpClient _long = new() { Timeout = TimeSpan.FromMinutes(3) };
    public EspClient(AppConfig cfg) { _cfg = cfg; }

    string Base => $"http://{_cfg.EspHost}";

    static bool Bool(JsonElement e, string name) =>
        e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.True;

    public async Task<EspReply?> PostNowPlayingAsync(string json)
    {
        using var content = new StringContent(json, Encoding.UTF8, "application/json");
        using var r = await _http.PostAsync(Base + "/api/nowplaying", content);
        if (!r.IsSuccessStatusCode) return null;
        using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync());
        var e = doc.RootElement;
        string? cmd = e.TryGetProperty("cmd", out var c) && c.ValueKind == JsonValueKind.String ? c.GetString() : null;
        return new EspReply(Bool(e, "needCover"), Bool(e, "needIcon"), cmd);
    }

    async Task<bool> PostBase64Async(string path, string id, byte[] jpeg)
    {
        using var content = new StringContent(Convert.ToBase64String(jpeg), Encoding.ASCII, "text/plain");
        using var r = await _http.PostAsync($"{Base}{path}?id={Uri.EscapeDataString(id)}", content);
        return r.IsSuccessStatusCode;
    }

    public Task<bool> PostCoverAsync(string id, byte[] jpeg) => PostBase64Async("/api/cover", id, jpeg);
    public Task<bool> PostIconAsync(string id, byte[] jpeg) => PostBase64Async("/api/icon", id, jpeg);

    public async Task<EspStatus?> GetStatusAsync()
    {
        using var r = await _http.GetAsync(Base + "/api/status");
        if (!r.IsSuccessStatusCode) return null;
        using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync());
        var e = doc.RootElement;
        string S(string n) => e.TryGetProperty(n, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";
        return new EspStatus(S("version"), S("ip"));
    }

    /// <summary>Envoie tous les reglages de la carte (affichage, mode nuit, veille).</summary>
    public async Task<bool> PushConfigAsync(AppConfig c)
    {
        var s = c.Esp;
        var json = JsonSerializer.Serialize(new
        {
            brightness = s.Brightness,
            accent = s.Accent,
            scrollSpeed = s.ScrollSpeed,
            showApp = s.ShowApp,
            nightOn = c.NightModeEnabled,
            nightStart = c.NightModeStart,
            nightEnd = c.NightModeEnd,
            nightBri = c.NightBrightness,
            dimMin = c.DimAfterMinutes,
            dimBri = c.DimBrightness
        });
        using var content = new StringContent(json, Encoding.UTF8, "application/json");
        using var r = await _http.PostAsync(Base + "/api/config", content);
        return r.IsSuccessStatusCode;
    }

    /// <summary>Envoie les donnees de l'ecran de veille (meteo + news) a l'ESP32.</summary>
    public async Task<bool> PostIdleAsync(WeatherData? weather, List<string> headlines)
    {
        var payload = new
        {
            temp        = weather?.Temp ?? 0f,
            feelsLike   = weather?.FeelsLike ?? 0f,
            humidity    = weather?.Humidity ?? 0f,
            uvIndex     = weather?.UvIndex ?? -1f,
            weatherCode = weather?.Code ?? -1,
            description = weather?.Description ?? "",
            windSpeed   = weather?.WindSpeed ?? 0f,
            location    = weather?.Location ?? "",
            sunrise     = weather?.Sunrise ?? "",
            sunset      = weather?.Sunset ?? "",
            hasWeather  = weather != null,
            news        = headlines.Take(8).ToArray()
        };
        var json = JsonSerializer.Serialize(payload);
        using var content = new StringContent(json, Encoding.UTF8, "application/json");
        try
        {
            using var r = await _http.PostAsync(Base + "/api/idle", content);
            if (!r.IsSuccessStatusCode)
                Log.Write($"idle POST HTTP {(int)r.StatusCode}");
            return r.IsSuccessStatusCode;
        }
        catch (Exception ex) { Log.Write("idle POST: " + ex.Message); return false; }
    }

    /// <summary>Envoie la config idle (lat/lon/loc/rss) directement a l'ESP32.</summary>
    public async Task<bool> PostIdleConfigAsync(float lat, float lon, string loc, string rssUrl, string tz)
    {
        var payload = new { lat, lon, loc, rssUrl, tz };
        var json = JsonSerializer.Serialize(payload);
        using var content = new StringContent(json, Encoding.UTF8, "application/json");
        try
        {
            using var r = await _http.PostAsync(Base + "/api/idleconfig", content);
            if (!r.IsSuccessStatusCode)
                Log.Write($"idleconfig POST HTTP {(int)r.StatusCode}");
            return r.IsSuccessStatusCode;
        }
        catch (Exception ex) { Log.Write("idleconfig POST: " + ex.Message); return false; }
    }

    /// <summary>Efface le Wi-Fi memorise dans la carte : elle redemarre en mode configuration.</summary>
    public async Task<bool> ResetWifiAsync()
    {
        try
        {
            using var r = await _http.PostAsync($"{Base}/api/wifireset?token={Uri.EscapeDataString(_cfg.EspToken)}", new StringContent(""));
            return r.IsSuccessStatusCode;
        }
        catch (Exception e) { Log.Write("wifireset: " + e.Message); return false; }
    }

    /// <summary>Envoie firmware.bin a l'ESP32 (OTA). Retourne (succes, message).</summary>
    public async Task<(bool ok, string msg)> FlashAsync(byte[] fw, Action<int> progress)
    {
        if (fw.Length < 100_000 || fw[0] != 0xE9)
            return (false, "Fichier invalide : ce n'est pas un firmware ESP32 (firmware.bin).");

        const string boundary = "NPHboundary7d3a91";
        using var form = new MultipartFormDataContent(boundary);
        // Boundary sans guillemets (le WebServer de l'ESP32 est plus tolerant ainsi)
        form.Headers.ContentType = MediaTypeHeaderValue.Parse("multipart/form-data; boundary=" + boundary);
        var part = new ProgressContent(fw, progress);
        part.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
        form.Add(part, "firmware", "firmware.bin");

        var url = $"{Base}/api/update?token={Uri.EscapeDataString(_cfg.EspToken)}&size={fw.Length}";
        try
        {
            using var r = await _long.PostAsync(url, form);
            var body = await r.Content.ReadAsStringAsync();
            if (r.IsSuccessStatusCode) return (true, "Mise a jour installee, la carte redemarre.");
            if (r.StatusCode == HttpStatusCode.Forbidden) return (false, "Jeton refuse : verifie qu'il est identique a OTA_TOKEN dans config.h.");
            return (false, $"Refuse par l'ESP32 ({(int)r.StatusCode}) {body}");
        }
        catch (Exception e)
        {
            // La carte redemarre juste apres la reponse : une coupure a 100 % est probablement un succes.
            if (part.Sent >= 100) return (true, "Firmware envoye a 100 %, la carte redemarre (verifie son retour en ligne).");
            return (false, "Echec : " + e.Message);
        }
    }
}
