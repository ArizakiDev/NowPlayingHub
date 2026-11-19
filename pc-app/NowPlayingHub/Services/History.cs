using System.Globalization;
using System.Text;
using System.Text.Json;

namespace NowPlayingHub;

public static class History
{
    const int MinListenSec = 30;
    const int MaxKeep = 20000;

    public record Entry(DateTime At, string Title, string Artist, int Sec);

    public static volatile bool Enabled = true;

    static readonly object Lock = new();
    static readonly List<Entry> Items = new();
    static bool _loaded;

    static string _key = "", _title = "", _artist = "";
    static double _sec;
    static long _lastTick;

    static string FilePath => Path.Combine(AppConfig.DirPath, "history.jsonl");

    static void EnsureLoaded()
    {
        if (_loaded) return;
        _loaded = true;
        try
        {
            if (!File.Exists(FilePath)) return;
            foreach (var line in File.ReadLines(FilePath, Encoding.UTF8))
            {
                try
                {
                    using var d = JsonDocument.Parse(line);
                    var r = d.RootElement;
                    Items.Add(new Entry(
                        DateTime.Parse(r.GetProperty("t").GetString()!, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind).ToLocalTime(),
                        r.GetProperty("title").GetString() ?? "", r.GetProperty("artist").GetString() ?? "", r.GetProperty("sec").GetInt32()));
                }
                catch {   }
            }
            if (Items.Count > MaxKeep) Items.RemoveRange(0, Items.Count - MaxKeep);
        }
        catch (Exception e) { Log.Write("historique : " + e.Message); }
    }

    public static void Track(Snapshot? snap)
    {
        lock (Lock)
        {
            long now = Environment.TickCount64;
            double dt = _lastTick == 0 ? 0 : Math.Min(3.0, (now - _lastTick) / 1000.0);
            _lastTick = now;

            if (snap == null || snap.Key != _key) { Commit(); }
            if (snap == null) return;
            if (snap.Key != _key) { _key = snap.Key; _title = snap.Title; _artist = snap.Artist; _sec = 0; }
            if (snap.State == "playing") _sec += dt;
        }
    }

    static void Commit()
    {
        if (_key.Length > 0 && _sec >= MinListenSec)
        {
            var e = new Entry(DateTime.Now, _title, _artist, (int)_sec);
            EnsureLoaded();
            Items.Add(e);
            if (Items.Count > MaxKeep) Items.RemoveAt(0);
            try
            {
                Directory.CreateDirectory(AppConfig.DirPath);
                var json = JsonSerializer.Serialize(new { t = e.At.ToUniversalTime().ToString("o", CultureInfo.InvariantCulture), title = e.Title, artist = e.Artist, sec = e.Sec });
                File.AppendAllText(FilePath, json + "\n", Encoding.UTF8);
            }
            catch (Exception ex) { Log.Write("historique : " + ex.Message); }
        }
        _key = ""; _title = ""; _artist = ""; _sec = 0;
    }

    public static void Clear()
    {
        lock (Lock)
        {
            Items.Clear(); _loaded = true;
            try { if (File.Exists(FilePath)) File.Delete(FilePath); } catch (Exception e) { Log.Write("historique : " + e.Message); }
        }
    }

    public static object? Summary()
    {
        if (!Enabled) return null;
        lock (Lock)
        {
            EnsureLoaded();
            var now = DateTime.Now;
            var month = Items.Where(x => x.At >= now.AddDays(-30)).ToList();
            var week = month.Where(x => x.At >= now.AddDays(-7)).ToList();
            var today = month.Where(x => x.At.Date == now.Date).ToList();

            static object Mins(List<Entry> l) => new { plays = l.Count, minutes = (int)Math.Round(l.Sum(x => x.Sec) / 60.0) };

            var topTracks = month.GroupBy(x => (x.Title, x.Artist))
                .Select(g => new { title = g.Key.Title, artist = g.Key.Artist, plays = g.Count(), minutes = (int)Math.Round(g.Sum(x => x.Sec) / 60.0) })
                .OrderByDescending(x => x.plays).ThenByDescending(x => x.minutes).Take(8).ToList();
            var topArtists = month.Where(x => x.Artist.Length > 0).GroupBy(x => x.Artist)
                .Select(g => new { artist = g.Key, plays = g.Count(), minutes = (int)Math.Round(g.Sum(x => x.Sec) / 60.0) })
                .OrderByDescending(x => x.minutes).Take(8).ToList();

            return new
            {
                today = Mins(today), week = Mins(week), month = Mins(month), total = Items.Count,
                topTracks, topArtists,
                recent = Items.AsEnumerable().Reverse().Take(8).Select(x => new { at = x.At.ToString("dd/MM HH:mm", CultureInfo.InvariantCulture), title = x.Title, artist = x.Artist })
            };
        }
    }
}
