#include <ATMERA/BloomBrightPass.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <new>

namespace atmera {
namespace {
constexpr char shader[] = R"(
Texture2D Source : register(t0);
SamplerState PointSampler : register(s0);
cbuffer BrightParameters : register(b0) { float Threshold; };
struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 positions[3] = { float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0) };
    VSOut result;
    result.position = float4(positions[id], 0.0, 1.0);
    result.uv = float2((positions[id].x + 1.0) * 0.5, (1.0 - positions[id].y) * 0.5);
    return result;
}
float4 PSMain(VSOut input) : SV_TARGET {
    float4 color = Source.Sample(PointSampler, input.uv);
    return max(color.r, max(color.g, color.b)) >= Threshold ? color : float4(0, 0, 0, 1);
}
)";
}

std::unique_ptr<BloomBrightPass> BloomBrightPass::create(GraphicsContext& context,
    atmera_texture_handle source, float threshold) {
    const auto& api = context.api();
    if (!source || !std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f ||
        !api.create_shader || !api.destroy_shader || !api.create_pipeline || !api.destroy_pipeline ||
        !api.create_texture_view || !api.destroy_texture_view || !api.create_sampler ||
        !api.destroy_sampler || !api.create_binding_set || !api.destroy_binding_set ||
        !api.create_buffer || !api.write_buffer || !api.destroy_buffer ||
        !api.command_begin_render_target || !api.command_bind_pipeline ||
        !api.command_bind_binding_set || !api.command_bind_constant_buffer ||
        !api.command_draw || !api.command_end_render_target) return {};
    auto pass = std::unique_ptr<BloomBrightPass>(new (std::nothrow) BloomBrightPass(context));
    if (!pass) return {};
    constexpr auto source_size = static_cast<uint32_t>(sizeof(shader) - 1);
    const atmera_shader_desc vs{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
        shader, source_size, "VSMain"};
    const atmera_shader_desc ps{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
        shader, source_size, "PSMain"};
    const atmera_sampler_desc sampler{sizeof(atmera_sampler_desc), ATMERA_FILTER_POINT, ATMERA_ADDRESS_CLAMP};
    const atmera_buffer_desc constant{sizeof(atmera_buffer_desc), 256, ATMERA_BUFFER_CONSTANT};
    if (api.create_shader(context.handle(), &vs, &pass->vertex_) != 0 || !pass->vertex_ ||
        api.create_shader(context.handle(), &ps, &pass->pixel_) != 0 || !pass->pixel_ ||
        api.create_pipeline(context.handle(), pass->vertex_, pass->pixel_, &pass->pipeline_) != 0 ||
        !pass->pipeline_ || api.create_texture_view(context.handle(), source, &pass->view_) != 0 ||
        !pass->view_ || api.create_sampler(context.handle(), &sampler, &pass->sampler_) != 0 ||
        !pass->sampler_ || api.create_binding_set(context.handle(), pass->view_, pass->sampler_,
            &pass->bindings_) != 0 || !pass->bindings_ ||
        api.create_buffer(context.handle(), &constant, &pass->threshold_) != 0 || !pass->threshold_)
        return {};
    std::array<std::uint8_t, 256> data{};
    std::memcpy(data.data(), &threshold, sizeof(threshold));
    if (api.write_buffer(context.handle(), pass->threshold_, 0, data.data(),
        static_cast<uint32_t>(data.size())) != 0) return {};
    return pass;
}

BloomBrightPass::~BloomBrightPass() {
    const auto& api = context_.api();
    if (threshold_) api.destroy_buffer(threshold_);
    if (bindings_) api.destroy_binding_set(bindings_);
    if (sampler_) api.destroy_sampler(sampler_);
    if (view_) api.destroy_texture_view(view_);
    if (pipeline_) api.destroy_pipeline(pipeline_);
    if (pixel_) api.destroy_shader(pixel_);
    if (vertex_) api.destroy_shader(vertex_);
}

bool BloomBrightPass::record(atmera_command_list_handle list, atmera_texture_handle target) const {
    if (!list || !target) return false;
    const auto& api = context_.api();
    if (api.command_begin_render_target(list, target) != 0) return false;
    const bool recorded = api.command_bind_pipeline(list, pipeline_) == 0 &&
        api.command_bind_binding_set(list, bindings_) == 0 &&
        api.command_bind_constant_buffer(list, threshold_) == 0 &&
        api.command_draw(list, 3) == 0;
    return api.command_end_render_target(list) == 0 && recorded;
}
}
