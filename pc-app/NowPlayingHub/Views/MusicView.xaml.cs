using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace NowPlayingHub;

public partial class MusicView : UserControl
{
    public MusicView() { InitializeComponent(); }

    void OnLoaded(object s, RoutedEventArgs e) { if (DataContext is MusicViewModel vm) Mxm.Password = vm.MusixmatchToken; }
    void OnMxmChanged(object s, RoutedEventArgs e) { if (DataContext is MusicViewModel vm) vm.MusixmatchToken = Mxm.Password; }
}
