#pragma once
#include <ATMERA/GraphicsContext.hpp>
#include <ATMERA/RenderGraph.hpp>
#include <cstdint>
#include <functional>
#include <vector>

namespace atmera {
// Records and submits one graphics command list per scheduled pass, waiting after each.
// The caller owns resources added with add_resource(). Graph-owned handles are
// indexed by ResourceId and valid only within their planned use interval.
bool execute_render_graph(const RenderGraph& graph, GraphicsContext& context,
    const std::function<bool(RenderGraph::PassId, atmera_command_list_handle,
        const std::vector<atmera_texture_handle>&,
        const std::vector<atmera_buffer_handle>&)>& record_pass,
    std::uint32_t timeout_ms = 5000);
// Compatibility overload for callbacks that only need texture handles.
bool execute_render_graph(const RenderGraph& graph, GraphicsContext& context,
    const std::function<bool(RenderGraph::PassId, atmera_command_list_handle,
        const std::vector<atmera_texture_handle>&)>& record_pass,
    std::uint32_t timeout_ms = 5000);
// Compatibility overload for graphs whose callbacks do not need texture handles.
bool execute_render_graph(const RenderGraph& graph, GraphicsContext& context,
    const std::function<bool(RenderGraph::PassId, atmera_command_list_handle)>& record_pass,
    std::uint32_t timeout_ms = 5000);
}
