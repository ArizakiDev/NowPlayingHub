using System.Globalization;
using System.Text.RegularExpressions;
using Windows.Media.Control;

namespace NowPlayingHub;

public record Snapshot(string State, string Title, string Artist, string Album,
                       double Position, double Duration, string AppId, string AppName)
{
    public string Key => $"{Title}|{Artist}|{Album}";
}

public record SessionInfo(string Id, string Name, string Status, string Title, string Artist);

public class MediaService
{
    static readonly Regex DefaultPlayers = new("cider|apple|itunes", RegexOptions.IgnoreCase | RegexOptions.Compiled);
    static readonly Regex HexId = new("^[0-9A-Fa-f]{16}$", RegexOptions.Compiled);

    public const string FollowPlaying = "*";

    readonly AppConfig _cfg;
    GlobalSystemMediaTransportControlsSessionManager? _mgr;

    public MediaService(AppConfig cfg) { _cfg = cfg; }

    async Task<GlobalSystemMediaTransportControlsSessionManager> MgrAsync() =>
        _mgr ??= await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();

    static bool IsPlaying(GlobalSystemMediaTransportControlsSession s) =>
        s.GetPlaybackInfo().PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;

    GlobalSystemMediaTransportControlsSession? Pick(IReadOnlyList<GlobalSystemMediaTransportControlsSession> list)
    {
        var sel = _cfg.SelectedAppId ?? "";
        if (sel == FollowPlaying)
            return list.FirstOrDefault(IsPlaying)
                ?? list.FirstOrDefault(s => DefaultPlayers.IsMatch(s.SourceAppUserModelId ?? ""))
                ?? list.FirstOrDefault();
        var matches = new List<GlobalSystemMediaTransportControlsSession>();
        foreach (var s in list)
        {
            var id = s.SourceAppUserModelId ?? "";
            bool ok = sel.Length > 0
                ? id.Equals(sel, StringComparison.OrdinalIgnoreCase)
                : DefaultPlayers.IsMatch(id);
            if (ok) matches.Add(s);
        }
        return matches.FirstOrDefault(IsPlaying) ?? matches.FirstOrDefault();
    }

    public static string FriendlyName(string id)
    {
        if (HexId.IsMatch(id)) return "Média";
        var s = id.Split('!')[0].Split('_')[0];
        if (s.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) s = s[..^4];
        if (s.Contains("AppleMusic", StringComparison.OrdinalIgnoreCase) ||
            s.Contains("iTunes", StringComparison.OrdinalIgnoreCase)) return "Apple Music";
        var n = s.Split('.')[^1];
        if (n.Length == 0) return "Média";
        return char.ToUpper(n[0], CultureInfo.InvariantCulture) + n[1..];
    }

    public async Task<Snapshot?> ReadAsync()
    {
        var mgr = await MgrAsync();
        var s = Pick(mgr.GetSessions());
        if (s == null) return null;

        var p = await s.TryGetMediaPropertiesAsync();
        var status = s.GetPlaybackInfo().PlaybackStatus;
        string state = status == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing ? "playing"
                     : status == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Paused ? "paused"
                     : "stopped";
        if (state == "stopped") return null;

        var title = p.Title ?? "";
        if (title.Length == 0 || title == "Cider Audio Session") return null;

        var tl = s.GetTimelineProperties();
        double dur = (tl.EndTime - tl.StartTime).TotalSeconds;
        double pos = tl.Position.TotalSeconds;
        if (state == "playing" && tl.LastUpdatedTime.Year > 2000)
        {
            var delta = (DateTimeOffset.UtcNow - tl.LastUpdatedTime).TotalSeconds;
            if (delta > 0) pos += delta;
        }
        if (dur < 0) dur = 0;
        if (dur > 0 && pos > dur) pos = dur;
        if (pos < 0) pos = 0;

        var id = s.SourceAppUserModelId ?? "";
        return new Snapshot(state, title, p.Artist ?? "", p.AlbumTitle ?? "", pos, dur, id, FriendlyName(id));
    }

    public async Task<List<SessionInfo>> ListAsync()
    {
        var res = new List<SessionInfo>();
        var mgr = await MgrAsync();
        foreach (var s in mgr.GetSessions())
        {
            try
            {
                var p = await s.TryGetMediaPropertiesAsync();
                var id = s.SourceAppUserModelId ?? "";
                res.Add(new SessionInfo(id, FriendlyName(id),
                    s.GetPlaybackInfo().PlaybackStatus.ToString(), p.Title ?? "", p.Artist ?? ""));
            }
            catch (Exception e) { Log.Write("sessions : " + e.Message); }
        }
        return res;
    }

    public async Task<byte[]?> GetThumbnailAsync()
    {
        var mgr = await MgrAsync();
        var s = Pick(mgr.GetSessions());
        if (s == null) return null;
        var p = await s.TryGetMediaPropertiesAsync();
        if (p.Thumbnail == null) return null;
        using var stream = await p.Thumbnail.OpenReadAsync();
        using var net = stream.AsStreamForRead();
        using var ms = new MemoryStream();
        await net.CopyToAsync(ms);
        return ms.Length > 0 ? ms.ToArray() : null;
    }

    public async Task<bool> CommandAsync(string cmd)
    {
        var mgr = await MgrAsync();
        var s = Pick(mgr.GetSessions());
        if (s == null) return false;
        if (cmd.StartsWith("seek:", StringComparison.Ordinal))
        {
            if (!double.TryParse(cmd[5..], NumberStyles.Float, CultureInfo.InvariantCulture, out var secs) || secs < 0) return false;
            return await s.TryChangePlaybackPositionAsync((long)(secs * 10_000_000));
        }
        switch (cmd)
        {
            case "next": return await s.TrySkipNextAsync();
            case "prev": return await s.TrySkipPreviousAsync();
            case "toggle": return await s.TryTogglePlayPauseAsync();
            default: return false;
        }
    }
}
