#include <ATMERA/RenderGraph.hpp>
#include <ATMERA/RenderABI.h>
#include <algorithm>
#include <functional>
#include <queue>
#include <utility>

namespace atmera {
RenderGraph::PassId RenderGraph::add_pass() {
    passes_.emplace_back();
    return passes_.size() - 1;
}

RenderGraph::ResourceId RenderGraph::add_resource() {
    textures_.push_back(std::nullopt);
    buffers_.push_back(std::nullopt);
    return textures_.size() - 1;
}

std::optional<RenderGraph::ResourceId> RenderGraph::add_texture(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) return std::nullopt;
    textures_.push_back(TextureDesc{width, height});
    buffers_.push_back(std::nullopt);
    return textures_.size() - 1;
}

std::optional<RenderGraph::ResourceId> RenderGraph::add_buffer(std::uint32_t size_bytes, std::uint32_t usage) {
    if (size_bytes == 0 ||
        (usage != ATMERA_BUFFER_VERTEX && usage != ATMERA_BUFFER_INDEX && usage != ATMERA_BUFFER_CONSTANT) ||
        (usage == ATMERA_BUFFER_CONSTANT && size_bytes % 256 != 0)) return std::nullopt;
    textures_.push_back(std::nullopt);
    buffers_.push_back(BufferDesc{size_bytes, usage});
    return buffers_.size() - 1;
}

bool RenderGraph::add_dependency(PassId before, PassId after) {
    if (before >= passes_.size() || after >= passes_.size() || before == after)
        return false;
    auto& edges = passes_[before].successors;
    if (std::find(edges.begin(), edges.end(), after) == edges.end()) edges.push_back(after);
    return true;
}

bool RenderGraph::use_resource(PassId pass, ResourceId resource) {
    return mark_resource(pass, resource, 0);
}

bool RenderGraph::read_resource(PassId pass, ResourceId resource) {
    return mark_resource(pass, resource, 1);
}

bool RenderGraph::write_resource(PassId pass, ResourceId resource) {
    return mark_resource(pass, resource, 2);
}

bool RenderGraph::mark_resource(PassId pass, ResourceId resource, std::uint8_t access) {
    if (pass >= passes_.size() || resource >= textures_.size()) return false;
    auto& uses = passes_[pass].resources;
    const auto existing = std::find_if(uses.begin(), uses.end(),
        [resource](const Pass::Use& use) { return use.resource == resource; });
    if (existing == uses.end()) uses.push_back({resource, access});
    else existing->access |= access;
    return true;
}

std::optional<std::vector<RenderGraph::PassId>> RenderGraph::schedule() const {
    std::vector<std::vector<PassId>> edges;
    edges.reserve(passes_.size());
    for (const auto& pass : passes_) edges.push_back(pass.successors);

    std::vector<std::vector<std::pair<PassId, std::uint8_t>>> accesses(textures_.size());
    for (PassId pass = 0; pass < passes_.size(); ++pass)
        for (const auto& use : passes_[pass].resources)
            if (use.access != 0) accesses[use.resource].push_back({pass, use.access});
    for (ResourceId id = 0; id < accesses.size(); ++id) {
        const auto& resource = accesses[id];
        if ((textures_[id] || buffers_[id]) && !resource.empty() &&
            !(resource.front().second & 2)) return std::nullopt;
        std::optional<PassId> last_writer;
        std::vector<PassId> readers;
        for (const auto& [pass, access] : resource) {
            if (access & 2) {
                if (last_writer) edges[*last_writer].push_back(pass);
                for (const auto reader : readers) edges[reader].push_back(pass);
                readers.clear();
                last_writer = pass;
            } else {
                if (last_writer) edges[*last_writer].push_back(pass);
                readers.push_back(pass);
            }
        }
    }

    std::vector<std::size_t> incoming(passes_.size());
    for (const auto& successors : edges)
        for (const auto after : successors) ++incoming[after];

    std::priority_queue<PassId, std::vector<PassId>, std::greater<PassId>> ready;
    for (PassId id = 0; id < incoming.size(); ++id)
        if (incoming[id] == 0) ready.push(id);

    std::vector<PassId> order;
    order.reserve(passes_.size());
    while (!ready.empty()) {
        const auto before = ready.top();
        ready.pop();
        order.push_back(before);
        for (const auto after : edges[before])
            if (--incoming[after] == 0) ready.push(after);
    }
    if (order.size() != passes_.size()) return std::nullopt;
    return order;
}

std::optional<RenderGraph::Plan> RenderGraph::plan() const {
    auto order = schedule();
    if (!order) return std::nullopt;
    Plan result{std::move(*order), std::vector<std::optional<ResourceLifetime>>(textures_.size()), textures_, buffers_};
    for (std::size_t step = 0; step < result.order.size(); ++step) {
        for (const auto& use : passes_[result.order[step]].resources) {
            auto& lifetime = result.lifetimes[use.resource];
            if (!lifetime) lifetime = ResourceLifetime{step, step};
            else lifetime->last_step = step;
        }
    }
    return result;
}
}
