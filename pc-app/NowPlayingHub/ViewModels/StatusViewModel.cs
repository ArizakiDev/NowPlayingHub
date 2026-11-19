using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.Windows.Media;

namespace NowPlayingHub;

public sealed class DayBar { public Brush Brush { get; init; } = Brushes.Gray; public string Tip { get; init; } = ""; }
public sealed class ComponentVm
{
    public string Name { get; init; } = "";
    public string StateText { get; init; } = "";
    public Brush StateBrush { get; init; } = Brushes.Gray;
    public string Note { get; init; } = "";
    public string UptimeText { get; init; } = "";
    public IReadOnlyList<DayBar> Days { get; init; } = Array.Empty<DayBar>();
}
public sealed class GroupVm { public string Name { get; init; } = ""; public IReadOnlyList<ComponentVm> Items { get; init; } = Array.Empty<ComponentVm>(); }
public sealed class SectionVm { public string Title { get; init; } = ""; public IReadOnlyList<InfoRow> Rows { get; init; } = Array.Empty<InfoRow>(); }
public sealed class SparkVm { public string Title { get; init; } = ""; public string Value { get; init; } = "—"; public PointCollection? Points { get; init; } }
public sealed class IncidentVm
{
    public string Title { get; init; } = "";
    public string When { get; init; } = "";
    public string Detail { get; init; } = "";
    public Brush Brush { get; init; } = Brushes.Gray;
}
public sealed record InfoRow(string Key, string Value);

