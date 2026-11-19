namespace NowPlayingHub;

public sealed class HardwareViewModel : SettingsPage
{
    public override string Title => "Matériel";
    public override string Glyph => "\uE772";

    public AsyncCommand CalDarkCommand { get; }
    public AsyncCommand CalBrightCommand { get; }
    public AsyncCommand ClearSdCommand { get; }

    public HardwareViewModel(AppServices s) : base(s)
    {
        CalDarkCommand = new AsyncCommand(_ => Calibrate("dark"), () => S.Sync.EspOnline);
        CalBrightCommand = new AsyncCommand(_ => Calibrate("bright"), () => S.Sync.EspOnline);
        ClearSdCommand = new AsyncCommand(_ => ClearSd(), () => S.Sync.EspOnline);
    }

    public bool AutoBrightness { get => E.AutoBrightness; set => Edit(E.AutoBrightness, value, v => E.AutoBrightness = v); }

    public int LedMode
    {
        get => E.LedMode;
        set
        {
            if (!Edit(E.LedMode, Math.Clamp(value, 0, 3), v => E.LedMode = v)) return;
            Raise(nameof(LedIsOff)); Raise(nameof(LedIsAccent)); Raise(nameof(LedIsPulse)); Raise(nameof(LedIsBass));
        }
    }
    public bool LedIsOff { get => LedMode == 0; set { if (value) LedMode = 0; } }
    public bool LedIsAccent { get => LedMode == 1; set { if (value) LedMode = 1; } }
    public bool LedIsPulse { get => LedMode == 2; set { if (value) LedMode = 2; } }
    public bool LedIsBass { get => LedMode == 3; set { if (value) LedMode = 3; } }

    public int LedBrightness { get => E.LedBrightness; set => Edit(E.LedBrightness, Math.Clamp(value, 5, 100), v => E.LedBrightness = v); }
    public bool LedNightOff { get => E.LedNightOff; set => Edit(E.LedNightOff, value, v => E.LedNightOff = v); }
    public bool SdCache { get => E.SdCache; set => Edit(E.SdCache, value, v => E.SdCache = v); }
    public int SdMax { get => E.SdMax; set => Edit(E.SdMax, Math.Clamp(value, 20, 1000), v => E.SdMax = v); }

    string _ldrText = "—"; public string LdrText { get => _ldrText; private set => Set(ref _ldrText, value); }
    string _sdText = "—"; public string SdText { get => _sdText; private set => Set(ref _sdText, value); }
    string _message = ""; public string Message { get => _message; private set => Set(ref _message, value); }

    async Task Calibrate(string which)
    {
        bool ok = await S.Esp.CalibrateLdrAsync(which);
        Message = ok ? (which == "dark" ? "Niveau « sombre » enregistré." : "Niveau « clair » enregistré.") : "La carte n'a pas répondu.";
    }

    async Task ClearSd()
    {
        if (!Dialogs.Confirm("Vider le cache de pochettes de la carte SD ?")) return;
        Message = await S.Esp.ClearSdAsync() ? "Cache vidé." : "La carte n'a pas répondu.";
    }

    public override void OnShown() => OnTick();

    public override void OnTick()
    {
        var i = S.Sync.Info;
        if (!S.Sync.EspOnline || i == null) { LdrText = "Carte hors ligne"; SdText = "Carte hors ligne"; return; }
        LdrText = $"Lumière ambiante : {i.LdrLight} % (mesure brute {i.LdrRaw})";
        SdText = !i.SdEnabled ? "Cache désactivé"
               : !i.SdOk ? "Carte SD non détectée"
               : $"Carte SD détectée · {i.SdFiles} fichiers · {i.SdSizeMb} Mo";
    }
}
