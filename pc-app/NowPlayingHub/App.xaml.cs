using System.Windows;
using System.Windows.Threading;

namespace NowPlayingHub;

public partial class App : Application
{
    Mutex? _mutex;
    EventWaitHandle? _showEvent;
    AppServices? _svc;
    TrayIcon? _tray;
    MainWindow? _win;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        _mutex = new Mutex(true, "NowPlayingHub.Singleton", out bool first);
        _showEvent = new EventWaitHandle(false, EventResetMode.AutoReset, "NowPlayingHub.Show");
        if (!first)
        {
            _showEvent.Set();
            Shutdown();
            return;
        }

        ShutdownMode = ShutdownMode.OnExplicitShutdown;
        AppDomain.CurrentDomain.UnhandledException += (s, a) => Log.Write("FATAL " + a.ExceptionObject);
        TaskScheduler.UnobservedTaskException += (s, a) => { Log.Write("TASK " + a.Exception); a.SetObserved(); };
        DispatcherUnhandledException += (s, a) => { Log.Write("UI " + a.Exception); a.Handled = true; };

        ThemeManager.Apply(ThemeManager.SystemIsDark());
        _svc = new AppServices();
        var shell = new ShellViewModel(_svc);
        _win = new MainWindow { DataContext = shell };
        _win.HideOnClose = () => _svc.Cfg.CloseToTray;

        _tray = new TrayIcon(_svc, ShowMain, Quit);
        _win.FirstHide = () => _tray.Hint("Now Playing Hub reste actif dans la zone de notification.");

        var th = new Thread(() =>
        {
            while (true)
            {
                _showEvent.WaitOne();
                Dispatcher.BeginInvoke(new Action(ShowMain));
            }
        }) { IsBackground = true, Name = "NPH-show" };
        th.Start();

        _svc.Start();
        if (!e.Args.Contains("--tray")) ShowMain();
    }

    void ShowMain()
    {
        if (_win == null) return;
        if (!_win.IsVisible) _win.Show();
        if (_win.WindowState == WindowState.Minimized) _win.WindowState = WindowState.Normal;
        _win.Activate();
    }

    void Quit()
    {
        if (_win != null) { _win.Quitting = true; _win.Close(); }
        Shutdown();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _tray?.Dispose();
        _svc?.Dispose();
        base.OnExit(e);
    }
}
