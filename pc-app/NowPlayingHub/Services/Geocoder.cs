using System.Net.Http;
using System.Text.Json;

namespace NowPlayingHub;

public sealed record GeoResult(string Name, string Admin, string Country, double Latitude, double Longitude, string Timezone)
{
    public string Label => string.Join(", ", new[] { Name, Admin, Country }.Where(x => x.Length > 0));
}

public static class Geocoder
{
    static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(8) };

    public static async Task<List<GeoResult>> SearchAsync(string query)
    {
        var res = new List<GeoResult>();
        if (query.Trim().Length < 2) return res;
        var json = await Http.GetStringAsync(
            $"https://geocoding-api.open-meteo.com/v1/search?name={Uri.EscapeDataString(query.Trim())}&count=6&language=fr");
        using var doc = JsonDocument.Parse(json);
        if (!doc.RootElement.TryGetProperty("results", out var arr)) return res;
        foreach (var it in arr.EnumerateArray())
        {
            string S(string n) => it.TryGetProperty(n, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";
            double D(string n) => it.TryGetProperty(n, out var v) && v.TryGetDouble(out var d) ? d : 0;
            res.Add(new GeoResult(S("name"), S("admin1"), S("country"), D("latitude"), D("longitude"), S("timezone")));
        }
        return res;
    }

    const string Cet = "CET-1CEST,M3.5.0,M10.5.0/3";
    const string Eet = "EET-2EEST,M3.5.0/3,M10.5.0/4";
    const string UsEast = "EST5EDT,M3.2.0,M11.1.0";
    const string UsCentral = "CST6CDT,M3.2.0,M11.1.0";
    const string UsMountain = "MST7MDT,M3.2.0,M11.1.0";
    const string UsPacific = "PST8PDT,M3.2.0,M11.1.0";

    static readonly Dictionary<string, string> Posix = new(StringComparer.OrdinalIgnoreCase)
    {
        ["Europe/Brussels"] = Cet, ["Europe/Paris"] = Cet, ["Europe/Berlin"] = Cet, ["Europe/Amsterdam"] = Cet,
        ["Europe/Madrid"] = Cet, ["Europe/Rome"] = Cet, ["Europe/Vienna"] = Cet, ["Europe/Zurich"] = Cet,
        ["Europe/Luxembourg"] = Cet, ["Europe/Stockholm"] = Cet, ["Europe/Oslo"] = Cet, ["Europe/Copenhagen"] = Cet,
        ["Europe/Prague"] = Cet, ["Europe/Warsaw"] = Cet, ["Europe/Budapest"] = Cet, ["Europe/Monaco"] = Cet,
        ["Europe/London"] = "GMT0BST,M3.5.0/1,M10.5.0", ["Europe/Dublin"] = "GMT0IST,M3.5.0/1,M10.5.0",
        ["Europe/Lisbon"] = "WET0WEST,M3.5.0/1,M10.5.0",
        ["Europe/Athens"] = Eet, ["Europe/Helsinki"] = Eet, ["Europe/Kyiv"] = Eet, ["Europe/Bucharest"] = Eet, ["Europe/Sofia"] = Eet,
        ["Europe/Moscow"] = "MSK-3", ["Europe/Istanbul"] = "<+03>-3",
        ["America/New_York"] = UsEast, ["America/Toronto"] = UsEast, ["America/Montreal"] = UsEast, ["America/Detroit"] = UsEast,
        ["America/Chicago"] = UsCentral, ["America/Denver"] = UsMountain, ["America/Phoenix"] = "MST7",
        ["America/Los_Angeles"] = UsPacific, ["America/Vancouver"] = UsPacific,
        ["America/Sao_Paulo"] = "<-03>3", ["America/Mexico_City"] = "CST6",
        ["Asia/Tokyo"] = "JST-9", ["Asia/Seoul"] = "KST-9", ["Asia/Shanghai"] = "CST-8", ["Asia/Hong_Kong"] = "HKT-8",
        ["Asia/Singapore"] = "<+08>-8", ["Asia/Dubai"] = "<+04>-4", ["Asia/Kolkata"] = "IST-5:30",
        ["Australia/Sydney"] = "AEST-10AEDT,M10.1.0,M4.1.0/3", ["Australia/Melbourne"] = "AEST-10AEDT,M10.1.0,M4.1.0/3",
        ["Pacific/Auckland"] = "NZST-12NZDT,M9.5.0,M4.1.0/3",
        ["Africa/Casablanca"] = "<+01>-1", ["Africa/Johannesburg"] = "SAST-2", ["Africa/Cairo"] = "EET-2EEST,M4.5.5/0,M10.5.4/24",
        ["Africa/Algiers"] = "CET-1", ["Africa/Tunis"] = "CET-1", ["Indian/Reunion"] = "<+04>-4",
    };

    public static string? PosixFor(string iana) => Posix.TryGetValue(iana ?? "", out var p) ? p : null;
}
