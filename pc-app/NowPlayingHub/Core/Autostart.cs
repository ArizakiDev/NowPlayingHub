using Microsoft.Win32;

namespace NowPlayingHub;

public static class Autostart
{
    const string KeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    const string ValueName = "NowPlayingHub";

    public static bool Get()
    {
        try { using var k = Registry.CurrentUser.OpenSubKey(KeyPath); return k?.GetValue(ValueName) != null; }
        catch { return false; }
    }

    public static void Set(bool on)
    {
        try
        {
            using var k = Registry.CurrentUser.OpenSubKey(KeyPath, true);
            if (k == null) return;
            if (on) k.SetValue(ValueName, $"\"{Environment.ProcessPath}\" --tray");
            else k.DeleteValue(ValueName, false);
        }
        catch (Exception e) { Log.Write("démarrage auto : " + e.Message); }
    }
}
