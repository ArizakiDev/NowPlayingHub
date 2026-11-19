using System.Collections.ObjectModel;

namespace NowPlayingHub;

public sealed class SessionItem : ObservableObject
{
    public string Id { get; }
    public string Name { get; }
    public string Detail { get; }

    bool _selected;
    public bool IsSelected { get => _selected; set => Set(ref _selected, value); }

    public SessionItem(string id, string name, string detail, bool selected)
    { Id = id; Name = name; Detail = detail; _selected = selected; }
}

public sealed class MusicViewModel : SettingsPage
{
    public override string Title => "Musique";
    public override string Glyph => "\uE8D6";

    public ObservableCollection<SessionItem> Sessions { get; } = new();
    public RelayCommand SelectCommand { get; }

    string _signature = "";
    int _tick;
    bool _loading;

    public MusicViewModel(AppServices s) : base(s)
    {
        SelectCommand = new RelayCommand(p =>
        {
            if (p is not SessionItem it) return;
            C.SelectedAppId = it.Id;
            S.Settings.Changed(Push.None);
            foreach (var x in Sessions) x.IsSelected = x.Id == it.Id;
        });
    }

    public bool ShowLyrics { get => E.ShowLyrics; set => Edit(E.ShowLyrics, value, v => E.ShowLyrics = v); }
    public bool LyricsLrclib { get => C.LyricsLrclib; set => Edit(C.LyricsLrclib, value, v => C.LyricsLrclib = v, Push.None); }
    public bool LyricsNetease { get => C.LyricsNetease; set => Edit(C.LyricsNetease, value, v => C.LyricsNetease = v, Push.None); }
    public int LyricsOffsetMs { get => C.LyricsOffsetMs; set => Edit(C.LyricsOffsetMs, Math.Clamp(value, -3000, 3000), v => C.LyricsOffsetMs = v, Push.None); }
    public string MusixmatchToken { get => C.MusixmatchToken; set => Edit(C.MusixmatchToken, (value ?? "").Trim(), v => C.MusixmatchToken = v, Push.None); }
    public bool Visualizer { get => C.Visualizer; set => Edit(C.Visualizer, value, v => C.Visualizer = v, Push.None); }
    public bool ListenHistory { get => C.ListenHistory; set => Edit(C.ListenHistory, value, v => C.ListenHistory = v, Push.None); }

    string _cacheStatus = "";
    public string CacheStatus { get => _cacheStatus; private set => Set(ref _cacheStatus, value); }
    public RelayCommand ClearLyricsCacheCommand => new(_ => CacheStatus = $"Cache des paroles vidé ({S.Sync.Lyrics.ClearDiskCache()} fichiers).");
    public RelayCommand ClearHistoryCommand => new(_ => { History.Clear(); CacheStatus = "Historique d'écoute effacé."; });

    string _lyricsStatus = "";
    public string LyricsStatus { get => _lyricsStatus; private set => Set(ref _lyricsStatus, value); }

    string _vizStatus = "";
    public string VizStatus { get => _vizStatus; private set => Set(ref _vizStatus, value); }

    public override void OnShown() { _signature = ""; _tick = 0; OnTick(); }

    public override void OnTick()
    {
        var lyr = S.Sync.Lyrics;
        LyricsStatus = !E.ShowLyrics ? "Paroles désactivées."
            : S.Sync.Current == null ? "Aucune lecture en cours."
            : lyr.LineCount > 0 ? $"Source : {lyr.LastSource} · {lyr.LineCount} lignes"
            : "Aucune parole synchronisée trouvée pour ce titre (ou recherche en cours).";

        var sp = S.Sync.Spectrum;
        VizStatus = !C.Visualizer ? "Visualiseur désactivé."
            : sp.Running ? $"Capture audio active · {sp.Packets} paquets envoyés"
            : string.IsNullOrEmpty(sp.Error) ? "Au repos : la capture démarre quand la carte affiche le spectre."
            : "Erreur : " + sp.Error;

        if (_tick++ % 4 == 0) Fire(RefreshSessionsAsync, "sessions");
    }

    async Task RefreshSessionsAsync()
    {
        if (_loading) return;
        _loading = true;
        try
        {
            var list = await S.Media.ListAsync();
            var sig = C.SelectedAppId + "|" + string.Join(";", list.Select(x => $"{x.Id}/{x.Status}/{x.Title}"));
            if (sig == _signature) return;
            _signature = sig;

            var sel = C.SelectedAppId ?? "";
            var items = new List<SessionItem>
            {
                new("", "Automatique", "Cider, Apple Music ou iTunes", sel.Length == 0),
                new(MediaService.FollowPlaying, "Celui qui joue", "Suit n'importe quel lecteur en cours de lecture", sel == MediaService.FollowPlaying)
            };
            foreach (var x in list)
            {
                var detail = x.Title.Length > 0 ? $"{x.Artist} — {x.Title}".Trim(' ', '—') : x.Status;
                items.Add(new SessionItem(x.Id, x.Name, detail, x.Id.Equals(sel, StringComparison.OrdinalIgnoreCase)));
            }
            if (sel.Length > 0 && !items.Any(i => i.IsSelected))
                items.Add(new SessionItem(sel, MediaService.FriendlyName(sel), "Application fermée", true));

            Sessions.Clear();
            foreach (var i in items) Sessions.Add(i);
        }
        finally { _loading = false; }
    }
}
