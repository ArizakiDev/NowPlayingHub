using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using Microsoft.Win32;

namespace NowPlayingHub;

public static class ThemeManager
{
    [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
    [DllImport("dwmapi.dll")] static extern int DwmExtendFrameIntoClientArea(IntPtr hwnd, ref Margins m);

    [StructLayout(LayoutKind.Sequential)]
    struct Margins { public int Left, Right, Top, Bottom; }

    const int DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19;
    const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
    const int DWMWA_SYSTEMBACKDROP_TYPE = 38;
    const int DWMWA_MICA_EFFECT = 1029;

    public static bool IsDark { get; private set; } = true;
    public static bool MicaActive { get; private set; }

    static Window? _window;

    public static bool SystemIsDark()
    {
        try
        {
            using var k = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            return !(k?.GetValue("AppsUseLightTheme") is int v && v == 1);
        }
        catch { return true; }
    }

    static void Set(string key, string hex) =>
        Application.Current.Resources[key] = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex));

    public static void Apply(bool dark)
    {
        IsDark = dark;
        if (dark)
        {
            Set("BgBrush", "#202020");            Set("ContentBrush", "#A0282828");
            Set("SurfaceBrush", "#0DFFFFFF");     Set("Surface2Brush", "#0FFFFFFF");
            Set("ControlHoverBrush", "#15FFFFFF"); Set("ControlPressBrush", "#08FFFFFF");
            Set("SubtleHoverBrush", "#0FFFFFFF"); Set("SubtleSelectBrush", "#18FFFFFF");
            Set("LineBrush", "#18FFFFFF");        Set("TrackBrush", "#8BFFFFFF");
            Set("RailBrush", "#33FFFFFF");        Set("ThumbBrush", "#FF454545");
            Set("TextBrush", "#FFFFFF");          Set("DimBrush", "#C5FFFFFF");
            Set("OkBrush", "#6CCB5F");            Set("BadBrush", "#FF99A4");   Set("WarnBrush", "#FCE100");
        }
        else
        {
            Set("BgBrush", "#F3F3F3");            Set("ContentBrush", "#B3FFFFFF");
            Set("SurfaceBrush", "#B3FFFFFF");     Set("Surface2Brush", "#B3FFFFFF");
            Set("ControlHoverBrush", "#80F9F9F9"); Set("ControlPressBrush", "#4DF9F9F9");
            Set("SubtleHoverBrush", "#0A000000"); Set("SubtleSelectBrush", "#12000000");
            Set("LineBrush", "#14000000");        Set("TrackBrush", "#9C000000");
            Set("RailBrush", "#29000000");        Set("ThumbBrush", "#FFFFFFFF");
            Set("TextBrush", "#E4000000");        Set("DimBrush", "#9E000000");
            Set("OkBrush", "#0F7B0F");            Set("BadBrush", "#C42B1C");   Set("WarnBrush", "#9D5D00");
        }
        ApplyToWindow();
    }

    public static void Attach(Window w)
    {
        _window = w;
        w.SourceInitialized += (s, e) => ApplyToWindow(initial: true);
        SystemEvents.UserPreferenceChanged += (s, e) =>
        {
            if (e.Category != UserPreferenceCategory.General) return;
            w.Dispatcher.BeginInvoke(new Action(() => { var d = SystemIsDark(); if (d != IsDark) Apply(d); }));
        };
    }

    static void ApplyToWindow(bool initial = false)
    {
        var w = _window;
        if (w == null) return;
        var h = new WindowInteropHelper(w).Handle;
        if (h == IntPtr.Zero) return;

        int dark = IsDark ? 1 : 0;
        DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, ref dark, sizeof(int));
        DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, ref dark, sizeof(int));

        if (!initial && MicaActive) return;
        if (!initial && !MicaActive) { w.SetResourceReference(Window.BackgroundProperty, "BgBrush"); return; }

        bool mica = false;
        int build = Environment.OSVersion.Version.Build;
        try
        {
            if (build >= 22621)
            {
                int type = 2;
                mica = DwmSetWindowAttribute(h, DWMWA_SYSTEMBACKDROP_TYPE, ref type, sizeof(int)) == 0;
            }
            else if (build >= 22000)
            {
                int on = 1;
                mica = DwmSetWindowAttribute(h, DWMWA_MICA_EFFECT, ref on, sizeof(int)) == 0;
            }
            if (mica)
            {
                var m = new Margins { Left = -1, Right = -1, Top = -1, Bottom = -1 };
                mica = DwmExtendFrameIntoClientArea(h, ref m) == 0;
                var src = HwndSource.FromHwnd(h);
                if (src?.CompositionTarget != null) src.CompositionTarget.BackgroundColor = Colors.Transparent;
            }
        }
        catch (Exception e) { Log.Write("Mica : " + e.Message); mica = false; }

        MicaActive = mica;
        if (mica) w.Background = Brushes.Transparent;
        else w.SetResourceReference(Window.BackgroundProperty, "BgBrush");
    }
}
