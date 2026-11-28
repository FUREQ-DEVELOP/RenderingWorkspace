# Render Graph — Phase 4

`ATMERA.Graphics` exposes `atmera::RenderGraph` through `SDK/Include/ATMERA/RenderGraph.hpp`. This C++ planning API does not depend on DirectX, Studio, a project manifest, or the optional Module Framework. It does not change the Render ABI v1 C table.

Add passes in insertion order and declare each required ordering with `add_dependency(before, after)`. `schedule()` returns pass IDs in a deterministic topological order, choosing the smallest ready ID first. It returns `std::nullopt` if the dependency graph contains a cycle. Unknown IDs and self-dependencies are rejected; duplicate dependencies have no additional effect. An empty graph returns an empty schedule.

```cpp
atmera::RenderGraph graph;
auto upload = graph.add_pass();
auto draw = graph.add_pass();
graph.add_dependency(upload, draw);
auto target = graph.add_resource();
graph.write_resource(draw, target);
auto plan = graph.plan(); // order: {upload, draw}; target lifetime: steps [1, 1]
```

`add_resource()` assigns a resource ID. `read_resource` and `write_resource` record typed uses. For accesses to the same resource, the graph infers read-after-write, write-after-read, and write-after-write ordering from pass insertion order. Multiple accesses in one pass are combined. Two reads remain unordered. An explicit dependency that conflicts with inferred order creates a cycle, so no schedule or plan is returned. `use_resource` remains available for lifetime tracking alone and does not infer ordering. All use methods reject unknown IDs and ignore duplicates.

Graph-owned textures and buffers start without initialized contents. If their first typed access is a read, `schedule()` and `plan()` return `std::nullopt`, including when a later pass writes the resource. Caller-owned resources may be read first because their contents can be supplied before graph execution. An untyped `use_resource()` only tracks lifetime and does not assert initialization.

`plan()` returns the same pass order as `schedule()` and a lifetime per resource. Each lifetime contains inclusive `first_step` and `last_step` indices into the plan order; unused resources have `std::nullopt`. A cyclic graph has no plan. `add_texture(width, height)` adds a graph-owned RGBA8 texture and rejects zero dimensions. `add_buffer(size_bytes, usage)` adds a graph-owned buffer, using the existing vertex, index, or constant usage values. Zero size, unknown usage, and unaligned constant-buffer sizes are rejected. The plan carries these descriptions; `add_resource()` remains for caller-owned resources.

`RenderGraphExecutor.hpp` adds `execute_render_graph(graph, context, record_pass)`. It obtains the graphics queue, creates one command list for each scheduled pass, calls the C++ recording callback, submits, waits for a fence, and releases the command list and fence before moving to the next pass. The four-argument callback receives `(pass, command_list, textures, buffers)`. Each handle vector is indexed by resource ID; entries for other resource types, caller-owned resources, and inactive lifetimes are null. Unused graph resources are never allocated. Each graph resource is created before its first pass and destroyed after the fence for its last pass completes. The previous two- and three-argument callbacks remain supported. The caller owns resources added with `add_resource()`, plus shaders and pipelines. The executor returns `false` on planning, allocation, recording, submission, or wait failure and cleans up graph-owned handles. Execution is serial.

The external consumer uses this helper to draw a triangle into a graph-owned texture, copy it into a caller-owned texture, and read the final pixel. It also writes a graph-owned buffer, copies it into a caller-owned buffer, and checks the bytes. D3D11 and D3D12 backends perform the resource transitions required by their existing commands. The graph does not generate backend barriers for this command ABI. `Tools/Test.ps1` checks the planner and both GPU paths, including a rejected recording callback and uninitialized reads.
