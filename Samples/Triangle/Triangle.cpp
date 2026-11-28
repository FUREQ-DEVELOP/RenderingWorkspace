#include <ATMERA/RenderABI.h>
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

struct ColoredVertex { float x, y, r, g, b; };
struct PackedVertex { float x, y; uint8_t color[4]; };
struct ScalarVertex { float x, y, r, g, b, intensity; };

const std::array<ColoredVertex, 3> colored_vertices{{
    {0.0f, 0.65f, 0.9f, 0.2f, 0.1f},
    {0.65f, -0.55f, 0.9f, 0.2f, 0.1f},
    {-0.65f, -0.55f, 0.9f, 0.2f, 0.1f}}};

const std::array<PackedVertex, 3> packed_vertices{{
    {0.0f, 0.65f, {230, 51, 26, 255}},
    {0.65f, -0.55f, {230, 51, 26, 255}},
    {-0.65f, -0.55f, {230, 51, 26, 255}}}};
static_assert(sizeof(PackedVertex) == 12);

const std::array<ScalarVertex, 3> scalar_vertices{{
    {0.0f, 0.65f, 0.9f, 0.2f, 0.1f, 0.5f},
    {0.65f, -0.55f, 0.9f, 0.2f, 0.1f, 0.5f},
    {-0.65f, -0.55f, 0.9f, 0.2f, 0.1f, 0.5f}}};

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool test_resources(const atmera_render_api& api, atmera_render_handle renderer) {
    atmera_buffer_handle buffer = nullptr;
    atmera_texture_handle texture = nullptr;
    bool passed = false;
    do {
        const atmera_buffer_desc buffer_desc{sizeof(atmera_buffer_desc), 64, ATMERA_BUFFER_VERTEX};
        if (api.create_buffer(renderer, &buffer_desc, &buffer) != 0 || !buffer) break;
        std::array<uint8_t, 64> input{};
        std::array<uint8_t, 64> output{};
        for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<uint8_t>(i * 3 + 7);
        if (api.begin_frame(renderer) != 0) break;
        const bool transfer_rejected = api.write_buffer(renderer, buffer, 0, input.data(), 64) != 0;
        const auto clear_frame_result = api.end_frame(renderer);
        if (!transfer_rejected || clear_frame_result != 0) break;
        if (api.write_buffer(renderer, buffer, 0, input.data(), static_cast<uint32_t>(input.size())) != 0) break;
        if (api.read_buffer(renderer, buffer, 0, output.data(), static_cast<uint32_t>(output.size())) != 0 || output != input) break;
        std::array<uint8_t, 8> patch{91, 92, 93, 94, 95, 96, 97, 98};
        if (api.write_buffer(renderer, buffer, 16, patch.data(), static_cast<uint32_t>(patch.size())) != 0) break;
        if (api.read_buffer(renderer, buffer, 16, output.data(), static_cast<uint32_t>(patch.size())) != 0 ||
            std::memcmp(output.data(), patch.data(), patch.size()) != 0) break;
        if (api.read_buffer(renderer, buffer, 62, output.data(), 8) == 0) break;
        api.destroy_buffer(buffer);
        buffer = nullptr;
        bool other_buffers_passed = true;
        for (const auto usage : {ATMERA_BUFFER_INDEX, ATMERA_BUFFER_CONSTANT}) {
            const uint32_t size = usage == ATMERA_BUFFER_CONSTANT ? 256u : 64u;
            const atmera_buffer_desc other_desc{sizeof(atmera_buffer_desc), size, usage};
            if (api.create_buffer(renderer, &other_desc, &buffer) != 0 || !buffer) {
                other_buffers_passed = false; break;
            }
            std::array<uint8_t, 256> source{};
            std::array<uint8_t, 256> copy{};
            for (uint32_t i = 0; i < size; ++i) source[i] = static_cast<uint8_t>(i ^ 0x5a);
            if (api.write_buffer(renderer, buffer, 0, source.data(), size) != 0 ||
                api.read_buffer(renderer, buffer, 0, copy.data(), size) != 0 ||
                std::memcmp(source.data(), copy.data(), size) != 0) {
                other_buffers_passed = false; break;
            }
            if (usage == ATMERA_BUFFER_CONSTANT &&
                api.write_buffer(renderer, buffer, 16, source.data(), 16) == 0) {
                other_buffers_passed = false; break;
            }
            api.destroy_buffer(buffer);
            buffer = nullptr;
        }
        if (!other_buffers_passed) break;

        const atmera_texture_desc texture_desc{sizeof(atmera_texture_desc), 4, 4};
        if (api.create_texture(renderer, &texture_desc, &texture) != 0 || !texture) break;
        std::array<uint8_t, 64> tex_input{};
        std::array<uint8_t, 64> tex_output{};
        for (size_t i = 0; i < tex_input.size(); ++i) tex_input[i] = static_cast<uint8_t>(i * 5 + 3);
        if (api.write_texture(renderer, texture, tex_input.data(), 16, static_cast<uint32_t>(tex_input.size())) != 0) break;
        if (api.read_texture(renderer, texture, tex_output.data(), 16, static_cast<uint32_t>(tex_output.size())) != 0 ||
            tex_output != tex_input) break;
        if (api.write_texture(renderer, texture, tex_input.data(), 12, static_cast<uint32_t>(tex_input.size())) == 0) break;
        std::array<uint8_t, 80> padded_input{};
        std::array<uint8_t, 80> padded_output{};
        for (size_t i = 0; i < padded_input.size(); ++i) padded_input[i] = static_cast<uint8_t>(i + 11);
        if (api.write_texture(renderer, texture, padded_input.data(), 20, static_cast<uint32_t>(padded_input.size())) != 0) break;
        if (api.read_texture(renderer, texture, padded_output.data(), 20, static_cast<uint32_t>(padded_output.size())) != 0) break;
        bool rows_match = true;
        for (size_t row = 0; row < 4; ++row)
            if (std::memcmp(padded_input.data() + row * 20, padded_output.data() + row * 20, 16) != 0)
                rows_match = false;
        if (!rows_match) break;
        passed = true;
    } while (false);
    if (texture) api.destroy_texture(texture);
    if (buffer) api.destroy_buffer(buffer);
    return passed;
}

bool test_commands(const atmera_render_api& api, atmera_render_handle renderer) {
    atmera_buffer_handle source = nullptr;
    atmera_buffer_handle destination = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue) break;
        const atmera_buffer_desc desc{sizeof(atmera_buffer_desc), 64, ATMERA_BUFFER_VERTEX};
        if (api.create_buffer(renderer, &desc, &source) != 0 || !source) break;
        if (api.create_buffer(renderer, &desc, &destination) != 0 || !destination) break;
        std::array<uint8_t, 64> source_data{};
        std::array<uint8_t, 64> zero{};
        for (size_t i = 0; i < source_data.size(); ++i) source_data[i] = static_cast<uint8_t>(i + 17);
        if (api.write_buffer(renderer, source, 0, source_data.data(), 64) != 0 ||
            api.write_buffer(renderer, destination, 0, zero.data(), 64) != 0) break;
        if (api.create_command_list(renderer, &list) != 0 || !list) break;
        if (api.command_copy_buffer(list, destination, 0, source, 0, 8) != 0 ||
            api.command_copy_buffer(list, destination, 16, source, 8, 32) != 0) break;
        if (api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        atmera_fence_handle duplicate = nullptr;
        if (api.submit_command_list(queue, list, &duplicate) == 0) {
            if (duplicate) api.destroy_fence(duplicate);
            break;
        }
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        std::array<uint8_t, 64> result{};
        if (api.read_buffer(renderer, destination, 0, result.data(), 64) != 0) break;
        std::array<uint8_t, 64> expected{};
        std::memcpy(expected.data(), source_data.data(), 8);
        std::memcpy(expected.data() + 16, source_data.data() + 8, 32);
        if (result != expected) break;
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (destination) api.destroy_buffer(destination);
    if (source) api.destroy_buffer(source);
    return passed;
}

bool test_texture_commands(const atmera_render_api& api, atmera_render_handle renderer) {
    atmera_texture_handle source = nullptr;
    atmera_texture_handle destination = nullptr;
    atmera_texture_handle wrong_size = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue) break;
        const atmera_texture_desc desc{sizeof(atmera_texture_desc), 4, 4};
        const atmera_texture_desc other_desc{sizeof(atmera_texture_desc), 2, 2};
        if (api.create_texture(renderer, &desc, &source) != 0 || !source ||
            api.create_texture(renderer, &desc, &destination) != 0 || !destination ||
            api.create_texture(renderer, &other_desc, &wrong_size) != 0 || !wrong_size) break;
        std::array<uint8_t, 64> input{};
        std::array<uint8_t, 64> output{};
        for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<uint8_t>(i * 7 + 3);
        if (api.write_texture(renderer, source, input.data(), 16, 64) != 0) break;
        if (api.create_command_list(renderer, &list) != 0 || !list) break;
        if (api.command_copy_texture(list, source, source) == 0 ||
            api.command_copy_texture(list, wrong_size, source) == 0) break;
        if (api.command_copy_texture(list, destination, source) != 0) break;
        if (api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0 ||
            api.read_texture(renderer, destination, output.data(), 16, 64) != 0 ||
            output != input) break;
        api.destroy_fence(fence);
        fence = nullptr;
        std::array<uint8_t, 64> zero{};
        std::array<uint8_t, 16> small_input{};
        for (size_t i = 0; i < small_input.size(); ++i)
            small_input[i] = static_cast<uint8_t>(i * 9 + 5);
        if (api.write_texture(renderer, destination, zero.data(), 16, 64) != 0 ||
            api.write_texture(renderer, wrong_size, small_input.data(), 8, 16) != 0 ||
            api.create_command_list(renderer, &list) != 0 || !list) break;
        if (api.command_copy_texture_region(list, destination, 0, 0, source, 0, 0, 0, 2) == 0 ||
            api.command_copy_texture_region(list, destination, 3, 2, source, 0, 0, 2, 2) == 0 ||
            api.command_copy_texture_region(list, destination, 0, 0, source, 3, 0, 2, 2) == 0 ||
            api.command_copy_texture_region(list, destination, 0, 0, source, 0, 0, 5, 1) == 0) break;
        if (api.command_copy_texture_region(list, destination, 1, 2, source, 0, 1, 2, 2) != 0 ||
            api.command_copy_texture_region(list, destination, 2, 0, wrong_size, 0, 0, 2, 2) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0 ||
            api.read_texture(renderer, destination, output.data(), 16, 64) != 0) break;
        std::array<uint8_t, 64> expected{};
        for (size_t y = 0; y < 2; ++y)
            for (size_t x = 0; x < 2; ++x)
                std::memcpy(expected.data() + ((y + 2) * 4 + x + 1) * 4,
                    input.data() + ((y + 1) * 4 + x) * 4, 4);
        for (size_t y = 0; y < 2; ++y)
            for (size_t x = 0; x < 2; ++x)
                std::memcpy(expected.data() + (y * 4 + x + 2) * 4,
                    small_input.data() + (y * 2 + x) * 4, 4);
        if (output != expected) break;
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (wrong_size) api.destroy_texture(wrong_size);
    if (destination) api.destroy_texture(destination);
    if (source) api.destroy_texture(source);
    return passed;
}

