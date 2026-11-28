# External C++ integration

Build `Native/ATMERA.ExternalConsumer.vcxproj` with Visual Studio 2022/MSBuild, or build the full `ATMERA.sln`. The standalone project builds `ATMERA.Graphics`, D3D11, and D3D12; it does not build Studio or the Module Framework. Copy `ATMERA.Graphics.lib`, the public `SDK/Include/ATMERA/GraphicsContext.hpp` and `RenderABI.h` headers, and the selected backend DLL beside the consumer executable. For Render Graph use, also include `RenderGraph.hpp` and `RenderGraphExecutor.hpp`. D3D12 also needs its DXC runtime DLLs from the Windows SDK.

The sample in `Samples/ExternalConsumer/main.cpp` loads one backend by explicit DLL path, opens `GraphicsContext`, renders the same triangle either to a hidden HWND swapchain or to a renderer-owned RGBA8 texture without an HWND, and validates a center pixel. The offscreen path waits for the queue fence before reading or destroying the texture. It uses no Studio headers, internal backend headers, project manifest, or Module Framework API.

From the repository root after a Release build:

```powershell
./out/vs2022/x64/Release/ATMERA.ExternalConsumer.exe --backend d3d11 --headless
./out/vs2022/x64/Release/ATMERA.ExternalConsumer.exe --backend d3d12 --headless
./out/vs2022/x64/Release/ATMERA.ExternalConsumer.exe --backend d3d11 --window
./out/vs2022/x64/Release/ATMERA.ExternalConsumer.exe --backend d3d12 --window
```

The default adapter is WARP for deterministic checks; add `--hardware` to validate the physical adapter. `Tools/Test.ps1` runs all four WARP paths in Debug and Release configurations.
