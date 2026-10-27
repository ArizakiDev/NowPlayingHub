using System.Globalization;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace NowPlayingHub;

/// <summary>Paroles synchronisees via LRCLIB (https://lrclib.net) : renvoie la ligne en cours.
/// Le titre / artiste / album / duree de la piste sont envoyes a lrclib.net pour la recherche.</summary>
public class LyricsService
{
    readonly HttpClient _http = new() { Timeout = TimeSpan.FromSeconds(8) };
    static readonly Regex LrcLine = new(@"^\[(\d+):(\d+(?:\.\d+)?)\]\s*(.*)$", RegexOptions.Compiled);

    string _key = "";
    volatile List<(double T, string Text)>? _lines;

    public LyricsService()
    {
        _http.DefaultRequestHeaders.UserAgent.ParseAdd("NowPlayingHub/1.1 (+https://github.com)");
    }

    /// <summary>Ligne a afficher maintenant (chaine vide si pas de paroles).</summary>
    public string Line(Snapshot snap)
    {
        if (snap.Key != _key)
        {
            _key = snap.Key;
            _lines = null;
            var key = _key;
            _ = Task.Run(() => LoadAsync(snap, key));
        }
        var ls = _lines;
        if (ls == null || ls.Count == 0) return "";
        double p = snap.Position + 0.4;   // petite avance pour compenser la latence
        string cur = "";
        foreach (var (t, s) in ls)
        {
            if (t <= p) cur = s; else break;
        }
        return cur;
    }

    async Task LoadAsync(Snapshot snap, string key)
    {
        List<(double, string)> result = new();
        try
        {
            string? lrc = await GetAsync(snap) ?? await SearchAsync(snap);
            if (!string.IsNullOrWhiteSpace(lrc)) result = Parse(lrc);
        }
        catch (Exception e) { Log.Write("paroles: " + e.Message); }
        if (_key == key) _lines = result;
    }

    static string Q(string s) => Uri.EscapeDataString(s ?? "");

    async Task<string?> GetAsync(Snapshot s)
    {
        var url = $"https://lrclib.net/api/get?artist_name={Q(s.Artist)}&track_name={Q(s.Title)}" +
                  $"&album_name={Q(s.Album)}&duration={(int)Math.Round(s.Duration)}";
        using var r = await _http.GetAsync(url);
        if (!r.IsSuccessStatusCode) return null;
        using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync());
        return Synced(doc.RootElement);
    }

    async Task<string?> SearchAsync(Snapshot s)
    {
        var url = $"https://lrclib.net/api/search?track_name={Q(s.Title)}&artist_name={Q(s.Artist)}";
        using var r = await _http.GetAsync(url);
        if (!r.IsSuccessStatusCode) return null;
        using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync());
        if (doc.RootElement.ValueKind != JsonValueKind.Array) return null;
        foreach (var item in doc.RootElement.EnumerateArray())
        {
            var l = Synced(item);
            if (!string.IsNullOrWhiteSpace(l)) return l;
        }
        return null;
    }

    static string? Synced(JsonElement e) =>
        e.ValueKind == JsonValueKind.Object && e.TryGetProperty("syncedLyrics", out var v) && v.ValueKind == JsonValueKind.String
            ? v.GetString() : null;

    static List<(double, string)> Parse(string lrc)
    {
        var list = new List<(double, string)>();
        foreach (var raw in lrc.Split('\n'))
        {
            var m = LrcLine.Match(raw.Trim());
            if (!m.Success) continue;
            double min = double.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
            double sec = double.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture);
            list.Add((min * 60 + sec, m.Groups[3].Value.Trim()));
        }
        list.Sort((a, b) => a.Item1.CompareTo(b.Item1));
        return list;
    }
}
