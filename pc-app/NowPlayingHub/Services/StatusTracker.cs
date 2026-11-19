using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace NowPlayingHub;

public enum Level { NoData = 0, Ok = 1, Degraded = 2, Outage = 3 }

public sealed record ComponentDef(string Id, string Group, string Name);
public sealed record DayPoint(string Date, double? Pct, double DownMin);
public sealed record ComponentStatus(ComponentDef Def, Level Level, string Note, double? UptimePct, IReadOnlyList<DayPoint> Days);

public sealed class DayCounts
{
    public int Ok { get; set; }
    public int Deg { get; set; }
    public int Out { get; set; }
    [JsonIgnore] public int Total => Ok + Deg + Out;
}

public sealed class Incident
{
    public DateTime Start { get; set; }
    public DateTime? End { get; set; }
    public string Component { get; set; } = "";
    public Level Level { get; set; }
    public string Detail { get; set; } = "";
}

public sealed record StatusSnapshot(
    Level Overall,
    IReadOnlyList<ComponentStatus> Components,
    IReadOnlyList<Incident> Incidents,
    IReadOnlyDictionary<string, double[]> Series,
    int Version);

sealed class HistoryFile
{
    public Dictionary<string, Dictionary<string, DayCounts>> Days { get; set; } = new();
    public List<Incident> Incidents { get; set; } = new();
    public DateTime SavedAt { get; set; }
}

public static class LevelText
{
    public static string Of(Level l) => l switch
    {
        Level.Ok => "Opérationnel",
        Level.Degraded => "Performances dégradées",
        Level.Outage => "Indisponible",
        _ => "Sans mesure"
    };
    public static string Key(Level l) => l switch
    {
        Level.Ok => "ok", Level.Degraded => "degraded", Level.Outage => "outage", _ => "nodata"
    };
    public static string Banner(Level l) => l switch
    {
        Level.Ok => "Tous les systèmes sont opérationnels",
        Level.Degraded => "Certains systèmes sont dégradés",
        Level.Outage => "Panne en cours",
        _ => "En attente des premières mesures…"
    };
}

public sealed class StatusTracker : IDisposable
{
    public const int SampleSeconds = 10;
    const int KeepDays = 35, ShowDays = 30, SeriesLen = 90, ConfirmSamples = 2;

    public static readonly ComponentDef[] Components =
    {
        new("esp",     "Carte ESP32",       "Connexion à la carte"),
        new("wifi",    "Carte ESP32",       "Signal Wi-Fi"),
        new("memory",  "Carte ESP32",       "Mémoire"),
        new("temp",    "Carte ESP32",       "Température de la puce"),
        new("sd",      "Carte ESP32",       "Carte SD (cache de pochettes)"),
        new("weather", "Services en ligne", "Météo (Open-Meteo)"),
        new("news",    "Services en ligne", "Actualités (RSS)"),
    };

    static string FilePath => Path.Combine(AppConfig.DirPath, "status-history.json");
    static readonly JsonSerializerOptions Opt = new() { WriteIndented = false };

    readonly SyncService _sync;
    readonly IdleService _idle;
    readonly object _gate = new();
    readonly System.Threading.Timer _timer;

    readonly Dictionary<string, Dictionary<string, DayCounts>> _days;
    readonly List<Incident> _incidents;
    readonly Dictionary<string, Level> _confirmed = new();
    readonly Dictionary<string, Level> _lastRaw = new();
    readonly Dictionary<string, int> _rawCount = new();
    readonly Dictionary<string, string> _notes = new();
    readonly Dictionary<string, Incident> _open = new();
    readonly Dictionary<string, List<double>> _series = new()
    { ["heapKB"] = new(), ["tempC"] = new(), ["rssi"] = new(), ["loopMs"] = new() };

    DateTime _lastSave = DateTime.UtcNow;
    int _version;

    public int Version => _version;

    public StatusTracker(SyncService sync, IdleService idle)
    {
        _sync = sync; _idle = idle;
        var h = LoadFile();
        _days = h.Days;
        _incidents = h.Incidents;
        foreach (var open in _incidents.Where(x => x.End == null)) open.End = h.SavedAt == default ? open.Start : h.SavedAt;
        Prune();
        foreach (var c in Components) _confirmed[c.Id] = Level.NoData;
        _timer = new System.Threading.Timer(_ => Tick(), null, Timeout.Infinite, Timeout.Infinite);
    }

