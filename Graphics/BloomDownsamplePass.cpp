#include <ATMERA/BloomDownsamplePass.hpp>
#include <new>

namespace atmera {
namespace {
constexpr char shader[] = R"(
Texture2D Source : register(t0);
SamplerState LinearSampler : register(s0);
struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 positions[3] = { float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0) };
    VSOut result;
    result.position = float4(positions[id], 0.0, 1.0);
    result.uv = float2((positions[id].x + 1.0) * 0.5, (1.0 - positions[id].y) * 0.5);
    return result;
}
float4 PSMain(VSOut input) : SV_TARGET {
    return float4(Source.Sample(LinearSampler, input.uv).rgb, 1.0);
}
)";
}

std::unique_ptr<BloomDownsamplePass> BloomDownsamplePass::create(GraphicsContext& context,
    atmera_texture_handle source) {
    const auto& api = context.api();
    if (!source || !api.create_shader || !api.destroy_shader || !api.create_pipeline ||
        !api.destroy_pipeline || !api.create_texture_view || !api.destroy_texture_view ||
        !api.create_sampler || !api.destroy_sampler || !api.create_binding_set ||
        !api.destroy_binding_set || !api.command_begin_render_target ||
        !api.command_bind_pipeline || !api.command_bind_binding_set ||
        !api.command_draw || !api.command_end_render_target) return {};
    auto pass = std::unique_ptr<BloomDownsamplePass>(new (std::nothrow) BloomDownsamplePass(context));
    if (!pass) return {};
    constexpr auto source_size = static_cast<uint32_t>(sizeof(shader) - 1);
    const atmera_shader_desc vs{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
        shader, source_size, "VSMain"};
    const atmera_shader_desc ps{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
        shader, source_size, "PSMain"};
    const atmera_sampler_desc sampler{sizeof(atmera_sampler_desc), ATMERA_FILTER_LINEAR, ATMERA_ADDRESS_CLAMP};
    if (api.create_shader(context.handle(), &vs, &pass->vertex_) != 0 || !pass->vertex_ ||
        api.create_shader(context.handle(), &ps, &pass->pixel_) != 0 || !pass->pixel_ ||
        api.create_pipeline(context.handle(), pass->vertex_, pass->pixel_, &pass->pipeline_) != 0 ||
        !pass->pipeline_ || api.create_texture_view(context.handle(), source, &pass->view_) != 0 ||
        !pass->view_ || api.create_sampler(context.handle(), &sampler, &pass->sampler_) != 0 ||
        !pass->sampler_ || api.create_binding_set(context.handle(), pass->view_, pass->sampler_,
            &pass->bindings_) != 0 || !pass->bindings_) return {};
    return pass;
}

BloomDownsamplePass::~BloomDownsamplePass() {
    const auto& api = context_.api();
    if (bindings_) api.destroy_binding_set(bindings_);
    if (sampler_) api.destroy_sampler(sampler_);
    if (view_) api.destroy_texture_view(view_);
    if (pipeline_) api.destroy_pipeline(pipeline_);
    if (pixel_) api.destroy_shader(pixel_);
    if (vertex_) api.destroy_shader(vertex_);
}

bool BloomDownsamplePass::record(atmera_command_list_handle list, atmera_texture_handle target) const {
    if (!list || !target) return false;
    const auto& api = context_.api();
    if (api.command_begin_render_target(list, target) != 0) return false;
    const bool recorded = api.command_bind_pipeline(list, pipeline_) == 0 &&
        api.command_bind_binding_set(list, bindings_) == 0 &&
        api.command_draw(list, 3) == 0;
    return api.command_end_render_target(list) == 0 && recorded;
}
}