bool test_command_draw(const atmera_render_api& api, atmera_render_handle renderer,
    atmera_pipeline_handle pipeline) {
    atmera_texture_handle target = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue) break;
        const atmera_texture_desc desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_texture(renderer, &desc, &target) != 0 || !target) break;
        if (api.create_command_list(renderer, &list) != 0 || !list) break;
        if (api.command_draw(list, 3) == 0) break;
        if (api.command_begin_render_target(list, target) != 0) break;
        atmera_fence_handle premature = nullptr;
        if (api.submit_command_list(queue, list, &premature) == 0) {
            if (premature) api.destroy_fence(premature);
            break;
        }
        if (api.command_bind_pipeline(list, pipeline) != 0 || api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0) break;
        if (api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        std::array<uint8_t, 64 * 64 * 4> pixels{};
        if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        const size_t center = (32 * 64 + 32) * 4;
        if (pixels[center] < 40 || pixels[center + 1] < 40 || pixels[center + 2] < 40 ||
            pixels[center + 3] != 255) break;
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (target) api.destroy_texture(target);
    return passed;
}

bool test_scalar_vertex_attribute(const atmera_render_api& api, atmera_render_handle renderer,
    const std::string& source) {
    atmera_shader_handle vertex_shader = nullptr;
    atmera_shader_handle pixel_shader = nullptr;
    atmera_pipeline_handle pipeline = nullptr;
    atmera_buffer_handle buffer = nullptr;
    atmera_texture_handle target = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        const atmera_shader_desc vs_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            source.data(), static_cast<uint32_t>(source.size()), "VSScalar"};
        const atmera_shader_desc ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "PSMain"};
        if (api.create_shader(renderer, &vs_desc, &vertex_shader) != 0 || !vertex_shader ||
            api.create_shader(renderer, &ps_desc, &pixel_shader) != 0 || !pixel_shader) break;
        const atmera_vertex_attribute_desc attrs[] = {
            {"POSITION", 0, ATMERA_VERTEX_FLOAT2, 0},
            {"COLOR", 0, ATMERA_VERTEX_FLOAT3, 8},
            {"TEXCOORD", 0, ATMERA_VERTEX_FLOAT1, 20}};
        const atmera_vertex_layout_desc layout{sizeof(atmera_vertex_layout_desc), 3, attrs};
        if (api.create_pipeline_with_vertex_layout(renderer, vertex_shader, pixel_shader,
            &layout, &pipeline) != 0 || !pipeline) break;
        const atmera_buffer_desc buffer_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(scalar_vertices)), ATMERA_BUFFER_VERTEX};
        const atmera_texture_desc texture_desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_buffer(renderer, &buffer_desc, &buffer) != 0 || !buffer ||
            api.write_buffer(renderer, buffer, 0, scalar_vertices.data(),
                static_cast<uint32_t>(sizeof(scalar_vertices))) != 0 ||
            api.create_texture(renderer, &texture_desc, &target) != 0 || !target ||
            api.create_command_list(renderer, &list) != 0 || !list) break;
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, pipeline) != 0 ||
            api.command_bind_vertex_buffer(list, buffer, 20, 0) == 0 ||
            api.command_bind_vertex_buffer(list, buffer, sizeof(ScalarVertex), 0) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        std::array<uint8_t, 64 * 64 * 4> pixels{};
        if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        const size_t center = (32 * 64 + 32) * 4;
        if (pixels[center] < 113 || pixels[center] > 116 ||
            pixels[center + 1] < 24 || pixels[center + 1] > 27 ||
            pixels[center + 2] < 11 || pixels[center + 2] > 14 ||
            pixels[center + 3] != 255) break;
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (target) api.destroy_texture(target);
    if (buffer) api.destroy_buffer(buffer);
    if (pipeline) api.destroy_pipeline(pipeline);
    if (pixel_shader) api.destroy_shader(pixel_shader);
    if (vertex_shader) api.destroy_shader(vertex_shader);
    return passed;
}

bool test_packed_vertex_color(const atmera_render_api& api, atmera_render_handle renderer,
    const std::string& source) {
    atmera_shader_handle vertex_shader = nullptr;
    atmera_shader_handle pixel_shader = nullptr;
    atmera_pipeline_handle pipeline = nullptr;
    atmera_buffer_handle buffer = nullptr;
    atmera_texture_handle target = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        const atmera_shader_desc vs_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            source.data(), static_cast<uint32_t>(source.size()), "VSBuffer"};
        const atmera_shader_desc ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "PSMain"};
        if (api.create_shader(renderer, &vs_desc, &vertex_shader) != 0 || !vertex_shader ||
            api.create_shader(renderer, &ps_desc, &pixel_shader) != 0 || !pixel_shader) break;
        const atmera_vertex_attribute_desc attrs[] = {
            {"POSITION", 0, ATMERA_VERTEX_FLOAT2, 0},
            {"COLOR", 0, ATMERA_VERTEX_UNORM8X4, 8}};
        const atmera_vertex_layout_desc layout{sizeof(atmera_vertex_layout_desc), 2, attrs};
        if (api.create_pipeline_with_vertex_layout(renderer, vertex_shader, pixel_shader,
            &layout, &pipeline) != 0 || !pipeline) break;
        const atmera_buffer_desc buffer_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(packed_vertices)), ATMERA_BUFFER_VERTEX};
        const atmera_texture_desc texture_desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_buffer(renderer, &buffer_desc, &buffer) != 0 || !buffer ||
            api.write_buffer(renderer, buffer, 0, packed_vertices.data(),
                static_cast<uint32_t>(sizeof(packed_vertices))) != 0 ||
            api.create_texture(renderer, &texture_desc, &target) != 0 || !target ||
            api.create_command_list(renderer, &list) != 0 || !list) break;
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, pipeline) != 0 ||
            api.command_bind_vertex_buffer(list, buffer, 11, 0) == 0 ||
            api.command_bind_vertex_buffer(list, buffer, sizeof(PackedVertex), 0) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        std::array<uint8_t, 64 * 64 * 4> pixels{};
        if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        const size_t center = (32 * 64 + 32) * 4;
        if (pixels[center] < 228 || pixels[center] > 231 ||
            pixels[center + 1] < 50 || pixels[center + 1] > 52 ||
            pixels[center + 2] < 25 || pixels[center + 2] > 27 ||
            pixels[center + 3] != 255) break;
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (target) api.destroy_texture(target);
    if (buffer) api.destroy_buffer(buffer);
    if (pipeline) api.destroy_pipeline(pipeline);
    if (pixel_shader) api.destroy_shader(pixel_shader);
    if (vertex_shader) api.destroy_shader(vertex_shader);
    return passed;
}

