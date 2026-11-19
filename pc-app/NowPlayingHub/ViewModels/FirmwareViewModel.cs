namespace NowPlayingHub;

public sealed class FirmwareViewModel : SettingsPage
{
    ReleaseInfo? _release;
    bool _updateAvailable;
    bool _autoChecked;
    bool _busyCheck;

    public override string Title => "Firmware";
    public override string Glyph => "\uE896";
    public override bool HasBadge => _updateAvailable;

    public AsyncCommand CheckCommand { get; }
    public AsyncCommand InstallCommand { get; }
    public AsyncCommand PickFileCommand { get; }

    public FirmwareViewModel(AppServices s) : base(s)
    {
        CheckCommand = new AsyncCommand(_ => CheckAsync(true));
        InstallCommand = new AsyncCommand(_ => InstallAsync(), () => CanInstall);
        PickFileCommand = new AsyncCommand(_ => PickFileAsync(), () => !IsBusy && S.Sync.EspOnline);
    }

    public string GithubRepo
    {
        get => C.GithubRepo;
        set { if (Edit(C.GithubRepo, (value ?? "").Trim(), v => C.GithubRepo = v, Push.None)) { _release = null; _autoChecked = false; ResultText = ""; Raise(nameof(CanInstall)); } }
    }
    public bool CheckUpdatesOnStart { get => C.CheckUpdatesOnStart; set => Edit(C.CheckUpdatesOnStart, value, v => C.CheckUpdatesOnStart = v, Push.None); }

    string _current = "—"; public string CurrentVersion { get => _current; private set => Set(ref _current, value); }
    string _result = ""; public string ResultText { get => _result; private set => Set(ref _result, value); }
    string _notes = ""; public string Notes { get => _notes; private set => Set(ref _notes, value); }
    bool _busy; public bool IsBusy { get => _busy; private set { if (Set(ref _busy, value)) Raise(nameof(CanInstall)); } }
    int _percent; public int Percent { get => _percent; private set => Set(ref _percent, value); }
    string _message = ""; public string StatusMessage { get => _message; private set => Set(ref _message, value); }

    public bool CanInstall => !IsBusy && _release?.AssetUrl != null && S.Sync.EspOnline;

    public void BackgroundCheck()
    {
        if (_autoChecked || !C.CheckUpdatesOnStart || !S.Sync.EspOnline || string.IsNullOrWhiteSpace(C.GithubRepo)) return;
        _autoChecked = true;
        Fire(() => CheckAsync(false), "vérification du firmware");
    }

    async Task CheckAsync(bool manual)
    {
        if (_busyCheck) return;
        _busyCheck = true;
        try
        {
            if (manual) ResultText = "Vérification…";
            var rel = await S.Updater.CheckLatestAsync();
            _release = rel;
            if (rel.Error != null)
            {
                if (manual) ResultText = rel.Error;
                return;
            }
            var cur = S.Sync.EspVersion;
            bool newer = Updater.IsNewer(rel.Tag ?? "", cur);
            if (_updateAvailable != newer) { _updateAvailable = newer; Raise(nameof(HasBadge)); }
            ResultText = newer ? $"Nouvelle version disponible : {rel.Tag} (la carte est en {cur})."
                               : $"Le firmware est à jour ({cur}). Dernière release : {rel.Tag}.";
            Notes = (rel.Notes ?? "").Trim();
            if (Notes.Length > 600) Notes = Notes[..600] + "…";
            Raise(nameof(CanInstall));
        }
        finally { _busyCheck = false; }
    }

    async Task InstallAsync()
    {
        var rel = _release;
        if (rel?.AssetUrl == null) return;
        if (!Dialogs.Confirm($"Installer le firmware {rel.Tag} sur la carte ?\nElle redémarrera à la fin de la mise à jour : ne la débranche pas.")) return;
        await S.Updater.FlashFromGithubAsync(rel.AssetUrl, rel.Sha256);
    }

    Task PickFileAsync()
    {
        var dlg = new Microsoft.Win32.OpenFileDialog
        {
            Title = "Choisir un firmware",
            Filter = "Firmware ESP32 (*.bin)|*.bin",
            CheckFileExists = true
        };
        if (dlg.ShowDialog() != true) return Task.CompletedTask;
        if (!Dialogs.Confirm($"Installer « {System.IO.Path.GetFileName(dlg.FileName)} » sur la carte ?\nElle redémarrera à la fin de la mise à jour : ne la débranche pas."))
            return Task.CompletedTask;
        return S.Updater.FlashFileAsync(dlg.FileName);
    }

    public override void OnShown() => OnTick();

    public override void OnTick()
    {
        var ver = S.Sync.EspVersion;
        CurrentVersion = S.Sync.EspOnline && ver.Length > 0 ? ver : "carte hors ligne";
        var st = S.Updater.Status;
        IsBusy = st.Busy;
        Percent = st.Percent;
        StatusMessage = st.Message;
        Raise(nameof(CanInstall));
    }
}
