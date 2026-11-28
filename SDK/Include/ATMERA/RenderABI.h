#pragma once
#include <stdint.h>

#ifdef _WIN32
#define ATMERA_RENDER_EXPORT __declspec(dllexport)
#else
#define ATMERA_RENDER_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ATMERA_RENDER_ABI_VERSION 1u
#define ATMERA_RENDER_ENTRY "atmera_get_render_api"

typedef void* atmera_render_handle;
typedef void* atmera_buffer_handle;
typedef void* atmera_texture_handle;
typedef void* atmera_shader_handle;
typedef void* atmera_pipeline_handle;
typedef void* atmera_texture_view_handle;
typedef void* atmera_sampler_handle;
typedef void* atmera_binding_set_handle;
typedef void* atmera_binding_layout_handle;
typedef void* atmera_command_queue_handle;
typedef void* atmera_command_list_handle;
typedef void* atmera_fence_handle;

#define ATMERA_FILTER_POINT 1u
#define ATMERA_FILTER_LINEAR 2u
#define ATMERA_ADDRESS_CLAMP 1u
#define ATMERA_ADDRESS_WRAP 2u

typedef struct atmera_sampler_desc {
    uint32_t struct_size;
    uint32_t filter;
    uint32_t address_mode;
} atmera_sampler_desc;

typedef struct atmera_binding_layout_desc {
    uint32_t struct_size;
    uint32_t texture_register;
    uint32_t sampler_register;
    uint32_t constant_register;
} atmera_binding_layout_desc;

#define ATMERA_SHADER_VERTEX 1u
#define ATMERA_SHADER_PIXEL 2u

typedef struct atmera_shader_desc {
    uint32_t struct_size;
    uint32_t stage;
    const char* source;
    uint32_t source_size;
    const char* entry_point;
} atmera_shader_desc;

// Counts resource binding declarations retained in compiled shader bytecode.
typedef struct atmera_shader_reflection {
    uint32_t struct_size;
    uint32_t stage;
    uint32_t constant_buffer_bindings;
    uint32_t texture_bindings;
    uint32_t sampler_bindings;
} atmera_shader_reflection;

#define ATMERA_SHADER_BINDING_CONSTANT_BUFFER 1u
#define ATMERA_SHADER_BINDING_TEXTURE 2u
#define ATMERA_SHADER_BINDING_SAMPLER 3u

typedef struct atmera_shader_binding {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t register_index;
    uint32_t register_space; /* D3D11 reports 0 */
    uint32_t register_count;
} atmera_shader_binding;

// Counts only create_shader calls on this renderer. The cache keeps at most
// one compiled source per stage (vertex and pixel) until renderer destruction.
typedef struct atmera_shader_cache_stats {
    uint32_t struct_size;
    uint32_t hits;
    uint32_t misses;
    uint32_t entries;
} atmera_shader_cache_stats;

#define ATMERA_BUFFER_VERTEX 1u
#define ATMERA_BUFFER_INDEX 2u
#define ATMERA_BUFFER_CONSTANT 4u
#define ATMERA_INDEX_UINT16 1u
#define ATMERA_INDEX_UINT32 2u

typedef struct atmera_buffer_desc {
    uint32_t struct_size;
    uint32_t size_bytes;
    uint32_t usage;
} atmera_buffer_desc;

#define ATMERA_VERTEX_FLOAT2 1u
#define ATMERA_VERTEX_FLOAT3 2u
#define ATMERA_VERTEX_FLOAT4 3u
#define ATMERA_VERTEX_UNORM8X4 4u
#define ATMERA_VERTEX_FLOAT1 5u
#define ATMERA_VERTEX_HALF2 6u
#define ATMERA_VERTEX_HALF4 7u
#define ATMERA_VERTEX_UNORM16X2 8u
#define ATMERA_VERTEX_UNORM8X2 9u

typedef struct atmera_vertex_attribute_desc {
    const char* semantic_name;
    uint32_t semantic_index;
    uint32_t format;
    uint32_t offset_bytes;
} atmera_vertex_attribute_desc;

typedef struct atmera_vertex_layout_desc {
    uint32_t struct_size;
    uint32_t attribute_count;
    const atmera_vertex_attribute_desc* attributes;
} atmera_vertex_layout_desc;

typedef struct atmera_texture_desc {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
} atmera_texture_desc;

typedef struct atmera_render_desc {
    uint32_t struct_size;
    void* window_handle; /* borrowed HWND for presentation; NULL for headless offscreen work */
    uint32_t width;
    uint32_t height;
    uint32_t use_warp;
    const char* hlsl_source;
    uint32_t hlsl_source_size;
    uint32_t capture_center_pixel;
} atmera_render_desc;