bool test_compressed_uv(const atmera_render_api& api, atmera_render_handle renderer,
    const std::string& source) {
    struct UvVertex { float x, y; uint16_t u, v; };
    struct ByteUvVertex { float x, y; uint8_t u, v; };
    static_assert(sizeof(UvVertex) == 12);
    static_assert(sizeof(ByteUvVertex) == 12);
    std::array<UvVertex, 3> vertices{{
        {0.0f, 0.65f, 0, 0}, {0.65f, -0.55f, 0, 0}, {-0.65f, -0.55f, 0, 0}}};
    std::array<ByteUvVertex, 3> byte_vertices{{
        {0.0f, 0.65f, 0, 0}, {0.65f, -0.55f, 0, 0}, {-0.65f, -0.55f, 0, 0}}};
    const std::array<std::array<float, 2>, 3> positions{{
        {0.0f, 0.65f}, {0.65f, -0.55f}, {-0.65f, -0.55f}}};
    const std::array<std::array<uint16_t, 2>, 3> right_uvs{{
        {65535, 0}, {65535, 0}, {65535, 0}}};
    atmera_shader_handle vertex_shader = nullptr;
    atmera_shader_handle pixel_shader = nullptr;
    atmera_pipeline_handle pipeline = nullptr;
    atmera_pipeline_handle two_pipeline = nullptr;
    atmera_shader_handle color_uv_shader = nullptr;
    atmera_shader_handle color_pixel_shader = nullptr;
    atmera_pipeline_handle three_pipeline = nullptr;
    atmera_pipeline_handle byte_pipeline = nullptr;
    atmera_pipeline_handle byte_three_pipeline = nullptr;
    atmera_buffer_handle buffer = nullptr;
    atmera_buffer_handle position_buffer = nullptr;
    atmera_buffer_handle uv_buffer = nullptr;
    atmera_buffer_handle color_buffer = nullptr;
    atmera_buffer_handle byte_buffer = nullptr;
    atmera_buffer_handle byte_uv_buffer = nullptr;
    atmera_texture_handle texture = nullptr;
    atmera_texture_handle target = nullptr;
    atmera_texture_view_handle view = nullptr;
    atmera_sampler_handle sampler = nullptr;
    atmera_binding_set_handle bindings = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        const atmera_shader_desc vs_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            source.data(), static_cast<uint32_t>(source.size()), "VSUv"};
        const atmera_shader_desc ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "TexturedPS"};
        const atmera_shader_desc color_vs_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            source.data(), static_cast<uint32_t>(source.size()), "VSColorUv"};
        const atmera_shader_desc color_ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "TexturedColorPS"};
        if (api.create_shader(renderer, &vs_desc, &vertex_shader) != 0 || !vertex_shader ||
            api.create_shader(renderer, &ps_desc, &pixel_shader) != 0 || !pixel_shader ||
            api.create_shader(renderer, &color_vs_desc, &color_uv_shader) != 0 || !color_uv_shader ||
            api.create_shader(renderer, &color_ps_desc, &color_pixel_shader) != 0 || !color_pixel_shader) break;
        const atmera_vertex_attribute_desc attrs[] = {
            {"POSITION", 0, ATMERA_VERTEX_FLOAT2, 0},
            {"TEXCOORD", 0, ATMERA_VERTEX_UNORM16X2, 8}};
        const atmera_vertex_layout_desc layout{sizeof(atmera_vertex_layout_desc), 2, attrs};
        const atmera_vertex_layout_desc position_layout{sizeof(atmera_vertex_layout_desc), 1, &attrs[0]};
        const atmera_vertex_attribute_desc uv_attr{"TEXCOORD", 0, ATMERA_VERTEX_UNORM16X2, 0};
        const atmera_vertex_layout_desc uv_layout{sizeof(atmera_vertex_layout_desc), 1, &uv_attr};
        const atmera_vertex_attribute_desc color_attr{"COLOR", 0, ATMERA_VERTEX_UNORM8X4, 0};
        const atmera_vertex_layout_desc color_layout{sizeof(atmera_vertex_layout_desc), 1, &color_attr};
        const atmera_vertex_attribute_desc byte_attrs[] = {
            {"POSITION", 0, ATMERA_VERTEX_FLOAT2, 0},
            {"TEXCOORD", 0, ATMERA_VERTEX_UNORM8X2, 8}};
        const atmera_vertex_layout_desc byte_layout{sizeof(atmera_vertex_layout_desc), 2, byte_attrs};
        const atmera_vertex_attribute_desc byte_uv_attr{"TEXCOORD", 0, ATMERA_VERTEX_UNORM8X2, 0};
        const atmera_vertex_layout_desc byte_uv_layout{sizeof(atmera_vertex_layout_desc), 1, &byte_uv_attr};
        if (api.create_pipeline_with_vertex_layout(renderer, vertex_shader, pixel_shader,
            &layout, &pipeline) != 0 || !pipeline ||
            api.create_pipeline_with_two_vertex_streams(renderer, vertex_shader, pixel_shader,
            &position_layout, &uv_layout, &two_pipeline) != 0 || !two_pipeline ||
            api.create_pipeline_with_three_vertex_streams(renderer, color_uv_shader, color_pixel_shader,
            &position_layout, &color_layout, &uv_layout, &three_pipeline) != 0 || !three_pipeline ||
            api.create_pipeline_with_vertex_layout(renderer, vertex_shader, pixel_shader,
            &byte_layout, &byte_pipeline) != 0 || !byte_pipeline ||
            api.create_pipeline_with_three_vertex_streams(renderer, color_uv_shader, color_pixel_shader,
            &position_layout, &color_layout, &byte_uv_layout, &byte_three_pipeline) != 0 ||
            !byte_three_pipeline) break;
        const atmera_buffer_desc buffer_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(vertices)), ATMERA_BUFFER_VERTEX};
        const atmera_buffer_desc position_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(positions)), ATMERA_BUFFER_VERTEX};
        const atmera_buffer_desc uv_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(right_uvs)), ATMERA_BUFFER_VERTEX};
        const std::array<std::array<uint8_t, 4>, 3> colors{{
            {255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}}};
        const atmera_buffer_desc color_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(colors)), ATMERA_BUFFER_VERTEX};
        const std::array<std::array<uint8_t, 2>, 3> byte_uvs{{{255, 0}, {255, 0}, {255, 0}}};
        const atmera_buffer_desc byte_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(byte_vertices)), ATMERA_BUFFER_VERTEX};
        const atmera_buffer_desc byte_uv_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(byte_uvs)), ATMERA_BUFFER_VERTEX};
        const atmera_texture_desc source_desc{sizeof(atmera_texture_desc), 2, 1};
        const atmera_texture_desc target_desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_buffer(renderer, &buffer_desc, &buffer) != 0 || !buffer ||
            api.create_buffer(renderer, &position_desc, &position_buffer) != 0 || !position_buffer ||
            api.create_buffer(renderer, &uv_desc, &uv_buffer) != 0 || !uv_buffer ||
            api.create_buffer(renderer, &color_desc, &color_buffer) != 0 || !color_buffer ||
            api.create_buffer(renderer, &byte_desc, &byte_buffer) != 0 || !byte_buffer ||
            api.create_buffer(renderer, &byte_uv_desc, &byte_uv_buffer) != 0 || !byte_uv_buffer ||
            api.write_buffer(renderer, buffer, 0, vertices.data(), sizeof(vertices)) != 0 ||
            api.write_buffer(renderer, position_buffer, 0, positions.data(), sizeof(positions)) != 0 ||
            api.write_buffer(renderer, uv_buffer, 0, right_uvs.data(), sizeof(right_uvs)) != 0 ||
            api.write_buffer(renderer, color_buffer, 0, colors.data(), sizeof(colors)) != 0 ||
            api.write_buffer(renderer, byte_buffer, 0, byte_vertices.data(), sizeof(byte_vertices)) != 0 ||
            api.write_buffer(renderer, byte_uv_buffer, 0, byte_uvs.data(), sizeof(byte_uvs)) != 0 ||
            api.create_texture(renderer, &source_desc, &texture) != 0 || !texture ||
            api.create_texture(renderer, &target_desc, &target) != 0 || !target) break;
        const std::array<uint8_t, 8> texels{200, 30, 20, 255, 20, 180, 40, 255};
        const atmera_sampler_desc sampler_desc{sizeof(atmera_sampler_desc),
            ATMERA_FILTER_POINT, ATMERA_ADDRESS_CLAMP};
        if (api.write_texture(renderer, texture, texels.data(), 8, 8) != 0 ||
            api.create_texture_view(renderer, texture, &view) != 0 || !view ||
            api.create_sampler(renderer, &sampler_desc, &sampler) != 0 || !sampler ||
            api.create_binding_set(renderer, view, sampler, &bindings) != 0 || !bindings ||
            api.create_command_list(renderer, &list) != 0 || !list) break;
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, pipeline) != 0 ||
            api.command_bind_vertex_buffer(list, buffer, 11, 0) == 0 ||
            api.command_bind_vertex_buffer(list, buffer, 12, 0) != 0 ||
            api.command_bind_binding_set(list, bindings) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        api.destroy_fence(fence);
        fence = nullptr;
        std::array<uint8_t, 64 * 64 * 4> pixels{};
        const size_t center = (32 * 64 + 32) * 4;
        if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
            static_cast<uint32_t>(pixels.size())) != 0 ||
            pixels[center] != 200 || pixels[center + 1] != 30 ||
            pixels[center + 2] != 20 || pixels[center + 3] != 255) break;
        if (api.create_command_list(renderer, &list) != 0 || !list ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, two_pipeline) != 0 ||
            api.command_bind_vertex_buffer(list, position_buffer, 8, 0) != 0 ||
            api.command_bind_vertex_buffer_slot1(list, uv_buffer, 3, 0) == 0 ||
            api.command_bind_vertex_buffer_slot1(list, uv_buffer, 4, 0) != 0 ||
            api.command_bind_binding_set(list, bindings) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        api.destroy_fence(fence);
        fence = nullptr;
        if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
            static_cast<uint32_t>(pixels.size())) != 0 ||
            pixels[center] != 20 || pixels[center + 1] != 180 ||
            pixels[center + 2] != 40 || pixels[center + 3] != 255) break;
        const std::array<std::array<uint16_t, 2>, 3> left_uvs{{{0, 0}, {0, 0}, {0, 0}}};
        if (api.write_buffer(renderer, uv_buffer, 0, left_uvs.data(), sizeof(left_uvs)) != 0 ||
            api.create_command_list(renderer, &list) != 0 || !list ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, three_pipeline) != 0 ||
            api.command_bind_vertex_buffer(list, position_buffer, 8, 0) != 0 ||
            api.command_bind_vertex_buffer_slot1(list, color_buffer, 4, 0) != 0 ||
            api.command_draw(list, 3) == 0 ||
            api.command_bind_vertex_buffer_slot2(list, uv_buffer, 3, 0) == 0 ||
            api.command_bind_vertex_buffer_slot2(list, uv_buffer, 4, 0) != 0 ||
            api.command_bind_binding_set(list, bindings) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        api.destroy_fence(fence);
        fence = nullptr;
        if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
            static_cast<uint32_t>(pixels.size())) != 0 ||
            pixels[center] != 200 || pixels[center + 1] != 30 ||
            pixels[center + 2] != 20 || pixels[center + 3] != 255) break;
        if (api.write_buffer(renderer, uv_buffer, 0, right_uvs.data(), sizeof(right_uvs)) != 0) break;
        for (auto& vertex : vertices) vertex.u = 65535;
        if (api.write_buffer(renderer, buffer, 0, vertices.data(), sizeof(vertices)) != 0 ||
            api.begin_frame(renderer) != 0) break;
        uint32_t frame_result = api.bind_pipeline(renderer, pipeline);
        if (frame_result == 0) frame_result = api.bind_vertex_buffer(renderer, buffer, 12, 0);
        if (frame_result == 0) frame_result = api.bind_binding_set(renderer, bindings);
        if (frame_result == 0) frame_result = api.draw(renderer, 3);
        const uint32_t first_frame_end = api.end_frame(renderer);
        uint8_t frame_pixel[4]{};
        if (frame_result != 0 || first_frame_end != 0 ||
            api.get_center_pixel(renderer, frame_pixel) != 0 ||
            frame_pixel[0] != 20 || frame_pixel[1] != 180 ||
            frame_pixel[2] != 40 || frame_pixel[3] != 255 ||
            api.begin_frame(renderer) != 0) break;
        frame_result = api.bind_pipeline(renderer, three_pipeline);
        if (frame_result == 0) frame_result = api.bind_vertex_buffer(renderer, position_buffer, 8, 0);
        if (frame_result == 0) frame_result = api.bind_vertex_buffer_slot1(renderer, color_buffer, 4, 0);
        if (frame_result == 0 && api.draw(renderer, 3) == 0) frame_result = 1;
        if (frame_result == 0) frame_result = api.bind_vertex_buffer_slot2(renderer, uv_buffer, 4, 0);
        if (frame_result == 0) frame_result = api.bind_binding_set(renderer, bindings);
        if (frame_result == 0) frame_result = api.draw(renderer, 3);
        const uint32_t frame_end = api.end_frame(renderer);
        passed = frame_result == 0 && frame_end == 0 &&
            api.get_center_pixel(renderer, frame_pixel) == 0 &&
            frame_pixel[0] == 20 && frame_pixel[1] == 180 &&
            frame_pixel[2] == 40 && frame_pixel[3] == 255;
        if (!passed) break;
        if (api.create_command_list(renderer, &list) != 0 || !list ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, byte_pipeline) != 0 ||
            api.command_bind_vertex_buffer(list, byte_buffer, 9, 0) == 0 ||
            api.command_bind_vertex_buffer(list, byte_buffer, sizeof(ByteUvVertex), 0) != 0 ||
            api.command_bind_binding_set(list, bindings) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) { passed = false; break; }
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) { passed = false; break; }
        api.destroy_fence(fence);
        fence = nullptr;
        if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
            static_cast<uint32_t>(pixels.size())) != 0 ||
            pixels[center] != 200 || pixels[center + 1] != 30 ||
            pixels[center + 2] != 20 || pixels[center + 3] != 255 ||
            api.begin_frame(renderer) != 0) { passed = false; break; }
        frame_result = api.bind_pipeline(renderer, byte_three_pipeline);
        if (frame_result == 0) frame_result = api.bind_vertex_buffer(renderer, position_buffer, 8, 0);
        if (frame_result == 0) frame_result = api.bind_vertex_buffer_slot1(renderer, color_buffer, 4, 0);
        if (frame_result == 0 && api.draw(renderer, 3) == 0) frame_result = 1;
        if (frame_result == 0 && api.bind_vertex_buffer_slot2(renderer, byte_uv_buffer, 1, 0) == 0)
            frame_result = 1;
        if (frame_result == 0) frame_result = api.bind_vertex_buffer_slot2(renderer, byte_uv_buffer, 2, 0);
        if (frame_result == 0) frame_result = api.bind_binding_set(renderer, bindings);
        if (frame_result == 0) frame_result = api.draw(renderer, 3);
        const uint32_t byte_frame_end = api.end_frame(renderer);
        passed = frame_result == 0 && byte_frame_end == 0 &&
            api.get_center_pixel(renderer, frame_pixel) == 0 &&
            frame_pixel[0] == 20 && frame_pixel[1] == 180 &&
            frame_pixel[2] == 40 && frame_pixel[3] == 255;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (bindings) api.destroy_binding_set(bindings);
    if (sampler) api.destroy_sampler(sampler);
    if (view) api.destroy_texture_view(view);
    if (target) api.destroy_texture(target);
    if (texture) api.destroy_texture(texture);
    if (uv_buffer) api.destroy_buffer(uv_buffer);
    if (byte_uv_buffer) api.destroy_buffer(byte_uv_buffer);
    if (byte_buffer) api.destroy_buffer(byte_buffer);
    if (color_buffer) api.destroy_buffer(color_buffer);
    if (position_buffer) api.destroy_buffer(position_buffer);
    if (buffer) api.destroy_buffer(buffer);
    if (two_pipeline) api.destroy_pipeline(two_pipeline);
    if (three_pipeline) api.destroy_pipeline(three_pipeline);
    if (byte_three_pipeline) api.destroy_pipeline(byte_three_pipeline);
    if (byte_pipeline) api.destroy_pipeline(byte_pipeline);
    if (pipeline) api.destroy_pipeline(pipeline);
    if (pixel_shader) api.destroy_shader(pixel_shader);
    if (color_pixel_shader) api.destroy_shader(color_pixel_shader);
    if (color_uv_shader) api.destroy_shader(color_uv_shader);
    if (vertex_shader) api.destroy_shader(vertex_shader);
    return passed;
}

