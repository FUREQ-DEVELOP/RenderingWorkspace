using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;

namespace ATMERA.Studio;

internal sealed class NativeViewportHost(string outputDirectory, BackendOption backend,
    string shaderPath, bool forceWarp) : HwndHost {
    private const int ChildWindowStyle = 0x56000000; // WS_CHILD | WS_VISIBLE | clip siblings/children
    private NativeRendererSession? session;
    private nint childWindow;
    private uint width;
    private uint height;
    private bool firstFrameRendered;
    private bool paused;

    public ulong RenderedFrameCount { get; private set; }
    public string DeviceInfo => session?.DeviceInfo ?? "";
    public bool IsPaused {
        get => paused;
        set => paused = value;
    }

    public event Action? FrameRendered;
    public event Action<string>? Failed;

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern nint CreateWindowExW(int exStyle, string className, string windowName,
        int style, int x, int y, int width, int height, nint parent, nint menu,
        nint instance, nint parameter);
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DestroyWindow(nint window);
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool MoveWindow(nint window, int x, int y, int width, int height,
        [MarshalAs(UnmanagedType.Bool)] bool repaint);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern nint GetModuleHandleW(string? moduleName);

    protected override HandleRef BuildWindowCore(HandleRef parent) {
        width = PixelSize(ActualWidth);
        height = PixelSize(ActualHeight);
        childWindow = CreateWindowExW(0, "static", "ATMERA Viewport", ChildWindowStyle,
            0, 0, (int)width, (int)height, parent.Handle, 0, GetModuleHandleW(null), 0);
        if (childWindow == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            session = new NativeRendererSession(
                Path.Combine(outputDirectory, backend.LibraryName),
                shaderPath, childWindow, width, height, forceWarp);
            SizeChanged += OnSizeChanged;
            CompositionTarget.Rendering += OnRendering;
        } catch (Exception error) {
            Failed?.Invoke(error.Message);
        }
        return new HandleRef(this, childWindow);
    }

    protected override void DestroyWindowCore(HandleRef window) {
        CompositionTarget.Rendering -= OnRendering;
        SizeChanged -= OnSizeChanged;
        session?.Dispose();
        session = null;
        if (childWindow != 0) {
            DestroyWindow(childWindow);
            childWindow = 0;
        }
    }

    private void OnSizeChanged(object sender, SizeChangedEventArgs args) {
        if (childWindow == 0 || session is null) return;
        var newWidth = PixelSize(ActualWidth);
        var newHeight = PixelSize(ActualHeight);
        if (newWidth == width && newHeight == height) return;
        try {
            session.Resize(newWidth, newHeight);
            MoveWindow(childWindow, 0, 0, (int)newWidth, (int)newHeight, true);
            width = newWidth;
            height = newHeight;
            firstFrameRendered = false;
        } catch (Exception error) {
            CompositionTarget.Rendering -= OnRendering;
            Failed?.Invoke(error.Message);
        }
    }

    private void OnRendering(object? sender, EventArgs args) {
        if (paused) return;
        RenderOneFrame();
    }

    public bool Step() => paused && RenderOneFrame();

    private bool RenderOneFrame() {
        if (!IsVisible || session is null) return false;
        try {
            session.Render();
            ++RenderedFrameCount;
            if (!firstFrameRendered) {
                firstFrameRendered = true;
                FrameRendered?.Invoke();
            }
            return true;
        } catch (Exception error) {
            CompositionTarget.Rendering -= OnRendering;
            Failed?.Invoke(error.Message);
            return false;
        }
    }

    private static uint PixelSize(double size) => (uint)Math.Max(1, (int)Math.Round(size));
}
