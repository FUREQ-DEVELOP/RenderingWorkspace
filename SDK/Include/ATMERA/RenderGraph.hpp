#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace atmera {
// Plans passes and resource-use intervals. The executor owns temporary resources.
class RenderGraph final {
public:
    using PassId = std::size_t;
    using ResourceId = std::size_t;
    struct ResourceLifetime {
        std::size_t first_step;
        std::size_t last_step;
    };
    struct TextureDesc {
        std::uint32_t width;
        std::uint32_t height;
    };
    struct BufferDesc {
        std::uint32_t size_bytes;
        std::uint32_t usage;
    };
    struct Plan {
        std::vector<PassId> order;
        // Indexed by ResourceId; unused resources have no lifetime.
        std::vector<std::optional<ResourceLifetime>> lifetimes;
        // Indexed by ResourceId; nullopt denotes a caller-owned resource.
        std::vector<std::optional<TextureDesc>> textures;
        std::vector<std::optional<BufferDesc>> buffers;
    };

    PassId add_pass();
    ResourceId add_resource();
    // Adds a graph-owned RGBA8 texture; zero dimensions are rejected.
    std::optional<ResourceId> add_texture(std::uint32_t width, std::uint32_t height);
    // Usage is ATMERA_BUFFER_VERTEX, INDEX, or CONSTANT; constant sizes are 256-byte aligned.
    std::optional<ResourceId> add_buffer(std::uint32_t size_bytes, std::uint32_t usage);
    // Returns false for an unknown pass or a dependency on itself. Duplicates are ignored.
    bool add_dependency(PassId before, PassId after);
    // Records a use without adding ordering dependencies. Duplicates are ignored.
    bool use_resource(PassId pass, ResourceId resource);
    // Typed uses infer read-after-write, write-after-read, and write-after-write order
    // from pass insertion order. Multiple uses in one pass are combined.
    bool read_resource(PassId pass, ResourceId resource);
    bool write_resource(PassId pass, ResourceId resource);
    // Returns nullopt for a cycle or a graph-owned resource read before its first write.
    // Ready passes keep insertion order.
    std::optional<std::vector<PassId>> schedule() const;
    // Lifetime steps are inclusive indices into Plan::order. Returns nullopt on a cycle.
    std::optional<Plan> plan() const;

private:
    struct Pass {
        struct Use {
            ResourceId resource;
            std::uint8_t access;
        };
        std::vector<PassId> successors;
        std::vector<Use> resources;
    };
    bool mark_resource(PassId pass, ResourceId resource, std::uint8_t access);
    std::vector<Pass> passes_;
    std::vector<std::optional<TextureDesc>> textures_;
    std::vector<std::optional<BufferDesc>> buffers_;
};
}