bool test_two_vertex_streams(const atmera_render_api& api, atmera_render_handle renderer,
    const std::string& source) {
    atmera_shader_handle vertex_shader = nullptr;
    atmera_shader_handle pixel_shader = nullptr;
    atmera_pipeline_handle pipeline = nullptr;
    atmera_buffer_handle positions = nullptr;
    atmera_buffer_handle colors = nullptr;
    atmera_texture_handle target = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        const atmera_shader_desc vs_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            source.data(), static_cast<uint32_t>(source.size()), "VSBuffer"};
        const atmera_shader_desc ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "PSMain"};
        if (api.create_shader(renderer, &vs_desc, &vertex_shader) != 0 || !vertex_shader ||
            api.create_shader(renderer, &ps_desc, &pixel_shader) != 0 || !pixel_shader) break;
        const atmera_vertex_attribute_desc position_attr{"POSITION", 0, ATMERA_VERTEX_FLOAT2, 0};
        const atmera_vertex_attribute_desc color_attr{"COLOR", 0, ATMERA_VERTEX_UNORM8X4, 0};
        const atmera_vertex_layout_desc position_layout{sizeof(atmera_vertex_layout_desc), 1, &position_attr};
        const atmera_vertex_layout_desc color_layout{sizeof(atmera_vertex_layout_desc), 1, &color_attr};
        if (api.create_pipeline_with_two_vertex_streams(renderer, vertex_shader, pixel_shader,
            &position_layout, &color_layout, &pipeline) != 0 || !pipeline) break;
        const std::array<std::array<float, 2>, 3> position_data{{
            {0.0f, 0.65f}, {0.65f, -0.55f}, {-0.65f, -0.55f}}};
        const std::array<std::array<uint8_t, 4>, 3> color_data{{
            {230, 51, 26, 255}, {230, 51, 26, 255}, {230, 51, 26, 255}}};
        const atmera_buffer_desc position_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(position_data)), ATMERA_BUFFER_VERTEX};
        const atmera_buffer_desc color_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(color_data)), ATMERA_BUFFER_VERTEX};
        const atmera_texture_desc texture_desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_buffer(renderer, &position_desc, &positions) != 0 || !positions ||
            api.create_buffer(renderer, &color_desc, &colors) != 0 || !colors ||
            api.write_buffer(renderer, positions, 0, position_data.data(), sizeof(position_data)) != 0 ||
            api.write_buffer(renderer, colors, 0, color_data.data(), sizeof(color_data)) != 0 ||
            api.create_texture(renderer, &texture_desc, &target) != 0 || !target ||
            api.create_command_list(renderer, &list) != 0 || !list) break;
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, pipeline) != 0 ||
            api.command_bind_vertex_buffer(list, positions, 8, 0) != 0 ||
            api.command_draw(list, 3) == 0 ||
            api.command_bind_vertex_buffer_slot1(list, colors, 3, 0) == 0 ||
            api.command_bind_vertex_buffer_slot1(list, colors, 4, 0) != 0 ||
            api.command_draw(list, 4) == 0 || api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        std::array<uint8_t, 64 * 64 * 4> pixels{};
        if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        const size_t center = (32 * 64 + 32) * 4;
        if (pixels[center] < 228 || pixels[center] > 231 ||
            pixels[center + 1] < 50 || pixels[center + 1] > 52 ||
            pixels[center + 2] < 25 || pixels[center + 2] > 27 ||
            pixels[center + 3] != 255) break;
        if (api.begin_frame(renderer) != 0) break;
        uint32_t frame_result = api.bind_pipeline(renderer, pipeline);
        if (frame_result == 0) frame_result = api.bind_vertex_buffer(renderer, positions, 8, 0);
        if (frame_result == 0 && api.draw(renderer, 3) == 0) frame_result = 1;
        if (frame_result == 0 && api.bind_vertex_buffer_slot1(renderer, colors, 3, 0) == 0)
            frame_result = 1;
        if (frame_result == 0) frame_result = api.bind_vertex_buffer_slot1(renderer, colors, 4, 0);
        if (frame_result == 0) frame_result = api.draw(renderer, 3);
        const uint32_t frame_end = api.end_frame(renderer);
        if (frame_result != 0 || frame_end != 0) break;
        uint8_t frame_pixel[4]{};
        if (api.get_center_pixel(renderer, frame_pixel) != 0 ||
            frame_pixel[0] < 228 || frame_pixel[0] > 231 ||
            frame_pixel[1] < 50 || frame_pixel[1] > 52 ||
            frame_pixel[2] < 25 || frame_pixel[2] > 27) break;
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (target) api.destroy_texture(target);
    if (colors) api.destroy_buffer(colors);
    if (positions) api.destroy_buffer(positions);
    if (pipeline) api.destroy_pipeline(pipeline);
    if (pixel_shader) api.destroy_shader(pixel_shader);
    if (vertex_shader) api.destroy_shader(vertex_shader);
    return passed;
}

bool test_half_vertex_attributes(const atmera_render_api& api, atmera_render_handle renderer,
    const std::string& source) {
    struct HalfPosition { uint16_t x, y; };
    struct HalfColor { uint16_t r, g, b, a; };
    struct HalfVertex { HalfPosition position; HalfColor color; };
    struct PackedVertex { HalfPosition position; uint8_t color[4]; };
    static_assert(sizeof(HalfPosition) == 4 && sizeof(HalfColor) == 8 && sizeof(HalfVertex) == 12);
    static_assert(sizeof(PackedVertex) == 8);
    const std::array<HalfPosition, 3> half_positions{{
        {0x0000, 0x3800}, {0x3800, 0xb800}, {0xb800, 0xb800}}};
    const HalfColor red{0x3b33, 0x3266, 0x2e66, 0x3c00};
    const std::array<HalfColor, 3> colors{{red, red, red}};
    const std::array<HalfVertex, 3> interleaved{{
        {half_positions[0], red}, {half_positions[1], red}, {half_positions[2], red}}};
    const std::array<PackedVertex, 3> packed{{
        {half_positions[0], {230, 51, 26, 255}},
        {half_positions[1], {230, 51, 26, 255}},
        {half_positions[2], {230, 51, 26, 255}}}};
    const std::array<std::array<uint8_t, 4>, 3> packed_colors{{
        {230, 51, 26, 255}, {230, 51, 26, 255}, {230, 51, 26, 255}}};
    atmera_shader_handle vertex_shader = nullptr;
    atmera_shader_handle pixel_shader = nullptr;
    atmera_pipeline_handle single_pipeline = nullptr;
    atmera_pipeline_handle two_pipeline = nullptr;
    atmera_pipeline_handle packed_pipeline = nullptr;
    atmera_pipeline_handle packed_two_pipeline = nullptr;
    atmera_buffer_handle single_buffer = nullptr;
    atmera_buffer_handle position_buffer = nullptr;
    atmera_buffer_handle color_buffer = nullptr;
    atmera_buffer_handle packed_buffer = nullptr;
    atmera_buffer_handle packed_color_buffer = nullptr;
    atmera_texture_handle target = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        const atmera_shader_desc vs_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            source.data(), static_cast<uint32_t>(source.size()), "VSBuffer"};
        const atmera_shader_desc ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "PSMain"};
        if (api.create_shader(renderer, &vs_desc, &vertex_shader) != 0 || !vertex_shader ||
            api.create_shader(renderer, &ps_desc, &pixel_shader) != 0 || !pixel_shader) break;
        const atmera_vertex_attribute_desc single_attrs[] = {
            {"POSITION", 0, ATMERA_VERTEX_HALF2, 0},
            {"COLOR", 0, ATMERA_VERTEX_HALF4, 4}};
        const atmera_vertex_attribute_desc position_attr{"POSITION", 0, ATMERA_VERTEX_HALF2, 0};
        const atmera_vertex_attribute_desc color_attr{"COLOR", 0, ATMERA_VERTEX_HALF4, 0};
        const atmera_vertex_attribute_desc packed_attrs[] = {
            {"POSITION", 0, ATMERA_VERTEX_HALF2, 0},
            {"COLOR", 0, ATMERA_VERTEX_UNORM8X4, 4}};
        const atmera_vertex_attribute_desc packed_color_attr{"COLOR", 0, ATMERA_VERTEX_UNORM8X4, 0};
        const atmera_vertex_layout_desc single_layout{sizeof(atmera_vertex_layout_desc), 2, single_attrs};
        const atmera_vertex_layout_desc position_layout{sizeof(atmera_vertex_layout_desc), 1, &position_attr};
        const atmera_vertex_layout_desc color_layout{sizeof(atmera_vertex_layout_desc), 1, &color_attr};
        const atmera_vertex_layout_desc packed_layout{sizeof(atmera_vertex_layout_desc), 2, packed_attrs};
        const atmera_vertex_layout_desc packed_color_layout{sizeof(atmera_vertex_layout_desc), 1, &packed_color_attr};
        if (api.create_pipeline_with_vertex_layout(renderer, vertex_shader, pixel_shader,
                &single_layout, &single_pipeline) != 0 || !single_pipeline ||
            api.create_pipeline_with_two_vertex_streams(renderer, vertex_shader, pixel_shader,
                &position_layout, &color_layout, &two_pipeline) != 0 || !two_pipeline ||
            api.create_pipeline_with_vertex_layout(renderer, vertex_shader, pixel_shader,
                &packed_layout, &packed_pipeline) != 0 || !packed_pipeline ||
            api.create_pipeline_with_two_vertex_streams(renderer, vertex_shader, pixel_shader,
                &position_layout, &packed_color_layout, &packed_two_pipeline) != 0 || !packed_two_pipeline) break;
        const atmera_buffer_desc single_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(interleaved)), ATMERA_BUFFER_VERTEX};
        const atmera_buffer_desc position_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(half_positions)), ATMERA_BUFFER_VERTEX};
        const atmera_buffer_desc color_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(colors)), ATMERA_BUFFER_VERTEX};
        const atmera_buffer_desc packed_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(packed)), ATMERA_BUFFER_VERTEX};
        const atmera_buffer_desc packed_color_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(packed_colors)), ATMERA_BUFFER_VERTEX};
        const atmera_texture_desc texture_desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_buffer(renderer, &single_desc, &single_buffer) != 0 || !single_buffer ||
            api.create_buffer(renderer, &position_desc, &position_buffer) != 0 || !position_buffer ||
            api.create_buffer(renderer, &color_desc, &color_buffer) != 0 || !color_buffer ||
            api.create_buffer(renderer, &packed_desc, &packed_buffer) != 0 || !packed_buffer ||
            api.create_buffer(renderer, &packed_color_desc, &packed_color_buffer) != 0 || !packed_color_buffer ||
            api.write_buffer(renderer, single_buffer, 0, interleaved.data(), sizeof(interleaved)) != 0 ||
            api.write_buffer(renderer, position_buffer, 0, half_positions.data(), sizeof(half_positions)) != 0 ||
            api.write_buffer(renderer, color_buffer, 0, colors.data(), sizeof(colors)) != 0 ||
            api.write_buffer(renderer, packed_buffer, 0, packed.data(), sizeof(packed)) != 0 ||
            api.write_buffer(renderer, packed_color_buffer, 0, packed_colors.data(), sizeof(packed_colors)) != 0 ||
            api.create_texture(renderer, &texture_desc, &target) != 0 || !target) break;
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue) break;
        bool draws_passed = true;
        for (uint32_t pass = 0; pass < 4; ++pass) {
            const auto selected_pipeline = pass == 0 ? packed_pipeline :
                pass == 1 ? packed_two_pipeline : pass == 2 ? single_pipeline : two_pipeline;
            if (api.create_command_list(renderer, &list) != 0 || !list ||
                api.command_begin_render_target(list, target) != 0 ||
                api.command_bind_pipeline(list, selected_pipeline) != 0) {
                draws_passed = false; break;
            }
            if (pass == 0) {
                if (api.command_bind_vertex_buffer(list, packed_buffer, 7, 0) == 0 ||
                    api.command_bind_vertex_buffer(list, packed_buffer, 8, 0) != 0) {
                    draws_passed = false; break;
                }
            } else if (pass == 1) {
                if (api.command_bind_vertex_buffer(list, position_buffer, 4, 0) != 0 ||
                    api.command_bind_vertex_buffer_slot1(list, packed_color_buffer, 4, 0) != 0) {
                    draws_passed = false; break;
                }
            } else if (pass == 2) {
                if (api.command_bind_vertex_buffer(list, single_buffer, 11, 0) == 0 ||
                    api.command_bind_vertex_buffer(list, single_buffer, 12, 0) != 0) {
                    draws_passed = false; break;
                }
            } else if (api.command_bind_vertex_buffer(list, position_buffer, 4, 0) != 0 ||
                api.command_bind_vertex_buffer_slot1(list, color_buffer, 8, 0) != 0) {
                draws_passed = false; break;
            }
            if (api.command_draw(list, 3) != 0 || api.command_end_render_target(list) != 0 ||
                api.submit_command_list(queue, list, &fence) != 0 || !fence) {
                draws_passed = false; break;
            }
            api.destroy_command_list(list);
            list = nullptr;
            if (api.wait_fence(queue, fence, 10000) != 0) { draws_passed = false; break; }
            api.destroy_fence(fence);
            fence = nullptr;
            std::array<uint8_t, 64 * 64 * 4> pixels{};
            if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
                    static_cast<uint32_t>(pixels.size())) != 0) { draws_passed = false; break; }
            const size_t center = (32 * 64 + 32) * 4;
            if (pixels[center] < 228 || pixels[center] > 231 ||
                pixels[center + 1] < 50 || pixels[center + 1] > 52 ||
                pixels[center + 2] < 25 || pixels[center + 2] > 27 ||
                pixels[center + 3] != 255) { draws_passed = false; break; }
        }
        if (!draws_passed || api.begin_frame(renderer) != 0) break;
        uint32_t frame_result = api.bind_pipeline(renderer, single_pipeline);
        if (frame_result == 0) frame_result = api.bind_vertex_buffer(renderer, single_buffer, 12, 0);
        if (frame_result == 0) frame_result = api.draw(renderer, 3);
        const uint32_t frame_end = api.end_frame(renderer);
        uint8_t frame_pixel[4]{};
        passed = frame_result == 0 && frame_end == 0 &&
            api.get_center_pixel(renderer, frame_pixel) == 0 &&
            frame_pixel[0] >= 228 && frame_pixel[0] <= 231 &&
            frame_pixel[1] >= 50 && frame_pixel[1] <= 52 &&
            frame_pixel[2] >= 25 && frame_pixel[2] <= 27;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (target) api.destroy_texture(target);
    if (packed_color_buffer) api.destroy_buffer(packed_color_buffer);
    if (packed_buffer) api.destroy_buffer(packed_buffer);
    if (color_buffer) api.destroy_buffer(color_buffer);
    if (position_buffer) api.destroy_buffer(position_buffer);
    if (single_buffer) api.destroy_buffer(single_buffer);
    if (two_pipeline) api.destroy_pipeline(two_pipeline);
    if (single_pipeline) api.destroy_pipeline(single_pipeline);
    if (packed_two_pipeline) api.destroy_pipeline(packed_two_pipeline);
    if (packed_pipeline) api.destroy_pipeline(packed_pipeline);
    if (pixel_shader) api.destroy_shader(pixel_shader);
    if (vertex_shader) api.destroy_shader(vertex_shader);
    return passed;
}

