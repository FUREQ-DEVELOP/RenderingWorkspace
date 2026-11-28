# Studio architecture

ATMERA Studio is a minimal graphics debugging application. It is a standalone WPF consumer of the public `RenderABI.h` C interface. It does not own projects, install modules, manage assets, or provide game-editor features.

`MainWindow` has one viewport and a small set of controls: choose D3D11/D3D12, force WARP, open a local HLSL file, reload it, restore the bundled shader, pause, and step one frame. The HLSL file must define `VSMain(uint : SV_VertexID)` and `PSMain` for the built-in triangle path. The status area shows compiler errors; the capability line shows the reported backend, Feature Level, Shader Model, and WARP/Hardware mode. Changing a backend, WARP mode, or shader recreates the viewport.

`NativeViewportHost` owns the Win32 child window inside WPF and drives rendering. `NativeRendererSession` loads the selected backend DLL from the active solution output, checks the versioned C table prefix, owns the renderer and DLL lifetime, and calls `create`, `get_caps`, `resize`, `render_frame`, and `destroy`. It tries hardware first unless WARP is forced; hardware failure falls back to WARP. After shader compilation fails, it copies UTF-8 diagnostics from the optional `atmera_get_last_shader_error` C export. The existing Render ABI v1 table is unchanged. Studio is not required by the rendering SDK or Module Framework.

`Tools/Test.ps1` starts Studio in hidden smoke mode after a solution build. The smoke path checks first rendering, pause and one-frame step, a temporary HLSL file, compiler failure and recovery on both backends, bundled-shader reset, forced WARP on both backends, and resize. Native graphics and Module Framework tests run separately in the same script.
