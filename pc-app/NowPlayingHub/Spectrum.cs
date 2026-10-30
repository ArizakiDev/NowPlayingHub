using System.Net;
using System.Net.Sockets;
using NAudio.Wave;

namespace NowPlayingHub;

/// <summary>Transforme un flux audio mono en N bandes de spectre (0-255) : FFT 2048 points, bandes en echelle log,
/// gain automatique (le spectre reste lisible quel que soit le volume) et lissage montee rapide / descente lente.</summary>
public sealed class SpectrumAnalyzer
{
    const int FftSize = 2048;
    readonly int _bands;
    readonly float[] _ring = new float[FftSize];
    int _pos;
    readonly float[] _window = new float[FftSize];
    readonly float[] _re = new float[FftSize], _im = new float[FftSize];
    readonly float[] _smooth;
    float _agc = 0.5f;

    public SpectrumAnalyzer(int bands)
    {
        _bands = bands;
        _smooth = new float[bands];
        for (int i = 0; i < FftSize; i++)
            _window[i] = (float)(0.5 - 0.5 * Math.Cos(2 * Math.PI * i / (FftSize - 1)));   // Hann
    }

    public int Bands => _bands;

    public void Push(ReadOnlySpan<float> mono)
    {
        foreach (var v in mono) { _ring[_pos] = v; _pos = (_pos + 1) % FftSize; }
    }

    /// <summary>Calcule les bandes ; avec fresh = false (silence), les barres retombent doucement.</summary>
    public byte[] Compute(int sampleRate, bool fresh)
    {
        var raw = new float[_bands];
        if (fresh)
        {
            for (int i = 0; i < FftSize; i++)
            {
                _re[i] = _ring[(_pos + i) % FftSize] * _window[i];
                _im[i] = 0;
            }
            Fft(_re, _im);

            double binHz = (double)sampleRate / FftSize;
            double f0 = 45, f1 = Math.Min(16000, sampleRate / 2.0 - 1);
            for (int b = 0; b < _bands; b++)
            {
                double lo = f0 * Math.Pow(f1 / f0, (double)b / _bands);
                double hi = f0 * Math.Pow(f1 / f0, (double)(b + 1) / _bands);
                int k0 = Math.Max(1, (int)Math.Floor(lo / binHz));
                int k1 = Math.Max(k0, Math.Min(FftSize / 2 - 1, (int)Math.Ceiling(hi / binHz) - 1));
                float peak = 0;
                for (int k = k0; k <= k1; k++)
                {
                    float m = MathF.Sqrt(_re[k] * _re[k] + _im[k] * _im[k]) * 4f / FftSize;   // ~amplitude (0..1)
                    if (m > peak) peak = m;
                }
                float db = 20f * MathF.Log10(peak + 1e-6f) + b * 0.5f;                       // petite compensation des aigus
                raw[b] = Math.Clamp((db + 65f) / 50f, 0f, 1f);
            }

            // gain automatique lent
            float top = raw.Max();
            _agc = top > _agc ? top : _agc * 0.998f + top * 0.002f;
            float g = 1f / Math.Max(0.3f, _agc);
            for (int b = 0; b < _bands; b++) raw[b] = Math.Min(1f, raw[b] * g);
        }

        var res = new byte[_bands];
        for (int b = 0; b < _bands; b++)
        {
            _smooth[b] = raw[b] > _smooth[b] ? raw[b] : _smooth[b] * 0.82f + raw[b] * 0.18f;
            res[b] = (byte)Math.Clamp((int)(_smooth[b] * 255f), 0, 255);
        }
        return res;
    }