bool test_vertex_input(const atmera_render_api& api, atmera_render_handle renderer,
    const std::string& source) {
    atmera_shader_handle vertex_shader = nullptr;
    atmera_shader_handle pixel_shader = nullptr;
    atmera_pipeline_handle pipeline = nullptr;
    atmera_buffer_handle buffer = nullptr;
    atmera_buffer_handle index16 = nullptr;
    atmera_buffer_handle index32 = nullptr;
    atmera_texture_handle target = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        const atmera_shader_desc vs_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            source.data(), static_cast<uint32_t>(source.size()), "VSBuffer"};
        const atmera_shader_desc ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "PSMain"};
        if (api.create_shader(renderer, &vs_desc, &vertex_shader) != 0 || !vertex_shader ||
            api.create_shader(renderer, &ps_desc, &pixel_shader) != 0 || !pixel_shader) break;
        const atmera_vertex_attribute_desc attrs[] = {
            {"POSITION", 0, ATMERA_VERTEX_FLOAT2, 0},
            {"COLOR", 0, ATMERA_VERTEX_FLOAT3, 8}};
        const atmera_vertex_layout_desc layout{sizeof(atmera_vertex_layout_desc), 2, attrs};
        if (api.create_pipeline_with_vertex_layout(renderer, vertex_shader, pixel_shader,
            &layout, &pipeline) != 0 || !pipeline) break;
        const atmera_buffer_desc buffer_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(colored_vertices)), ATMERA_BUFFER_VERTEX};
        const atmera_texture_desc texture_desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_buffer(renderer, &buffer_desc, &buffer) != 0 || !buffer ||
            api.write_buffer(renderer, buffer, 0, colored_vertices.data(), sizeof(colored_vertices)) != 0 ||
            api.create_texture(renderer, &texture_desc, &target) != 0 || !target ||
            api.create_command_list(renderer, &list) != 0 || !list) break;
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, pipeline) != 0) break;
        if (api.command_draw(list, 3) == 0 ||
            api.command_bind_vertex_buffer(list, buffer, 8, 0) == 0 ||
            api.command_bind_vertex_buffer(list, buffer, sizeof(ColoredVertex), 0) != 0 ||
            api.command_draw(list, 4) == 0 || api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        std::array<uint8_t, 64 * 64 * 4> pixels{};
        if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        const size_t center = (32 * 64 + 32) * 4;
        if (pixels[center] < 180 || pixels[center + 1] < 30 || pixels[center + 1] > 90 ||
            pixels[center + 2] > 70 || pixels[center + 3] != 255) break;
        api.destroy_fence(fence);
        fence = nullptr;
        const std::array<uint16_t, 3> indices16{0, 1, 2};
        const std::array<uint32_t, 3> indices32{0, 1, 2};
        const atmera_buffer_desc index16_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(indices16)), ATMERA_BUFFER_INDEX};
        const atmera_buffer_desc index32_desc{sizeof(atmera_buffer_desc),
            static_cast<uint32_t>(sizeof(indices32)), ATMERA_BUFFER_INDEX};
        if (api.create_buffer(renderer, &index16_desc, &index16) != 0 || !index16 ||
            api.create_buffer(renderer, &index32_desc, &index32) != 0 || !index32 ||
            api.write_buffer(renderer, index16, 0, indices16.data(), sizeof(indices16)) != 0 ||
            api.write_buffer(renderer, index32, 0, indices32.data(), sizeof(indices32)) != 0 ||
            api.create_command_list(renderer, &list) != 0 || !list ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, pipeline) != 0 ||
            api.command_bind_vertex_buffer(list, buffer, sizeof(ColoredVertex), 0) != 0) break;
        if (api.command_draw_indexed(list, 3) == 0 ||
            api.command_bind_index_buffer(list, index16, 99, 0) == 0 ||
            api.command_bind_index_buffer(list, index16, ATMERA_INDEX_UINT16, 0) != 0 ||
            api.command_draw_indexed(list, 4) == 0 ||
            api.command_draw_indexed(list, 3) != 0 ||
            api.command_end_render_target(list) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0 ||
            api.read_texture(renderer, target, pixels.data(), 64 * 4,
                static_cast<uint32_t>(pixels.size())) != 0 ||
            pixels[center] < 180 || pixels[center + 1] < 30 || pixels[center + 2] > 70) break;
        api.destroy_fence(fence);
        fence = nullptr;
        if (api.begin_frame(renderer) != 0) break;
        uint32_t frame_result = api.bind_pipeline(renderer, pipeline);
        if (frame_result == 0) frame_result = api.bind_vertex_buffer(renderer, buffer,
            sizeof(ColoredVertex), 0);
        if (frame_result == 0 && api.draw_indexed(renderer, 3) == 0) frame_result = 1;
        if (frame_result == 0) frame_result = api.bind_index_buffer(renderer, index32,
            ATMERA_INDEX_UINT32, 0);
        if (frame_result == 0 && api.draw_indexed(renderer, 4) == 0) frame_result = 1;
        if (frame_result == 0) frame_result = api.draw_indexed(renderer, 3);
        const uint32_t frame_end = api.end_frame(renderer);
        if (frame_result != 0 || frame_end != 0) break;
        uint8_t frame_pixel[4]{};
        if (api.get_center_pixel(renderer, frame_pixel) != 0 ||
            frame_pixel[0] < 180 || frame_pixel[1] < 30 || frame_pixel[2] > 70) break;
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (target) api.destroy_texture(target);
    if (index32) api.destroy_buffer(index32);
    if (index16) api.destroy_buffer(index16);
    if (buffer) api.destroy_buffer(buffer);
    if (pipeline) api.destroy_pipeline(pipeline);
    if (pixel_shader) api.destroy_shader(pixel_shader);
    if (vertex_shader) api.destroy_shader(vertex_shader);
    return passed;
}

uint32_t draw_frame(const atmera_render_api& api, atmera_render_handle renderer,
    atmera_pipeline_handle pipeline, atmera_binding_set_handle bindings = nullptr,
    atmera_buffer_handle vertices = nullptr, uint32_t stride = 0) {
    uint32_t result = api.begin_frame(renderer);
    if (result != 0) return result;
    result = api.bind_pipeline(renderer, pipeline);
    if (result == 0 && vertices) result = api.bind_vertex_buffer(renderer, vertices, stride, 0);
    if (result == 0 && bindings) result = api.bind_binding_set(renderer, bindings);
    if (result == 0) result = api.draw(renderer, 3);
    const uint32_t submit = api.end_frame(renderer);
    return result != 0 ? result : submit;
}

bool test_textured_frame(const atmera_render_api& api, atmera_render_handle renderer,
    atmera_pipeline_handle pipeline) {
    atmera_texture_handle texture = nullptr;
    atmera_texture_handle target = nullptr;
    atmera_texture_view_handle view = nullptr;
    atmera_texture_view_handle target_view = nullptr;
    atmera_sampler_handle sampler = nullptr;
    atmera_binding_set_handle bindings = nullptr;
    atmera_binding_set_handle target_bindings = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        const atmera_texture_desc texture_desc{sizeof(atmera_texture_desc), 4, 4};
        if (api.create_texture(renderer, &texture_desc, &texture) != 0 || !texture) break;
        std::array<uint8_t, 64> pixels{};
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = 40; pixels[i + 1] = 190; pixels[i + 2] = 90; pixels[i + 3] = 255;
        }
        if (api.write_texture(renderer, texture, pixels.data(), 16, 64) != 0) break;
        if (api.create_texture_view(renderer, texture, &view) != 0 || !view) break;
        const atmera_sampler_desc sampler_desc{sizeof(atmera_sampler_desc), ATMERA_FILTER_POINT, ATMERA_ADDRESS_CLAMP};
        if (api.create_sampler(renderer, &sampler_desc, &sampler) != 0 || !sampler) break;
        if (api.create_binding_set(renderer, view, sampler, &bindings) != 0 || !bindings) break;
        if (api.bind_binding_set(renderer, bindings) == 0) break;
        if (draw_frame(api, renderer, pipeline, bindings) != 0) break;
        uint8_t center[4]{};
        if (api.get_center_pixel(renderer, center) != 0) break;
        if (center[0] != 40 || center[1] != 190 || center[2] != 90) break;
        std::array<uint8_t, 64> readback{};
        if (api.read_texture(renderer, texture, readback.data(), 16, 64) != 0 || readback != pixels) break;
        const atmera_texture_desc target_desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_texture(renderer, &target_desc, &target) != 0 || !target ||
            api.create_texture_view(renderer, target, &target_view) != 0 || !target_view ||
            api.create_binding_set(renderer, target_view, sampler, &target_bindings) != 0 || !target_bindings ||
            api.create_command_list(renderer, &list) != 0 || !list) break;
        if (api.command_bind_binding_set(list, bindings) == 0 ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_binding_set(list, target_bindings) == 0 ||
            api.command_bind_pipeline(list, pipeline) != 0 ||
            api.command_bind_binding_set(list, bindings) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0) break;
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        std::array<uint8_t, 64 * 64 * 4> target_pixels{};
        if (api.read_texture(renderer, target, target_pixels.data(), 64 * 4,
            static_cast<uint32_t>(target_pixels.size())) != 0) break;
        const size_t center_offset = (32 * 64 + 32) * 4;
        if (target_pixels[center_offset] != 40 || target_pixels[center_offset + 1] != 190 ||
            target_pixels[center_offset + 2] != 90 || target_pixels[center_offset + 3] != 255 ||
            api.read_texture(renderer, texture, readback.data(), 16, 64) != 0 || readback != pixels) break;
        std::cout << "Textured center pixel: " << static_cast<int>(center[0]) << ','
            << static_cast<int>(center[1]) << ',' << static_cast<int>(center[2]) << '\n';
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (target_bindings) api.destroy_binding_set(target_bindings);
    if (target_view) api.destroy_texture_view(target_view);
    if (target) api.destroy_texture(target);
    if (bindings) api.destroy_binding_set(bindings);
    if (sampler) api.destroy_sampler(sampler);
    if (view) api.destroy_texture_view(view);
    if (texture) api.destroy_texture(texture);
    return passed;
}

