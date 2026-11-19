using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;

namespace NowPlayingHub;

public record EspReply(bool NeedCover, bool NeedIcon, string? Cmd, bool NeedBig = false, bool Viz = false);

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

public sealed class EspInfo
{
    public string Version { get; init; } = "";
    public string Ip { get; init; } = "";
    public string State { get; init; } = "";
    public string Screen { get; init; } = "";
    public long Heap { get; init; }
    public long MaxAlloc { get; init; }
    public int Rssi { get; init; }
    public long UptimeSec { get; init; }
    public double ChipTemp { get; init; }
    public int CpuMhz { get; init; }
    public int LdrRaw { get; init; }
    public int LdrLight { get; init; }
    public bool AutoBri { get; init; }
    public bool Night { get; init; }
    public int Brightness { get; init; }
    public bool Viz { get; init; }
    public long VizPackets { get; init; }
    public bool SdEnabled { get; init; }
    public bool SdOk { get; init; }
    public long SdSizeMb { get; init; }
    public long SdFiles { get; init; }

    public string ChipModel { get; init; } = "";
    public int ChipRev { get; init; }
    public int Cores { get; init; }
    public string Sdk { get; init; } = "";
    public string ResetReason { get; init; } = "";
    public long HeapTotal { get; init; }
    public long HeapMinFree { get; init; }
    public int FragPct { get; init; }
    public long FlashSize { get; init; }
    public int FlashMhz { get; init; }
    public long SketchSize { get; init; }
    public long FreeSketch { get; init; }
    public string Ssid { get; init; } = "";
    public int Channel { get; init; }
    public long LoopAvgUs { get; init; }
    public long LoopWorstUs { get; init; }
    public long LoopHz { get; init; }
    public long LoopSlow { get; init; }
    public long LoopSlowTotal { get; init; }
    public long StackFree { get; init; }

    public bool HasV19 => Cores > 0;
    public string Raw { get; init; } = "";

    public static EspInfo Parse(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var e = doc.RootElement;

        static JsonElement? Sub(JsonElement o, string n) =>
            o.TryGetProperty(n, out var v) && v.ValueKind == JsonValueKind.Object ? v : null;
        static string S(JsonElement? o, string n) =>
            o is { } x && x.TryGetProperty(n, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";
        static long L(JsonElement? o, string n) =>
            o is { } x && x.TryGetProperty(n, out var v) && v.TryGetInt64(out var l) ? l : 0;
        static double D(JsonElement? o, string n) =>
            o is { } x && x.TryGetProperty(n, out var v) && v.TryGetDouble(out var d) ? d : 0;
        static bool B(JsonElement? o, string n) =>
            o is { } x && x.TryGetProperty(n, out var v) && v.ValueKind == JsonValueKind.True;

        var sd = Sub(e, "sd"); var chip = Sub(e, "chip"); var heap = Sub(e, "heapInfo");
        var flash = Sub(e, "flash"); var wifi = Sub(e, "wifi"); var loop = Sub(e, "loop");

        return new EspInfo
        {
            Version = S(e, "version"), Ip = S(e, "ip"), State = S(e, "state"), Screen = S(e, "screen"),
            Heap = L(e, "heap"), MaxAlloc = L(e, "maxAlloc"), Rssi = (int)L(e, "rssi"), UptimeSec = L(e, "uptime"),
            ChipTemp = D(e, "chipTemp"), CpuMhz = (int)L(e, "cpuMhz"),
            LdrRaw = (int)L(e, "ldrRaw"), LdrLight = (int)L(e, "ldrLight"), AutoBri = B(e, "autoBri"),
            Night = B(e, "night"), Brightness = (int)L(e, "brightnessPct"), Viz = B(e, "viz"), VizPackets = L(e, "vizPackets"),
            SdEnabled = B(sd, "enabled"), SdOk = B(sd, "ok"), SdSizeMb = L(sd, "sizeMB"), SdFiles = L(sd, "files"),

            ChipModel = S(chip, "model"), ChipRev = (int)L(chip, "rev"), Cores = (int)L(chip, "cores"), Sdk = S(chip, "sdk"),
            ResetReason = S(e, "reset"),
            HeapTotal = L(heap, "total"), HeapMinFree = L(heap, "minFree"), FragPct = (int)L(heap, "fragPct"),
            FlashSize = L(flash, "size"), FlashMhz = (int)L(flash, "mhz"), SketchSize = L(flash, "sketch"), FreeSketch = L(flash, "freeSketch"),
            Ssid = S(wifi, "ssid"), Channel = (int)L(wifi, "channel"),
            LoopAvgUs = L(loop, "avgUs"), LoopWorstUs = L(loop, "worstUs"), LoopHz = L(loop, "hz"),
            LoopSlow = L(loop, "slow"), LoopSlowTotal = L(loop, "slowTotal"),
            StackFree = L(e, "stackFree"),
            Raw = json
        };
    }
}

public class EspClient
{
    readonly AppConfig _cfg;
    static HttpClient Make(TimeSpan timeout) =>
        new(new SocketsHttpHandler { PooledConnectionLifetime = TimeSpan.FromMinutes(2) }) { Timeout = timeout };
    readonly HttpClient _http = Make(TimeSpan.FromSeconds(3));
    readonly HttpClient _long = Make(TimeSpan.FromMinutes(3));
    readonly HttpClient _img = Make(TimeSpan.FromSeconds(10));

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
        return new EspReply(Bool(e, "needCover"), Bool(e, "needIcon"), cmd, Bool(e, "needBig"), Bool(e, "viz"));
    }

