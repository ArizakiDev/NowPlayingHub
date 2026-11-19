using System.Text.RegularExpressions;

namespace NowPlayingHub;

public sealed record Swatch(string Hex);

public sealed class DisplayViewModel : SettingsPage
{
    static readonly Regex HexRx = new("^#[0-9A-Fa-f]{6}$", RegexOptions.Compiled);

    public override string Title => "Écran";
    public override string Glyph => "\uE7F4";

    public IReadOnlyList<Swatch> Swatches { get; } = new[]
    {
        "#FA2D48", "#FF9500", "#FFD60A", "#30D158", "#0A84FF", "#BF5AF2", "#FF6482", "#FFFFFF"
    }.Select(h => new Swatch(h)).ToList();

    public RelayCommand PickAccentCommand { get; }

    public DisplayViewModel(AppServices s) : base(s)
    {
        PickAccentCommand = new RelayCommand(p => { if (p is string hex) AccentText = hex; });
    }

    public int Brightness { get => E.Brightness; set => Edit(E.Brightness, Math.Clamp(value, 5, 100), v => E.Brightness = v); }
    public int ScrollSpeed { get => E.ScrollSpeed; set => Edit(E.ScrollSpeed, Math.Clamp(value, 10, 150), v => E.ScrollSpeed = v); }
    public bool ShowApp { get => E.ShowApp; set => Edit(E.ShowApp, value, v => E.ShowApp = v); }
    public bool DynamicAccent { get => E.DynamicAccent; set => Edit(E.DynamicAccent, value, v => E.DynamicAccent = v); }

    public string AccentText
    {
        get => E.Accent;
        set
        {
            var hex = (value ?? "").Trim();
            if (hex.Length == 6 && !hex.StartsWith('#')) hex = "#" + hex;
            if (!HexRx.IsMatch(hex)) { Raise(); return; }
            Edit(E.Accent, hex.ToUpperInvariant(), v => E.Accent = v);
        }
    }

    public bool NightEnabled { get => C.NightModeEnabled; set => Edit(C.NightModeEnabled, value, v => C.NightModeEnabled = v); }
    public int NightStart { get => C.NightModeStart; set => Edit(C.NightModeStart, Math.Clamp(value, 0, 23), v => C.NightModeStart = v); }
    public int NightEnd { get => C.NightModeEnd; set => Edit(C.NightModeEnd, Math.Clamp(value, 0, 23), v => C.NightModeEnd = v); }
    public int NightBrightness { get => C.NightBrightness; set => Edit(C.NightBrightness, Math.Clamp(value, 1, 100), v => C.NightBrightness = v); }
    public int DimAfterMinutes { get => C.DimAfterMinutes; set => Edit(C.DimAfterMinutes, Math.Clamp(value, 1, 60), v => C.DimAfterMinutes = v); }
    public int DimBrightness { get => C.DimBrightness; set => Edit(C.DimBrightness, Math.Clamp(value, 1, 100), v => C.DimBrightness = v); }
}
