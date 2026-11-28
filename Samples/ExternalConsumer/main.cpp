#include <ATMERA/BloomBrightPass.hpp>
#include <ATMERA/BloomBlurPass.hpp>
#include <ATMERA/BloomCompositePass.hpp>
#include <ATMERA/BloomDownsamplePass.hpp>
#include <ATMERA/GraphicsContext.hpp>
#include <ATMERA/RenderGraph.hpp>
#include <ATMERA/RenderGraphExecutor.hpp>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr char shader[] = R"(
struct VSOut { float4 position : SV_POSITION; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 positions[3] = { float2(-0.6, -0.6), float2(0.0, 0.6), float2(0.6, -0.6) };
    VSOut result;
    result.position = float4(positions[id], 0.0, 1.0);
    return result;
}
float4 PSMain(VSOut input) : SV_TARGET { return float4(0.9, 0.2, 0.1, 1.0); }
)";

constexpr char reflected_shader[] = R"(
Texture2D Image : register(t2);
SamplerState Linear : register(s3);
cbuffer Color : register(b4) { float4 Tint; };
float4 PSReflect() : SV_Target {
    return Image.Sample(Linear, float2(0.5, 0.5)) * Tint;
}
)";

bool triangle_pixel(const uint8_t* pixel) {
    return pixel[0] > 180 && pixel[1] > 30 && pixel[1] < 90 && pixel[2] < 70;
}

bool test_access_dependencies() {
    atmera::RenderGraph reads;
    const auto read0 = reads.add_pass(), read1 = reads.add_pass();
    const auto read_resource = reads.add_resource();
    if (!reads.read_resource(read0, read_resource) || !reads.read_resource(read1, read_resource) ||
        !reads.add_dependency(read1, read0) ||
        reads.schedule() != std::vector<atmera::RenderGraph::PassId>{read1, read0}) return false;

    for (int hazard = 0; hazard < 3; ++hazard) {
        atmera::RenderGraph graph;
        const auto first = graph.add_pass(), second = graph.add_pass();
        const auto resource = graph.add_resource();
        const bool first_ok = hazard == 0 ? graph.write_resource(first, resource) :
            hazard == 1 ? graph.read_resource(first, resource) : graph.write_resource(first, resource);
        const bool second_ok = hazard == 0 ? graph.read_resource(second, resource) :
            graph.write_resource(second, resource);
        if (!first_ok || !second_ok || !graph.add_dependency(second, first) ||
            graph.schedule() || graph.plan()) return false;
    }

    atmera::RenderGraph read_write;
    const auto first = read_write.add_pass(), second = read_write.add_pass();
    const auto resource = read_write.add_resource();
    return read_write.read_resource(first, resource) &&
        read_write.write_resource(first, resource) && read_write.read_resource(second, resource) &&
        read_write.add_dependency(second, first) && !read_write.schedule();
}

bool test_render_graph() {
    atmera::RenderGraph empty;
    if (empty.schedule() != std::vector<atmera::RenderGraph::PassId>{}) return false;
    const auto empty_plan = empty.plan();
    if (!empty_plan || !empty_plan->order.empty() || !empty_plan->lifetimes.empty() ||
        !empty_plan->textures.empty() || !empty_plan->buffers.empty() ||
        empty.add_texture(0, 1) || empty.add_texture(1, 0) ||
        empty.add_buffer(0, ATMERA_BUFFER_VERTEX) || empty.add_buffer(1, ATMERA_BUFFER_CONSTANT) ||
        empty.add_buffer(64, 0)) return false;

    for (bool texture : {false, true}) {
        atmera::RenderGraph uninitialized;
        const auto read = uninitialized.add_pass();
        const auto resource = texture ? uninitialized.add_texture(4, 4) :
            uninitialized.add_buffer(64, ATMERA_BUFFER_VERTEX);
        if (!resource || !uninitialized.read_resource(read, *resource) ||
            uninitialized.schedule() || uninitialized.plan()) return false;
        const auto write = uninitialized.add_pass();
        if (!uninitialized.write_resource(write, *resource) || uninitialized.schedule()) return false;
    }

    atmera::RenderGraph graph;
    const auto first = graph.add_pass();
    const auto second = graph.add_pass();
    const auto third = graph.add_pass();
    const auto fourth = graph.add_pass();
    if (!graph.add_dependency(third, first) || !graph.add_dependency(second, fourth) ||
        !graph.add_dependency(first, fourth) || !graph.add_dependency(third, first) ||
        graph.add_dependency(first, first) || graph.add_dependency(first, fourth + 1) ||
        graph.add_dependency(fourth + 1, first)) return false;
    if (graph.schedule() != std::vector<atmera::RenderGraph::PassId>{second, third, first, fourth})
        return false;
    const auto shared = graph.add_resource();
    const auto single = graph.add_resource();
    const auto unused = graph.add_resource();
    if (!graph.use_resource(third, shared) || !graph.use_resource(fourth, shared) ||
        !graph.use_resource(third, shared) || !graph.use_resource(second, single) ||
        graph.use_resource(fourth + 1, shared) || graph.use_resource(first, unused + 1)) return false;
    const auto plan = graph.plan();
    if (!plan || plan->order != std::vector<atmera::RenderGraph::PassId>{second, third, first, fourth} ||
        plan->lifetimes.size() != 3 || !plan->lifetimes[shared] ||
        plan->lifetimes[shared]->first_step != 1 || plan->lifetimes[shared]->last_step != 3 ||
        !plan->lifetimes[single] || plan->lifetimes[single]->first_step != 0 ||
        plan->lifetimes[single]->last_step != 0 || plan->lifetimes[unused]) return false;

    atmera::RenderGraph cycle;
    const auto a = cycle.add_pass();
    const auto b = cycle.add_pass();
    return cycle.add_dependency(a, b) && cycle.add_dependency(b, a) &&
        !cycle.schedule() && !cycle.plan() && test_access_dependencies();
}

