using System.Windows;

namespace NowPlayingHub;

public static class Dialogs
{
    public static bool Confirm(string text) =>
        MessageBox.Show(Application.Current.MainWindow, text, "Now Playing Hub",
                        MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes;

    public static void Info(string text) =>
        MessageBox.Show(Application.Current.MainWindow, text, "Now Playing Hub",
                        MessageBoxButton.OK, MessageBoxImage.Information);
}