public sealed class StatusViewModel : PageViewModel
{
    static readonly CultureInfo Fr = new("fr-FR");
    static Brush Hex(string hex)
    {
        var b = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex));
        b.Freeze();
        return b;
    }
    static readonly Brush COk = Hex("#3AA75B"), CDeg = Hex("#E0A100"), CWarn = Hex("#E8742C"), COut = Hex("#D64545"),
                          CNone = Hex("#2C2C39"), CBannerNone = Hex("#4A4A5A");
    static readonly Brush BannerOk = Hex("#2E9B55"), BannerDeg = Hex("#C98A12"), BannerOut = Hex("#C9423B");

    readonly AppServices _svc;
    int _lastVersion = -1, _tick;
    EspInfo? _lastInfo;
    bool _lastOnline, _refreshing;

    public override string Title => "Statut";
    public override string Glyph => "\uE9D9";

    public ObservableCollection<GroupVm> Groups { get; } = new();
    public ObservableCollection<SectionVm> Sections { get; } = new();
    public ObservableCollection<SparkVm> Sparks { get; } = new();
    public ObservableCollection<IncidentVm> Incidents { get; } = new();

    public RelayCommand OpenWebCommand { get; }
    public RelayCommand CopyJsonCommand { get; }

    public StatusViewModel(AppServices svc)
    {
        _svc = svc;
        OpenWebCommand = new RelayCommand(_ => OpenWeb());
        CopyJsonCommand = new RelayCommand(_ => CopyJson());
    }

    string _bannerText = "En attente des premières mesures…";
    public string BannerText { get => _bannerText; private set => Set(ref _bannerText, value); }
    Brush _bannerBrush = CBannerNone;
    public Brush BannerBrush { get => _bannerBrush; private set => Set(ref _bannerBrush, value); }
    string _webText = "";
    public string WebText { get => _webText; private set => Set(ref _webText, value); }
    string _message = "";
    public string Message { get => _message; private set => Set(ref _message, value); }

    static Brush LevelBrush(Level l) => l switch { Level.Ok => COk, Level.Degraded => CDeg, Level.Outage => COut, _ => CNone };

    static Brush DayBrush(double? u) => u is not double v ? CNone : v >= 99.5 ? COk : v >= 97 ? CDeg : v >= 90 ? CWarn : COut;

    static string Pct(double v) => v.ToString(v >= 99.995 ? "0" : "0.##", Fr) + " %";

    public override void OnShown() { _lastVersion = -1; _lastInfo = null; _tick = 0; OnTick(); }

    public override void OnTick()
    {
        if (_tick++ % 4 == 0 && _svc.Sync.EspOnline && !_refreshing)
        {
            _refreshing = true;
            Fire(async () => { try { await _svc.Sync.RefreshInfoAsync(); } finally { _refreshing = false; } }, "statut de la carte");
        }

        if (_svc.Tracker.Version != _lastVersion)
        {
            _lastVersion = _svc.Tracker.Version;
            BuildHistory(_svc.Tracker.Snapshot());
        }

        var info = _svc.Sync.Info;
        bool online = _svc.Sync.EspOnline;
        if (!ReferenceEquals(info, _lastInfo) || online != _lastOnline)
        {
            _lastInfo = info; _lastOnline = online;
            BuildSections(online ? info : null);
        }

        var web = _svc.StatusWeb;
        WebText = !_svc.Cfg.StatusEnabled ? "Page web désactivée (réglage dans « Système »)."
                : web.Error != null ? web.Error
                : web.Running ? "Page web : " + string.Join("  ·  ", web.Urls())
                : "Page web arrêtée.";
    }

    void BuildHistory(StatusSnapshot s)
    {
        BannerText = LevelText.Banner(s.Overall);
        BannerBrush = s.Overall switch { Level.Ok => BannerOk, Level.Degraded => BannerDeg, Level.Outage => BannerOut, _ => CBannerNone };

        Groups.Clear();
        foreach (var g in s.Components.GroupBy(c => c.Def.Group))
        {
            var items = g.Select(c => new ComponentVm
            {
                Name = c.Def.Name,
                StateText = LevelText.Of(c.Level),
                StateBrush = c.Level == Level.NoData ? Brushes.Gray : LevelBrush(c.Level),
                Note = c.Note,
                UptimeText = c.UptimePct is double u ? Pct(u) + " de disponibilité" : "pas encore de mesure",
                Days = c.Days.Select(d => new DayBar
                {
                    Brush = DayBrush(d.Pct),
                    Tip = DateTime.ParseExact(d.Date, "yyyy-MM-dd", CultureInfo.InvariantCulture).ToString("d MMMM", Fr) + " · " +
                          (d.Pct is double p ? Pct(p) + (d.DownMin > 0 ? $" · {d.DownMin:0} min d'indisponibilité" : "") : "aucune mesure")
                }).ToList()
            }).ToList();
            Groups.Add(new GroupVm { Name = g.Key.ToUpperInvariant(), Items = items });
        }

        Sparks.Clear();
        AddSpark(s, "heapKB", "Mémoire libre", " Ko", "0");
        AddSpark(s, "tempC", "Température", " °C", "0");
        AddSpark(s, "rssi", "Signal Wi-Fi", " dBm", "0");
        AddSpark(s, "loopMs", "Tour de boucle", " ms", "0.##");

        Incidents.Clear();
        foreach (var i in s.Incidents)
        {
            Incidents.Add(new IncidentVm
            {
                Title = $"{i.Component} · {(i.Level == Level.Outage ? "indisponible" : "dégradé")}",
                When = i.Start.ToString("d MMM HH:mm", Fr) + " → " + (i.End is DateTime e ? e.ToString("d MMM HH:mm", Fr) : "en cours"),
                Detail = i.Detail,
                Brush = i.Level == Level.Outage ? COut : CDeg
            });
        }
    }

    void AddSpark(StatusSnapshot s, string key, string title, string unit, string fmt)
    {
        if (!s.Series.TryGetValue(key, out var data) || data.Length == 0) { Sparks.Add(new SparkVm { Title = title }); return; }
        PointCollection? pts = null;
        if (data.Length > 1)
        {
            double min = data.Min(), max = data.Max(), span = Math.Max(max - min, 1e-6);
            pts = new PointCollection(data.Select((v, i) => new System.Windows.Point(i * 100.0 / (data.Length - 1), 100 - (v - min) / span * 100)));
            pts.Freeze();
        }
        Sparks.Add(new SparkVm { Title = title, Value = data[^1].ToString(fmt, Fr) + unit, Points = pts });
    }

    static string Uptime(long s)
    {
        var t = TimeSpan.FromSeconds(s);
        return t.TotalDays >= 1 ? $"{(int)t.TotalDays} j {t.Hours} h" : t.TotalHours >= 1 ? $"{(int)t.TotalHours} h {t.Minutes:00}" : $"{t.Minutes} min {t.Seconds} s";
    }

    void BuildSections(EspInfo? i)
    {
        Sections.Clear();
        if (i == null)
        {
            Sections.Add(new SectionVm { Title = "CARTE ESP32", Rows = new[] { new InfoRow("État", "Hors ligne : en attente de la carte…") } });
            return;
        }
        static string Kb(long bytes) => (bytes / 1024).ToString("N0", Fr) + " Ko";
        string dash = "—";

        Sections.Add(new SectionVm
        {
            Title = "PROCESSEUR",
            Rows = new[]
            {
                new InfoRow("Puce", i.ChipModel.Length > 0 ? $"{i.ChipModel} rév. {i.ChipRev}" : dash),
                new InfoRow("Cœurs", i.Cores > 0 ? i.Cores.ToString() : dash),
                new InfoRow("Fréquence", i.CpuMhz > 0 ? $"{i.CpuMhz} MHz" : dash),
                new InfoRow("Température", i.ChipTemp > 0 && i.ChipTemp < 120 ? $"{i.ChipTemp.ToString("0", Fr)} °C" : dash),
            }
        });
        Sections.Add(new SectionVm
        {
            Title = "CHARGE",
            Rows = i.HasV19
                ? new[]
                {
                    new InfoRow("Tour de boucle (moy.)", $"{(i.LoopAvgUs / 1000.0).ToString("0.##", Fr)} ms"),
                    new InfoRow("Pire tour (5 s)", $"{(i.LoopWorstUs / 1000.0).ToString("0.#", Fr)} ms"),
                    new InfoRow("Tours > 100 ms", $"{i.LoopSlow} (total {i.LoopSlowTotal})"),
                    new InfoRow("Pile libre", Kb(i.StackFree)),
                }
                : new[] { new InfoRow("Firmware", $"{i.Version} : passe en 1.9 pour ces mesures") }
        });
        Sections.Add(new SectionVm
        {
            Title = "MÉMOIRE",
            Rows = new[]
            {
                new InfoRow("Libre", Kb(i.Heap)),
                new InfoRow("Minimum depuis le démarrage", i.HasV19 ? Kb(i.HeapMinFree) : dash),
                new InfoRow("Plus grand bloc", Kb(i.MaxAlloc)),
                new InfoRow("Fragmentation", i.HasV19 ? $"{i.FragPct} %" : dash),
                new InfoRow("Total", i.HasV19 ? Kb(i.HeapTotal) : dash),
            }
        });
        Sections.Add(new SectionVm
        {
            Title = "FLASH",
            Rows = i.HasV19
                ? new[]
                {
                    new InfoRow("Capacité", $"{i.FlashSize / 1048576} Mo à {i.FlashMhz} MHz"),
                    new InfoRow("Firmware", Kb(i.SketchSize)),
                    new InfoRow("Libre pour les mises à jour", Kb(i.FreeSketch)),
                }
                : new[] { new InfoRow("Détail", dash) }
        });
        Sections.Add(new SectionVm
        {
            Title = "RÉSEAU",
            Rows = new[]
            {
                new InfoRow("Réseau Wi-Fi", i.Ssid.Length > 0 ? i.Ssid : dash),
                new InfoRow("Canal", i.Channel > 0 ? i.Channel.ToString() : dash),
                new InfoRow("Signal", $"{i.Rssi} dBm"),
                new InfoRow("Adresse IP", i.Ip),
            }
        });
        Sections.Add(new SectionVm
        {
            Title = "SYSTÈME",
            Rows = new[]
            {
                new InfoRow("Firmware", i.Version),
                new InfoRow("Allumée depuis", Uptime(i.UptimeSec)),
                new InfoRow("Dernier démarrage", i.ResetReason.Length > 0 ? i.ResetReason : dash),
                new InfoRow("Écran affiché", i.Screen),
                new InfoRow("SDK", i.Sdk.Length > 0 ? i.Sdk : dash),
            }
        });
        Sections.Add(new SectionVm
        {
            Title = "CAPTEURS ET STOCKAGE",
            Rows = new[]
            {
                new InfoRow("Lumière ambiante", $"{i.LdrLight} % (brut {i.LdrRaw})"),
                new InfoRow("Luminosité", $"{i.Brightness} %" + (i.AutoBri ? " (auto)" : "") + (i.Night ? " · mode nuit" : "")),
                new InfoRow("Spectre audio", i.Viz ? $"reçu ({i.VizPackets} paquets)" : "inactif"),
                new InfoRow("Carte SD", !i.SdEnabled ? "désactivée" : i.SdOk ? $"{i.SdFiles} fichiers · {i.SdSizeMb} Mo" : "non détectée"),
            }
        });
    }

    void OpenWeb()
    {
        try
        {
            var url = _svc.StatusWeb.Urls()[0];
            if (!_svc.StatusWeb.Running) { Message = "La page web n'est pas démarrée."; return; }
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
        }
        catch (Exception e) { Message = "Ouverture impossible : " + e.Message; }
    }

    void CopyJson()
    {
        var raw = _svc.Sync.Info?.Raw;
        if (string.IsNullOrEmpty(raw)) { Message = "Aucun statut à copier : carte hors ligne."; return; }
        try { System.Windows.Clipboard.SetText(raw); Message = "Statut de la carte copié."; }
        catch (Exception e) { Message = "Copie impossible : " + e.Message; }
    }
}
