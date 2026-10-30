using System.Net.NetworkInformation;
using System.Runtime.InteropServices;

namespace NowPlayingHub;

/// <summary>Instantane des stats du PC (CPU, memoire, disque systeme, reseau, uptime).</summary>
public record PcStatsSample(int Cpu, int Ram, int Disk, double RamUsed, double RamTotal, double DiskFree,
                            long DownKBs, long UpKBs, long UptimeMin);

/// <summary>Echantillonne les stats du PC toutes les 2 s (appels Win32 directs, aucune dependance supplementaire).</summary>
public sealed class PcStatsService : IDisposable
{
    [StructLayout(LayoutKind.Sequential)]
    struct FILETIME { public uint Low, High; }

    [StructLayout(LayoutKind.Sequential)]
    struct MEMORYSTATUSEX
    {
        public uint Length, MemoryLoad;
        public ulong TotalPhys, AvailPhys, TotalPageFile, AvailPageFile, TotalVirtual, AvailVirtual, AvailExtendedVirtual;
    }

    [DllImport("kernel32.dll")] static extern bool GetSystemTimes(out FILETIME idle, out FILETIME kernel, out FILETIME user);
    [DllImport("kernel32.dll")] static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX m);

    static ulong U(FILETIME f) => ((ulong)f.High << 32) | f.Low;

    System.Threading.Timer? _timer;
    ulong _lastIdle, _lastKernel, _lastUser;
    long _lastRx, _lastTx, _lastTicks;

    /// <summary>Derniere mesure (null avant la premiere).</summary>
    public volatile PcStatsSample? Latest;

    public void Start()
    {
        if (_timer != null) return;
        Sample();                                   // amorce les compteurs
        _timer = new System.Threading.Timer(_ => { try { Sample(); } catch { } }, null, 2000, 2000);
    }

    static (long rx, long tx) NetBytes()
    {
        long rx = 0, tx = 0;
        foreach (var ni in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (ni.OperationalStatus != OperationalStatus.Up) continue;
            if (ni.NetworkInterfaceType is NetworkInterfaceType.Loopback or NetworkInterfaceType.Tunnel) continue;
            try
            {
                var st = ni.GetIPv4Statistics();
                rx += st.BytesReceived; tx += st.BytesSent;
            }
            catch { }
        }
        return (rx, tx);
    }

    void Sample()
    {
        // CPU : (noyau + utilisateur - repos) / (noyau + utilisateur) ; "noyau" inclut deja le repos
        int cpu = Latest?.Cpu ?? 0;
        if (GetSystemTimes(out var idle, out var kernel, out var user))
        {
            ulong i = U(idle), k = U(kernel), u = U(user);
            ulong dTotal = (k - _lastKernel) + (u - _lastUser), dIdle = i - _lastIdle;
            if (_lastKernel != 0 && dTotal > 0)
                cpu = (int)Math.Clamp(100.0 * (dTotal - dIdle) / dTotal, 0, 100);
            _lastIdle = i; _lastKernel = k; _lastUser = u;
        }

        var mem = new MEMORYSTATUSEX { Length = (uint)Marshal.SizeOf<MEMORYSTATUSEX>() };
        double ramTotal = 0, ramUsed = 0; int ram = 0;
        if (GlobalMemoryStatusEx(ref mem))
        {
            ramTotal = mem.TotalPhys / 1073741824.0;
            ramUsed = (mem.TotalPhys - mem.AvailPhys) / 1073741824.0;
            ram = (int)mem.MemoryLoad;
        }

        int disk = 0; double diskFree = 0;
        try
        {
            var d = new DriveInfo(Path.GetPathRoot(Environment.SystemDirectory) ?? "C:\\");
            diskFree = d.AvailableFreeSpace / 1073741824.0;
            disk = d.TotalSize > 0 ? (int)Math.Round(100.0 * (d.TotalSize - d.AvailableFreeSpace) / d.TotalSize) : 0;
        }
        catch { }

        long down = 0, up = 0;
        try
        {
            var (rx, tx) = NetBytes();
            long now = Environment.TickCount64;
            if (_lastTicks != 0 && now > _lastTicks)
            {
                double dt = (now - _lastTicks) / 1000.0;
                down = (long)Math.Max(0, (rx - _lastRx) / 1024.0 / dt);
                up = (long)Math.Max(0, (tx - _lastTx) / 1024.0 / dt);
            }
            _lastRx = rx; _lastTx = tx; _lastTicks = now;
        }
        catch { }

        Latest = new PcStatsSample(cpu, ram, disk, ramUsed, ramTotal, diskFree, down, up, Environment.TickCount64 / 60000);
    }

    public void Dispose() { _timer?.Dispose(); _timer = null; }
}