bool test_shader_reflection(atmera::GraphicsContext& context) {
    const auto& api = context.api();
    if (api.struct_size < sizeof(atmera_render_api) || !api.get_shader_reflection ||
        !api.get_shader_bindings || !api.get_shader_cache_stats || !api.get_shader_binding_name ||
        !api.create_shader || !api.destroy_shader) return false;
    atmera_shader_cache_stats initial{sizeof(atmera_shader_cache_stats)};
    if (api.get_shader_cache_stats(context.handle(), &initial) != 0) return false;
    const atmera_shader_desc desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
        reflected_shader, static_cast<uint32_t>(sizeof(reflected_shader) - 1), "PSReflect"};
    atmera_shader_handle shader_handle = nullptr;
    if (api.create_shader(context.handle(), &desc, &shader_handle) != 0 || !shader_handle)
        return false;
    atmera_shader_reflection reflection{sizeof(atmera_shader_reflection)};
    const bool valid = api.get_shader_reflection(shader_handle, &reflection) == 0 &&
        reflection.stage == ATMERA_SHADER_PIXEL &&
        reflection.constant_buffer_bindings == 1 && reflection.texture_bindings == 1 &&
        reflection.sampler_bindings == 1;
    reflection.struct_size = sizeof(atmera_shader_reflection) - 4;
    const bool rejects_short_struct = api.get_shader_reflection(shader_handle, &reflection) != 0;
    uint32_t required = 0;
    const bool count_valid = api.get_shader_bindings(shader_handle, nullptr, 0, &required) == 0 &&
        required == 3;
    std::array<atmera_shader_binding, 3> bindings{};
    for (auto& binding : bindings) binding.struct_size = sizeof(atmera_shader_binding);
    const bool rejects_short_buffer = api.get_shader_bindings(shader_handle, bindings.data(), 2,
        &required) != 0 && required == 3 && bindings[0].kind == 0;
    bool details_valid = count_valid &&
        api.get_shader_bindings(shader_handle, bindings.data(), 3, &required) == 0 && required == 3;
    std::array<bool, 3> seen{};
    const uint32_t registers[] = {4, 2, 3};
    if (details_valid) for (const auto& binding : bindings) {
        if (binding.kind < 1 || binding.kind > 3 || binding.register_index != registers[binding.kind - 1] ||
            binding.register_space != 0 || binding.register_count != 1) {
            details_valid = false;
            break;
        }
        seen[binding.kind - 1] = true;
    }
    details_valid = details_valid && seen[0] && seen[1] && seen[2];
    bool names_valid = details_valid;
    const char* expected_names[] = {"Color", "Image", "Linear"};
    if (names_valid) for (uint32_t i = 0; i < bindings.size(); ++i) {
        uint32_t bytes = 0;
        std::array<char, 32> name{};
        const auto expected = expected_names[bindings[i].kind - 1];
        const auto expected_bytes = static_cast<uint32_t>(std::strlen(expected) + 1);
        names_valid = api.get_shader_binding_name(shader_handle, i, nullptr, 0, &bytes) == 0 &&
            bytes == expected_bytes &&
            api.get_shader_binding_name(shader_handle, i, name.data(), bytes - 1, &bytes) != 0 &&
            name[0] == 0 &&
            api.get_shader_binding_name(shader_handle, i, name.data(),
                static_cast<uint32_t>(name.size()), &bytes) == 0 &&
            std::strcmp(name.data(), expected) == 0;
        if (!names_valid) break;
    }
    uint32_t invalid_required = 0;
    names_valid = names_valid &&
        api.get_shader_binding_name(shader_handle, 3, nullptr, 0, &invalid_required) != 0 &&
        invalid_required == 0;
    bindings[0].struct_size = sizeof(atmera_shader_binding) - 4;
    const bool rejects_short_entry = api.get_shader_bindings(shader_handle, bindings.data(), 3,
        &required) != 0;
    atmera_shader_cache_stats after_first{sizeof(atmera_shader_cache_stats)};
    const bool first_miss = api.get_shader_cache_stats(context.handle(), &after_first) == 0 &&
        after_first.hits == initial.hits && after_first.misses == initial.misses + 1 &&
        after_first.entries == 1;
    atmera_shader_handle duplicate = nullptr;
    const bool duplicate_created = api.create_shader(context.handle(), &desc, &duplicate) == 0 && duplicate;
    atmera_shader_cache_stats after_duplicate{sizeof(atmera_shader_cache_stats)};
    const bool cache_hit = duplicate_created &&
        api.get_shader_cache_stats(context.handle(), &after_duplicate) == 0 &&
        after_duplicate.hits == after_first.hits + 1 &&
        after_duplicate.misses == after_first.misses;
    if (duplicate) api.destroy_shader(duplicate);
    std::string changed_source(reflected_shader);
    changed_source += "\n// changed source";
    const atmera_shader_desc changed_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
        changed_source.data(), static_cast<uint32_t>(changed_source.size()), "PSReflect"};
    atmera_shader_handle changed = nullptr;
    const bool changed_created = api.create_shader(context.handle(), &changed_desc, &changed) == 0 && changed;
    atmera_shader_cache_stats after_change{sizeof(atmera_shader_cache_stats)};
    const bool new_miss = changed_created &&
        api.get_shader_cache_stats(context.handle(), &after_change) == 0 &&
        after_change.misses == after_duplicate.misses + 1 && after_change.entries == 1;
    atmera_shader_reflection original_after_change{sizeof(atmera_shader_reflection)};
    const bool original_alive = api.get_shader_reflection(shader_handle, &original_after_change) == 0 &&
        original_after_change.texture_bindings == 1;
    if (changed) api.destroy_shader(changed);
    after_change.struct_size = sizeof(atmera_shader_cache_stats) - 4;
    const bool rejects_short_stats = api.get_shader_cache_stats(context.handle(), &after_change) != 0;
    api.destroy_shader(shader_handle);
    return valid && rejects_short_struct && rejects_short_buffer && details_valid && names_valid && rejects_short_entry &&
        first_miss && cache_hit && new_miss && original_alive && rejects_short_stats;
}

