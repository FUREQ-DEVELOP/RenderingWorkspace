# Changelog

## Phase 7 — Project-local installation from repository downloads

- Added `ATMERA.ModuleTool` to the Visual Studio solution for installing and updating verified local DLLs through the existing Module Framework.
- `DownloadModule.ps1` can now invoke the tool after SHA-256 verification. Offline tests use real plugin DLLs to cover install, update, and rejection of a mismatched ABI module ID.
- Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed. No live package release exists yet.

## Phase 7 — Verified module download tool

- Added a GitHub CLI tool that downloads a module DLL and SHA-256 sidecar from a named RenderingLibrary release into a project-local directory, then publishes the download only after hash validation.
- Offline tests cover successful download and hash-mismatch cleanup. No live release is available yet.

## Phase 7 — Repository discovery tool

- Mapped development and verification to `FUREQ-DEVELOP/RenderingWorkspace`, and completed SDK and release assets to `FUREQ-DEVELOP/RenderingLibrary`.
- Added a GitHub CLI search tool for published module-tagged releases and an offline filtering test. A live query with `fureq-dev` returned the current empty catalog.

## Phase 6 — Automatic startup rollback

- Added opt-in `Engine::start_with_rollback`. A plugin initialization failure selects the retained previous version, persists it, and retries startup once; ordinary `start` keeps its existing behavior.
- Kernel tests cover successful retry, persisted selection, later reselection, and an unversioned plugin with no rollback candidate. Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed.

## Phase 6 — Explicit module rollback

- Version 3 project lockfiles retain one previous module path/version/hash, while version 1 and 2 files remain readable.
- Added `Engine::rollback_module` to verify and select the retained DLL while stopped. A second call reselects the newer version; failed lockfile writes retain the current selection.
- Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed, including rollback, reload, and legacy-lockfile tests.

## Phase 6 — Versioned module replacement

- Added `Engine::update_module_versioned` for a stopped project. It validates the replacement DLL's module ID, installs a separate version directory, and atomically switches the lockfile to the new path/hash/version.
- On copy or lockfile commit failure, the new directory and in-memory version are restored while the previous DLL remains available. Kernel tests cover mismatch, active-update rejection, failed commit rollback, and updated reload/start.
- Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed.

## Phase 6 — Package version metadata

- Added explicit versioned installation and a package-version query. Version 2 project lockfiles persist numeric `major.minor.patch` metadata; existing unversioned installation remains supported.
- Version 1 lockfiles remain readable, including CRLF files, and are migrated on the next write. Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed.

## Phase 6 — Module enable and disable

- Added `Engine::set_module_enabled` with dependency validation and project-local lockfile persistence. Disabling an active module or a required dependency fails.
- A lockfile commit failure restores the previous in-memory flag. Kernel tests verify disable/re-enable across Engine instances and rollback on save failure.
- Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed.

## Phase 6 — Project module lockfile

- Installation now atomically writes a project-local `Modules.lock` with module IDs, relative DLL paths, enabled flags, dependencies, and SHA-256 hashes. A failed lockfile commit rolls back the newly installed DLL.
- Added explicit reload with all-entry validation. Tests cover reload across Engine instances, preserving entries during later installs, lockfile commit failure, and changed-DLL detection.
- Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed.

## Phase 6 — Project-local module installation

- Added `Engine::install_module` and `ModuleRegistry::install` for ABI-validated local DLL installation into one project's `Modules/`, with staging and registration.
- The registry rejects a redirected `Modules/` root. Kernel tests cover unsafe/mismatched IDs, duplicate and running installs, dependency startup, and two-project isolation.
- Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed.

## Phase 5 — Bloom downsampling

- Added `BloomDownsamplePass` to optional `ATMERA.Bloom.lib`, using linear sampling for smaller RGBA8 targets.
- The composite pass now samples the blurred source linearly, allowing half-resolution blur to combine with a full-resolution original. D3D11/D3D12 tests verify 8×8→4×4 checkerboard averaging and the four-pass Bloom chain.
- Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed.

## Phase 5 — Bloom composition

- Added `BloomCompositePass` to optional `ATMERA.Bloom.lib`, combining original and blurred textures with 0–1 intensity and saturating RGB output.
- The external consumer now runs bright extraction, blur, and composition as a three-pass Render Graph and checks GPU pixels on D3D11/D3D12 WARP. Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed.

## Phase 5 — Bloom blur

- Added `BloomBlurPass` to the optional `ATMERA.Bloom.lib`: a fixed 3×3 Gaussian filter with source dimensions supplied at creation.
- D3D11/D3D12 GPU readback checks center, neighboring, and distant pixels from an impulse image. Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed.

## Phase 5 — Bloom bright extraction

