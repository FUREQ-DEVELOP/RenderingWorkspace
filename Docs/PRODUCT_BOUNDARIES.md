# Product boundaries

ATMERA is an independent modular graphics library. Its core product is graphics: the public rendering SDK, graphics device and resources, RHI, D3D11/D3D12 backends, shaders, pipelines, render graphs, graphics modules, and GPU diagnostics. Basic rendering must work without Studio, a project manifest, or the Module Framework.

The Module Framework is a separate, optional product boundary for plugin loading, project-scoped module registration, dependency resolution, services, events, lifecycle, and future package and repository services. It must remain independent of DirectX, WPF, and game-specific types. Modules are installed and resolved within one project; there is no shared installation directory. The existing Plugin ABI v1 remains compatible during separation.

ATMERA Studio is an independent WPF consumer of the public graphics SDK. It is a minimal graphics debugger with a viewport, backend selection, HLSL reload, and rendering diagnostics. It does not manage projects or assets and must not become a dependency of ATMERA graphics or the Module Framework.

A future game engine is a separate product that may consume the ATMERA SDK and optionally the Module Framework. ECS, entities, GameObjects, scene management, physics, audio, input, animation state machines, gameplay scripting, networking, game loops, prefabs, and game build pipelines are outside the ATMERA roadmap.

Dependency direction: external application / Studio / future game engine → public graphics SDK → graphics core → RHI → D3D11 or D3D12. Each consumer may opt into the Module Framework separately. Graphics must not depend on a game engine, WPF, Studio, or project management. No dependency cycle is allowed.

## Current implementation versus target

- Implemented: `RenderABI.h` defines a versioned C rendering table; independent D3D11/D3D12 DLLs and Triangle sample use it without Module Framework, WPF, or a project manifest. Studio loads backend DLLs through the same public ABI.
- Implemented: `ATMERA.ModuleFramework` builds a DirectX-free, WPF-free module runtime. The existing `Engine` class wraps its logger, service registry, event bus, and project-bound module registry; it does not own rendering and remains the compatible module-host facade.
- Implemented: `ATMERA.Graphics` provides a public C++ RAII backend/renderer owner. Both backends can create a device without HWND for offscreen commands and can optionally attach a swapchain for window rendering. An independent external consumer validates both paths.
- Implemented: project-local module installation, version selection, and rollback live in the optional Module Framework. GitHub CLI development tools can search and download RenderingLibrary releases, then invoke the Module Framework installer through `ATMERA.ModuleTool`.
- Pending: a public runtime repository API and a live release download check. Repository access must remain optional to basic graphics.
