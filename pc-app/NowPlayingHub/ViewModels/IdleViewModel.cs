using System.Collections.ObjectModel;
using System.Globalization;

namespace NowPlayingHub;

public sealed record Preset(string Label, string Value);

public sealed class IdleViewModel : SettingsPage
{
    public override string Title => "Veille";
    public override string Glyph => "\uE823";

    public IReadOnlyList<Preset> TimezonePresets { get; } = new[]
    {
        new Preset("Bruxelles · Paris · Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"),
        new Preset("Londres", "GMT0BST,M3.5.0/1,M10.5.0"),
        new Preset("Lisbonne", "WET0WEST,M3.5.0/1,M10.5.0"),
        new Preset("Athènes · Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"),
        new Preset("New York · Montréal", "EST5EDT,M3.2.0,M11.1.0"),
        new Preset("Los Angeles", "PST8PDT,M3.2.0,M11.1.0"),
    };

    public IReadOnlyList<Preset> RssPresets { get; } = new[]
    {
        new Preset("Google Actualités (FR)", "https://news.google.com/rss?hl=fr&gl=FR&ceid=FR:fr"),
        new Preset("Le Monde", "https://www.lemonde.fr/rss/une.xml"),
        new Preset("France 24", "https://www.france24.com/fr/rss"),
        new Preset("BBC News", "https://feeds.bbci.co.uk/news/rss.xml"),
    };

    public ObservableCollection<GeoResult> Results { get; } = new();
    public AsyncCommand SearchCommand { get; }
    public RelayCommand PickCommand { get; }
    public RelayCommand TimezonePresetCommand { get; }
    public RelayCommand RssPresetCommand { get; }

    public IdleViewModel(AppServices s) : base(s)
    {
        SearchCommand = new AsyncCommand(_ => SearchAsync());
        PickCommand = new RelayCommand(p => { if (p is GeoResult g) ApplyCity(g); });
        TimezonePresetCommand = new RelayCommand(p => { if (p is string tz) Timezone = tz; });
        RssPresetCommand = new RelayCommand(p => { if (p is string url) NewsRssUrl = url; });
    }

    string _query = "";
    public string Query { get => _query; set => Set(ref _query, value); }

    string _searchInfo = "";
    public string SearchInfo { get => _searchInfo; private set => Set(ref _searchInfo, value); }

    string _weatherText = "";
    public string WeatherText { get => _weatherText; private set => Set(ref _weatherText, value); }

    string _newsText = "";
    public string NewsText { get => _newsText; private set => Set(ref _newsText, value); }

    public string LocationName
    {
        get => C.LocationName;
        set => Edit(C.LocationName, (value ?? "").Trim(), v => C.LocationName = v, Push.Idle);
    }

    public string LatText
    {
        get => C.Latitude.ToString("0.0000", CultureInfo.InvariantCulture);
        set
        {
            if (double.TryParse((value ?? "").Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out var d) && Math.Abs(d) <= 90)
                Edit(C.Latitude, d, v => C.Latitude = v, Push.Idle);
            else Raise();
        }
    }

    public string LonText
    {
        get => C.Longitude.ToString("0.0000", CultureInfo.InvariantCulture);
        set
        {
            if (double.TryParse((value ?? "").Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out var d) && Math.Abs(d) <= 180)
                Edit(C.Longitude, d, v => C.Longitude = v, Push.Idle);
            else Raise();
        }
    }

    public string Timezone
    {
        get => C.Timezone;
        set
        {
            var tz = (value ?? "").Trim();
            if (tz.Length < 3 || tz.Length > 64 || !System.Text.RegularExpressions.Regex.IsMatch(tz, @"^[A-Za-z0-9+\-:,./<>]+$")) { Raise(); return; }
            Edit(C.Timezone, tz, v => C.Timezone = v, Push.Idle);
        }
    }

    public string NewsRssUrl
    {
        get => C.NewsRssUrl;
        set
        {
            var url = (value ?? "").Trim();
            if (url.Length > 0 && !url.StartsWith("http", StringComparison.OrdinalIgnoreCase)) { Raise(); return; }
            Edit(C.NewsRssUrl, url, v => C.NewsRssUrl = v, Push.Idle);
        }
    }

    public int NewsCount { get => C.NewsCount; set => Edit(C.NewsCount, Math.Clamp(value, 1, 10), v => C.NewsCount = v, Push.Idle); }

    async Task SearchAsync()
    {
        SearchInfo = "Recherche…";
        Results.Clear();
        try
        {
            var list = await Geocoder.SearchAsync(Query);
            foreach (var g in list) Results.Add(g);
            SearchInfo = list.Count == 0 ? "Aucun résultat." : "";
        }
        catch (Exception e)
        {
            Log.Write("recherche de ville : " + e.Message);
            SearchInfo = "Recherche impossible (connexion ?).";
        }
    }

    void ApplyCity(GeoResult g)
    {
        C.LocationName = g.Name;
        C.Latitude = Math.Round(g.Latitude, 4);
        C.Longitude = Math.Round(g.Longitude, 4);
        var tz = Geocoder.PosixFor(g.Timezone);
        if (tz != null) C.Timezone = tz;
        Raise(nameof(LocationName)); Raise(nameof(LatText)); Raise(nameof(LonText)); Raise(nameof(Timezone));
        S.Settings.Changed(Push.Idle);
        Results.Clear();
        SearchInfo = tz != null ? $"Lieu appliqué : {g.Label}." : $"Lieu appliqué, mais le fuseau « {g.Timezone} » n'est pas connu : choisis-le ci-dessous.";
    }

    public override void OnShown() => OnTick();

    public override void OnTick()
    {
        var w = S.Idle.Weather;
        WeatherText = w == null
            ? "Météo : en attente du premier chargement…"
            : $"{w.Location} · {w.Temp.ToString("0.#", CultureInfo.InvariantCulture)} °C · {w.Description} · lever {w.Sunrise} / coucher {w.Sunset}";
        var n = S.Idle.NewsItems.Count;
        NewsText = n == 0 ? "Actualités : aucune pour l'instant." : $"{n} actualité(s) chargée(s) · « {S.Idle.Headlines[0]} »";
    }
}