bool test_constant_buffer(const atmera_render_api& api, atmera_render_handle renderer,
    const std::string& source) {
    atmera_shader_handle vertex_shader = nullptr;
    atmera_shader_handle pixel_shader = nullptr;
    atmera_pipeline_handle pipeline = nullptr;
    atmera_buffer_handle constant = nullptr;
    atmera_texture_handle target = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        const atmera_shader_desc vs_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            source.data(), static_cast<uint32_t>(source.size()), "VSMain"};
        const atmera_shader_desc ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "TintedPS"};
        if (api.create_shader(renderer, &vs_desc, &vertex_shader) != 0 || !vertex_shader ||
            api.create_shader(renderer, &ps_desc, &pixel_shader) != 0 || !pixel_shader ||
            api.create_pipeline(renderer, vertex_shader, pixel_shader, &pipeline) != 0 || !pipeline) break;
        const atmera_buffer_desc buffer_desc{sizeof(atmera_buffer_desc), 256, ATMERA_BUFFER_CONSTANT};
        const atmera_texture_desc texture_desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_buffer(renderer, &buffer_desc, &constant) != 0 || !constant ||
            api.create_texture(renderer, &texture_desc, &target) != 0 || !target) break;
        std::array<uint8_t, 256> input{};
        const float tint[4]{0.0f, 1.0f, 0.0f, 1.0f};
        std::memcpy(input.data(), tint, sizeof(tint));
        if (api.write_buffer(renderer, constant, 0, input.data(), 256) != 0 ||
            api.bind_constant_buffer(renderer, constant) == 0 ||
            api.create_command_list(renderer, &list) != 0 || !list ||
            api.command_bind_constant_buffer(list, constant) == 0 ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, pipeline) != 0 ||
            api.command_bind_constant_buffer(list, constant) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0) break;
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        std::array<uint8_t, 64 * 64 * 4> pixels{};
        if (api.read_texture(renderer, target, pixels.data(), 64 * 4,
            static_cast<uint32_t>(pixels.size())) != 0) break;
        const size_t center = (32 * 64 + 32) * 4;
        if (pixels[center] > 5 || pixels[center + 1] < 100 ||
            pixels[center + 2] > 5 || pixels[center + 3] != 255) break;
        if (api.begin_frame(renderer) != 0) break;
        uint32_t frame_result = api.bind_pipeline(renderer, pipeline);
        if (frame_result == 0) frame_result = api.bind_constant_buffer(renderer, constant);
        if (frame_result == 0) frame_result = api.draw(renderer, 3);
        const uint32_t frame_end = api.end_frame(renderer);
        if (frame_result != 0 || frame_end != 0) break;
        uint8_t frame_pixel[4]{};
        if (api.get_center_pixel(renderer, frame_pixel) != 0 ||
            frame_pixel[0] > 5 || frame_pixel[1] < 100 || frame_pixel[2] > 5) break;
        std::array<uint8_t, 256> output{};
        if (api.read_buffer(renderer, constant, 0, output.data(), 256) != 0 || output != input) break;
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (target) api.destroy_texture(target);
    if (constant) api.destroy_buffer(constant);
    if (pipeline) api.destroy_pipeline(pipeline);
    if (pixel_shader) api.destroy_shader(pixel_shader);
    if (vertex_shader) api.destroy_shader(vertex_shader);
    return passed;
}

bool test_binding_layout(const atmera_render_api& api, atmera_render_handle renderer,
    const std::string& source) {
    atmera_binding_layout_handle layout = nullptr;
    atmera_shader_handle vertex_shader = nullptr;
    atmera_shader_handle pixel_shader = nullptr;
    atmera_pipeline_handle pipeline = nullptr;
    atmera_texture_handle texture = nullptr;
    atmera_texture_handle target = nullptr;
    atmera_texture_view_handle view = nullptr;
    atmera_sampler_handle sampler = nullptr;
    atmera_binding_set_handle fixed_set = nullptr;
    atmera_binding_set_handle alternate_set = nullptr;
    atmera_buffer_handle constant = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        const atmera_binding_layout_desc invalid{sizeof(atmera_binding_layout_desc), 2, 3, 14};
        atmera_binding_layout_handle rejected = nullptr;
        if (api.create_binding_layout(renderer, &invalid, &rejected) == 0) {
            if (rejected) api.destroy_binding_layout(rejected);
            break;
        }
        const atmera_binding_layout_desc desc{sizeof(atmera_binding_layout_desc), 2, 3, 4};
        if (api.create_binding_layout(renderer, &desc, &layout) != 0 || !layout) break;
        const atmera_shader_desc vs_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            source.data(), static_cast<uint32_t>(source.size()), "VSMain"};
        const atmera_shader_desc ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "AlternatePS"};
        if (api.create_shader(renderer, &vs_desc, &vertex_shader) != 0 || !vertex_shader ||
            api.create_shader(renderer, &ps_desc, &pixel_shader) != 0 || !pixel_shader ||
            api.create_pipeline_with_binding_layout(renderer, vertex_shader, pixel_shader,
                layout, &pipeline) != 0 || !pipeline) break;
        const atmera_texture_desc source_desc{sizeof(atmera_texture_desc), 4, 4};
        const atmera_texture_desc target_desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_texture(renderer, &source_desc, &texture) != 0 || !texture ||
            api.create_texture(renderer, &target_desc, &target) != 0 || !target) break;
        std::array<uint8_t, 64> pixels{};
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = 40; pixels[i + 1] = 190; pixels[i + 2] = 90; pixels[i + 3] = 255;
        }
        if (api.write_texture(renderer, texture, pixels.data(), 16, 64) != 0 ||
            api.create_texture_view(renderer, texture, &view) != 0 || !view) break;
        const atmera_sampler_desc sampler_desc{sizeof(atmera_sampler_desc), ATMERA_FILTER_POINT, ATMERA_ADDRESS_CLAMP};
        if (api.create_sampler(renderer, &sampler_desc, &sampler) != 0 || !sampler ||
            api.create_binding_set(renderer, view, sampler, &fixed_set) != 0 || !fixed_set ||
            api.create_binding_set_with_layout(renderer, layout, view, sampler,
                &alternate_set) != 0 || !alternate_set) break;
        api.destroy_binding_layout(layout);
        layout = nullptr;
        const atmera_buffer_desc buffer_desc{sizeof(atmera_buffer_desc), 256, ATMERA_BUFFER_CONSTANT};
        if (api.create_buffer(renderer, &buffer_desc, &constant) != 0 || !constant) break;
        std::array<uint8_t, 256> constants{};
        const float tint[4]{1.0f, 0.5f, 1.0f, 1.0f};
        std::memcpy(constants.data(), tint, sizeof(tint));
        if (api.write_buffer(renderer, constant, 0, constants.data(), 256) != 0 ||
            api.begin_frame(renderer) != 0) break;
        uint32_t frame_result = api.bind_pipeline(renderer, pipeline);
        if (frame_result == 0 && api.bind_binding_set(renderer, fixed_set) == 0) frame_result = 1;
        if (frame_result == 0) frame_result = api.bind_binding_set(renderer, alternate_set);
        if (frame_result == 0) frame_result = api.bind_constant_buffer(renderer, constant);
        if (frame_result == 0) frame_result = api.draw(renderer, 3);
        const uint32_t frame_end = api.end_frame(renderer);
        if (frame_result != 0 || frame_end != 0) break;
        uint8_t frame_pixel[4]{};
        if (api.get_center_pixel(renderer, frame_pixel) != 0 ||
            frame_pixel[0] != 40 || frame_pixel[1] != 95 || frame_pixel[2] != 90) break;
        if (api.create_command_list(renderer, &list) != 0 || !list ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, pipeline) != 0 ||
            api.command_bind_binding_set(list, fixed_set) == 0 ||
            api.command_bind_binding_set(list, alternate_set) != 0 ||
            api.command_bind_constant_buffer(list, constant) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0) break;
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        std::array<uint8_t, 64 * 64 * 4> output{};
        if (api.read_texture(renderer, target, output.data(), 64 * 4,
            static_cast<uint32_t>(output.size())) != 0) break;
        const size_t center = (32 * 64 + 32) * 4;
        if (output[center] != 40 || output[center + 1] != 95 ||
            output[center + 2] != 90 || output[center + 3] != 255) break;
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (constant) api.destroy_buffer(constant);
    if (alternate_set) api.destroy_binding_set(alternate_set);
    if (fixed_set) api.destroy_binding_set(fixed_set);
    if (sampler) api.destroy_sampler(sampler);
    if (view) api.destroy_texture_view(view);
    if (target) api.destroy_texture(target);
    if (texture) api.destroy_texture(texture);
    if (pipeline) api.destroy_pipeline(pipeline);
    if (pixel_shader) api.destroy_shader(pixel_shader);
    if (vertex_shader) api.destroy_shader(vertex_shader);
    if (layout) api.destroy_binding_layout(layout);
    return passed;
}

