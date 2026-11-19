using System.Windows.Controls;
using System.Windows.Input;

namespace NowPlayingHub;

public partial class HomeView : UserControl
{
    public HomeView() { InitializeComponent(); }

    void OnBarClick(object sender, MouseButtonEventArgs e)
    {
        if (DataContext is HomeViewModel vm && Bar.ActualWidth > 0)
            vm.SeekFraction(e.GetPosition(Bar).X / Bar.ActualWidth);
    }
}
