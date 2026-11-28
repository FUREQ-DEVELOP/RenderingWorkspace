# Security

The Module Framework's DLL loading and future package/repository features are optional to basic ATMERA rendering. Project-root path validation applies to modules, not to the graphics device. Studio and external consumers must select backend DLL paths deliberately and validate the public Render ABI before use.

The loader uses `LoadLibraryExW` with DLL-directory and default-directory search flags. The module registry canonicalizes paths and accepts DLLs only within the selected project's `Modules/` subtree, including after symlink resolution. ABI version, table sizes, and exported module IDs are checked before initialization. Native DLLs execute arbitrary code, so plugins from untrusted publishers are unsafe to load. Future package installation must verify integrity, prevent archive path traversal, stage updates, and preserve per-project isolation. ABI and path checks are compatibility and organization controls, not a security sandbox.
