using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Reflection;
using System.Text;

namespace NowPlayingHub;

public sealed class StatusServer : IDisposable
{
    readonly AppConfig _cfg;
    readonly Func<string> _json;
    readonly byte[] _html;
    TcpListener? _listener;
    CancellationTokenSource? _cts;
    bool _lan;

    public string? Error { get; private set; }
    public bool Running => _listener != null;

    public StatusServer(AppConfig cfg, Func<string> json)
    {
        _cfg = cfg; _json = json;
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("status.html");
        using var ms = new MemoryStream();
        s?.CopyTo(ms);
        _html = ms.Length > 0 ? ms.ToArray() : Encoding.UTF8.GetBytes("<h1>status.html introuvable</h1>");
    }

    public void Restart()
    {
        Stop();
        if (!_cfg.StatusEnabled) return;
        try
        {
            _lan = _cfg.StatusLan;
            var l = new TcpListener(_lan ? IPAddress.Any : IPAddress.Loopback, _cfg.StatusPort);
            l.Start();
            _listener = l;
            _cts = new CancellationTokenSource();
            Error = null;
            _ = AcceptLoop(l, _cts.Token);
            Log.Write($"page de statut : http://localhost:{_cfg.StatusPort}/ ({(_lan ? "réseau local" : "ce PC uniquement")})");
        }
        catch (Exception e)
        {
            _listener = null;
            Error = e is SocketException { SocketErrorCode: SocketError.AddressAlreadyInUse }
                ? $"Le port {_cfg.StatusPort} est déjà utilisé par un autre programme."
                : e.Message;
            Log.Write("page de statut : " + e.Message);
        }
    }

    void Stop()
    {
        try { _cts?.Cancel(); } catch { }
        try { _listener?.Stop(); } catch { }
        _listener = null;
    }

    public List<string> Urls()
    {
        var res = new List<string> { $"http://localhost:{_cfg.StatusPort}/" };
        if (_cfg.StatusLan)
        {
            try
            {
                foreach (var ni in NetworkInterface.GetAllNetworkInterfaces())
                {
                    if (ni.OperationalStatus != OperationalStatus.Up || ni.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
                    foreach (var ua in ni.GetIPProperties().UnicastAddresses)
                        if (ua.Address.AddressFamily == AddressFamily.InterNetwork && !ua.Address.ToString().StartsWith("169.254"))
                            res.Add($"http://{ua.Address}:{_cfg.StatusPort}/");
                }
            }
            catch (Exception e) { Log.Write("adresses réseau : " + e.Message); }
        }
        return res;
    }

    async Task AcceptLoop(TcpListener l, CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            TcpClient c;
            try { c = await l.AcceptTcpClientAsync(ct); }
            catch { break; }
            _ = Task.Run(() => HandleAsync(c, ct));
        }
    }

    async Task HandleAsync(TcpClient client, CancellationToken ct)
    {
        using (client)
        {
            try
            {
                using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
                cts.CancelAfter(TimeSpan.FromSeconds(5));
                var ns = client.GetStream();

                var buf = new byte[8192];
                int len = 0;
                while (len < buf.Length)
                {
                    int n = await ns.ReadAsync(buf.AsMemory(len), cts.Token);
                    if (n == 0) break;
                    len += n;
                    if (Encoding.ASCII.GetString(buf, Math.Max(0, len - n - 3), Math.Min(len, n + 3)).Contains("\r\n\r\n")) break;
                }
                var lines = Encoding.ASCII.GetString(buf, 0, len).Split("\r\n");
                var first = lines[0].Split(' ');
                if (first.Length < 2) { await Reply(ns, 400, "text/plain", "Bad request", cts.Token); return; }
                if (first[0] != "GET") { await Reply(ns, 405, "text/plain", "Method not allowed", cts.Token); return; }

                if (!_lan)
                {
                    var host = lines.FirstOrDefault(x => x.StartsWith("Host:", StringComparison.OrdinalIgnoreCase))?[5..].Trim() ?? "";
                    var h = host.StartsWith('[') ? host : host.Split(':')[0];
                    if (h != "localhost" && h != "127.0.0.1" && h != "[::1]") { await Reply(ns, 403, "text/plain", "Forbidden", cts.Token); return; }
                }

                var path = first[1].Split('?')[0];
                switch (path)
                {
                    case "/":
                    case "/status":
                    case "/index.html":
                        await Reply(ns, 200, "text/html; charset=utf-8", _html, cts.Token);
                        break;
                    case "/api/status.json":
                        await Reply(ns, 200, "application/json; charset=utf-8", Encoding.UTF8.GetBytes(_json()), cts.Token);
                        break;
                    default:
                        await Reply(ns, 404, "text/plain", "Not found", cts.Token);
                        break;
                }
            }
            catch (Exception e) when (e is OperationCanceledException or IOException or SocketException) { }
            catch (Exception e) { Log.Write("page de statut : " + e.Message); }
        }
    }

    static Task Reply(NetworkStream ns, int code, string type, string body, CancellationToken ct) =>
        Reply(ns, code, type, Encoding.UTF8.GetBytes(body), ct);

    static async Task Reply(NetworkStream ns, int code, string type, byte[] body, CancellationToken ct)
    {
        string reason = code switch { 200 => "OK", 400 => "Bad Request", 403 => "Forbidden", 404 => "Not Found", 405 => "Method Not Allowed", _ => "Error" };
        var head = $"HTTP/1.1 {code} {reason}\r\nContent-Type: {type}\r\nContent-Length: {body.Length}\r\n" +
                   "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nX-Frame-Options: DENY\r\n" +
                   "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'\r\n" +
                   "Connection: close\r\n\r\n";
        await ns.WriteAsync(Encoding.ASCII.GetBytes(head), ct);
        await ns.WriteAsync(body, ct);
        await ns.FlushAsync(ct);
    }

    public void Dispose() => Stop();
}