    // FFT iterative radix-2 (taille = puissance de 2)
    static void Fft(float[] re, float[] im)
    {
        int n = re.Length;
        for (int i = 1, j = 0; i < n; i++)
        {
            int bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { (re[i], re[j]) = (re[j], re[i]); (im[i], im[j]) = (im[j], im[i]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            double ang = -2 * Math.PI / len;
            float wr = (float)Math.Cos(ang), wi = (float)Math.Sin(ang);
            for (int i = 0; i < n; i += len)
            {
                float cr = 1, ci = 0;
                for (int k = 0; k < len / 2; k++)
                {
                    int a = i + k, b = i + k + len / 2;
                    float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr; im[a] += ti;
                    float ncr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr; cr = ncr;
                }
            }
        }
    }
}

/// <summary>Capture le son du PC (WASAPI loopback) et l'envoie en UDP (port 8124) a la carte pour le visualiseur et la LED.
/// La capture ne tourne que quand la carte le demande (ecran visualiseur ou LED "basses" ouverts).</summary>
public sealed class SpectrumService : IDisposable
{
    const int Port = 8124, Bands = 24;

    readonly AppConfig _cfg;
    readonly SpectrumAnalyzer _an = new(Bands);
    readonly UdpClient _udp = new();
    readonly object _lock = new();

    WasapiLoopbackCapture? _cap;
    System.Threading.Timer? _timer;
    int _channels = 2, _sampleRate = 48000, _bits = 32;
    long _lastData, _lastWanted;
    IPEndPoint? _target;
    string _targetKey = "";
    DateTime _targetAt = DateTime.MinValue;

    public volatile bool Running;
    public long Packets;
    public string Error = "";

    public SpectrumService(AppConfig cfg) { _cfg = cfg; }

    IPEndPoint? Target(string espIp)
    {
        string host = string.IsNullOrWhiteSpace(espIp) ? _cfg.EspHost : espIp;
        if (_target != null && _targetKey == host && (DateTime.UtcNow - _targetAt).TotalSeconds < 60) return _target;
        try
        {
            var ip = IPAddress.TryParse(host, out var p) ? p
                   : Dns.GetHostAddresses(host).FirstOrDefault(a => a.AddressFamily == AddressFamily.InterNetwork);
            if (ip == null) return null;
            _target = new IPEndPoint(ip, Port); _targetKey = host; _targetAt = DateTime.UtcNow;
            return _target;
        }
        catch (Exception e) { Error = "resolution: " + e.Message; return null; }
    }

    /// <summary>Appele a chaque echange avec la carte : wanted = la carte affiche le spectre.</summary>
    public void Request(bool wanted, string espIp)
    {
        if (wanted)
        {
            _lastWanted = Environment.TickCount64;
            _espIp = espIp;
            if (!Running) Start();
        }
        else if (Running && Environment.TickCount64 - _lastWanted > 4000) Stop();   // petit delai : evite les va-et-vient
    }
    string _espIp = "";

    void Start()
    {
        lock (_lock)
        {
            if (Running) return;
            try
            {
                var cap = new WasapiLoopbackCapture();   // peripherique de sortie par defaut
                var wf = cap.WaveFormat;
                _channels = Math.Max(1, wf.Channels); _sampleRate = wf.SampleRate; _bits = wf.BitsPerSample;
                cap.DataAvailable += OnData;
                cap.StartRecording();
                _cap = cap;
                _lastData = 0;
                _timer = new System.Threading.Timer(_ => Tick(), null, 0, 33);   // ~30 images/s
                Running = true; Error = "";
                Log.Write($"visualiseur : capture audio demarree ({_sampleRate} Hz, {_channels} canaux, {_bits} bits)");
            }
            catch (Exception e)
            {
                Error = e.Message;
                Log.Write("visualiseur : " + e.Message);
                StopInternal();
            }
        }
    }

    public void Stop() { lock (_lock) StopInternal(); }

    void StopInternal()
    {
        Running = false;
        try { _timer?.Dispose(); } catch { }
        _timer = null;
        try { if (_cap != null) { _cap.DataAvailable -= OnData; _cap.StopRecording(); _cap.Dispose(); } } catch { }
        _cap = null;
    }

    void OnData(object? sender, WaveInEventArgs e)
    {
        int bytes = _bits / 8;
        if (bytes != 4 && bytes != 2) return;                        // 32 bits flottants (cas normal) ou 16 bits PCM
        int frames = e.BytesRecorded / (bytes * _channels);
        if (frames <= 0) return;
        var mono = new float[frames];
        for (int f = 0; f < frames; f++)
        {
            float sum = 0;
            for (int c = 0; c < _channels; c++)
            {
                int ofs = (f * _channels + c) * bytes;
                sum += bytes == 4 ? BitConverter.ToSingle(e.Buffer, ofs) : BitConverter.ToInt16(e.Buffer, ofs) / 32768f;
            }
            mono[f] = sum / _channels;
        }
        lock (_an) _an.Push(mono);
        Interlocked.Exchange(ref _lastData, Environment.TickCount64);
    }

    void Tick()
    {
        if (!Running) return;
        try
        {
            bool fresh = Environment.TickCount64 - Interlocked.Read(ref _lastData) < 300;   // le loopback n'emet rien en silence
            byte[] bands;
            lock (_an) bands = _an.Compute(_sampleRate, fresh);

            var target = Target(_espIp);
            if (target == null) return;
            var pkt = new byte[3 + bands.Length];
            pkt[0] = (byte)'V'; pkt[1] = (byte)'Z'; pkt[2] = (byte)bands.Length;
            Buffer.BlockCopy(bands, 0, pkt, 3, bands.Length);
            _udp.Send(pkt, pkt.Length, target);
            Interlocked.Increment(ref Packets);
        }
        catch (Exception e) { Error = e.Message; }
    }

    public void Dispose() { Stop(); _udp.Dispose(); }
}
