using System.Windows;

namespace Dtrek.App;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        try
        {
            var window = new MainWindow();
            MainWindow = window;
            window.Show();
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                "DTREK could not start.\n\n"
                + ex.ToString()
                + "\n\nBuild the native core first (scripts\\build.ps1), "
                + "then make sure dtrek_core.dll sits beside Dtrek.App.exe.",
                "DTREK - startup failure",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            Shutdown(1);
        }
    }
}