typedef struct atmera_render_caps {
    uint32_t struct_size;
    uint32_t api_major;
    uint32_t feature_level;
    uint32_t shader_model;
    uint32_t is_software;
} atmera_render_caps;

typedef struct atmera_render_api {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* backend_id;
    uint32_t (*create)(const atmera_render_desc* desc, atmera_render_handle* out_handle);
    uint32_t (*get_caps)(atmera_render_handle handle, atmera_render_caps* out_caps);
    uint32_t (*resize)(atmera_render_handle handle, uint32_t width, uint32_t height);
    uint32_t (*render_frame)(atmera_render_handle handle);
    uint32_t (*get_center_pixel)(atmera_render_handle handle, uint8_t out_rgba[4]);
    void (*destroy)(atmera_render_handle handle);
    // Resource handles belong to the renderer that created them. Destroy them before destroy(renderer).
    uint32_t (*create_buffer)(atmera_render_handle renderer, const atmera_buffer_desc* desc,
        atmera_buffer_handle* out_buffer);
    uint32_t (*write_buffer)(atmera_render_handle renderer, atmera_buffer_handle buffer,
        uint32_t offset, const void* data, uint32_t size);
    uint32_t (*read_buffer)(atmera_render_handle renderer, atmera_buffer_handle buffer,
        uint32_t offset, void* data, uint32_t size);
    void (*destroy_buffer)(atmera_buffer_handle buffer);
    uint32_t (*create_texture)(atmera_render_handle renderer, const atmera_texture_desc* desc,
        atmera_texture_handle* out_texture);
    uint32_t (*write_texture)(atmera_render_handle renderer, atmera_texture_handle texture,
        const uint8_t* pixels, uint32_t row_pitch, uint32_t byte_count);
    uint32_t (*read_texture)(atmera_render_handle renderer, atmera_texture_handle texture,
        uint8_t* pixels, uint32_t row_pitch, uint32_t byte_count);
    void (*destroy_texture)(atmera_texture_handle texture);
    uint32_t (*create_shader)(atmera_render_handle renderer, const atmera_shader_desc* desc,
        atmera_shader_handle* out_shader);
    void (*destroy_shader)(atmera_shader_handle shader);
    uint32_t (*create_pipeline)(atmera_render_handle renderer, atmera_shader_handle vertex_shader,
        atmera_shader_handle pixel_shader, atmera_pipeline_handle* out_pipeline);
    void (*destroy_pipeline)(atmera_pipeline_handle pipeline);
    // One frame may be recorded at a time. end_frame submits and presents it.
    uint32_t (*begin_frame)(atmera_render_handle renderer);
    uint32_t (*bind_pipeline)(atmera_render_handle renderer, atmera_pipeline_handle pipeline);
    uint32_t (*draw)(atmera_render_handle renderer, uint32_t vertex_count);
    uint32_t (*end_frame)(atmera_render_handle renderer);
    // Fixed first binding set: one RGBA8 shader resource at t0 and one sampler at s0.
    uint32_t (*create_texture_view)(atmera_render_handle renderer, atmera_texture_handle texture,
        atmera_texture_view_handle* out_view);
    void (*destroy_texture_view)(atmera_texture_view_handle view);
    uint32_t (*create_sampler)(atmera_render_handle renderer, const atmera_sampler_desc* desc,
        atmera_sampler_handle* out_sampler);
    void (*destroy_sampler)(atmera_sampler_handle sampler);
    uint32_t (*create_binding_set)(atmera_render_handle renderer, atmera_texture_view_handle view,
        atmera_sampler_handle sampler, atmera_binding_set_handle* out_set);
    void (*destroy_binding_set)(atmera_binding_set_handle set);
    uint32_t (*bind_binding_set)(atmera_render_handle renderer, atmera_binding_set_handle set);
    // One graphics queue per renderer. A command list may be submitted once.
    uint32_t (*get_graphics_queue)(atmera_render_handle renderer, atmera_command_queue_handle* out_queue);
    uint32_t (*create_command_list)(atmera_render_handle renderer, atmera_command_list_handle* out_list);
    uint32_t (*command_copy_buffer)(atmera_command_list_handle list, atmera_buffer_handle destination,
        uint32_t destination_offset, atmera_buffer_handle source, uint32_t source_offset, uint32_t size);
    uint32_t (*submit_command_list)(atmera_command_queue_handle queue, atmera_command_list_handle list,
        atmera_fence_handle* out_fence);
    uint32_t (*wait_fence)(atmera_command_queue_handle queue, atmera_fence_handle fence, uint32_t timeout_ms);
    void (*destroy_fence)(atmera_fence_handle fence);
    void (*destroy_command_list)(atmera_command_list_handle list);
    // Offscreen RGBA8 drawing. A target pass must be ended before submission.
    uint32_t (*command_begin_render_target)(atmera_command_list_handle list, atmera_texture_handle target);
    uint32_t (*command_bind_pipeline)(atmera_command_list_handle list, atmera_pipeline_handle pipeline);
    uint32_t (*command_draw)(atmera_command_list_handle list, uint32_t vertex_count);
    uint32_t (*command_end_render_target)(atmera_command_list_handle list);
    // Copies the whole RGBA8 image between distinct, equally sized textures.
    uint32_t (*command_copy_texture)(atmera_command_list_handle list,
        atmera_texture_handle destination, atmera_texture_handle source);
    // Copies a rectangular RGBA8 region; coordinates and extent are in pixels.
    uint32_t (*command_copy_texture_region)(atmera_command_list_handle list,
        atmera_texture_handle destination, uint32_t destination_x, uint32_t destination_y,
        atmera_texture_handle source, uint32_t source_x, uint32_t source_y,
        uint32_t width, uint32_t height);
    // One interleaved vertex stream at input slot 0.
    uint32_t (*create_pipeline_with_vertex_layout)(atmera_render_handle renderer,
        atmera_shader_handle vertex_shader, atmera_shader_handle pixel_shader,
        const atmera_vertex_layout_desc* layout, atmera_pipeline_handle* out_pipeline);
    uint32_t (*command_bind_vertex_buffer)(atmera_command_list_handle list,
        atmera_buffer_handle buffer, uint32_t stride_bytes, uint32_t offset_bytes);
    // Binds slot 0 during begin_frame/end_frame swap-chain drawing.
    uint32_t (*bind_vertex_buffer)(atmera_render_handle renderer,
        atmera_buffer_handle buffer, uint32_t stride_bytes, uint32_t offset_bytes);
    uint32_t (*bind_index_buffer)(atmera_render_handle renderer,
        atmera_buffer_handle buffer, uint32_t index_format, uint32_t offset_bytes);
    uint32_t (*draw_indexed)(atmera_render_handle renderer, uint32_t index_count);
    uint32_t (*command_bind_index_buffer)(atmera_command_list_handle list,
        atmera_buffer_handle buffer, uint32_t index_format, uint32_t offset_bytes);
    uint32_t (*command_draw_indexed)(atmera_command_list_handle list, uint32_t index_count);
    // Fixed t0/s0 binding set during an independent offscreen render-target pass.
    uint32_t (*command_bind_binding_set)(atmera_command_list_handle list,
        atmera_binding_set_handle set);
    // One 256-byte-aligned constant buffer at b0, visible to vertex and pixel shaders.
    uint32_t (*bind_constant_buffer)(atmera_render_handle renderer, atmera_buffer_handle buffer);
    uint32_t (*command_bind_constant_buffer)(atmera_command_list_handle list,
        atmera_buffer_handle buffer);
    // Explicit register layout for one texture, one sampler and one constant buffer.
    uint32_t (*create_binding_layout)(atmera_render_handle renderer,
        const atmera_binding_layout_desc* desc, atmera_binding_layout_handle* out_layout);
    void (*destroy_binding_layout)(atmera_binding_layout_handle layout);
    uint32_t (*create_pipeline_with_binding_layout)(atmera_render_handle renderer,
        atmera_shader_handle vertex_shader, atmera_shader_handle pixel_shader,
        atmera_binding_layout_handle layout, atmera_pipeline_handle* out_pipeline);
    uint32_t (*create_binding_set_with_layout)(atmera_render_handle renderer,
        atmera_binding_layout_handle layout, atmera_texture_view_handle view,
        atmera_sampler_handle sampler, atmera_binding_set_handle* out_set);
    // Two distinct vertex streams. Attribute offsets are relative to their own slot.
    uint32_t (*create_pipeline_with_two_vertex_streams)(atmera_render_handle renderer,
        atmera_shader_handle vertex_shader, atmera_shader_handle pixel_shader,
        const atmera_vertex_layout_desc* slot0, const atmera_vertex_layout_desc* slot1,
        atmera_pipeline_handle* out_pipeline);
    uint32_t (*bind_vertex_buffer_slot1)(atmera_render_handle renderer,
        atmera_buffer_handle buffer, uint32_t stride_bytes, uint32_t offset_bytes);
    uint32_t (*command_bind_vertex_buffer_slot1)(atmera_command_list_handle list,
        atmera_buffer_handle buffer, uint32_t stride_bytes, uint32_t offset_bytes);
    // Two textures at tN and t(N+1), sharing one sampler at sN.
    uint32_t (*create_pipeline_with_texture_pair)(atmera_render_handle renderer,
        atmera_shader_handle vertex_shader, atmera_shader_handle pixel_shader,
        atmera_binding_layout_handle layout, atmera_pipeline_handle* out_pipeline);
    uint32_t (*create_binding_set_with_texture_pair)(atmera_render_handle renderer,
        atmera_binding_layout_handle layout, atmera_texture_view_handle first_view,
        atmera_texture_view_handle second_view, atmera_sampler_handle sampler,
        atmera_binding_set_handle* out_set);
    // Two textures and two independent samplers at consecutive t/s registers.
    uint32_t (*create_pipeline_with_texture_sampler_pair)(atmera_render_handle renderer,
        atmera_shader_handle vertex_shader, atmera_shader_handle pixel_shader,
        atmera_binding_layout_handle layout, atmera_pipeline_handle* out_pipeline);
    uint32_t (*create_binding_set_with_texture_sampler_pair)(atmera_render_handle renderer,
        atmera_binding_layout_handle layout, atmera_texture_view_handle first_view,
        atmera_sampler_handle first_sampler, atmera_texture_view_handle second_view,
        atmera_sampler_handle second_sampler, atmera_binding_set_handle* out_set);
    // Three distinct vertex streams. Appended to preserve the ABI v1 prefix.
    uint32_t (*create_pipeline_with_three_vertex_streams)(atmera_render_handle renderer,
        atmera_shader_handle vertex_shader, atmera_shader_handle pixel_shader,
        const atmera_vertex_layout_desc* slot0, const atmera_vertex_layout_desc* slot1,
        const atmera_vertex_layout_desc* slot2, atmera_pipeline_handle* out_pipeline);
    uint32_t (*bind_vertex_buffer_slot2)(atmera_render_handle renderer,
        atmera_buffer_handle buffer, uint32_t stride_bytes, uint32_t offset_bytes);
    uint32_t (*command_bind_vertex_buffer_slot2)(atmera_command_list_handle list,
        atmera_buffer_handle buffer, uint32_t stride_bytes, uint32_t offset_bytes);
    // Appended in ABI v1; the caller owns out_reflection and sets struct_size.
    uint32_t (*get_shader_reflection)(atmera_shader_handle shader,
        atmera_shader_reflection* out_reflection);
    // Query required_count with (out_bindings=NULL, capacity=0), then provide
    // caller-owned entries with struct_size set. Invalid capacity or entry sizes
    // are rejected before writing entries.
    uint32_t (*get_shader_bindings)(atmera_shader_handle shader,
        atmera_shader_binding* out_bindings, uint32_t capacity, uint32_t* required_count);
    uint32_t (*get_shader_cache_stats)(atmera_render_handle renderer,
        atmera_shader_cache_stats* out_stats);
    // Binding indices match get_shader_bindings. Query required_bytes (including NUL)
    // with (out_utf8=NULL, capacity=0); caller owns the UTF-8 output buffer.
    uint32_t (*get_shader_binding_name)(atmera_shader_handle shader, uint32_t binding_index,
        char* out_utf8, uint32_t capacity, uint32_t* required_bytes);
} atmera_render_api;

typedef uint32_t (*atmera_get_render_api_fn)(uint32_t host_version, const atmera_render_api** out_api);
ATMERA_RENDER_EXPORT uint32_t atmera_get_render_api(uint32_t host_version, const atmera_render_api** out_api);

// Optional diagnostic export. Reads the latest shader compilation error on this thread.
// The caller owns out_utf8. Query required_bytes (including NUL) with a null buffer;
// then provide at least that many bytes. Returns nonzero for invalid version/arguments
// or insufficient capacity. An empty string means no shader compilation error.
typedef uint32_t (*atmera_get_last_shader_error_fn)(uint32_t host_version,
    char* out_utf8, uint32_t capacity, uint32_t* required_bytes);
ATMERA_RENDER_EXPORT uint32_t atmera_get_last_shader_error(uint32_t host_version,
    char* out_utf8, uint32_t capacity, uint32_t* required_bytes);

#ifdef __cplusplus
}
#endif
