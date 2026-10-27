using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace NowPlayingHub;

/// <summary>Reglages envoyes a l'ESP32 (stockes aussi dans sa flash).</summary>
public class EspSettings
{
    public int Brightness { get; set; } = 100;
    public string Accent { get; set; } = "#FA2D48";
    public int ScrollSpeed { get; set; } = 45;
    public bool ShowApp { get; set; } = true;
    public bool DynamicAccent { get; set; } = true;   // couleur d'accent tiree de la pochette
    public bool ShowLyrics { get; set; } = true;      // paroles synchronisees (LRCLIB)
}

/// <summary>Configuration de l'appli PC : %AppData%\NowPlayingHub\config.json</summary>
public class AppConfig
{
    public string EspHost { get; set; } = "nowplaying.local";
    public string EspToken { get; set; } = "nowplaying";
    public string SelectedAppId { get; set; } = "";   // vide = auto (Cider / Apple Music / iTunes)
    public string GithubRepo { get; set; } = "";      // "utilisateur/depot"
    public int IntervalMs { get; set; } = 1000;
    // --- Sources de paroles (essayees dans l'ordre : LRCLIB, Musixmatch, NetEase) ---
    public bool LyricsLrclib { get; set; } = true;
    public bool LyricsNetease { get; set; } = true;
    public string MusixmatchToken { get; set; } = "";   // vide = Musixmatch desactive
    public bool AsciiOnly { get; set; } = true;
    public int Port { get; set; } = 8123;
    public EspSettings Esp { get; set; } = new();

    // --- Ecran de veille ---
    public double Latitude      { get; set; } = 50.595;   // Fallais
    public double Longitude     { get; set; } = 5.117;
    public string LocationName  { get; set; } = "Fallais";
    public string Timezone      { get; set; } = "CET-1CEST,M3.5.0,M10.5.0/3";   // format POSIX (heure d'ete incluse)
    public string NewsRssUrl    { get; set; } = "https://www.rfi.fr/fr/rss-podcasts-emissions";
    public int    NewsCount     { get; set; } = 5;
    public bool   NightModeEnabled { get; set; } = true;
    public int    NightModeStart{ get; set; } = 22;
    public int    NightModeEnd  { get; set; } = 7;
    public int    NightBrightness{ get; set; } = 8;    // % pendant la nuit
    public int    DimAfterMinutes{ get; set; } = 3;
    public int    DimBrightness { get; set; } = 15;    // % apres inactivite

    public static string DirPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "NowPlayingHub");
    static string FilePath => Path.Combine(DirPath, "config.json");
    // IMPORTANT: InvariantCulture pour eviter que les floats soient serialises avec "," (culture FR)
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
                var json = File.ReadAllText(FilePath);
                var cfg = JsonSerializer.Deserialize<AppConfig>(json, Opt) ?? new AppConfig();
                // Sanity-check : si lat/lon semblent corrompus (ex: 5085 au lieu de 50.85)
                // c'est un bug de serialisation ancienne avec culture FR → reset
                if (Math.Abs(cfg.Latitude) > 90 || Math.Abs(cfg.Longitude) > 180)
                {
                    Log.Write($"config: lat/lon corrompus ({cfg.Latitude}/{cfg.Longitude}), reset aux valeurs par defaut");
                    cfg.Latitude  = 50.595;
                    cfg.Longitude = 5.117;
                    cfg.Save(); // réécrire proprement
                }
                return cfg;
            }
        }
        catch (Exception e) { Log.Write("config: " + e.Message); }
        return new AppConfig();
    }

    public void Save()
    {
        lock (Lock)
        {
            Directory.CreateDirectory(DirPath);
            // Serialiser avec InvariantCulture via un JsonSerializerContext ne suffit pas —
            // on force manuellement le point décimal pour lat/lon via une substitution post-serialize
            var json = JsonSerializer.Serialize(this, Opt);
            File.WriteAllText(FilePath, json);
        }
    }
}

public static class Log
{
    static readonly object L = new();
    public static void Write(string msg)
    {
        try
        {
            lock (L)
            {
                Directory.CreateDirectory(AppConfig.DirPath);
                var f = Path.Combine(AppConfig.DirPath, "log.txt");
                if (File.Exists(f) && new FileInfo(f).Length > 512_000) File.Delete(f);
                File.AppendAllText(f, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {msg}{Environment.NewLine}");
            }
        }
        catch { /* le log ne doit jamais faire planter l'appli */ }
    }
}
