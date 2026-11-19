using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace NowPlayingHub;

public partial class SystemView : UserControl
{
    public SystemView() { InitializeComponent(); }

    void OnLoaded(object s, RoutedEventArgs e) { if (DataContext is SystemViewModel vm) Tok.Password = vm.EspToken; }
    void OnTokChanged(object s, RoutedEventArgs e) { if (DataContext is SystemViewModel vm) vm.EspToken = Tok.Password; }
}