bool test_texture_pair(const atmera_render_api& api, atmera_render_handle renderer,
    const std::string& source) {
    atmera_binding_layout_handle layout = nullptr;
    atmera_shader_handle vertex_shader = nullptr;
    atmera_shader_handle pixel_shader = nullptr;
    atmera_shader_handle sampler_pixel_shader = nullptr;
    atmera_pipeline_handle pipeline = nullptr;
    atmera_pipeline_handle sampler_pipeline = nullptr;
    atmera_texture_handle first = nullptr;
    atmera_texture_handle second = nullptr;
    atmera_texture_handle target = nullptr;
    atmera_texture_view_handle first_view = nullptr;
    atmera_texture_view_handle second_view = nullptr;
    atmera_sampler_handle sampler = nullptr;
    atmera_sampler_handle linear_sampler = nullptr;
    atmera_binding_set_handle single_set = nullptr;
    atmera_binding_set_handle pair_set = nullptr;
    atmera_binding_set_handle point_pair_set = nullptr;
    atmera_binding_set_handle mixed_pair_set = nullptr;
    atmera_command_list_handle list = nullptr;
    atmera_fence_handle fence = nullptr;
    bool passed = false;
    do {
        const atmera_binding_layout_desc layout_desc{sizeof(atmera_binding_layout_desc), 0, 0, 0};
        if (api.create_binding_layout(renderer, &layout_desc, &layout) != 0 || !layout) break;
        const atmera_shader_desc vs_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
            source.data(), static_cast<uint32_t>(source.size()), "VSMain"};
        const atmera_shader_desc ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "PairPS"};
        if (api.create_shader(renderer, &vs_desc, &vertex_shader) != 0 || !vertex_shader ||
            api.create_shader(renderer, &ps_desc, &pixel_shader) != 0 || !pixel_shader ||
            api.create_pipeline_with_texture_pair(renderer, vertex_shader, pixel_shader,
                layout, &pipeline) != 0 || !pipeline) break;
        const atmera_texture_desc source_desc{sizeof(atmera_texture_desc), 4, 4};
        const atmera_texture_desc target_desc{sizeof(atmera_texture_desc), 64, 64};
        if (api.create_texture(renderer, &source_desc, &first) != 0 || !first ||
            api.create_texture(renderer, &source_desc, &second) != 0 || !second ||
            api.create_texture(renderer, &target_desc, &target) != 0 || !target) break;
        std::array<uint8_t, 64> first_pixels{};
        std::array<uint8_t, 64> second_pixels{};
        for (size_t i = 0; i < first_pixels.size(); i += 4) {
            first_pixels[i] = 40; first_pixels[i + 1] = 190;
            first_pixels[i + 2] = 90; first_pixels[i + 3] = 255;
            second_pixels[i] = 200; second_pixels[i + 1] = 10;
            second_pixels[i + 2] = 50; second_pixels[i + 3] = 255;
        }
        if (api.write_texture(renderer, first, first_pixels.data(), 16, 64) != 0 ||
            api.write_texture(renderer, second, second_pixels.data(), 16, 64) != 0 ||
            api.create_texture_view(renderer, first, &first_view) != 0 || !first_view ||
            api.create_texture_view(renderer, second, &second_view) != 0 || !second_view) break;
        const atmera_sampler_desc sampler_desc{sizeof(atmera_sampler_desc),
            ATMERA_FILTER_POINT, ATMERA_ADDRESS_CLAMP};
        if (api.create_sampler(renderer, &sampler_desc, &sampler) != 0 || !sampler ||
            api.create_binding_set_with_layout(renderer, layout, first_view, sampler,
                &single_set) != 0 || !single_set ||
            api.create_binding_set_with_texture_pair(renderer, layout, first_view,
                second_view, sampler, &pair_set) != 0 || !pair_set) break;
        atmera_binding_set_handle rejected = nullptr;
        if (api.create_binding_set_with_texture_pair(renderer, layout, first_view,
                first_view, sampler, &rejected) == 0) {
            if (rejected) api.destroy_binding_set(rejected);
            break;
        }
        api.destroy_binding_layout(layout);
        layout = nullptr;
        if (api.begin_frame(renderer) != 0) break;
        uint32_t frame_result = api.bind_pipeline(renderer, pipeline);
        if (frame_result == 0 && api.bind_binding_set(renderer, single_set) == 0) frame_result = 1;
        if (frame_result == 0) frame_result = api.bind_binding_set(renderer, pair_set);
        if (frame_result == 0) frame_result = api.draw(renderer, 3);
        const uint32_t frame_end = api.end_frame(renderer);
        if (frame_result != 0 || frame_end != 0) break;
        uint8_t frame_pixel[4]{};
        if (api.get_center_pixel(renderer, frame_pixel) != 0 ||
            frame_pixel[0] < 119 || frame_pixel[0] > 121 ||
            frame_pixel[1] < 99 || frame_pixel[1] > 101 ||
            frame_pixel[2] < 69 || frame_pixel[2] > 71) break;
        if (api.create_command_list(renderer, &list) != 0 || !list ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, pipeline) != 0 ||
            api.command_bind_binding_set(list, single_set) == 0 ||
            api.command_bind_binding_set(list, pair_set) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0) break;
        atmera_command_queue_handle queue = nullptr;
        if (api.get_graphics_queue(renderer, &queue) != 0 || !queue ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0) break;
        std::array<uint8_t, 64 * 64 * 4> output{};
        if (api.read_texture(renderer, target, output.data(), 64 * 4,
            static_cast<uint32_t>(output.size())) != 0) break;
        const size_t center = (32 * 64 + 32) * 4;
        if (output[center] < 119 || output[center] > 121 ||
            output[center + 1] < 99 || output[center + 1] > 101 ||
            output[center + 2] < 69 || output[center + 2] > 71 ||
            output[center + 3] != 255) break;
        if (api.create_command_list(renderer, &list) != 0 || !list ||
            api.command_begin_render_target(list, second) != 0 ||
            api.command_bind_pipeline(list, pipeline) != 0 ||
            api.command_bind_binding_set(list, pair_set) == 0) break;
        api.destroy_command_list(list);
        list = nullptr;
        api.destroy_fence(fence);
        fence = nullptr;
        for (size_t i = 0; i < second_pixels.size(); i += 4) {
            second_pixels[i] = 0;
            second_pixels[i + 1] = 10;
            second_pixels[i + 2] = 50;
            second_pixels[i + 3] = 255;
        }
        second_pixels[(2 * 4 + 2) * 4] = 200;
        if (api.write_texture(renderer, second, second_pixels.data(), 16, 64) != 0 ||
            api.create_binding_layout(renderer, &layout_desc, &layout) != 0 || !layout) break;
        const atmera_shader_desc sampler_ps_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
            source.data(), static_cast<uint32_t>(source.size()), "PairSamplerPS"};
        const atmera_sampler_desc linear_desc{sizeof(atmera_sampler_desc),
            ATMERA_FILTER_LINEAR, ATMERA_ADDRESS_CLAMP};
        if (api.create_shader(renderer, &sampler_ps_desc, &sampler_pixel_shader) != 0 ||
                !sampler_pixel_shader ||
            api.create_pipeline_with_texture_sampler_pair(renderer, vertex_shader,
                sampler_pixel_shader, layout, &sampler_pipeline) != 0 || !sampler_pipeline ||
            api.create_sampler(renderer, &linear_desc, &linear_sampler) != 0 || !linear_sampler ||
            api.create_binding_set_with_texture_sampler_pair(renderer, layout, first_view,
                sampler, second_view, sampler, &point_pair_set) != 0 || !point_pair_set ||
            api.create_binding_set_with_texture_sampler_pair(renderer, layout, first_view,
                sampler, second_view, linear_sampler, &mixed_pair_set) != 0 || !mixed_pair_set) break;
        api.destroy_binding_layout(layout);
        layout = nullptr;
        for (uint32_t pass = 0; pass < 2; ++pass) {
            if (api.begin_frame(renderer) != 0) break;
            uint32_t draw_result = api.bind_pipeline(renderer, sampler_pipeline);
            if (draw_result == 0 && api.bind_binding_set(renderer, pair_set) == 0) draw_result = 1;
            if (draw_result == 0) draw_result = api.bind_binding_set(renderer,
                pass == 0 ? point_pair_set : mixed_pair_set);
            if (draw_result == 0) draw_result = api.draw(renderer, 3);
            const uint32_t end_result = api.end_frame(renderer);
            if (draw_result != 0 || end_result != 0 ||
                api.get_center_pixel(renderer, frame_pixel) != 0) break;
            const int expected_red = pass == 0 ? 120 : 45;
            if (frame_pixel[0] + 1 < expected_red || frame_pixel[0] > expected_red + 1 ||
                frame_pixel[1] < 99 || frame_pixel[1] > 101 ||
                frame_pixel[2] < 69 || frame_pixel[2] > 71) break;
            if (pass == 1) passed = true;
        }
        if (!passed) break;
        passed = false;
        if (api.create_command_list(renderer, &list) != 0 || !list ||
            api.command_begin_render_target(list, target) != 0 ||
            api.command_bind_pipeline(list, sampler_pipeline) != 0 ||
            api.command_bind_binding_set(list, pair_set) == 0 ||
            api.command_bind_binding_set(list, mixed_pair_set) != 0 ||
            api.command_draw(list, 3) != 0 ||
            api.command_end_render_target(list) != 0 ||
            api.submit_command_list(queue, list, &fence) != 0 || !fence) break;
        api.destroy_command_list(list);
        list = nullptr;
        if (api.wait_fence(queue, fence, 10000) != 0 ||
            api.read_texture(renderer, target, output.data(), 64 * 4,
                static_cast<uint32_t>(output.size())) != 0) break;
        if (output[center] < 44 || output[center] > 46 ||
            output[center + 1] < 99 || output[center + 1] > 101 ||
            output[center + 2] < 69 || output[center + 2] > 71 ||
            output[center + 3] != 255) break;
        passed = true;
    } while (false);
    if (fence) api.destroy_fence(fence);
    if (list) api.destroy_command_list(list);
    if (pair_set) api.destroy_binding_set(pair_set);
    if (mixed_pair_set) api.destroy_binding_set(mixed_pair_set);
    if (point_pair_set) api.destroy_binding_set(point_pair_set);
    if (single_set) api.destroy_binding_set(single_set);
    if (sampler) api.destroy_sampler(sampler);
    if (linear_sampler) api.destroy_sampler(linear_sampler);
    if (second_view) api.destroy_texture_view(second_view);
    if (first_view) api.destroy_texture_view(first_view);
    if (target) api.destroy_texture(target);
    if (second) api.destroy_texture(second);
    if (first) api.destroy_texture(first);
    if (pipeline) api.destroy_pipeline(pipeline);
    if (sampler_pipeline) api.destroy_pipeline(sampler_pipeline);
    if (pixel_shader) api.destroy_shader(pixel_shader);
    if (sampler_pixel_shader) api.destroy_shader(sampler_pixel_shader);
    if (vertex_shader) api.destroy_shader(vertex_shader);
    if (layout) api.destroy_binding_layout(layout);
    return passed;
}

