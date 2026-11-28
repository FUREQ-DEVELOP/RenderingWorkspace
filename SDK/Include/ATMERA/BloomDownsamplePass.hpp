#pragma once
#include <ATMERA/GraphicsContext.hpp>
#include <memory>

namespace atmera {
// Samples an RGBA8 source linearly into a smaller target, normally half-size.
// Keep this object and both textures alive until the submitted fence completes.
class BloomDownsamplePass final {
public:
    static std::unique_ptr<BloomDownsamplePass> create(GraphicsContext& context,
        atmera_texture_handle source);
    ~BloomDownsamplePass();
    BloomDownsamplePass(const BloomDownsamplePass&) = delete;
    BloomDownsamplePass& operator=(const BloomDownsamplePass&) = delete;
    bool record(atmera_command_list_handle list, atmera_texture_handle target) const;

private:
    explicit BloomDownsamplePass(GraphicsContext& context) : context_(context) {}
    GraphicsContext& context_;
    atmera_shader_handle vertex_ = nullptr;
    atmera_shader_handle pixel_ = nullptr;
    atmera_pipeline_handle pipeline_ = nullptr;
    atmera_texture_view_handle view_ = nullptr;
    atmera_sampler_handle sampler_ = nullptr;
    atmera_binding_set_handle bindings_ = nullptr;
};
}
