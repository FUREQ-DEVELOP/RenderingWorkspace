# Package specification

Package installation is a future optional Module Framework capability. It is not required to create a graphics device, use the RHI, or run an external ATMERA consumer. Packages remain installed per project, never in a user-wide shared installation.

Planned `.atmpkg` packages contain `manifest.json`, `bin/win-x64/`, optional `shaders/`, `editor/`, `resources/`, and a license. The installer will validate manifest schema, compatibility, dependency versions, hashes/signatures, archive paths, and extracted contents before committing into a project's `Modules/`. Installation will stage changes and roll back on failure. Package tooling is not implemented yet.
