using System.Diagnostics;

namespace NowPlayingHub;

public sealed class SystemViewModel : SettingsPage
{
    public override string Title => "Système";
    public override string Glyph => "\uE713";

    public AsyncCommand TestCommand { get; }
    public AsyncCommand RebootCommand { get; }
    public AsyncCommand WifiResetCommand { get; }
    public RelayCommand OpenLogsCommand { get; }
    public RelayCommand RefreshLogCommand { get; }
    public RelayCommand OpenStatusCommand { get; }

    public SystemViewModel(AppServices s) : base(s)
    {
        TestCommand = new AsyncCommand(_ => TestAsync());
        RebootCommand = new AsyncCommand(_ => RebootAsync());
        WifiResetCommand = new AsyncCommand(_ => WifiResetAsync());
        OpenLogsCommand = new RelayCommand(_ => OpenLogs());
        RefreshLogCommand = new RelayCommand(_ => LogText = Log.Tail());
        OpenStatusCommand = new RelayCommand(_ => OpenStatus());
    }

    public string EspHost
    {
        get => C.EspHost;
        set
        {
            var h = (value ?? "").Trim();
            if (h.Length == 0 || h.Contains(' ') || h.Contains('/')) { Raise(); return; }
            if (Edit(C.EspHost, h, v => C.EspHost = v, Push.None)) S.Sync.ForceReconnect();
        }
    }

    public string EspToken
    {
        get => C.EspToken;
        set { if (Edit(C.EspToken, (value ?? "").Trim(), v => C.EspToken = v, Push.None)) Raise(nameof(TokenWarning)); }
    }

    public string TokenWarning => C.EspToken == "nowplaying"
        ? "Le jeton par défaut est encore utilisé : choisis-en un autre dans le portail Wi-Fi de la carte (ou OTA_TOKEN de config.h), puis saisis le même ici."
        : "";

    string _testResult = "";
    public string TestResult { get => _testResult; private set => Set(ref _testResult, value); }

    public bool StatusEnabled
    {
        get => C.StatusEnabled;
        set { if (Edit(C.StatusEnabled, value, v => C.StatusEnabled = v, Push.None)) RestartWeb(); }
    }
    public bool StatusLan
    {
        get => C.StatusLan;
        set { if (Edit(C.StatusLan, value, v => C.StatusLan = v, Push.None)) RestartWeb(); }
    }
    public string StatusPortText
    {
        get => C.StatusPort.ToString();
        set
        {
            if (int.TryParse((value ?? "").Trim(), out var p) && p >= 1024 && p <= 65535)
            { if (Edit(C.StatusPort, p, v => C.StatusPort = v, Push.None)) RestartWeb(); }
            else Raise();
        }
    }
    public string StatusUrls => !C.StatusEnabled || !S.StatusWeb.Running ? "" : string.Join("\n", S.StatusWeb.Urls());
    public string StatusError => C.StatusEnabled ? (S.StatusWeb.Error ?? "") : "";

    void RestartWeb()
    {
        S.StatusWeb.Restart();
        Raise(nameof(StatusUrls)); Raise(nameof(StatusError));
    }

    void OpenStatus()
    {
        try
        {
            if (!S.StatusWeb.Running) { Message = "La page web n'est pas démarrée."; return; }
            Process.Start(new ProcessStartInfo(S.StatusWeb.Urls()[0]) { UseShellExecute = true });
        }
        catch (Exception e) { Message = "Ouverture impossible : " + e.Message; }
    }

    public bool Autostart_
    {
        get => Autostart.Get();
        set { Autostart.Set(value); Raise(); }
    }
    public bool CloseToTray { get => C.CloseToTray; set => Edit(C.CloseToTray, value, v => C.CloseToTray = v, Push.None); }
    public bool AsciiOnly { get => C.AsciiOnly; set => Edit(C.AsciiOnly, value, v => C.AsciiOnly = v, Push.None); }
    public int IntervalMs { get => C.IntervalMs; set => Edit(C.IntervalMs, Math.Clamp(value, 250, 5000), v => C.IntervalMs = v, Push.None); }
    public string AppVersion => "Now Playing Hub " + AppInfo.Version;
    public string DataFolder => AppConfig.DirPath;

    string _logText = ""; public string LogText { get => _logText; private set => Set(ref _logText, value); }
    string _message = ""; public string Message { get => _message; private set => Set(ref _message, value); }

    async Task TestAsync()
    {
        TestResult = "Test en cours…";
        try
        {
            var info = await S.Esp.GetInfoAsync();
            TestResult = info == null ? "La carte a répondu, mais avec une erreur."
                                      : $"Connexion réussie · firmware {info.Version} · {info.Ip}";
        }
        catch (Exception e) { TestResult = "Carte injoignable : " + e.Message; }
    }

    async Task RebootAsync()
    {
        if (!Dialogs.Confirm("Redémarrer la carte ?")) return;
        Message = await S.Esp.RebootAsync() ? "La carte redémarre." : "Échec (jeton refusé ou carte hors ligne).";
    }

    async Task WifiResetAsync()
    {
        if (!Dialogs.Confirm("Oublier le Wi-Fi de la carte ?\nElle redémarrera en mode configuration (réseau « NowPlaying-Setup »).")) return;
        Message = await S.Esp.ResetWifiAsync() ? "Wi-Fi oublié, la carte redémarre." : "Échec (jeton refusé ou carte hors ligne).";
        S.Sync.ForceReconnect();
    }

    void OpenLogs()
    {
        try
        {
            Directory.CreateDirectory(AppConfig.DirPath);
            Process.Start(new ProcessStartInfo("explorer.exe", $"\"{AppConfig.DirPath}\"") { UseShellExecute = true });
        }
        catch (Exception e) { Message = "Ouverture impossible : " + e.Message; }
    }

    public override void OnShown()
    {
        LogText = Log.Tail();
        Raise(nameof(Autostart_));
        Raise(nameof(StatusUrls)); Raise(nameof(StatusError));
    }
}
