using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace NowPlayingHub;

/// <summary>Paroles synchronisees (format LRC) : renvoie la ligne en cours.
/// Les sources sont essayees dans l'ordre, la premiere qui trouve des paroles gagne :
///   1. LRCLIB (https://lrclib.net)                       - ouverte, sans cle
///   2. Musixmatch (API non officielle de l'appli desktop) - uniquement si un jeton est renseigne
///   3. NetEase Cloud Music (music.163.com)                - tres gros catalogue, sans cle
/// Le titre / artiste / album / duree de la piste sont envoyes aux sources activees.</summary>
public class LyricsService
{
    readonly AppConfig _cfg;
    readonly HttpClient _http = new() { Timeout = TimeSpan.FromSeconds(8) };

    static readonly Regex TsRun  = new(@"^(?:\[\d+:\d+(?:[.:]\d+)?\])+", RegexOptions.Compiled);
    static readonly Regex Ts     = new(@"\[(\d+):(\d+(?:[.:]\d+)?)\]", RegexOptions.Compiled);
    static readonly Regex Enh    = new(@"<\d+:\d+(?:[.:]\d+)?>", RegexOptions.Compiled);   // LRC "enhanced" (mot par mot)
    // Lignes de credits ajoutees par certaines sources (NetEase surtout) : on ne les affiche pas
    static readonly Regex Credit = new(
        @"^(?:\u4f5c\u8bcd|\u4f5c\u66f2|\u7f16\u66f2|\u5236\u4f5c\u4eba|lyrics?\s+by|music\s+by|composed\s+by|written\s+by)\s*[:\uff1a]",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);
    // "(feat. X)", "[Remastered 2011]", " - Radio Edit"... : gene les recherches
    static readonly Regex TitleJunk = new(
        @"\s*[\(\[][^\)\]]*\b(?:feat|ft|remaster(?:ed)?|live|version|edit|explicit|deluxe|mix|mono|stereo|bonus)\b[^\)\]]*[\)\]]" +
        @"|\s+-\s+.*\b(?:remaster(?:ed)?|version|edit|live|mono|stereo|deluxe)\b.*$",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);

    const double DurTol = 8;   // tolerance (s) entre la duree de la piste et celle du resultat

    string _key = "";
    volatile List<(double T, string Text)>? _lines;
    public string LastSource { get; private set; } = "";     // source qui a fourni les paroles de la piste en cours
    public int LineCount => _lines?.Count ?? 0;
    readonly Dictionary<string, List<(double, string)>> _cache = new();

    public LyricsService(AppConfig cfg)
    {
        _cfg = cfg;
        _http.DefaultRequestHeaders.UserAgent.ParseAdd("NowPlayingHub/1.1 (+https://github.com)");
    }

    /// <summary>Ligne a afficher maintenant (chaine vide si pas de paroles).</summary>
    public string Line(Snapshot snap) => Context(snap).Cur;

    /// <summary>Ligne precedente, en cours et suivante (pour l'affichage plein ecran des paroles).</summary>
    public (string Prev, string Cur, string Next) Context(Snapshot snap)
    {
        if (snap.Key != _key)
        {
            _key = snap.Key;
            _lines = null;
            LastSource = "";
            var key = _key;
            _ = Task.Run(() => LoadAsync(snap, key));
        }
        var ls = _lines;
        if (ls == null || ls.Count == 0) return ("", "", "");
        // petite avance pour compenser la latence + decalage reglable depuis le panel
        double p = snap.Position + 0.4 + _cfg.LyricsOffsetMs / 1000.0;
        int idx = -1;
        for (int i = 0; i < ls.Count; i++) { if (ls[i].T <= p) idx = i; else break; }
        string cur = idx >= 0 ? ls[idx].Text : "";
        string prev = "", next = "";
        for (int i = idx - 1; i >= 0 && i >= idx - 3; i--) if (ls[i].Text.Length > 0) { prev = ls[i].Text; break; }
        for (int i = idx + 1; i < ls.Count && i <= idx + 3; i++) if (ls[i].Text.Length > 0) { next = ls[i].Text; break; }
        return (prev, cur, next);
    }