- Added a public C++ bright extraction pass with a 0–1 threshold, full-screen triangle, and owned pipeline/bindings in the independently linked `ATMERA.Bloom.lib`. The Graphics core has no Bloom dependency.
- External-consumer GPU readback verifies bright and dim regions on D3D11/D3D12 WARP. Debug|x64 `ATMERA.sln` and `Tools/Test.ps1` passed.

## Phase 4 — Shader binding names

- Appended an optional C ABI v1 function to query each reflected binding's UTF-8 HLSL name using a caller-owned buffer. It follows the order of `get_shader_bindings` and validates index and capacity.
- D3D11/D3D12 external-consumer checks and the full Debug|x64 `ATMERA.sln` build and `Tools/Test.ps1` passed.

## Phase 4 — Render Graph initialization validation

- Reject schedules that read a graph-owned texture or buffer before its first write. Caller-owned resources retain their existing read-first behavior.
- Confirmed the Debug|x64 solution build and `Tools/Test.ps1`, including both backends and Studio regressions.

## Phase 4 — Render Graph temporary buffers

- Added graph-owned vertex, index, and constant buffer declarations, with allocation at first use and release after the final pass fence. Four-argument recording callbacks receive texture and buffer handles by resource ID; earlier callback forms remain available.
- The external consumer verifies a graph-owned buffer write, GPU copy, and caller-owned buffer readback on D3D11 and D3D12. Debug|x64 solution build and `Tools/Test.ps1` passed.

## Phase 4 — Render Graph temporary textures

- Added graph-owned temporary RGBA8 textures with allocation at first use and release after the final pass fence. The callback can access handles by resource ID, while the existing callback signature remains available.
- The external consumer now draws into a graph-owned texture and verifies the copied pixel on both backends. Debug|x64 solution build and `Tools/Test.ps1` passed.

## Phase 4 — Shader compilation cache

- Added bounded per-renderer D3D11/D3D12 shader caches: one exact source and entry-point match per vertex/pixel stage, retaining bytecode and reflection until replacement or renderer destruction.
- Appended a fixed-width C ABI cache-statistics query for `create_shader` hits, misses, and entry count. External-consumer tests verify hit, changed-source miss, old-handle lifetime, and output-struct validation.
- Verified the Debug|x64 solution build and `Tools/Test.ps1`; Release was not run for this slice.

## Phase 4 — Render Graph GPU execution

- Added a C++ `execute_render_graph` helper that records and submits each scheduled pass on the graphics queue, waits for completion, and owns command-list/fence cleanup.
- The external consumer now renders a triangle and copies its texture through two graph passes on D3D11/D3D12; it also checks cleanup after a recording callback rejects a pass.
- Verified the Debug|x64 solution build and `Tools/Test.ps1`. Release was not run for this slice.

## Phase 4 — Shader binding details

- Added an append-only Render ABI v1 query for shader binding kind, `b`/`t`/`s` register index, register space, and array count on both D3D11 and D3D12.
- Verified caller-owned buffer sizing, record-size checks, and `b4`/`t2`/`s3` reflection in external-consumer tests. Debug|x64 solution build and `Tools/Test.ps1` passed; Release was not run.

## Phase 4 — Shader binding reflection

- Added an append-only Render ABI v1 shader-reflection query for stage and compiled constant-buffer, texture, and sampler binding counts on D3D11/D3D12.
- Kept GraphicsContext and Triangle compatible with the pre-extension table prefix. External-consumer tests compile an HLSL shader with one binding of each kind and reject an undersized output struct.
- Verified the Debug|x64 solution build and `Tools/Test.ps1`; Release was not run for this slice.

## Phase 4 — Render Graph access ordering

- Added `read_resource` and `write_resource` to infer RAW, WAR, and WAW dependencies from pass insertion order; read-only passes remain unordered.
- Verified conflicting explicit edges produce a cycle, including a pass that both reads and writes one resource. Debug|x64 solution build and `Tools/Test.ps1` passed; Release was not run.

## Phase 4 — Render Graph resource lifetimes

- Added resource-use declarations and per-resource first/last use steps to the public C++ Render Graph plan without changing Render ABI v1.
- Verified reordered-pass, unused-resource, invalid-ID, and cyclic-graph cases in the external-consumer test. Debug|x64 solution build and `Tools/Test.ps1` passed; Release was not run.

## Phase 4 — Render Graph scheduling

- Added the public C++ `RenderGraph` planner to `ATMERA.Graphics` with deterministic dependency ordering and cycle detection; Render ABI v1 is unchanged.
- Added an external-consumer graph test to `Tools/Test.ps1`. Debug|x64 solution build and tests passed; Release was not run for this slice.

## Phase 3 — Minimal Studio rebuild