int main(int argc, char** argv) {
#ifdef ATMERA_TRIANGLE_DEFAULT_D3D12
    std::string backend = "d3d12";
#else
    std::string backend = "d3d11";
#endif
    bool smoke = false;
    bool warp = false;
    bool force_hardware = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--backend" && i + 1 < argc) backend = argv[++i];
        else if (arg == "--smoke") smoke = true;
        else if (arg == "--warp") warp = true;
        else if (arg == "--hardware") force_hardware = true;
        else { std::cerr << "Usage: ATMERA.Triangle [--backend d3d11|d3d12] [--warp] [--smoke] [--hardware]\n"; return 2; }
    }
    if (backend != "d3d11" && backend != "d3d12") return 2;
    if (warp && force_hardware) return 2;
    if (smoke && !force_hardware) warp = true;

    wchar_t exe_path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return 3;
    const auto output_dir = fs::path(exe_path).parent_path();
    std::ifstream shader_file(output_dir / "Triangle.hlsl", std::ios::binary);
    if (!shader_file) { std::cerr << "Triangle.hlsl is missing\n"; return 4; }
    const std::string shader((std::istreambuf_iterator<char>(shader_file)), std::istreambuf_iterator<char>());
    const auto dll_path = output_dir / (backend == "d3d11" ? L"ATMERA.D3D11.dll" : L"ATMERA.D3D12.dll");
    HMODULE library = LoadLibraryExW(dll_path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library) { std::cerr << "Backend load failed: " << GetLastError() << '\n'; return 5; }
    const auto entry = reinterpret_cast<atmera_get_render_api_fn>(GetProcAddress(library, ATMERA_RENDER_ENTRY));
    const atmera_render_api* api = nullptr;
    if (!entry || entry(ATMERA_RENDER_ABI_VERSION, &api) != 0 || !api ||
        api->struct_size < offsetof(atmera_render_api, get_shader_reflection) ||
        api->abi_version != ATMERA_RENDER_ABI_VERSION ||
        !api->create || !api->get_caps || !api->resize || !api->render_frame ||
        !api->get_center_pixel || !api->destroy || !api->create_buffer || !api->write_buffer ||
        !api->read_buffer || !api->destroy_buffer || !api->create_texture ||
        !api->write_texture || !api->read_texture || !api->destroy_texture ||
        !api->create_shader || !api->destroy_shader || !api->create_pipeline ||
        !api->destroy_pipeline || !api->begin_frame || !api->bind_pipeline ||
        !api->draw || !api->end_frame || !api->create_texture_view ||
        !api->destroy_texture_view || !api->create_sampler || !api->destroy_sampler ||
        !api->create_binding_set || !api->destroy_binding_set || !api->bind_binding_set ||
        !api->get_graphics_queue || !api->create_command_list || !api->command_copy_buffer ||
        !api->submit_command_list || !api->wait_fence || !api->destroy_fence ||
        !api->destroy_command_list || !api->command_begin_render_target ||
        !api->command_bind_pipeline || !api->command_draw || !api->command_end_render_target ||
        !api->command_copy_texture || !api->command_copy_texture_region ||
        !api->create_pipeline_with_vertex_layout || !api->command_bind_vertex_buffer ||
        !api->bind_vertex_buffer || !api->bind_index_buffer || !api->draw_indexed ||
        !api->command_bind_index_buffer || !api->command_draw_indexed ||
        !api->command_bind_binding_set || !api->bind_constant_buffer ||
        !api->command_bind_constant_buffer || !api->create_binding_layout ||
        !api->destroy_binding_layout || !api->create_pipeline_with_binding_layout ||
        !api->create_binding_set_with_layout || !api->create_pipeline_with_two_vertex_streams ||
        !api->bind_vertex_buffer_slot1 || !api->command_bind_vertex_buffer_slot1 ||
        !api->create_pipeline_with_texture_pair || !api->create_binding_set_with_texture_pair ||
        !api->create_pipeline_with_texture_sampler_pair ||
        !api->create_binding_set_with_texture_sampler_pair ||
        !api->create_pipeline_with_three_vertex_streams || !api->bind_vertex_buffer_slot2 ||
        !api->command_bind_vertex_buffer_slot2) {
        std::cerr << "Backend ABI mismatch\n";
        FreeLibrary(library);
        return 6;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"ATMERA.Triangle.Window";
    if (!RegisterClassW(&window_class)) { FreeLibrary(library); return 7; }
    HWND window = CreateWindowExW(0, window_class.lpszClassName,
        backend == "d3d11" ? L"ATMERA Triangle - D3D11" : L"ATMERA Triangle - D3D12",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 650,
        nullptr, nullptr, instance, nullptr);
    if (!window) { FreeLibrary(library); return 8; }
    if (!smoke) ShowWindow(window, SW_SHOW);

    atmera_render_desc desc{sizeof(atmera_render_desc), window, 900, 650,
        warp ? 1u : 0u, shader.data(), static_cast<uint32_t>(shader.size()), smoke ? 1u : 0u};
    atmera_render_handle renderer = nullptr;
    uint32_t result = api->create(&desc, &renderer);
    if (result != 0 && !warp && !force_hardware) {
        std::cerr << "Hardware unavailable; retrying with WARP\n";
        desc.use_warp = 1;
        result = api->create(&desc, &renderer);
    }
    if (result != 0 || !renderer) {
        std::cerr << "Renderer creation failed: 0x" << std::hex << result << '\n';
        DestroyWindow(window);
        FreeLibrary(library);
        return 9;
    }
    atmera_render_caps caps{};
    caps.struct_size = sizeof(caps);
    result = api->get_caps(renderer, &caps);
    if (result != 0) { std::cerr << "Capability query failed\n"; api->destroy(renderer); DestroyWindow(window); FreeLibrary(library); return 10; }
    std::cout << api->backend_id << " feature level 0x" << std::hex << caps.feature_level
              << " shader model " << std::dec << caps.shader_model << " WARP=" << caps.is_software << '\n';

    atmera_shader_handle vertex_shader = nullptr;
    atmera_shader_handle pixel_shader = nullptr;
    atmera_shader_handle textured_pixel_shader = nullptr;
    atmera_shader_handle buffer_vertex_shader = nullptr;
    atmera_pipeline_handle pipeline = nullptr;
    atmera_pipeline_handle textured_pipeline = nullptr;
    atmera_pipeline_handle buffer_pipeline = nullptr;
    atmera_buffer_handle vertex_buffer = nullptr;
    const atmera_shader_desc vertex_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
        shader.data(), static_cast<uint32_t>(shader.size()), "VSMain"};
    const atmera_shader_desc pixel_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
        shader.data(), static_cast<uint32_t>(shader.size()), "PSMain"};
    const atmera_shader_desc textured_pixel_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_PIXEL,
        shader.data(), static_cast<uint32_t>(shader.size()), "TexturedPS"};
    const atmera_shader_desc buffer_vertex_desc{sizeof(atmera_shader_desc), ATMERA_SHADER_VERTEX,
        shader.data(), static_cast<uint32_t>(shader.size()), "VSBuffer"};
    const atmera_vertex_attribute_desc attrs[] = {
        {"POSITION", 0, ATMERA_VERTEX_FLOAT2, 0},
        {"COLOR", 0, ATMERA_VERTEX_FLOAT3, 8}};
    const atmera_vertex_layout_desc layout{sizeof(atmera_vertex_layout_desc), 2, attrs};
    const atmera_buffer_desc vertex_buffer_desc{sizeof(atmera_buffer_desc),
        static_cast<uint32_t>(sizeof(colored_vertices)), ATMERA_BUFFER_VERTEX};
    result = api->create_shader(renderer, &vertex_desc, &vertex_shader);
    if (result == 0) result = api->create_shader(renderer, &pixel_desc, &pixel_shader);
    if (result == 0) result = api->create_pipeline(renderer, vertex_shader, pixel_shader, &pipeline);
    if (result == 0) result = api->create_shader(renderer, &textured_pixel_desc, &textured_pixel_shader);
    if (result == 0) result = api->create_pipeline(renderer, vertex_shader, textured_pixel_shader, &textured_pipeline);
    if (result == 0) result = api->create_shader(renderer, &buffer_vertex_desc, &buffer_vertex_shader);
    if (result == 0) result = api->create_pipeline_with_vertex_layout(renderer,
        buffer_vertex_shader, pixel_shader, &layout, &buffer_pipeline);
    if (vertex_shader) api->destroy_shader(vertex_shader);
    if (pixel_shader) api->destroy_shader(pixel_shader);
    if (textured_pixel_shader) api->destroy_shader(textured_pixel_shader);
    if (buffer_vertex_shader) api->destroy_shader(buffer_vertex_shader);
    if (result == 0) result = api->create_buffer(renderer, &vertex_buffer_desc, &vertex_buffer);
    if (result == 0) result = api->write_buffer(renderer, vertex_buffer, 0,
        colored_vertices.data(), sizeof(colored_vertices));
    if (result != 0 || !pipeline || !textured_pipeline || !buffer_pipeline || !vertex_buffer) {
        std::cerr << "Pipeline creation failed: 0x" << std::hex << result << '\n';
        if (pipeline) api->destroy_pipeline(pipeline);
        if (textured_pipeline) api->destroy_pipeline(textured_pipeline);
        if (buffer_pipeline) api->destroy_pipeline(buffer_pipeline);
        if (vertex_buffer) api->destroy_buffer(vertex_buffer);
        api->destroy(renderer);
        DestroyWindow(window);
        FreeLibrary(library);
        return 12;
    }

    if (smoke) {
        if (api->draw(renderer, 3) == 0) result = 1;
        if (result == 0 && api->bind_vertex_buffer(renderer, vertex_buffer,
            sizeof(ColoredVertex), 0) == 0) result = 1;
        if (result == 0) result = test_resources(*api, renderer) ? 0u : 1u;
        if (result == 0) result = test_commands(*api, renderer) ? 0u : 1u;
        if (result == 0) result = test_texture_commands(*api, renderer) ? 0u : 1u;
        if (result == 0) result = test_command_draw(*api, renderer, pipeline) ? 0u : 1u;
        if (result == 0) result = test_vertex_input(*api, renderer, shader) ? 0u : 1u;
        if (result == 0) result = test_packed_vertex_color(*api, renderer, shader) ? 0u : 1u;
        if (result == 0) result = test_compressed_uv(*api, renderer, shader) ? 0u : 1u;
        if (result == 0) result = test_scalar_vertex_attribute(*api, renderer, shader) ? 0u : 1u;
        if (result == 0) result = test_two_vertex_streams(*api, renderer, shader) ? 0u : 1u;
        if (result == 0) result = test_half_vertex_attributes(*api, renderer, shader) ? 0u : 1u;
        if (result == 0) result = test_constant_buffer(*api, renderer, shader) ? 0u : 1u;
        if (result == 0) result = test_binding_layout(*api, renderer, shader) ? 0u : 1u;
        if (result == 0) result = test_texture_pair(*api, renderer, shader) ? 0u : 1u;
        if (result == 0) std::cout << "GPU resources, copies, binding layouts, draw and queue/fence tests passed\n";
        else std::cerr << "GPU resource test failed\n";
        if (result == 0) result = draw_frame(*api, renderer, buffer_pipeline,
            nullptr, vertex_buffer, sizeof(ColoredVertex));
        uint8_t pixel[4]{};
        if (result == 0) result = api->get_center_pixel(renderer, pixel);
        if (result == 0 && (pixel[0] < 180 || pixel[1] < 30 || pixel[2] > 70)) result = 1;
        if (result == 0) result = api->resize(renderer, 800, 500);
        if (result == 0 && !test_resources(*api, renderer)) result = 1;
        if (result == 0) result = draw_frame(*api, renderer, buffer_pipeline,
            nullptr, vertex_buffer, sizeof(ColoredVertex));
        if (result == 0) result = api->get_center_pixel(renderer, pixel);
        if (result == 0 && (pixel[0] < 180 || pixel[1] < 30 || pixel[2] > 70)) result = 1;
        if (result == 0)
            std::cout << "Center pixel: " << static_cast<int>(pixel[0]) << ','
                << static_cast<int>(pixel[1]) << ',' << static_cast<int>(pixel[2]) << '\n';
        if (result == 0 && !test_textured_frame(*api, renderer, textured_pipeline)) result = 1;
    } else {
        MSG message{};
        bool running = true;
        while (running) {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running) break;
            RECT client{};
            GetClientRect(window, &client);
            const auto width = static_cast<uint32_t>(client.right - client.left);
            const auto height = static_cast<uint32_t>(client.bottom - client.top);
            if (!width || !height) { Sleep(16); continue; }
            if (width != desc.width || height != desc.height) {
                result = api->resize(renderer, width, height);
                if (result != 0) break;
                desc.width = width;
                desc.height = height;
            }
            result = draw_frame(*api, renderer, buffer_pipeline,
                nullptr, vertex_buffer, sizeof(ColoredVertex));
            if (result != 0) break;
            Sleep(16);
        }
    }
    api->destroy_pipeline(pipeline);
    api->destroy_pipeline(textured_pipeline);
    api->destroy_pipeline(buffer_pipeline);
    api->destroy_buffer(vertex_buffer);
    api->destroy(renderer);
    DestroyWindow(window);
    FreeLibrary(library);
    if (result != 0) std::cerr << "Rendering failed: 0x" << std::hex << result << '\n';
    return result == 0 ? 0 : 11;
}