    async Task LoadAsync(Snapshot snap, string key)
    {
        List<(double, string)>? cached;
        lock (_cache) _cache.TryGetValue(key, out cached);
        if (cached != null) { if (_key == key) _lines = cached; return; }

        var sources = new List<(string Name, Func<Snapshot, CancellationToken, Task<string?>> Fn)>();
        if (_cfg.LyricsLrclib) sources.Add(("LRCLIB", LrclibAsync));
        if (!string.IsNullOrWhiteSpace(_cfg.MusixmatchToken)) sources.Add(("Musixmatch", MusixmatchAsync));
        if (_cfg.LyricsNetease) sources.Add(("NetEase", NeteaseAsync));

        List<(double, string)> result = new();
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(25));
        foreach (var (name, fn) in sources)
        {
            if (_key != key) return;   // la piste a change entre-temps
            try
            {
                var lrc = await fn(snap, cts.Token);
                if (string.IsNullOrWhiteSpace(lrc)) continue;
                var parsed = Parse(lrc);
                if (parsed.Count < 3) continue;
                // L'ecran de la carte n'affiche que de l'ASCII : on passe a la source suivante si les paroles sont en
                // alphabet non latin (chinois, coreen, cyrillique...), qui s'afficheraient en "????".
                if (_cfg.AsciiOnly && !MostlyLatin(parsed))
                {
                    Log.Write($"paroles {name}: ignorees (alphabet non affichable)");
                    continue;
                }
                result = parsed;
                LastSource = name;
                Log.Write($"paroles: {name} ({parsed.Count} lignes)");
                break;
            }
            catch (Exception e) { Log.Write($"paroles {name}: {e.Message}"); }
        }

