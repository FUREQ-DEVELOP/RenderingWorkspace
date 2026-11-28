#pragma once
#include <ATMERA/GraphicsContext.hpp>
#include <memory>

namespace atmera {
// Applies a fixed 3x3 Gaussian blur to an RGBA8 texture of the given dimensions.
// Keep this object and its source/target textures alive until the submitted fence completes.
class BloomBlurPass final {
public:
    static std::unique_ptr<BloomBlurPass> create(GraphicsContext& context,
        atmera_texture_handle source, std::uint32_t width, std::uint32_t height);
    ~BloomBlurPass();
    BloomBlurPass(const BloomBlurPass&) = delete;
    BloomBlurPass& operator=(const BloomBlurPass&) = delete;
    bool record(atmera_command_list_handle list, atmera_texture_handle target) const;

private:
    explicit BloomBlurPass(GraphicsContext& context) : context_(context) {}
    GraphicsContext& context_;
    atmera_shader_handle vertex_ = nullptr;
    atmera_shader_handle pixel_ = nullptr;
    atmera_pipeline_handle pipeline_ = nullptr;
    atmera_texture_view_handle view_ = nullptr;
    atmera_sampler_handle sampler_ = nullptr;
    atmera_binding_set_handle bindings_ = nullptr;
    atmera_buffer_handle texel_size_ = nullptr;
};
}
