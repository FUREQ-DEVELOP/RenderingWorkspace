using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Microsoft.Win32;

namespace ATMERA.Studio;

internal sealed record BackendOption(string Name, string LibraryName);

public partial class MainWindow : Window {
    private static readonly BackendOption[] backends = [
        new("DirectX 11", "ATMERA.D3D11.dll"),
        new("DirectX 12", "ATMERA.D3D12.dll")
    ];

    private NativeViewportHost? viewport;
    private string? shaderPath;
    private string? smokeShaderPath;
    private bool paused;
    private readonly bool smokeTest;
    private int smokeStage;

    public MainWindow() {
        InitializeComponent();
        smokeTest = Environment.GetCommandLineArgs().Contains("--smoke-test");
        BackendSelector.ItemsSource = backends;
        BackendSelector.SelectedIndex = 0;
        Loaded += (_, _) => UpdateViewport();
        Closing += (_, _) => {
            ViewportContent.Content = null;
            viewport?.Dispose();
            if (smokeShaderPath is not null) File.Delete(smokeShaderPath);
        };
    }

    private void OnBackendChanged(object sender, SelectionChangedEventArgs args) {
        if (IsLoaded) UpdateViewport();
    }

    private void OnForceWarpChanged(object sender, RoutedEventArgs args) {
        if (IsLoaded) UpdateViewport();
    }

    private void OnOpenShader(object sender, RoutedEventArgs args) {
        var dialog = new OpenFileDialog {
            Title = "Open HLSL shader",
            Filter = "HLSL shader (*.hlsl)|*.hlsl",
            CheckFileExists = true
        };
        if (dialog.ShowDialog(this) == true) {
            shaderPath = dialog.FileName;
            UpdateViewport();
        }
    }

    private void OnReloadShader(object sender, RoutedEventArgs args) => UpdateViewport();

    private void OnUseBundledShader(object sender, RoutedEventArgs args) {
        shaderPath = null;
        UpdateViewport();
    }

    private void OnTogglePause(object sender, RoutedEventArgs args) {
        paused = !paused;
        if (viewport is not null) viewport.IsPaused = paused;
        PauseButton.Content = paused ? "Resume" : "Pause";
        StepButton.IsEnabled = paused && viewport is not null;
        SetStatus(paused ? "Viewport paused." : "Viewport running.");
    }

    private void OnStepFrame(object sender, RoutedEventArgs args) {
        if (paused && viewport?.Step() == true) SetStatus("Advanced one frame.");
    }

    private void UpdateViewport() {
        ViewportContent.Content = null;
        viewport?.Dispose();
        viewport = null;
        paused = false;
        PauseButton.Content = "Pause";
        PauseButton.IsEnabled = false;
        StepButton.IsEnabled = false;
        DeviceInfoLabel.Text = "";

        var backend = BackendSelector.SelectedItem as BackendOption;
        var directory = ResolveOutputDirectory();
        var source = shaderPath ?? (directory is null ? null : Path.Combine(directory, "Triangle.hlsl"));
        ShaderLabel.Text = source is null ? "No shader" : Path.GetFileName(source);
        ShaderLabel.ToolTip = source;
        if (backend is null || directory is null || source is null ||
            !File.Exists(Path.Combine(directory, backend.LibraryName)) || !File.Exists(source)) {
            SetStatus("Build ATMERA.sln or choose an existing HLSL file.");
            if (smokeTest) Dispatcher.BeginInvoke(() => Application.Current.Shutdown(1));
            return;
        }

        viewport = new NativeViewportHost(directory, backend, source, ForceWarpCheckBox.IsChecked == true);
        viewport.FrameRendered += OnFrameRendered;
        viewport.Failed += OnViewportFailed;
        ViewportContent.Content = viewport;
    }

    private void OnViewportFailed(string error) {
        PauseButton.IsEnabled = false;
        StepButton.IsEnabled = false;
        SetStatus(error);
        if (!smokeTest) return;
        if ((smokeStage == 2 || smokeStage == 5) && smokeShaderPath is not null &&
            error.Contains("error", StringComparison.OrdinalIgnoreCase)) {
            var directory = ResolveOutputDirectory();
            if (directory is not null) {
                File.Copy(Path.Combine(directory, "Triangle.hlsl"), smokeShaderPath, true);
                smokeStage = smokeStage == 2 ? 3 : 6;
                Dispatcher.BeginInvoke(() => UpdateViewport());
                return;
            }
        }
        Dispatcher.BeginInvoke(() => Application.Current.Shutdown(1));
    }