        if (result.Count > 0)
            lock (_cache) { if (_cache.Count > 40) _cache.Clear(); _cache[key] = result; }
        if (_key == key) _lines = result;
    }

    // ------------------------------------------------------------------ LRCLIB
    async Task<string?> LrclibAsync(Snapshot s, CancellationToken ct)
    {
        var l = await LrclibGetAsync(s, ct);
        if (!string.IsNullOrWhiteSpace(l)) return l;
        // Recherche large : titre brut, puis titre nettoye ("(feat. X)", "Remastered"...)
        var variants = new[] { (s.Title, s.Artist), (CleanTitle(s.Title), FirstArtist(s.Artist)) }.Distinct();
        foreach (var (title, artist) in variants)
        {
            l = await LrclibSearchAsync(title, artist, s.Duration, ct);
            if (!string.IsNullOrWhiteSpace(l)) return l;
        }
        return null;
    }

    async Task<string?> LrclibGetAsync(Snapshot s, CancellationToken ct)
    {
        var url = $"https://lrclib.net/api/get?artist_name={Q(s.Artist)}&track_name={Q(s.Title)}" +
                  $"&album_name={Q(s.Album)}&duration={(int)Math.Round(s.Duration)}";
        using var r = await _http.GetAsync(url, ct);
        if (!r.IsSuccessStatusCode) return null;
        using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync(ct));
        return Synced(doc.RootElement);
    }

    async Task<string?> LrclibSearchAsync(string title, string artist, double duration, CancellationToken ct)
    {
        var url = $"https://lrclib.net/api/search?track_name={Q(title)}&artist_name={Q(artist)}";
        using var r = await _http.GetAsync(url, ct);
        if (!r.IsSuccessStatusCode) return null;
        using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync(ct));
        if (doc.RootElement.ValueKind != JsonValueKind.Array) return null;

        string? best = null;
        double bestDiff = double.MaxValue;
        foreach (var item in doc.RootElement.EnumerateArray())
        {
            var l = Synced(item);
            if (string.IsNullOrWhiteSpace(l)) continue;
            double diff = 0;
            if (duration > 0 && item.TryGetProperty("duration", out var d) && d.TryGetDouble(out var dv) && dv > 0)
                diff = Math.Abs(dv - duration);
            if (diff > DurTol) continue;           // autre version (live, extended...) : paroles decalees
            if (diff < bestDiff) { best = l; bestDiff = diff; }
        }
        return best;
    }

    static string? Synced(JsonElement e) =>
        e.ValueKind == JsonValueKind.Object && e.TryGetProperty("syncedLyrics", out var v) && v.ValueKind == JsonValueKind.String
            ? v.GetString() : null;

    // --------------------------------------------------------------- Musixmatch
    // Pas d'API officielle gratuite pour les paroles synchronisees : on utilise l'API (non documentee) de l'appli
    // desktop Musixmatch avec TON jeton utilisateur ("usertoken"). Source desactivee tant que le jeton est vide.
    async Task<string?> MusixmatchAsync(Snapshot s, CancellationToken ct)
    {
        var token = _cfg.MusixmatchToken.Trim();
        int dur = (int)Math.Round(s.Duration);
        var url = "https://apic-desktop.musixmatch.com/ws/1.1/macro.subtitles.get" +
                  "?format=json&namespace=lyrics_richsynched&subtitle_format=lrc&app_id=web-desktop-app-v1.0" +
                  $"&q_artist={Q(FirstArtist(s.Artist))}&q_track={Q(CleanTitle(s.Title))}&q_album={Q(s.Album)}" +
                  $"&q_duration={dur}&f_subtitle_length={dur}&usertoken={Q(token)}";
        using var req = new HttpRequestMessage(HttpMethod.Get, url);
        req.Headers.Add("Cookie", "x-mxm-token-guid=");
        using var r = await _http.SendAsync(req, ct);
        if (!r.IsSuccessStatusCode) return null;
        using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync(ct));

        if (Dig(doc.RootElement, "message", "header", "status_code") is { ValueKind: JsonValueKind.Number } st
            && st.TryGetInt32(out var code) && code != 200)
        {
            Log.Write($"paroles Musixmatch: code {code} (jeton refuse ou limite atteinte ?)");
            return null;
        }

        var list = Dig(doc.RootElement, "message", "body", "macro_calls", "track.subtitles.get",
                       "message", "body", "subtitle_list");
        if (list is { ValueKind: JsonValueKind.Array } arr && arr.GetArrayLength() > 0
            && Dig(arr[0], "subtitle", "subtitle_body") is { ValueKind: JsonValueKind.String } body)
            return body.GetString();
        return null;
    }

    // ------------------------------------------------------------------ NetEase
    async Task<string?> NeteaseAsync(Snapshot s, CancellationToken ct)
    {
        string artist = FirstArtist(s.Artist);
        using var req = new HttpRequestMessage(HttpMethod.Post, "https://music.163.com/api/search/get/web")
        {
            Content = new FormUrlEncodedContent(new[]
            {
                new KeyValuePair<string, string>("s", $"{CleanTitle(s.Title)} {artist}"),
                new KeyValuePair<string, string>("type", "1"),
                new KeyValuePair<string, string>("offset", "0"),
                new KeyValuePair<string, string>("limit", "8"),
            })
        };
        req.Headers.Add("Referer", "https://music.163.com/");
        req.Headers.Add("Cookie", "appver=2.0.2");
        using var r = await _http.SendAsync(req, ct);
        if (!r.IsSuccessStatusCode) return null;
        using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync(ct));
        if (Dig(doc.RootElement, "result", "songs") is not { ValueKind: JsonValueKind.Array } songs) return null;

        // Candidats : meme artiste + duree proche. On teste les 3 meilleurs.
        var cands = new List<(long Id, double Diff)>();
        foreach (var song in songs.EnumerateArray())
        {
            if (!song.TryGetProperty("id", out var idEl) || !idEl.TryGetInt64(out var id)) continue;

            var names = new List<string>();
            foreach (var key in new[] { "artists", "ar" })
                if (song.TryGetProperty(key, out var ars) && ars.ValueKind == JsonValueKind.Array)
                    foreach (var a in ars.EnumerateArray())
                        if (a.TryGetProperty("name", out var n) && n.ValueKind == JsonValueKind.String)
                            names.Add(n.GetString() ?? "");
            if (!ArtistMatch(artist, string.Join(" ", names))) continue;

            double diff = 4;   // duree inconnue : score moyen
            foreach (var key in new[] { "duration", "dt" })
                if (s.Duration > 0 && song.TryGetProperty(key, out var d) && d.TryGetDouble(out var ms) && ms > 0)
                { diff = Math.Abs(ms / 1000.0 - s.Duration); break; }
            if (s.Duration > 0 && diff > DurTol) continue;
            cands.Add((id, diff));
        }

        foreach (var (id, _) in cands.OrderBy(c => c.Diff).Take(3))
        {
            using var lreq = NeteaseGet($"https://music.163.com/api/song/lyric?id={id}&lv=1&kv=1&tv=-1");
            using var lr = await _http.SendAsync(lreq, ct);
            if (!lr.IsSuccessStatusCode) continue;
            using var ld = JsonDocument.Parse(await lr.Content.ReadAsStringAsync(ct));
            if (Dig(ld.RootElement, "lrc", "lyric") is { ValueKind: JsonValueKind.String } lrc)
            {
                var text = lrc.GetString();
                if (!string.IsNullOrWhiteSpace(text) && Ts.IsMatch(text)) return text;   // ignore les paroles non synchronisees
            }
        }
        return null;
    }

    static HttpRequestMessage NeteaseGet(string url)
    {
        var req = new HttpRequestMessage(HttpMethod.Get, url);
        req.Headers.Add("Referer", "https://music.163.com/");
        req.Headers.Add("Cookie", "appver=2.0.2");
        return req;
    }

    // ------------------------------------------------------------------ Outils
    static string Q(string s) => Uri.EscapeDataString(s ?? "");

    static JsonElement? Dig(JsonElement e, params string[] path)
    {
        foreach (var p in path)
            if (e.ValueKind != JsonValueKind.Object || !e.TryGetProperty(p, out e)) return null;
        return e;
    }

    static string CleanTitle(string t)
    {
        t ??= "";
        var c = TitleJunk.Replace(t, "").Trim();
        return c.Length > 0 ? c : t;
    }

    static string FirstArtist(string a)
    {
        a ??= "";
        var parts = Regex.Split(a, @"\s*(?:,|;|&|\bfeat\.?\b|\bft\.?\b|\bx\b|\bet\b)\s*", RegexOptions.IgnoreCase);
        return parts.Length > 0 && parts[0].Trim().Length > 0 ? parts[0].Trim() : a;
    }

    static string Norm(string s)
    {
        var sb = new StringBuilder();
        foreach (var ch in (s ?? "").Normalize(NormalizationForm.FormD))
        {
            var cat = CharUnicodeInfo.GetUnicodeCategory(ch);
            if (cat == UnicodeCategory.NonSpacingMark) continue;
            if (char.IsLetterOrDigit(ch)) sb.Append(char.ToLowerInvariant(ch));
        }
        return sb.ToString();
    }

    static bool ArtistMatch(string wanted, string found)
    {
        string a = Norm(wanted), b = Norm(found);
        return a.Length > 0 && b.Length > 0 && (b.Contains(a) || a.Contains(b));
    }

    // true si au moins 60 % des lettres sont de l'alphabet latin (accents compris)
    static bool MostlyLatin(List<(double, string)> lines)
    {
        int letters = 0, latin = 0;
        foreach (var (_, text) in lines)
            foreach (var ch in text)
            {
                if (!char.IsLetter(ch)) continue;
                letters++;
                if (ch < 0x250 || (ch >= 0x1E00 && ch <= 0x1EFF)) latin++;
            }
        return letters == 0 || latin >= letters * 0.6;
    }

    static List<(double, string)> Parse(string lrc)
    {
        var list = new List<(double, string)>();
        foreach (var raw in lrc.Split('\n'))
        {
            var line = raw.Trim();
            var run = TsRun.Match(line);
            if (!run.Success) continue;                       // balises [ar:..], [ti:..] ou ligne vide
            var text = Enh.Replace(line.Substring(run.Length), "").Trim();
            if (Credit.IsMatch(text)) continue;
            foreach (Match m in Ts.Matches(run.Value))        // une ligne peut avoir plusieurs horodatages (refrains)
            {
                double min = double.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                double sec = double.Parse(m.Groups[2].Value.Replace(':', '.'), CultureInfo.InvariantCulture);
                list.Add((min * 60 + sec, text));
            }
        }
        return list.OrderBy(x => x.Item1).ToList();
    }
}
