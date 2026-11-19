using NAudio.CoreAudioApi;

namespace NowPlayingHub;

public static class Volume
{
    static (int Pct, bool Mute)? _cache;
    static long _cacheAt;

    static bool With(Action<AudioEndpointVolume> act)
    {
        try
        {
            using var en = new MMDeviceEnumerator();
            using var dev = en.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
            act(dev.AudioEndpointVolume);
            _cacheAt = 0;
            return true;
        }
        catch (Exception e) { Log.Write("volume : " + e.Message); return false; }
    }

    public static bool Step(float delta) => With(v =>
    {
        v.MasterVolumeLevelScalar = Math.Clamp(v.MasterVolumeLevelScalar + delta, 0f, 1f);
        if (delta > 0 && v.Mute) v.Mute = false;
    });

    public static bool ToggleMute() => With(v => v.Mute = !v.Mute);

    public static (int Pct, bool Mute)? Read()
    {
        long now = Environment.TickCount64;
        if (_cacheAt != 0 && now - _cacheAt < 500) return _cache;
        try
        {
            using var en = new MMDeviceEnumerator();
            using var dev = en.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
            var v = dev.AudioEndpointVolume;
            _cache = ((int)Math.Round(v.MasterVolumeLevelScalar * 100), v.Mute);
        }
        catch { _cache = null; }
        _cacheAt = now;
        return _cache;
    }
}