bool draw_offscreen(atmera::GraphicsContext& context) {
    const auto& api = context.api();
    const auto renderer = context.handle();
    if (!api.create_shader || !api.destroy_shader || !api.create_pipeline || !api.destroy_pipeline ||
        !api.create_texture || !api.destroy_texture || !api.read_texture ||
        !api.command_begin_render_target ||
        !api.command_bind_pipeline || !api.command_draw || !api.command_end_render_target ||
        !api.command_copy_texture) return false;

    atmera_shader_handle vertex = nullptr, pixel = nullptr;
    atmera_pipeline_handle pipeline = nullptr;
    atmera_texture_handle copied = nullptr;
    bool success = false;
    do {
        const atmera_shader_desc vertex_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            shader, static_cast<uint32_t>(sizeof(shader) - 1), "VSMain"};
        const atmera_shader_desc pixel_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            shader, static_cast<uint32_t>(sizeof(shader) - 1), "PSMain"};
        if (api.create_shader(renderer, &vertex_desc, &vertex) != 0 ||
            api.create_shader(renderer, &pixel_desc, &pixel) != 0 ||
            api.create_pipeline(renderer, vertex, pixel, &pipeline) != 0) break;
        const atmera_texture_desc texture_desc{sizeof(atmera_texture_desc), 128, 128};
        if (api.create_texture(renderer, &texture_desc, &copied) != 0) break;
        atmera::RenderGraph graph;
        const auto draw_pass = graph.add_pass();
        const auto copy_pass = graph.add_pass();
        const auto target_resource = graph.add_texture(128, 128);
        const auto unused_texture = graph.add_texture(1, 1);
        if (!target_resource || !unused_texture) break;
        const auto copied_resource = graph.add_resource();
        graph.write_resource(draw_pass, *target_resource);
        graph.read_resource(copy_pass, *target_resource);
        graph.write_resource(copy_pass, copied_resource);
        const auto plan = graph.plan();
        if (!plan || plan->order != std::vector<atmera::RenderGraph::PassId>{draw_pass, copy_pass} ||
            !plan->lifetimes[*target_resource] ||
            plan->lifetimes[*target_resource]->first_step != 0 ||
            plan->lifetimes[*target_resource]->last_step != 1 ||
            plan->lifetimes[*unused_texture] || !plan->textures[*target_resource] ||
            plan->textures[*target_resource]->width != 128 ||
            plan->textures[*target_resource]->height != 128) break;

        atmera::RenderGraph rejected;
        const auto rejected_pass = rejected.add_pass();
        const auto rejected_texture = rejected.add_texture(2, 2);
        if (!rejected_texture || !rejected.write_resource(rejected_pass, *rejected_texture)) break;
        if (atmera::execute_render_graph(rejected, context,
            [&](atmera::RenderGraph::PassId, atmera_command_list_handle,
                const std::vector<atmera_texture_handle>& textures) {
                return textures[*rejected_texture] == nullptr;
            })) break;

        if (!atmera::execute_render_graph(graph, context,
            [&](atmera::RenderGraph::PassId pass, atmera_command_list_handle list,
                const std::vector<atmera_texture_handle>& textures) {
                if (!textures[*target_resource] || textures[*unused_texture]) return false;
                if (pass == draw_pass) return api.command_begin_render_target(list, textures[*target_resource]) == 0 &&
                    api.command_bind_pipeline(list, pipeline) == 0 &&
                    api.command_draw(list, 3) == 0 &&
                    api.command_end_render_target(list) == 0;
                if (pass == copy_pass) return api.command_copy_texture(list, copied, textures[*target_resource]) == 0;
                return false;
            })) break;
        std::vector<uint8_t> pixels(128 * 128 * 4);
        if (api.read_texture(renderer, copied, pixels.data(), 128 * 4,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        success = triangle_pixel(pixels.data() + ((64 * 128 + 64) * 4));
    } while (false);
    if (copied) api.destroy_texture(copied);
    if (pipeline) api.destroy_pipeline(pipeline);
    if (pixel) api.destroy_shader(pixel);
    if (vertex) api.destroy_shader(vertex);
    return success;
}

