using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Input;

namespace NowPlayingHub;

public abstract class ObservableObject : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected void Raise([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    protected bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        Raise(name);
        return true;
    }
}

public sealed class RelayCommand : ICommand
{
    readonly Action<object?> _run;
    readonly Func<object?, bool>? _can;

    public RelayCommand(Action<object?> run, Func<object?, bool>? can = null) { _run = run; _can = can; }

    public event EventHandler? CanExecuteChanged
    {
        add => CommandManager.RequerySuggested += value;
        remove => CommandManager.RequerySuggested -= value;
    }
    public bool CanExecute(object? parameter) => _can?.Invoke(parameter) ?? true;
    public void Execute(object? parameter) => _run(parameter);
}

public sealed class AsyncCommand : ICommand
{
    readonly Func<object?, Task> _run;
    readonly Func<bool>? _can;
    bool _busy;

    public AsyncCommand(Func<object?, Task> run, Func<bool>? can = null) { _run = run; _can = can; }

    public event EventHandler? CanExecuteChanged
    {
        add => CommandManager.RequerySuggested += value;
        remove => CommandManager.RequerySuggested -= value;
    }
    public bool CanExecute(object? parameter) => !_busy && (_can?.Invoke() ?? true);

    public async void Execute(object? parameter)
    {
        if (_busy) return;
        _busy = true;
        CommandManager.InvalidateRequerySuggested();
        try { await _run(parameter); }
        catch (Exception e) { Log.Write("commande : " + e); }
        finally { _busy = false; CommandManager.InvalidateRequerySuggested(); }
    }
}
