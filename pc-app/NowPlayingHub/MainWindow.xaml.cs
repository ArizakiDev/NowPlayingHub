using System.ComponentModel;
using System.Windows;
using System.Windows.Media.Animation;

namespace NowPlayingHub;

public partial class MainWindow : Window
{
    ShellViewModel? _vm;

    public bool Quitting { get; set; }
    public Func<bool>? HideOnClose { get; set; }
    public Action? FirstHide { get; set; }
    bool _hintShown;

    public MainWindow()
    {
        InitializeComponent();
        DataContextChanged += (s, e) =>
        {
            if (_vm != null) _vm.PageChanged -= AnimatePage;
            _vm = e.NewValue as ShellViewModel;
            if (_vm != null) _vm.PageChanged += AnimatePage;
        };
        IsVisibleChanged += (s, e) => { if (_vm != null) _vm.WindowVisible = IsVisible; };
        ThemeManager.Attach(this);
    }

    void AnimatePage()
    {
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        PageShift.BeginAnimation(System.Windows.Media.TranslateTransform.YProperty,
            new DoubleAnimation(28, 0, TimeSpan.FromMilliseconds(300)) { EasingFunction = ease });
        PageHost.BeginAnimation(OpacityProperty,
            new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(240)));
    }

    protected override void OnClosing(CancelEventArgs e)
    {
        if (!Quitting && HideOnClose?.Invoke() == true)
        {
            e.Cancel = true;
            Hide();
            if (!_hintShown) { _hintShown = true; FirstHide?.Invoke(); }
            return;
        }
        base.OnClosing(e);
    }
}
