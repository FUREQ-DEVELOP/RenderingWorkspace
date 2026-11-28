#pragma once
#include <ATMERA/GraphicsContext.hpp>
#include <memory>

namespace atmera {
// Extracts pixels whose largest RGB channel meets threshold into an RGBA8 target.
// Keep this object and its source/target textures alive until the submitted fence completes.
class BloomBrightPass final {
public:
    static std::unique_ptr<BloomBrightPass> create(GraphicsContext& context,
        atmera_texture_handle source, float threshold);
    ~BloomBrightPass();
    BloomBrightPass(const BloomBrightPass&) = delete;
    BloomBrightPass& operator=(const BloomBrightPass&) = delete;
    bool record(atmera_command_list_handle list, atmera_texture_handle target) const;

private:
    explicit BloomBrightPass(GraphicsContext& context) : context_(context) {}
    GraphicsContext& context_;
    atmera_shader_handle vertex_ = nullptr;
    atmera_shader_handle pixel_ = nullptr;
    atmera_pipeline_handle pipeline_ = nullptr;
    atmera_texture_view_handle view_ = nullptr;
    atmera_sampler_handle sampler_ = nullptr;
    atmera_binding_set_handle bindings_ = nullptr;
    atmera_buffer_handle threshold_ = nullptr;
};
}