- Rebuilt Studio as a small graphics debugger with one viewport, D3D11/D3D12 selection, Force WARP, local HLSL open/reload/reset, compiler errors, capability display, and pause/step.
- Removed Studio project management, Assets browsing/editing, Inspector previews, and external preview launching. The optional Module Framework and native graphics SDK remain separate.
- Replaced the Studio smoke path and verified `ATMERA.sln` Debug|x64 plus `Tools/Test.ps1`. Release and manual UI inspection were not run for this rebuild.

## Phase 3 — Forced WARP viewport

- Added a Force WARP toggle for the embedded Studio viewport and retained hardware-first fallback when unchecked.
- Verified the Debug solution build and `Tools/Test.ps1`, including both backends in forced WARP mode and switching the UI toggle. Release and manual UI inspection were not run for this slice.

## Phase 3 — Renderer capability display

- Showed the active backend, Feature Level, Shader Model, and WARP/Hardware mode in Studio using the public `get_caps` call.
- Verified the Debug solution build and `Tools/Test.ps1`, including capability queries on both backends. Release build/test and manual UI inspection were not run for this change.

## Phase 3 — HLSL source inspection

- Added a read-only, bounded HLSL source view in Studio's Inspector and refreshed it when reloading the shader.
- Corrected the viewport grid rows so the toolbar and render surface occupy separate rows.
- Verified the Debug solution build and `Tools/Test.ps1`. Release verification remains pending because elevated execution was rejected and the restricted build could not read the Windows SDK.

## Phase 3 — Studio shader diagnostics

- Exposed D3D11/D3D12 compiler error text through a caller-owned UTF-8 C diagnostic export without changing the Render ABI v1 table.
- Displayed shader errors in Studio's wrapping status area and verified failed reload and recovery on both backends in Debug/Release builds and tests.

## Phase 3.5 — Architecture Separation

- Changed the product definition to an independent modular graphics library; the Module Framework is optional, Studio is a consumer, and game-engine features belong to a future separate product.
- Audited build targets, dependencies, public ABIs, and the Studio/native boundary; recorded the passing pre-migration Debug/Release solution builds and tests in `MIGRATION_PLAN.md`.
- Added the Phase 3.5 roadmap gate and product-boundary documentation; retained all prior phase history and tests.
- Separated `ATMERA.ModuleFramework` and `ATMERA.Graphics` as independent VS2022 build targets. The existing module-only `Engine` API and Plugin ABI v1 remain intact.
- Added the public `GraphicsContext` C++ RAII wrapper and a standalone external C++ sample that uses only public ATMERA headers.
- Allowed null-HWND device creation in both backends for offscreen triangle rendering and GPU pixel readback. Kept the ABI v1 table layout and HWND presentation path; split internal presentation state from device/queue state.
- Verified Debug/Release solution builds and `Tools/Test.ps1`, direct graphics/external-consumer build without Studio, and Release WARP/hardware window and headless pixel checks on D3D11/D3D12.

## 0.1.0 — Phase 0

- Added initial C++20 Kernel, plugin ABI and loader, test DLL, lifecycle test, WPF shell, Visual Studio solution, and baseline documentation.
- Replaced the initial build setup with native Visual Studio projects and a shared Studio launch profile.
- Verified Debug and Release solution builds, native lifecycle checks, and WPF startup checks.

## 0.2.0 — Phase 1

- Added project-scoped module registration, dependency resolution, ordered startup/shutdown, and startup rollback.
- Added service and event APIs to the append-only C host table, with cleanup on DLL unload.
- Added a second test DLL and tests for project isolation, service/event exchange, dependency failures, and lifecycle ordering.

## 0.3.0 — Phase 2 in progress

