using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace NowPlayingHub;

public static class AppInfo
{
    public static readonly string Version =
        Assembly.GetExecutingAssembly().GetName().Version is { } v ? $"{v.Major}.{v.Minor}.{v.Build}" : "2.0.0";
}

public class EspSettings
{
    public int Brightness { get; set; } = 100;
    public string Accent { get; set; } = "#FA2D48";
    public int ScrollSpeed { get; set; } = 45;
    public bool ShowApp { get; set; } = true;
    public bool DynamicAccent { get; set; } = true;
    public bool ShowLyrics { get; set; } = true;
    public bool AutoBrightness { get; set; } = false;
    public int LedMode { get; set; } = 0;
    public int LedBrightness { get; set; } = 40;
    public bool LedNightOff { get; set; } = true;
    public bool SdCache { get; set; } = false;
    public int SdMax { get; set; } = 150;
}

public class AppConfig
{
    public string EspHost { get; set; } = "nowplaying.local";
    public string EspToken { get; set; } = "nowplaying";
    public string SelectedAppId { get; set; } = "";
    public string GithubRepo { get; set; } = "";
    public int IntervalMs { get; set; } = 1000;

    public bool LyricsLrclib { get; set; } = true;
    public bool LyricsNetease { get; set; } = true;
    public string MusixmatchToken { get; set; } = "";
    public int LyricsOffsetMs { get; set; } = 0;

    public bool Visualizer { get; set; } = true;
    public bool ListenHistory { get; set; } = true;
    public bool AsciiOnly { get; set; } = true;
    public EspSettings Esp { get; set; } = new();

    public double Latitude { get; set; } = 50.8466;
    public double Longitude { get; set; } = 4.3517;
    public string LocationName { get; set; } = "Bruxelles";
    public string Timezone { get; set; } = "CET-1CEST,M3.5.0,M10.5.0/3";
    public string NewsRssUrl { get; set; } = "https://news.google.com/rss?hl=fr&gl=FR&ceid=FR:fr";
    public int NewsCount { get; set; } = 5;
    public bool NightModeEnabled { get; set; } = true;
    public int NightModeStart { get; set; } = 22;
    public int NightModeEnd { get; set; } = 7;
    public int NightBrightness { get; set; } = 8;
    public int DimAfterMinutes { get; set; } = 3;
    public int DimBrightness { get; set; } = 15;

    public bool CloseToTray { get; set; } = true;
    public bool CheckUpdatesOnStart { get; set; } = true;

    public bool StatusEnabled { get; set; } = true;
    public bool StatusLan { get; set; } = false;
    public int StatusPort { get; set; } = 8123;

    public static string DirPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "NowPlayingHub");
    static string FilePath => Path.Combine(DirPath, "config.json");

    static readonly JsonSerializerOptions Opt = new()
    {
        WriteIndented = true,
        NumberHandling = JsonNumberHandling.AllowReadingFromString
    };
    static readonly object Lock = new();

    public static AppConfig Load()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                var cfg = JsonSerializer.Deserialize<AppConfig>(File.ReadAllText(FilePath), Opt);
                if (cfg != null) { cfg.Normalize(); return cfg; }
            }
        }
        catch (Exception e)
        {
            Log.Write("config illisible, valeurs par défaut utilisées : " + e.Message);
            try { File.Copy(FilePath, FilePath + ".bad", true); } catch { }
        }
        return new AppConfig();
    }

    void Normalize()
    {
        Esp ??= new EspSettings();
        if (string.IsNullOrWhiteSpace(EspHost)) EspHost = "nowplaying.local";
        EspHost = EspHost.Trim();
        if (Math.Abs(Latitude) > 90 || Math.Abs(Longitude) > 180) { Latitude = 50.8466; Longitude = 4.3517; }
        IntervalMs = Math.Clamp(IntervalMs, 250, 5000);
        NewsCount = Math.Clamp(NewsCount, 1, 10);
        if (StatusPort < 1024 || StatusPort > 65535) StatusPort = 8123;
        Esp.Brightness = Math.Clamp(Esp.Brightness, 5, 100);
        Esp.ScrollSpeed = Math.Clamp(Esp.ScrollSpeed, 10, 150);
        Esp.LedMode = Math.Clamp(Esp.LedMode, 0, 3);
        Esp.LedBrightness = Math.Clamp(Esp.LedBrightness, 5, 100);
        Esp.SdMax = Math.Clamp(Esp.SdMax, 20, 1000);
        if (!System.Text.RegularExpressions.Regex.IsMatch(Esp.Accent ?? "", "^#[0-9A-Fa-f]{6}$")) Esp.Accent = "#FA2D48";
    }

    public void Save()
    {
        try
        {
            lock (Lock)
            {
                Directory.CreateDirectory(DirPath);
                var tmp = FilePath + ".tmp";
                File.WriteAllText(tmp, JsonSerializer.Serialize(this, Opt));
                File.Move(tmp, FilePath, true);
            }
        }
        catch (Exception e) { Log.Write("config : écriture impossible : " + e.Message); }
    }
}
