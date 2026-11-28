# ATMERA development rules

- ATMERA is an independent modular graphics library. Keep the Module Framework optional and graphics-independent; Studio is a public SDK consumer. Game-engine features belong to a separate future product.
- Follow `Docs/PRODUCT_BOUNDARIES.md` and `Docs/MIGRATION_PLAN.md`; finish Phase 3.5 before starting Phase 4. Preserve completed Phase 0–3 behavior and tests.
- Work in small, buildable phases and update `Docs/ROADMAP.md` with verified progress.
- Keep Kernel free of DirectX, WPF, and game-specific dependencies.
- Keep the plugin boundary in C ABI: fixed-width fields, version and size checks, opaque handles, explicit ownership, no STL or exceptions across it.
- Install and resolve modules per project only; never share installed modules across projects.
- Build `ATMERA.sln` with Visual Studio 2022/MSBuild and run `Tools/Test.ps1` for native and Studio changes. Do not add a CMake build system. Report unrun checks honestly.
- Add only the capabilities required by the current phase; keep rendering modules independent where possible.
