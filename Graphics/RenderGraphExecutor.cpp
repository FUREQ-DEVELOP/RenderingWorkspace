#include <ATMERA/RenderGraphExecutor.hpp>

namespace atmera {
bool execute_render_graph(const RenderGraph& graph, GraphicsContext& context,
    const std::function<bool(RenderGraph::PassId, atmera_command_list_handle,
        const std::vector<atmera_texture_handle>&,
        const std::vector<atmera_buffer_handle>&)>& record_pass,
    std::uint32_t timeout_ms) {
    const auto plan = graph.plan();
    if (!plan || !record_pass) return false;
    if (plan->order.empty()) return true;
    const auto& api = context.api();
    if (!api.get_graphics_queue || !api.create_command_list || !api.destroy_command_list ||
        !api.submit_command_list || !api.wait_fence || !api.destroy_fence) return false;
    bool has_textures = false;
    bool has_buffers = false;
    for (std::size_t id = 0; id < plan->textures.size(); ++id)
        has_textures |= plan->textures[id].has_value() && plan->lifetimes[id].has_value();
    for (std::size_t id = 0; id < plan->buffers.size(); ++id)
        has_buffers |= plan->buffers[id].has_value() && plan->lifetimes[id].has_value();
    if (has_textures && (!api.create_texture || !api.destroy_texture)) return false;
    if (has_buffers && (!api.create_buffer || !api.destroy_buffer)) return false;
    atmera_command_queue_handle queue = nullptr;
    if (api.get_graphics_queue(context.handle(), &queue) != 0 || !queue) return false;

    std::vector<atmera_texture_handle> textures(plan->textures.size(), nullptr);
    std::vector<atmera_buffer_handle> buffers(plan->buffers.size(), nullptr);
    const auto cleanup = [&] {
        for (auto& texture : textures) {
            if (texture) api.destroy_texture(texture);
            texture = nullptr;
        }
        for (auto& buffer : buffers) {
            if (buffer) api.destroy_buffer(buffer);
            buffer = nullptr;
        }
    };
    for (std::size_t step = 0; step < plan->order.size(); ++step) {
        for (std::size_t id = 0; id < textures.size(); ++id) {
            if (!plan->textures[id] || !plan->lifetimes[id] ||
                plan->lifetimes[id]->first_step != step) continue;
            const auto& desc = *plan->textures[id];
            const atmera_texture_desc rhi_desc{sizeof(atmera_texture_desc), desc.width, desc.height};
            if (api.create_texture(context.handle(), &rhi_desc, &textures[id]) != 0 || !textures[id]) {
                cleanup();
                return false;
            }
        }
        for (std::size_t id = 0; id < buffers.size(); ++id) {
            if (!plan->buffers[id] || !plan->lifetimes[id] ||
                plan->lifetimes[id]->first_step != step) continue;
            const auto& desc = *plan->buffers[id];
            const atmera_buffer_desc rhi_desc{sizeof(atmera_buffer_desc), desc.size_bytes, desc.usage};
            if (api.create_buffer(context.handle(), &rhi_desc, &buffers[id]) != 0 || !buffers[id]) {
                cleanup();
                return false;
            }
        }
        atmera_command_list_handle list = nullptr;
        atmera_fence_handle fence = nullptr;
        if (api.create_command_list(context.handle(), &list) != 0 || !list) {
            if (list) api.destroy_command_list(list);
            cleanup();
            return false;
        }
        bool recorded = false;
        try { recorded = record_pass(plan->order[step], list, textures, buffers); }
        catch (...) { api.destroy_command_list(list); cleanup(); return false; }
        if (!recorded || api.submit_command_list(queue, list, &fence) != 0 || !fence) {
            if (fence) api.destroy_fence(fence);
            api.destroy_command_list(list);
            cleanup();
            return false;
        }
        const bool completed = api.wait_fence(queue, fence, timeout_ms) == 0;
        api.destroy_fence(fence);
        api.destroy_command_list(list);
        if (!completed) { cleanup(); return false; }
        for (std::size_t id = 0; id < textures.size(); ++id) {
            if (textures[id] && plan->lifetimes[id]->last_step == step) {
                api.destroy_texture(textures[id]);
                textures[id] = nullptr;
            }
        }
        for (std::size_t id = 0; id < buffers.size(); ++id) {
            if (buffers[id] && plan->lifetimes[id]->last_step == step) {
                api.destroy_buffer(buffers[id]);
                buffers[id] = nullptr;
            }
        }
    }
    return true;
}

bool execute_render_graph(const RenderGraph& graph, GraphicsContext& context,
    const std::function<bool(RenderGraph::PassId, atmera_command_list_handle,
        const std::vector<atmera_texture_handle>&)>& record_pass,
    std::uint32_t timeout_ms) {
    if (!record_pass) return false;
    return execute_render_graph(graph, context,
        [&](RenderGraph::PassId pass, atmera_command_list_handle list,
            const std::vector<atmera_texture_handle>& textures,
            const std::vector<atmera_buffer_handle>&) { return record_pass(pass, list, textures); }, timeout_ms);
}

bool execute_render_graph(const RenderGraph& graph, GraphicsContext& context,
    const std::function<bool(RenderGraph::PassId, atmera_command_list_handle)>& record_pass,
    std::uint32_t timeout_ms) {
    if (!record_pass) return false;
    return execute_render_graph(graph, context,
        [&](RenderGraph::PassId pass, atmera_command_list_handle list,
            const std::vector<atmera_texture_handle>&) { return record_pass(pass, list); }, timeout_ms);
}
}
