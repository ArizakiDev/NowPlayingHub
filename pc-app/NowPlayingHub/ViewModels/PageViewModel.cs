using System.Runtime.CompilerServices;

namespace NowPlayingHub;

public abstract class PageViewModel : ObservableObject
{
    public abstract string Title { get; }
    public virtual string Glyph => "\uE713";
    public virtual bool HasBadge => false;
    public virtual void OnShown() { }
    public virtual void OnHidden() { }
    public virtual void OnTick() { }

    protected static async void Fire(Func<Task> work, string what)
    {
        try { await work(); }
        catch (Exception e) { Log.Write(what + " : " + e.Message); }
    }
}

public abstract class SettingsPage : PageViewModel
{
    protected readonly AppServices S;
    protected SettingsPage(AppServices s) { S = s; }

    protected AppConfig C => S.Cfg;
    protected EspSettings E => S.Cfg.Esp;

    protected bool Edit<T>(T current, T value, Action<T> write, Push push = Push.Card, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(current, value)) return false;
        write(value);
        Raise(name);
        S.Settings.Changed(push);
        return true;
    }
}