    async Task<bool> PostImageAsync(string path, string id, byte[] jpeg)
    {
        const string boundary = "NPHimg5c1e47";
        using var form = new MultipartFormDataContent(boundary);
        form.Headers.ContentType = MediaTypeHeaderValue.Parse("multipart/form-data; boundary=" + boundary);
        var part = new ByteArrayContent(jpeg);
        part.Headers.ContentType = new MediaTypeHeaderValue("image/jpeg");
        form.Add(part, "file", "img.jpg");
        using var r = await _img.PostAsync($"{Base}{path}?id={Uri.EscapeDataString(id)}", form);
        return r.IsSuccessStatusCode;
    }

    public Task<bool> PostCoverAsync(string id, byte[] jpeg) => PostImageAsync("/api/cover", id, jpeg);
    public Task<bool> PostIconAsync(string id, byte[] jpeg) => PostImageAsync("/api/icon", id, jpeg);
    public Task<bool> PostBigCoverAsync(string id, byte[] jpeg) => PostImageAsync("/api/coverbig", id, jpeg);

    async Task<bool> PostJsonAsync(string path, object payload)
    {
        using var content = new StringContent(JsonSerializer.Serialize(payload), Encoding.UTF8, "application/json");
        using var r = await _http.PostAsync(Base + path, content);
        if (!r.IsSuccessStatusCode) Log.Write($"POST {path} : HTTP {(int)r.StatusCode}");
        return r.IsSuccessStatusCode;
    }

    public Task<bool> CalibrateLdrAsync(string which) => PostJsonAsync("/api/config", new { ldrCal = which });

    public Task<bool> ClearSdAsync() => PostJsonAsync("/api/sdclear", new { });

    public async Task<EspInfo?> GetInfoAsync()
    {
        using var r = await _http.GetAsync(Base + "/api/status");
        return r.IsSuccessStatusCode ? EspInfo.Parse(await r.Content.ReadAsStringAsync()) : null;
    }

    public Task<bool> PushConfigAsync(AppConfig c)
    {
        var s = c.Esp;
        return PostJsonAsync("/api/config", new
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
            dimBri = c.DimBrightness,
            autoBri = s.AutoBrightness,
            ledMode = s.LedMode,
            ledBri = s.LedBrightness,
            ledNightOff = s.LedNightOff,
            sdCache = s.SdCache,
            sdMax = s.SdMax
        });
    }

    public Task<bool> PostIdleConfigAsync(AppConfig c) => PostJsonAsync("/api/idleconfig", new
    {
        lat = (float)c.Latitude, lon = (float)c.Longitude,
        loc = c.LocationName ?? "", rssUrl = c.NewsRssUrl ?? "", tz = c.Timezone
    });

    public Task<bool> PostIdleAsync(WeatherData? weather, List<string> headlines, List<NewsItem>? items = null) => PostJsonAsync("/api/idle", new
    {
        temp = weather?.Temp ?? 0f,
        feelsLike = weather?.FeelsLike ?? 0f,
        humidity = weather?.Humidity ?? 0f,
        uvIndex = weather?.UvIndex ?? -1f,
        weatherCode = weather?.Code ?? -1,
        description = weather?.Description ?? "",
        windSpeed = weather?.WindSpeed ?? 0f,
        location = weather?.Location ?? "",
        sunrise = weather?.Sunrise ?? "",
        sunset = weather?.Sunset ?? "",
        hasWeather = weather != null,
        news = (items == null || items.Count == 0) ? headlines.Take(8).ToArray() : Array.Empty<string>(),
        newsItems = (items ?? new List<NewsItem>()).Take(8).Select(i => new { t = i.Title, d = i.Desc, p = i.When, s = i.Source }).ToArray()
    });

    public async Task<bool> RebootAsync()
    {
        try
        {
            using var r = await _http.PostAsync($"{Base}/api/reboot?token={Uri.EscapeDataString(_cfg.EspToken)}", new StringContent(""));
            return r.IsSuccessStatusCode;
        }
        catch (Exception e) { Log.Write("reboot : " + e.Message); return false; }
    }

    public async Task<bool> ResetWifiAsync()
    {
        try
        {
            using var r = await _http.PostAsync($"{Base}/api/wifireset?token={Uri.EscapeDataString(_cfg.EspToken)}", new StringContent(""));
            return r.IsSuccessStatusCode;
        }
        catch (Exception e) { Log.Write("wifireset : " + e.Message); return false; }
    }

    public async Task<(bool ok, string msg)> FlashAsync(byte[] fw, Action<int> progress)
    {
        if (fw.Length < 100_000 || fw[0] != 0xE9)
            return (false, "Fichier invalide : ce n'est pas un firmware ESP32 (firmware.bin).");

        const string boundary = "NPHboundary7d3a91";
        using var form = new MultipartFormDataContent(boundary);
        form.Headers.ContentType = MediaTypeHeaderValue.Parse("multipart/form-data; boundary=" + boundary);
        var part = new ProgressContent(fw, progress);
        part.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
        form.Add(part, "firmware", "firmware.bin");

        var url = $"{Base}/api/update?token={Uri.EscapeDataString(_cfg.EspToken)}&size={fw.Length}";
        try
        {
            using var r = await _long.PostAsync(url, form);
            var body = await r.Content.ReadAsStringAsync();
            if (r.IsSuccessStatusCode) return (true, "Mise à jour installée, la carte redémarre.");
            if (r.StatusCode == HttpStatusCode.Forbidden) return (false, "Jeton refusé : il doit être identique à celui de la carte (portail Wi-Fi, ou OTA_TOKEN de config.h).");
            return (false, $"Refusé par l'ESP32 ({(int)r.StatusCode}) {body}");
        }
        catch (Exception e)
        {
            if (part.Sent >= 100) return (true, "Firmware envoyé à 100 %, la carte redémarre (vérifie son retour en ligne).");
            return (false, "Échec : " + e.Message);
        }
    }
}
