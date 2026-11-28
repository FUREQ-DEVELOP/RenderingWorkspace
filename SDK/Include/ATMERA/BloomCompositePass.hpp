#pragma once
#include <ATMERA/GraphicsContext.hpp>
#include <memory>

namespace atmera {
// Adds an intensity-scaled blurred texture to the original RGBA8 image.
// Keep this object and all textures alive until the submitted fence completes.
class BloomCompositePass final {
public:
    static std::unique_ptr<BloomCompositePass> create(GraphicsContext& context,
        atmera_texture_handle original, atmera_texture_handle blurred, float intensity);
    ~BloomCompositePass();
    BloomCompositePass(const BloomCompositePass&) = delete;
    BloomCompositePass& operator=(const BloomCompositePass&) = delete;
    bool record(atmera_command_list_handle list, atmera_texture_handle target) const;

private:
    explicit BloomCompositePass(GraphicsContext& context) : context_(context) {}
    GraphicsContext& context_;
    atmera_shader_handle vertex_ = nullptr;
    atmera_shader_handle pixel_ = nullptr;
    atmera_pipeline_handle pipeline_ = nullptr;
    atmera_binding_layout_handle layout_ = nullptr;
    atmera_texture_view_handle original_view_ = nullptr;
    atmera_texture_view_handle blurred_view_ = nullptr;
    atmera_sampler_handle sampler_ = nullptr;
    atmera_sampler_handle blur_sampler_ = nullptr;
    atmera_binding_set_handle bindings_ = nullptr;
    atmera_buffer_handle intensity_ = nullptr;
};
}
