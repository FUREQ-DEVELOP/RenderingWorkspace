using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace ATMERA.Studio;

internal sealed class NativeRendererSession : IDisposable {
    [StructLayout(LayoutKind.Sequential)]
    private struct RenderDesc {
        public uint StructSize;
        public nint WindowHandle;
        public uint Width;
        public uint Height;
        public uint UseWarp;
        public nint HlslSource;
        public uint HlslSourceSize;
        public uint CaptureCenterPixel;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RenderApiPrefix {
        public uint StructSize;
        public uint AbiVersion;
        public nint BackendId;
        public nint Create;
        public nint GetCaps;
        public nint Resize;
        public nint RenderFrame;
        public nint GetCenterPixel;
        public nint Destroy;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RenderCaps {
        public uint StructSize;
        public uint ApiMajor;
        public uint FeatureLevel;
        public uint ShaderModel;
        public uint IsSoftware;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint GetApiDelegate(uint version, out nint api);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint GetLastShaderErrorDelegate(uint version, nint buffer,
        uint capacity, out uint requiredBytes);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint CreateDelegate(ref RenderDesc desc, out nint renderer);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint GetCapsDelegate(nint renderer, ref RenderCaps caps);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint ResizeDelegate(nint renderer, uint width, uint height);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint RenderFrameDelegate(nint renderer);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void DestroyDelegate(nint renderer);

    private nint library;
    private nint renderer;
    private ResizeDelegate? resize;
    private RenderFrameDelegate? renderFrame;
    private DestroyDelegate? destroy;
    private GetLastShaderErrorDelegate? getLastShaderError;
    public string DeviceInfo { get; private set; } = "";

    public NativeRendererSession(string libraryPath, string shaderPath, nint window,
        uint width, uint height, bool forceWarp = false) {
        try {
            library = NativeLibrary.Load(libraryPath);
            if (NativeLibrary.TryGetExport(library, "atmera_get_last_shader_error", out var diagnosticExport))
                getLastShaderError = Marshal.GetDelegateForFunctionPointer<GetLastShaderErrorDelegate>(diagnosticExport);
            var getApi = Marshal.GetDelegateForFunctionPointer<GetApiDelegate>(
                NativeLibrary.GetExport(library, "atmera_get_render_api"));
            var status = getApi(1, out var apiAddress);
            if (status != 0 || apiAddress == 0)
                throw new InvalidOperationException("Rendering ABI version mismatch.");
            var api = Marshal.PtrToStructure<RenderApiPrefix>(apiAddress);
            if (api.StructSize < Marshal.SizeOf<RenderApiPrefix>() || api.AbiVersion != 1 ||
                api.Create == 0 || api.GetCaps == 0 || api.Resize == 0 ||
                api.RenderFrame == 0 || api.Destroy == 0)
                throw new InvalidOperationException("Rendering API table is incomplete.");
            var create = Marshal.GetDelegateForFunctionPointer<CreateDelegate>(api.Create);
            var getCaps = Marshal.GetDelegateForFunctionPointer<GetCapsDelegate>(api.GetCaps);
            resize = Marshal.GetDelegateForFunctionPointer<ResizeDelegate>(api.Resize);
            renderFrame = Marshal.GetDelegateForFunctionPointer<RenderFrameDelegate>(api.RenderFrame);
            destroy = Marshal.GetDelegateForFunctionPointer<DestroyDelegate>(api.Destroy);
            var shader = File.ReadAllBytes(shaderPath);
            var shaderAddress = Marshal.AllocHGlobal(shader.Length);
            try {
                Marshal.Copy(shader, 0, shaderAddress, shader.Length);
                var desc = new RenderDesc {
                    StructSize = (uint)Marshal.SizeOf<RenderDesc>(),
                    WindowHandle = window,
                    Width = width,
                    Height = height,
                    UseWarp = forceWarp ? 1u : 0u,
                    HlslSource = shaderAddress,
                    HlslSourceSize = (uint)shader.Length
                };
                status = create(ref desc, out renderer);
                if (status != 0 && !forceWarp) {
                    desc.UseWarp = 1;
                    status = create(ref desc, out renderer);
                }
                if (status != 0 || renderer == 0)
                    throw new InvalidOperationException($"Renderer creation failed: 0x{status:X8}." +
                        ReadShaderError());
                var caps = new RenderCaps { StructSize = (uint)Marshal.SizeOf<RenderCaps>() };
                status = getCaps(renderer, ref caps);
                if (status != 0)
                    throw new InvalidOperationException($"Renderer capability query failed: 0x{status:X8}.");
                DeviceInfo = $"D3D{caps.ApiMajor} · FL {caps.FeatureLevel >> 12}.{(caps.FeatureLevel >> 8) & 15}" +
                    $" · SM {caps.ShaderModel / 10}.{caps.ShaderModel % 10} · " +
                    (caps.IsSoftware != 0 ? "WARP" : "Hardware");
            } finally {
                Marshal.FreeHGlobal(shaderAddress);
            }
        } catch {
            Dispose();
            throw;
        }
    }

    private string ReadShaderError() {
        if (getLastShaderError is null ||
            getLastShaderError(1, 0, 0, out var required) != 0 || required <= 1 || required > 8192)
            return string.Empty;
        var buffer = Marshal.AllocHGlobal((int)required);
        try {
            if (getLastShaderError(1, buffer, required, out _) != 0) return string.Empty;
            var bytes = new byte[required - 1];
            Marshal.Copy(buffer, bytes, 0, bytes.Length);
            return "\n" + Encoding.UTF8.GetString(bytes).TrimEnd('\0', '\r', '\n');
        } finally {
            Marshal.FreeHGlobal(buffer);
        }
    }

    public void Resize(uint width, uint height) {
        if (renderer == 0 || resize is null) return;
        var status = resize(renderer, width, height);
        if (status != 0) throw new InvalidOperationException($"Viewport resize failed: 0x{status:X8}.");
    }

    public void Render() {
        if (renderer == 0 || renderFrame is null) return;
        var status = renderFrame(renderer);
        if (status != 0) throw new InvalidOperationException($"Viewport rendering failed: 0x{status:X8}.");
    }

    public void Dispose() {
        if (renderer != 0) {
            destroy?.Invoke(renderer);
            renderer = 0;
        }
        if (library != 0) {
            NativeLibrary.Free(library);
            library = 0;
        }
        GC.SuppressFinalize(this);
    }
}
