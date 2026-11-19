using System.Text.Json;
using System.Text.RegularExpressions;

namespace NowPlayingHub;

public class UpdateStatus
{
    public volatile bool Busy;
    public volatile int Percent;
    public volatile string Message = "";
}

public record ReleaseInfo(string? Tag, string? AssetUrl, string? Notes, string? Error, string? Sha256 = null);

public class Updater
{
    const int MaxFirmwareBytes = 4_000_000;

    readonly AppConfig _cfg;
    readonly EspClient _esp;
    readonly HttpClient _gh = new() { Timeout = TimeSpan.FromSeconds(120) };
    public UpdateStatus Status { get; } = new();

    public Updater(AppConfig cfg, EspClient esp)
    {
        _cfg = cfg; _esp = esp;
        _gh.DefaultRequestHeaders.UserAgent.ParseAdd("NowPlayingHub/" + AppInfo.Version);
        _gh.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
    }

    public async Task<ReleaseInfo> CheckLatestAsync()
    {
        var repo = (_cfg.GithubRepo ?? "").Trim();
        if (!Regex.IsMatch(repo, @"^[\w.-]+/[\w.-]+$"))
            return new ReleaseInfo(null, null, null, "Dépôt GitHub invalide (format : utilisateur/depot).");
        try
        {
            using var r = await _gh.GetAsync($"https://api.github.com/repos/{repo}/releases/latest");
            if (r.StatusCode == System.Net.HttpStatusCode.NotFound)
                return new ReleaseInfo(null, null, null, "Aucune release trouvée sur ce dépôt.");
            if (!r.IsSuccessStatusCode)
                return new ReleaseInfo(null, null, null, $"GitHub a répondu {(int)r.StatusCode}.");
            using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync());
            var root = doc.RootElement;
            string tag = root.TryGetProperty("tag_name", out var t) ? t.GetString() ?? "" : "";
            string notes = root.TryGetProperty("body", out var b) && b.ValueKind == JsonValueKind.String ? b.GetString() ?? "" : "";
            string? url = null, sha = null;
            if (root.TryGetProperty("assets", out var assets))
            {
                foreach (var a in assets.EnumerateArray())
                {
                    var name = a.GetProperty("name").GetString() ?? "";
                    if (!name.EndsWith(".bin", StringComparison.OrdinalIgnoreCase)) continue;
                    url = a.GetProperty("browser_download_url").GetString();
                    sha = a.TryGetProperty("digest", out var dg) && dg.ValueKind == JsonValueKind.String &&
                          (dg.GetString() ?? "").StartsWith("sha256:", StringComparison.OrdinalIgnoreCase)
                        ? dg.GetString()![7..].Trim().ToLowerInvariant() : null;
                    if (name.Contains("firmware", StringComparison.OrdinalIgnoreCase)) break;
                }
            }
            if (url == null) return new ReleaseInfo(tag, null, notes, "La release ne contient aucun fichier .bin.");
            return new ReleaseInfo(tag, url, notes, null, sha);
        }
        catch (Exception e) { return new ReleaseInfo(null, null, null, "Vérification impossible : " + e.Message); }
    }

    public static bool IsNewer(string latest, string current)
    {
        static Version? P(string s) => Version.TryParse(s.Trim().TrimStart('v', 'V'), out var v) ? v : null;
        var l = P(latest); var c = P(current);
        return l != null && c != null && l > c;
    }

    public async Task FlashBytesAsync(byte[] fw)
    {
        if (Status.Busy) return;
        Status.Busy = true; Status.Percent = 0; Status.Message = "Envoi du firmware…";
        try
        {
            var (ok, msg) = await _esp.FlashAsync(fw, p => Status.Percent = p);
            Status.Message = msg;
            if (ok) Status.Percent = 100;
            Log.Write("OTA : " + msg);
        }
        catch (Exception e) { Status.Message = "Erreur : " + e.Message; Log.Write("OTA : " + e); }
        finally { Status.Busy = false; }
    }

    public async Task FlashFromGithubAsync(string url, string? sha256 = null)
    {
        if (Status.Busy) return;
        Status.Busy = true; Status.Percent = 0; Status.Message = "Téléchargement…";
        byte[] fw;
        try
        {
            var u = new Uri(url);
            bool trusted = u.Scheme == "https" &&
                (u.Host == "github.com" || u.Host.EndsWith(".github.com") || u.Host.EndsWith(".githubusercontent.com"));
            if (!trusted) { Status.Message = "URL refusée (hors GitHub)."; Status.Busy = false; return; }
            fw = await _gh.GetByteArrayAsync(u);
            if (fw.Length > MaxFirmwareBytes) { Status.Message = "Fichier trop gros pour un firmware ESP32."; Status.Busy = false; return; }
            if (!string.IsNullOrEmpty(sha256) &&
                !Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(fw)).Equals(sha256, StringComparison.OrdinalIgnoreCase))
            {
                Status.Message = "Empreinte SHA-256 différente de celle de la release : mise à jour annulée.";
                Log.Write("OTA : " + Status.Message);
                Status.Busy = false; return;
            }
        }
        catch (Exception e) { Status.Message = "Téléchargement impossible : " + e.Message; Status.Busy = false; return; }
        Status.Busy = false;
        await FlashBytesAsync(fw);
    }

    public Task FlashFileAsync(string path)
    {
        var fi = new FileInfo(path);
        if (!fi.Exists) { Status.Message = "Fichier introuvable."; return Task.CompletedTask; }
        if (fi.Length > MaxFirmwareBytes) { Status.Message = "Fichier trop gros pour un firmware ESP32."; return Task.CompletedTask; }
        return FlashBytesAsync(File.ReadAllBytes(path));
    }
}