bool copy_temporary_buffer(atmera::GraphicsContext& context) {
    const auto& api = context.api();
    if (!api.create_buffer || !api.destroy_buffer || !api.write_buffer || !api.read_buffer ||
        !api.command_copy_buffer) return false;
    atmera_buffer_handle destination = nullptr;
    const atmera_buffer_desc desc{sizeof(atmera_buffer_desc), 64, ATMERA_BUFFER_VERTEX};
    if (api.create_buffer(context.handle(), &desc, &destination) != 0 || !destination) {
        if (destination) api.destroy_buffer(destination);
        return false;
    }
    atmera::RenderGraph graph;
    const auto upload = graph.add_pass();
    const auto copy = graph.add_pass();
    const auto source = graph.add_buffer(64, ATMERA_BUFFER_VERTEX);
    const auto unused = graph.add_buffer(64, ATMERA_BUFFER_VERTEX);
    const auto output = graph.add_resource();
    std::array<uint8_t, 64> expected{};
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>(i + 17);
    bool success = source && unused && graph.write_resource(upload, *source) &&
        graph.read_resource(copy, *source) && graph.write_resource(copy, output);
    const auto plan = graph.plan();
    success = success && plan && plan->lifetimes[*source] &&
        plan->lifetimes[*source]->first_step == 0 && plan->lifetimes[*source]->last_step == 1 &&
        !plan->lifetimes[*unused] && plan->buffers[*source] &&
        plan->buffers[*source]->size_bytes == 64;
    if (success) success = atmera::execute_render_graph(graph, context,
        [&](atmera::RenderGraph::PassId pass, atmera_command_list_handle list,
            const std::vector<atmera_texture_handle>&,
            const std::vector<atmera_buffer_handle>& buffers) {
            if (!buffers[*source] || buffers[*unused]) return false;
            if (pass == upload) return api.write_buffer(context.handle(), buffers[*source], 0,
                expected.data(), static_cast<uint32_t>(expected.size())) == 0;
            if (pass == copy) return api.command_copy_buffer(list, destination, 0,
                buffers[*source], 0, static_cast<uint32_t>(expected.size())) == 0;
            return false;
        });
    std::array<uint8_t, 64> actual{};
    if (success) success = api.read_buffer(context.handle(), destination, 0,
        actual.data(), static_cast<uint32_t>(actual.size())) == 0 && actual == expected;
    api.destroy_buffer(destination);
    return success;
}