- Added separate D3D11 and D3D12 backend DLLs through a C rendering table and a shared HLSL triangle sample.
- Added capability queries, resize, WARP fallback, D3D12 resource transitions and fences, and GPU center-pixel checks.
- Verified both backends on WARP and hardware adapters; Studio smoke tests now wait for the GUI process to exit.
- Added public GPU buffer and RGBA8 texture transfer functions, with round-trip and resize-lifetime tests on both backends.
- Added public shader/pipeline creation and frame commands; the triangle sample now draws through that API on D3D11 and D3D12.
- Added texture views, point/linear samplers, fixed t0/s0 binding sets, and textured-pixel validation on WARP and hardware.
- Added independent buffer-copy command lists, queue submission, and fence waits on both backends.
- Added independent offscreen render-target commands and a readback check on both backends.
- Added whole-image texture copy commands with fence waits and exact readback checks on both backends.
- Added rectangular texture copy commands with bounds checks, untouched-pixel verification, and support for different source and destination dimensions.
- Added interleaved vertex input layouts and vertex-buffer binding for independent offscreen drawing.
- Added frame-level vertex-buffer binding; the interactive triangle and swap-chain smoke path now render from vertex data.
- Added 16-bit and 32-bit index-buffer binding and indexed draws to both frame and independent command-list paths.
- Added fixed t0/s0 binding-set support to independent offscreen command lists, including sampled-texture state transitions and render-target conflict checks.
- Added b0 constant-buffer binding for frame and independent offscreen drawing, with a tint-shader pixel check and post-draw readback.
- Added renderer-owned single-resource binding layouts with selectable t/s/b registers and matching D3D12 root signatures; verified t2/s3/b4 on both backends.
- Added a dedicated D3D12 triangle startup project and solution launch profile for F5 visual inspection; verified Debug/Release builds, tests, and a Release hardware smoke run.
- Added packed `UNORM8X4` vertex attributes in both backends, with stride validation and offscreen pixel checks on WARP and hardware.
- Added `FLOAT1` vertex attributes and an intensity-shader test with offscreen pixel checks on WARP and hardware.
- Added two-stream vertex pipelines and slot 1 binding for frame and independent offscreen drawing, with separate position/color buffers and WARP/hardware pixel checks on both backends.
- Added `HALF2` vertex attributes mapped to `R16G16_FLOAT` in single- and two-stream layouts, with offscreen pixel checks on both backends.
- Added a two-texture binding set and matching pipeline using consecutive SRV registers and one shared sampler; verified frame and offscreen blending on both backends.
- Added two independent samplers for the texture pair, with point/linear pixel comparisons in frames and offscreen command lists on both backends.
- Added `HALF4` vertex attributes to both backends, preserving the existing `HALF2`/packed-color checks and verifying half-float colors in one- and two-stream offscreen draws and swap-chain frames on WARP and hardware.
- Added `UNORM16X2` compressed UV vertex input with red/green texture pixel checks in one- and two-stream offscreen draws and swap-chain frames on both backends, WARP and hardware.
- Added three-stream vertex pipelines and slot 2 bindings to the append-only Render ABI v1 table. Position, packed color, and compressed UV streams now draw in offscreen passes and swap-chain frames on D3D11/D3D12, with WARP and hardware pixel checks.
- Added `UNORM8X2` compressed UV vertex input mapped to `R8G8_UNORM` in both backends, with single-stream offscreen and three-stream frame pixel checks on WARP and hardware.

## 0.4.0 — Phase 3 in progress

- Added a WPF view model with D3D11/D3D12 preview selection, launch command, and status feedback. Studio starts the matching Triangle executable from the active Debug/Release solution output.
- Extended Studio smoke startup to verify both preview executables and the shared shader. Visually confirmed D3D12 preview launch from Studio.
- Added an embedded `HwndHost` viewport with C ABI interop to both native backends, hardware-to-WARP fallback, backend switching, and resize. Verified Debug/Release smoke renders and visually checked both backends in Studio.
- Added Studio project creation and opening with a versioned manifest and project-local folders. Smoke tests cover creation, reopening, and separation of two projects.
- Added nested project asset browsing, manual Refresh, and Inspector details for the selected file or folder. Smoke tests cover nested files and clearing the selection when switching projects.
- Added Studio's Import file action for a project-local `Assets/` copy, automatic tree/Inspector refresh, and duplicate-name rejection. Debug/Release smoke tests verify copied bytes and isolation between projects.
- Added Inspector file rename within the selected Assets folder. The action validates the new name and project path, keeps existing files intact on collision, and refreshes the tree and Inspector selection. Debug/Release smoke tests cover nested rename and rejected paths.
- Made Import file honor the Assets tree selection: a folder receives the file, a selected file uses its parent folder, and no selection uses the Assets root. Debug/Release smoke tests cover all destinations, duplicate rejection, and path containment.
- Added Create folder to Studio's Assets panel. It creates under the selected folder, beside a selected file, or at the Assets root; rejects invalid names and existing files/folders; and selects the new folder. Debug/Release smoke tests cover nested/root creation and path containment.
- Extended Inspector rename to folders, preserving nested files and rejecting collisions with files. Debug/Release Studio smoke tests check contents, selection, and invalid names.
- Added Pause/Resume and Step frame controls to Studio's embedded viewport. Debug/Release smoke tests verify no continuous frames while paused, one frame per step, and backend switching after resume.
- Added a scaled PNG/JPEG preview in the Inspector, with invalid-image feedback and project-local path validation. Debug/Release smoke tests decode both formats and clear the preview for non-image assets and project changes.
- Added project-local HLSL selection for Studio's embedded viewport and a bundled-shader reset action. Debug/Release smoke tests compile a custom `VSMain`/`PSMain` shader on both backends, reject invalid HLSL, and exercise viewport source switching and reset.
- Added Reload shader for the embedded viewport. Debug/Release smoke tests modify a temporary HLSL copy, verify compile failure, repair it, and verify a new renderer draws after reload.
