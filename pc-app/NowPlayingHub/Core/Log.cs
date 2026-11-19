using System.Text;

namespace NowPlayingHub;

public static class Log
{
    static readonly object L = new();
    public static string Dir => AppConfig.DirPath;
    public static string FilePath => Path.Combine(Dir, "log.txt");

    public static void Write(string msg)
    {
        try
        {
            lock (L)
            {
                Directory.CreateDirectory(Dir);
                var f = FilePath;
                if (File.Exists(f) && new FileInfo(f).Length > 512_000)
                    File.Move(f, Path.Combine(Dir, "log.old.txt"), true);
                File.AppendAllText(f, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {msg}{Environment.NewLine}");
            }
        }
        catch {   }
    }

    public static string Tail(int lines = 80)
    {
        try
        {
            lock (L)
            {
                if (!File.Exists(FilePath)) return "";
                using var fs = new FileStream(FilePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                using var sr = new StreamReader(fs, Encoding.UTF8);
                var all = sr.ReadToEnd().Split('\n');
                var take = all.Skip(Math.Max(0, all.Length - lines - 1)).Select(x => x.TrimEnd('\r'));
                return string.Join(Environment.NewLine, take);
            }
        }
        catch (Exception e) { return "Journal illisible : " + e.Message; }
    }
}