    public void Start() => _timer.Change(TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(SampleSeconds));

    static HistoryFile LoadFile()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<HistoryFile>(File.ReadAllText(FilePath), Opt) ?? new HistoryFile();
        }
        catch (Exception e) { Log.Write("historique de statut illisible : " + e.Message); }
        return new HistoryFile();
    }

    void Save()
    {
        try
        {
            Directory.CreateDirectory(AppConfig.DirPath);
            var f = new HistoryFile { Days = _days, Incidents = _incidents, SavedAt = DateTime.Now };
            var tmp = FilePath + ".tmp";
            File.WriteAllText(tmp, JsonSerializer.Serialize(f, Opt));
            File.Move(tmp, FilePath, true);
            _lastSave = DateTime.UtcNow;
        }
        catch (Exception e) { Log.Write("historique de statut : " + e.Message); }
    }

    void Prune()
    {
        var limit = DateTime.Now.Date.AddDays(-KeepDays).ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
        foreach (var k in _days.Keys.Where(k => string.CompareOrdinal(k, limit) < 0).ToList()) _days.Remove(k);
        _incidents.RemoveAll(i => i.Start < DateTime.Now.AddDays(-KeepDays));
        if (_incidents.Count > 120) _incidents.RemoveRange(0, _incidents.Count - 120);
    }

    Dictionary<string, (Level level, string note)> Evaluate()
    {
        var r = new Dictionary<string, (Level, string)>();
        var info = _sync.Info;
        bool online = _sync.EspOnline;

        if (online) r["esp"] = (Level.Ok, "");
        else if (!string.IsNullOrEmpty(_sync.LastError)) r["esp"] = (Level.Outage, "La carte ne répond pas : " + _sync.LastError);
        else r["esp"] = (Level.NoData, "");

        if (!online || info == null)
        {
            r["wifi"] = r["memory"] = r["temp"] = r["sd"] = (Level.NoData, "Carte hors ligne");
        }
        else
        {
            r["wifi"] = info.Rssi == 0 ? (Level.NoData, "")
                      : info.Rssi >= -75 ? (Level.Ok, $"{info.Rssi} dBm")
                      : (Level.Degraded, $"Signal faible ({info.Rssi} dBm)");

            long freeKb = info.Heap / 1024, maxKb = info.MaxAlloc / 1024;
            r["memory"] = info.Heap < 12 * 1024 ? (Level.Outage, $"Mémoire presque épuisée ({freeKb} Ko libres)")
                        : (info.Heap < 30 * 1024 || info.MaxAlloc < 16 * 1024) ? (Level.Degraded, $"Mémoire basse ({freeKb} Ko libres, plus grand bloc {maxKb} Ko)")
                        : (Level.Ok, $"{freeKb} Ko libres");

            double t = info.ChipTemp;
            r["temp"] = (t <= 0 || t > 120) ? (Level.NoData, "Mesure indisponible")
                      : t > 100 ? (Level.Outage, $"Température critique ({t:0} °C)")
                      : t > 85 ? (Level.Degraded, $"Puce chaude ({t:0} °C)")
                      : (Level.Ok, $"{t:0} °C");

            r["sd"] = !info.SdEnabled ? (Level.NoData, "Cache SD désactivé")
                    : info.SdOk ? (Level.Ok, $"{info.SdFiles} fichiers")
                    : (Level.Degraded, "Carte SD non détectée");
        }

        bool wFail = _idle.WeatherFailing, hasW = _idle.Weather != null;
        r["weather"] = wFail ? (hasW ? (Level.Degraded, "Dernier chargement en échec : données conservées") : (Level.Outage, "Météo indisponible"))
                     : hasW ? (Level.Ok, "") : (Level.NoData, "Premier chargement…");

        bool nFail = _idle.NewsFailing, hasN = _idle.NewsItems.Count > 0;
        r["news"] = !_idle.NewsConfigured ? (Level.NoData, "Aucun flux configuré")
                  : nFail ? (hasN ? (Level.Degraded, "Dernier chargement en échec : actus conservées") : (Level.Outage, "Flux RSS indisponible"))
                  : hasN ? (Level.Ok, "") : (Level.NoData, "Premier chargement…");
        return r;
    }

    void Tick()
    {
        try
        {
            var raw = Evaluate();
            var info = _sync.Info;
            lock (_gate)
            {
                var now = DateTime.Now;
                var day = now.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
                if (!_days.TryGetValue(day, out var today)) { today = new(); _days[day] = today; }

                foreach (var c in Components)
                {
                    var (lvl, note) = raw[c.Id];
                    if (_lastRaw.TryGetValue(c.Id, out var prev) && prev == lvl) _rawCount[c.Id]++;
                    else { _lastRaw[c.Id] = lvl; _rawCount[c.Id] = 1; }
                    if (_rawCount[c.Id] >= ConfirmSamples) { _confirmed[c.Id] = lvl; _notes[c.Id] = note; }

                    var conf = _confirmed[c.Id];
                    if (conf != Level.NoData)
                    {
                        if (!today.TryGetValue(c.Id, out var dc)) { dc = new DayCounts(); today[c.Id] = dc; }
                        if (conf == Level.Ok) dc.Ok++; else if (conf == Level.Degraded) dc.Deg++; else dc.Out++;
                    }
                    TrackIncident(c, conf, _notes.GetValueOrDefault(c.Id, ""), now);
                }

                if (_sync.EspOnline && info != null)
                {
                    Push("heapKB", info.Heap / 1024.0);
                    Push("rssi", info.Rssi);
                    if (info.ChipTemp > 0 && info.ChipTemp < 120) Push("tempC", info.ChipTemp);
                    if (info.LoopAvgUs > 0) Push("loopMs", info.LoopAvgUs / 1000.0);
                }
                _version++;
                if ((DateTime.UtcNow - _lastSave).TotalMinutes >= 5) { Prune(); Save(); }
            }
        }
        catch (Exception e) { Log.Write("mesure du statut : " + e.Message); }
    }

    void Push(string key, double v)
    {
        var l = _series[key];
        l.Add(v);
        if (l.Count > SeriesLen) l.RemoveAt(0);
    }

    void TrackIncident(ComponentDef c, Level conf, string note, DateTime now)
    {
        _open.TryGetValue(c.Id, out var inc);
        if (conf == Level.Degraded || conf == Level.Outage)
        {
            if (inc == null)
            {
                inc = new Incident { Start = now, Component = c.Name, Level = conf, Detail = note };
                _incidents.Add(inc); _open[c.Id] = inc;
            }
            else if (conf > inc.Level) { inc.Level = conf; if (note.Length > 0) inc.Detail = note; }
        }
        else if (inc != null)
        {
            inc.End = now; _open.Remove(c.Id);
        }
    }

    public StatusSnapshot Snapshot()
    {
        lock (_gate)
        {
            var list = new List<ComponentStatus>();
            var today = DateTime.Now.Date;
            foreach (var c in Components)
            {
                var pts = new List<DayPoint>();
                double ok = 0, tot = 0;
                for (int i = ShowDays - 1; i >= 0; i--)
                {
                    var d = today.AddDays(-i);
                    var key = d.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
                    DayCounts? dc = null;
                    if (_days.TryGetValue(key, out var m)) m.TryGetValue(c.Id, out dc);
                    if (dc == null || dc.Total == 0) { pts.Add(new DayPoint(key, null, 0)); continue; }
                    double u = (dc.Ok + 0.5 * dc.Deg) / dc.Total * 100.0;
                    ok += dc.Ok + 0.5 * dc.Deg; tot += dc.Total;
                    pts.Add(new DayPoint(key, u, dc.Out * SampleSeconds / 60.0));
                }
                list.Add(new ComponentStatus(c, _confirmed[c.Id], _notes.GetValueOrDefault(c.Id, ""),
                                             tot > 0 ? ok / tot * 100.0 : null, pts));
            }

            var present = list.Where(x => x.Level != Level.NoData).Select(x => x.Level).ToList();
            var overall = present.Count == 0 ? Level.NoData : present.Max();

            var inc = _incidents.OrderByDescending(i => i.Start).Take(15)
                .Select(i => new Incident { Start = i.Start, End = i.End, Component = i.Component, Level = i.Level, Detail = i.Detail })
                .ToList();
            var series = _series.ToDictionary(k => k.Key, k => k.Value.ToArray());
            return new StatusSnapshot(overall, list, inc, series, _version);
        }
    }

    public void Dispose()
    {
        _timer.Change(Timeout.Infinite, Timeout.Infinite);
        lock (_gate) { Save(); }
    }
}