bool test_bloom_bright_pass(atmera::GraphicsContext& context) {
    const auto& api = context.api();
    if (!api.create_texture || !api.destroy_texture || !api.write_texture || !api.read_texture) return false;
    if (atmera::BloomBrightPass::create(context, nullptr, 0.5f)) return false;
    atmera_texture_handle source = nullptr;
    atmera_texture_handle target = nullptr;
    bool success = false;
    do {
        const atmera_texture_desc desc{sizeof(atmera_texture_desc), 4, 4};
        if (api.create_texture(context.handle(), &desc, &source) != 0 || !source ||
            api.create_texture(context.handle(), &desc, &target) != 0 || !target) break;
        if (atmera::BloomBrightPass::create(context, source, -0.1f)) break;
        std::array<uint8_t, 4 * 4 * 4> input{};
        for (size_t y = 0; y < 4; ++y) for (size_t x = 0; x < 4; ++x) {
            const auto i = (y * 4 + x) * 4;
            input[i] = x < 2 ? 230 : 40;
            input[i + 1] = x < 2 ? 180 : 20;
            input[i + 2] = 10;
            input[i + 3] = 255;
        }
        if (api.write_texture(context.handle(), source, input.data(), 16,
            static_cast<uint32_t>(input.size())) != 0) break;
        auto bright = atmera::BloomBrightPass::create(context, source, 0.5f);
        if (!bright) break;
        atmera::RenderGraph graph;
        const auto pass = graph.add_pass();
        const auto input_resource = graph.add_resource();
        const auto output_resource = graph.add_resource();
        if (!graph.read_resource(pass, input_resource) || !graph.write_resource(pass, output_resource) ||
            !atmera::execute_render_graph(graph, context,
                [&](atmera::RenderGraph::PassId, atmera_command_list_handle list) {
                    return bright->record(list, target);
                })) break;
        std::array<uint8_t, 4 * 4 * 4> pixels{};
        if (api.read_texture(context.handle(), target, pixels.data(), 16,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        const auto left = (2 * 4 + 0) * 4;
        const auto right = (2 * 4 + 3) * 4;
        success = pixels[left] > 200 && pixels[left + 1] > 150 && pixels[left + 2] < 20 &&
            pixels[left + 3] == 255 && pixels[right] < 5 && pixels[right + 1] < 5 &&
            pixels[right + 2] < 5 && pixels[right + 3] == 255;
    } while (false);
    if (target) api.destroy_texture(target);
    if (source) api.destroy_texture(source);
    return success;
}

bool test_bloom_blur_pass(atmera::GraphicsContext& context) {
    const auto& api = context.api();
    if (!api.create_texture || !api.destroy_texture || !api.write_texture || !api.read_texture) return false;
    if (atmera::BloomBlurPass::create(context, nullptr, 5, 5)) return false;
    atmera_texture_handle source = nullptr;
    atmera_texture_handle target = nullptr;
    bool success = false;
    do {
        const atmera_texture_desc desc{sizeof(atmera_texture_desc), 5, 5};
        if (api.create_texture(context.handle(), &desc, &source) != 0 || !source ||
            api.create_texture(context.handle(), &desc, &target) != 0 || !target) break;
        if (atmera::BloomBlurPass::create(context, source, 0, 5)) break;
        std::array<uint8_t, 5 * 5 * 4> input{};
        for (size_t i = 3; i < input.size(); i += 4) input[i] = 255;
        const auto center = (2 * 5 + 2) * 4;
        input[center] = 255;
        input[center + 1] = 255;
        input[center + 2] = 255;
        if (api.write_texture(context.handle(), source, input.data(), 20,
            static_cast<uint32_t>(input.size())) != 0) break;
        auto blur = atmera::BloomBlurPass::create(context, source, 5, 5);
        if (!blur) break;
        atmera::RenderGraph graph;
        const auto pass = graph.add_pass();
        const auto input_resource = graph.add_resource();
        const auto output_resource = graph.add_resource();
        if (!graph.read_resource(pass, input_resource) || !graph.write_resource(pass, output_resource) ||
            !atmera::execute_render_graph(graph, context,
                [&](atmera::RenderGraph::PassId, atmera_command_list_handle list) {
                    return blur->record(list, target);
                })) break;
        std::array<uint8_t, 5 * 5 * 4> pixels{};
        if (api.read_texture(context.handle(), target, pixels.data(), 20,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        const auto neighbor = (2 * 5 + 3) * 4;
        const auto corner = (0 * 5 + 0) * 4;
        success = pixels[center] >= 55 && pixels[center] <= 75 &&
            pixels[neighbor] >= 25 && pixels[neighbor] <= 40 &&
            pixels[corner] < 5 && pixels[center + 3] == 255;
    } while (false);
    if (target) api.destroy_texture(target);
    if (source) api.destroy_texture(source);
    return success;
}

bool test_bloom_composite(atmera::GraphicsContext& context) {
    const auto& api = context.api();
    if (!api.create_texture || !api.destroy_texture || !api.write_texture || !api.read_texture) return false;
    if (atmera::BloomCompositePass::create(context, nullptr, nullptr, 0.5f)) return false;
    atmera_texture_handle original = nullptr, extracted = nullptr, blurred = nullptr, output = nullptr;
    bool success = false;
    do {
        const atmera_texture_desc desc{sizeof(atmera_texture_desc), 5, 5};
        if (api.create_texture(context.handle(), &desc, &original) != 0 || !original ||
            api.create_texture(context.handle(), &desc, &extracted) != 0 || !extracted ||
            api.create_texture(context.handle(), &desc, &blurred) != 0 || !blurred ||
            api.create_texture(context.handle(), &desc, &output) != 0 || !output) break;
        if (atmera::BloomCompositePass::create(context, original, blurred, 1.1f)) break;
        std::array<uint8_t, 5 * 5 * 4> input{};
        for (size_t i = 0; i < input.size(); i += 4) {
            input[i] = 40; input[i + 1] = 40; input[i + 2] = 40; input[i + 3] = 255;
        }
        const auto center = (2 * 5 + 2) * 4;
        input[center] = 220; input[center + 1] = 220; input[center + 2] = 220;
        if (api.write_texture(context.handle(), original, input.data(), 20,
            static_cast<uint32_t>(input.size())) != 0) break;
        auto bright_pass = atmera::BloomBrightPass::create(context, original, 0.5f);
        auto blur_pass = atmera::BloomBlurPass::create(context, extracted, 5, 5);
        auto composite_pass = atmera::BloomCompositePass::create(context, original, blurred, 0.5f);
        if (!bright_pass || !blur_pass || !composite_pass) break;
        atmera::RenderGraph graph;
        const auto bright_step = graph.add_pass();
        const auto blur_step = graph.add_pass();
        const auto composite_step = graph.add_pass();
        const auto original_id = graph.add_resource();
        const auto extracted_id = graph.add_resource();
        const auto blurred_id = graph.add_resource();
        const auto output_id = graph.add_resource();
        if (!graph.read_resource(bright_step, original_id) ||
            !graph.write_resource(bright_step, extracted_id) ||
            !graph.read_resource(blur_step, extracted_id) ||
            !graph.write_resource(blur_step, blurred_id) ||
            !graph.read_resource(composite_step, original_id) ||
            !graph.read_resource(composite_step, blurred_id) ||
            !graph.write_resource(composite_step, output_id) ||
            !atmera::execute_render_graph(graph, context,
                [&](atmera::RenderGraph::PassId pass, atmera_command_list_handle list) {
                    if (pass == bright_step) return bright_pass->record(list, extracted);
                    if (pass == blur_step) return blur_pass->record(list, blurred);
                    if (pass == composite_step) return composite_pass->record(list, output);
                    return false;
                })) break;
        std::array<uint8_t, 5 * 5 * 4> pixels{};
        if (api.read_texture(context.handle(), output, pixels.data(), 20,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        const auto neighbor = (2 * 5 + 3) * 4;
        const auto corner = (0 * 5 + 0) * 4;
        success = pixels[center] >= 240 && pixels[center] <= 255 &&
            pixels[neighbor] >= 50 && pixels[neighbor] <= 60 &&
            pixels[corner] >= 38 && pixels[corner] <= 42 && pixels[center + 3] == 255;
    } while (false);
    if (output) api.destroy_texture(output);
    if (blurred) api.destroy_texture(blurred);
    if (extracted) api.destroy_texture(extracted);
    if (original) api.destroy_texture(original);
    return success;
}

bool test_bloom_downsample(atmera::GraphicsContext& context) {
    const auto& api = context.api();
    if (!api.create_texture || !api.destroy_texture || !api.write_texture || !api.read_texture) return false;
    if (atmera::BloomDownsamplePass::create(context, nullptr)) return false;
    atmera_texture_handle source = nullptr, target = nullptr;
    bool success = false;
    do {
        const atmera_texture_desc source_desc{sizeof(atmera_texture_desc), 8, 8};
        const atmera_texture_desc target_desc{sizeof(atmera_texture_desc), 4, 4};
        if (api.create_texture(context.handle(), &source_desc, &source) != 0 || !source ||
            api.create_texture(context.handle(), &target_desc, &target) != 0 || !target) break;
        std::array<uint8_t, 8 * 8 * 4> input{};
        for (size_t y = 0; y < 8; ++y) for (size_t x = 0; x < 8; ++x) {
            const auto i = (y * 8 + x) * 4;
            input[i] = input[i + 1] = input[i + 2] = (x + y) % 2 == 0 ? 255 : 0;
            input[i + 3] = 255;
        }
        if (api.write_texture(context.handle(), source, input.data(), 32,
            static_cast<uint32_t>(input.size())) != 0) break;
        auto downsample = atmera::BloomDownsamplePass::create(context, source);
        if (!downsample) break;
        atmera::RenderGraph graph;
        const auto pass = graph.add_pass();
        const auto source_id = graph.add_resource();
        const auto target_id = graph.add_resource();
        if (!graph.read_resource(pass, source_id) || !graph.write_resource(pass, target_id) ||
            !atmera::execute_render_graph(graph, context,
                [&](atmera::RenderGraph::PassId, atmera_command_list_handle list) {
                    return downsample->record(list, target);
                })) break;
        std::array<uint8_t, 4 * 4 * 4> pixels{};
        if (api.read_texture(context.handle(), target, pixels.data(), 16,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        success = true;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i] < 115 || pixels[i] > 140 || pixels[i + 1] < 115 ||
                pixels[i + 1] > 140 || pixels[i + 2] < 115 || pixels[i + 2] > 140 ||
                pixels[i + 3] != 255) { success = false; break; }
        }
    } while (false);
    if (target) api.destroy_texture(target);
    if (source) api.destroy_texture(source);
    return success;
}

bool test_bloom_multires(atmera::GraphicsContext& context) {
    const auto& api = context.api();
    atmera_texture_handle original = nullptr, extracted = nullptr, small = nullptr;
    atmera_texture_handle blurred = nullptr, output = nullptr;
    bool success = false;
    do {
        const atmera_texture_desc full{sizeof(atmera_texture_desc), 8, 8};
        const atmera_texture_desc half{sizeof(atmera_texture_desc), 4, 4};
        if (api.create_texture(context.handle(), &full, &original) != 0 || !original ||
            api.create_texture(context.handle(), &full, &extracted) != 0 || !extracted ||
            api.create_texture(context.handle(), &half, &small) != 0 || !small ||
            api.create_texture(context.handle(), &half, &blurred) != 0 || !blurred ||
            api.create_texture(context.handle(), &full, &output) != 0 || !output) break;
        std::array<uint8_t, 8 * 8 * 4> input{};
        for (size_t y = 0; y < 8; ++y) for (size_t x = 0; x < 8; ++x) {
            const auto i = (y * 8 + x) * 4;
            input[i] = input[i + 1] = input[i + 2] =
                (x >= 3 && x <= 4 && y >= 3 && y <= 4) ? 220 : 40;
            input[i + 3] = 255;
        }
        if (api.write_texture(context.handle(), original, input.data(), 32,
            static_cast<uint32_t>(input.size())) != 0) break;
        auto bright = atmera::BloomBrightPass::create(context, original, 0.5f);
        auto downsample = atmera::BloomDownsamplePass::create(context, extracted);
        auto blur = atmera::BloomBlurPass::create(context, small, 4, 4);
        auto composite = atmera::BloomCompositePass::create(context, original, blurred, 0.5f);
        if (!bright || !downsample || !blur || !composite) break;
        atmera::RenderGraph graph;
        const auto p0 = graph.add_pass(), p1 = graph.add_pass(), p2 = graph.add_pass(), p3 = graph.add_pass();
        const auto r0 = graph.add_resource(), r1 = graph.add_resource(), r2 = graph.add_resource();
        const auto r3 = graph.add_resource(), r4 = graph.add_resource();
        if (!graph.read_resource(p0, r0) || !graph.write_resource(p0, r1) ||
            !graph.read_resource(p1, r1) || !graph.write_resource(p1, r2) ||
            !graph.read_resource(p2, r2) || !graph.write_resource(p2, r3) ||
            !graph.read_resource(p3, r0) || !graph.read_resource(p3, r3) ||
            !graph.write_resource(p3, r4) ||
            !atmera::execute_render_graph(graph, context,
                [&](atmera::RenderGraph::PassId pass, atmera_command_list_handle list) {
                    if (pass == p0) return bright->record(list, extracted);
                    if (pass == p1) return downsample->record(list, small);
                    if (pass == p2) return blur->record(list, blurred);
                    if (pass == p3) return composite->record(list, output);
                    return false;
                })) break;
        std::array<uint8_t, 8 * 8 * 4> pixels{};
        if (api.read_texture(context.handle(), output, pixels.data(), 32,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        const auto center = (3 * 8 + 3) * 4;
        const auto adjacent = (3 * 8 + 2) * 4;
        const auto corner = 0;
        success = pixels[center] > 220 && pixels[adjacent] > 40 &&
            pixels[corner] >= 38 && pixels[corner] <= 42 && pixels[center + 3] == 255;
    } while (false);
    if (output) api.destroy_texture(output);
    if (blurred) api.destroy_texture(blurred);
    if (small) api.destroy_texture(small);
    if (extracted) api.destroy_texture(extracted);
    if (original) api.destroy_texture(original);
    return success;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--graph-test") {
        const bool success = test_render_graph();
        std::cout << "Render graph scheduling " << (success ? "passed\n" : "failed\n");
        return success ? 0 : 1;
    }
    std::string backend;
    bool window_mode = false;
    bool hardware = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--backend" && i + 1 < argc) backend = argv[++i];
        else if (arg == "--window") window_mode = true;
        else if (arg == "--headless") window_mode = false;
        else if (arg == "--hardware") hardware = true;
        else { std::cerr << "Usage: ATMERA.ExternalConsumer --backend d3d11|d3d12 [--headless|--window] [--hardware]\n"; return 2; }
    }
    if (backend != "d3d11" && backend != "d3d12") return 2;
    wchar_t exe_path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return 3;
    const auto dll_path = std::filesystem::path(exe_path).parent_path() /
        (backend == "d3d11" ? L"ATMERA.D3D11.dll" : L"ATMERA.D3D12.dll");

    HWND window = nullptr;
    if (window_mode) {
        window = CreateWindowExW(0, L"STATIC", L"ATMERA external consumer",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 128, 128,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) return 4;
    }
    const atmera_render_desc desc{sizeof(atmera_render_desc), window, 128, 128,
        hardware ? 0u : 1u, shader, static_cast<uint32_t>(sizeof(shader) - 1),
        window_mode ? 1u : 0u};
    std::string error;
    auto context = atmera::GraphicsContext::open(dll_path, desc, error);
    if (!context) {
        std::cerr << error << '\n';
        if (window) DestroyWindow(window);
        return 5;
    }
    bool success = test_shader_reflection(*context);
    if (window_mode) {
        std::array<uint8_t, 4> pixel{};
        success = success && context->api().render_frame(context->handle()) == 0 &&
            context->api().get_center_pixel(context->handle(), pixel.data()) == 0 &&
            triangle_pixel(pixel.data());
    } else {
        success = success && draw_offscreen(*context) && copy_temporary_buffer(*context) &&
            test_bloom_bright_pass(*context) && test_bloom_blur_pass(*context) &&
            test_bloom_composite(*context) && test_bloom_downsample(*context) &&
            test_bloom_multires(*context);
        std::array<uint8_t, 4> unused{};
        success = success && context->api().begin_frame(context->handle()) != 0 &&
            context->api().get_center_pixel(context->handle(), unused.data()) != 0;
    }
    std::cout << backend << (window_mode ? " window" : " headless") <<
        (hardware ? " hardware" : " WARP") << (success ? " passed\n" : " failed\n");
    context.reset();
    if (window) DestroyWindow(window);
    return success ? 0 : 6;
}
