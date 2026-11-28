# Phase 3.5 migration plan

## Baseline and safe boundary

On 2026-11-24, the pre-migration `ATMERA.sln` built in Debug|x64 and Release|x64 with Visual Studio 2022/MSBuild. `Tools/Test.ps1` passed in both configurations: Kernel/plugin lifecycle and project isolation, Studio startup/backend switch/resize and project smoke checks, and D3D11/D3D12 WARP resource and pixel checks. Hardware smoke tests were not rerun for this audit.

The verified pre-rebuild Phase 2/3 slice established the safe migration boundary: both backend DLLs, the Triangle sample, and the embedded Studio viewport worked. Studio's former project creation/opening and Assets browser were subsequently removed in the minimal graphics-debugger rebuild. New Phase 2/3 work must follow the product boundaries; Phase 3.5 acceptance checks were completed before Phase 4 work.

## Audit inventory (3.5.1)

| Surface | Current owner/dependency | Migration decision |
| --- | --- | --- |
| `Native/ATMERA.Core.vcxproj` | Static library with Logger, EventBus, Engine, DependencyResolver, ModuleRegistry, PluginLoader, ServiceRegistry | Classify and build as optional Module Framework; preserve existing client behavior while migrating `Engine` |
| `SDK/Include/ATMERA/PluginABI.h` | Public C plugin ABI v1 | Keep ABI and ownership rules; do not mix rendering into it |
| Other C++ headers in `SDK/Include/ATMERA/` | Public module-runtime C++ declarations | Move/classify under Module Framework SDK boundary without breaking tests |
| `SDK/Include/ATMERA/RenderABI.h` | Public rendering C ABI v1 | Preserve append-only table and version/size checks; add a small C++ consumer wrapper separately |
| `Native/ATMERA.D3D11.vcxproj`, `ATMERA.D3D12.vcxproj` | Independent DLLs; include `RenderABI.h`; no `ATMERA.Core` or Studio project reference | Retain as separate backend targets; extract device/presentation responsibilities incrementally |
| `Samples/Triangle`, `Native/ATMERA.Triangle*.vcxproj` | Loads backend DLLs via public ABI; needs HWND, no project manifest | Retain as window-rendering compatibility sample |
| `Studio/ATMERA.Studio` | WPF app; dynamically loads rendering DLLs via C ABI | Keep consumer-only; avoid dependencies on backend internals |
| `Native/ATMERA.KernelTests.vcxproj` and plugin test DLLs | Validate project-bound module runtime | Keep as Module Framework tests |

The native project references are KernelTests → Core/test plugins and Triangle samples → backend DLLs for build order. There is no Core → backend, backend → Core, or backend → Studio project reference. Studio has no C++ project reference; its runtime DLL lookup depends on the solution output layout. Backend implementation currently combines device and swap-chain state in its renderer handle and requires an HWND during `create`.

## Steps and completion checks

| Step | Status | Completion evidence |
| --- | --- | --- |
| 3.5.1 Architecture Audit | Complete | Inventory above, project references and ABI classified, Debug/Release baseline builds and tests recorded |
| 3.5.2 Responsibility Separation | Complete | `Engine` was audited: it hosts only optional module services and no graphics state; `ATMERA.ModuleFramework` and `ATMERA.Graphics` are independent targets, with no forced project/graphics coupling or dependency cycle |
| 3.5.3 Build Target Separation | Complete | Independent Graphics, D3D11, D3D12, Module Framework, Studio, samples, and tests targets in VS2022; direct external-consumer project build omits WPF/Studio and Module Framework |
| 3.5.4 Public SDK Boundary | Complete for ABI v1 | `RenderABI.h`, `GraphicsContext.hpp`, and Module Framework headers classified; v1 table layout preserved; loader/resource ownership documented in `PUBLIC_API_SPEC.md`; external sample verifies RAII wrapper |
| 3.5.5 Device / Presentation Separation | Complete for ABI v1 | D3D11 creates a device before an optional swapchain; both backends separate presentation state and allow null HWND offscreen queues, lists, resources, and readback; window path retained |
| 3.5.6 Studio Independence | Complete | Studio uses the public C Render ABI via P/Invoke; HwndHost viewport, backend switch, and resize remain; no backend-internal header or graphics dependency on Studio. The later minimal rebuild removed editor project data. |
| 3.5.7 External Consumer Validation | Complete | `Samples/ExternalConsumer/main.cpp` uses only public ATMERA headers, no Studio/Module Framework/project manifest; both backends passed window and headless triangle pixel checks on WARP and hardware |
| 3.5.8 Regression Testing | Complete | Final Debug/Release `.sln` builds and `Tools/Test.ps1` passed; both Triangle hardware smoke tests and all four Release external-consumer hardware modes passed |

Each step is additive where possible. Keep the current ABI v1 HWND path while adding an offscreen path; change the ABI version only if compatibility cannot be maintained. Build and run the relevant tests after each native or Studio slice. Do not add CMake or split the repository in Phase 3.5.

The existing `Engine` C++ class remains as the compatible module-host facade; introducing a second class with identical state and methods would add no responsibility boundary. No public ABI v1 layout changed. The current null-HWND mode still requires width, height, and HLSL source because v1 creation is monolithic; a future explicit device/surface handle split would be a separate additive ABI design task, not a reason to rewrite the proven backends now.

`PluginLoader.hpp` was an implementation-only C++ class in the public include tree. It now lives in `Engine/PluginRuntime/`; external code that included it directly should use the supported `Engine.hpp` or `ModuleRegistry.hpp` facade. The C Plugin ABI and all tested module behavior are unchanged.