    private void OnFrameRendered() {
        DeviceInfoLabel.Text = viewport?.DeviceInfo ?? "";
        PauseButton.IsEnabled = true;
        SetStatus($"{(BackendSelector.SelectedItem as BackendOption)?.Name} ready.");
        if (!smokeTest) return;
        if (DeviceInfoLabel.Text.Length == 0) { Application.Current.Shutdown(1); return; }

        if (smokeStage == 0) {
            smokeStage = 1;
            var host = viewport!;
            OnTogglePause(this, new RoutedEventArgs());
            var count = host.RenderedFrameCount;
            var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
            timer.Tick += (_, _) => {
                timer.Stop();
                if (!ReferenceEquals(viewport, host) || host.RenderedFrameCount != count) {
                    Application.Current.Shutdown(1);
                    return;
                }
                OnStepFrame(this, new RoutedEventArgs());
                if (host.RenderedFrameCount != count + 1) {
                    Application.Current.Shutdown(1);
                    return;
                }
                smokeShaderPath = Path.Combine(Path.GetTempPath(),
                    "ATMERA-Studio-" + Guid.NewGuid().ToString("N") + ".hlsl");
                File.Copy(Path.Combine(ResolveOutputDirectory()!, "Triangle.hlsl"), smokeShaderPath);
                shaderPath = smokeShaderPath;
                UpdateViewport();
            };
            timer.Start();
        } else if (smokeStage == 1) {
            smokeStage = 2;
            File.WriteAllText(smokeShaderPath!, "not valid HLSL");
            Dispatcher.BeginInvoke(() => UpdateViewport());
        } else if (smokeStage == 3) {
            smokeStage = 4;
            Dispatcher.BeginInvoke(() => BackendSelector.SelectedIndex = 1);
        } else if (smokeStage == 4) {
            smokeStage = 5;
            File.WriteAllText(smokeShaderPath!, "not valid HLSL");
            Dispatcher.BeginInvoke(() => UpdateViewport());
        } else if (smokeStage == 6) {
            smokeStage = 7;
            Dispatcher.BeginInvoke(() => OnUseBundledShader(this, new RoutedEventArgs()));
        } else if (smokeStage == 7) {
            if (shaderPath is not null) { Application.Current.Shutdown(1); return; }
            smokeStage = 8;
            Dispatcher.BeginInvoke(() => ForceWarpCheckBox.IsChecked = true);
        } else if (smokeStage == 8) {
            if (!DeviceInfoLabel.Text.EndsWith("WARP", StringComparison.Ordinal)) {
                Application.Current.Shutdown(1);
                return;
            }
            smokeStage = 9;
            Dispatcher.BeginInvoke(() => BackendSelector.SelectedIndex = 0);
        } else if (smokeStage == 9) {
            if (!DeviceInfoLabel.Text.StartsWith("D3D11", StringComparison.Ordinal) ||
                !DeviceInfoLabel.Text.EndsWith("WARP", StringComparison.Ordinal)) {
                Application.Current.Shutdown(1);
                return;
            }
            smokeStage = 10;
            Dispatcher.BeginInvoke(() => Width += 100);
        } else if (smokeStage == 10) {
            Dispatcher.BeginInvoke(() => Application.Current.Shutdown(0));
        }
    }

    private void SetStatus(string message) => StatusText.Text = message;

    private static string? ResolveOutputDirectory() {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        string? configuration = null;
        while (directory is not null) {
            if (configuration is null && (directory.Name is "Debug" or "Release"))
                configuration = directory.Name;
            if (File.Exists(Path.Combine(directory.FullName, "ATMERA.sln")))
                return Path.Combine(directory.FullName, "out", "vs2022", "x64",
                    configuration ?? "Debug");
            directory = directory.Parent;
        }
        return null;
    }
}
