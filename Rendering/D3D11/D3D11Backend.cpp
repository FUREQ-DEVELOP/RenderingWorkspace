#include <ATMERA/RenderABI.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <limits>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {
thread_local char last_shader_error[8192]{};

void save_shader_error(ID3DBlob* errors) {
    if (!errors) return;
    const size_t count = (std::min)(errors->GetBufferSize(), sizeof(last_shader_error) - 1);
    std::memcpy(last_shader_error, errors->GetBufferPointer(), count);
    last_shader_error[count] = '\0';
}

struct Presentation {
    ComPtr<IDXGISwapChain> swap_chain;
    ComPtr<ID3D11RenderTargetView> target;
};

struct ShaderCacheEntry {
    std::string source;
    std::string entry;
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3D11ShaderReflection> reflection;
    uint32_t constant_buffer_bindings = 0;
    uint32_t texture_bindings = 0;
    uint32_t sampler_bindings = 0;
    bool valid = false;
};

struct Renderer {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Presentation presentation;
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11Texture2D> readback;
    std::array<uint8_t, 4> center_pixel{};
    bool capture = false;
    D3D_FEATURE_LEVEL feature_level{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool software = false;
    bool recording = false;
    bool pipeline_bound = false;
    uint32_t required_stride = 0;
    uint32_t bound_stride = 0;
    uint32_t bound_bytes = 0;
    uint32_t required_stride1 = 0;
    uint32_t bound_stride1 = 0;
    uint32_t bound_bytes1 = 0;
    uint32_t required_stride2 = 0;
    uint32_t bound_stride2 = 0;
    uint32_t bound_bytes2 = 0;
    uint32_t index_element_bytes = 0;
    uint32_t index_bound_bytes = 0;
    uint32_t texture_register = 0;
    uint32_t texture_count = 1;
    uint32_t sampler_register = 0;
    uint32_t sampler_count = 1;
    uint32_t constant_register = 0;
    std::array<ShaderCacheEntry, 2> shader_cache;
    uint32_t shader_cache_hits = 0;
    uint32_t shader_cache_misses = 0;
};

struct BindingLayout {
    Renderer* owner = nullptr;
    uint32_t texture_register = 0;
    uint32_t sampler_register = 0;
    uint32_t constant_register = 0;
};

struct Shader {
    Renderer* owner = nullptr;
    uint32_t stage = 0;
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3D11ShaderReflection> reflection;
    uint32_t constant_buffer_bindings = 0;
    uint32_t texture_bindings = 0;
    uint32_t sampler_bindings = 0;
};

struct Pipeline {
    Renderer* owner = nullptr;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11InputLayout> input_layout;
    uint32_t required_stride = 0;
    uint32_t required_stride1 = 0;
    uint32_t required_stride2 = 0;
    uint32_t texture_register = 0;
    uint32_t texture_count = 1;
    uint32_t sampler_register = 0;
    uint32_t sampler_count = 1;
    uint32_t constant_register = 0;
};

struct TextureView {
    Renderer* owner = nullptr;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
};

struct Sampler {
    Renderer* owner = nullptr;
    ComPtr<ID3D11SamplerState> state;
};

struct BindingSet {
    Renderer* owner = nullptr;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11Texture2D> texture2;
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11ShaderResourceView> view2;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11SamplerState> sampler2;
    uint32_t texture_register = 0;
    uint32_t texture_count = 1;
    uint32_t sampler_register = 0;
    uint32_t sampler_count = 1;
};

struct CommandList {
    Renderer* owner = nullptr;
    ComPtr<ID3D11DeviceContext> deferred;
    ComPtr<ID3D11CommandList> compiled;
    ComPtr<ID3D11Texture2D> active_target;
    bool submitted = false;
    bool render_target_open = false;
    bool pipeline_bound = false;
    uint32_t required_stride = 0;
    uint32_t bound_stride = 0;
    uint32_t bound_bytes = 0;
    uint32_t required_stride1 = 0;
    uint32_t bound_stride1 = 0;
    uint32_t bound_bytes1 = 0;
    uint32_t required_stride2 = 0;
    uint32_t bound_stride2 = 0;
    uint32_t bound_bytes2 = 0;
    uint32_t index_element_bytes = 0;
    uint32_t index_bound_bytes = 0;
    uint32_t texture_register = 0;
    uint32_t texture_count = 1;
    uint32_t sampler_register = 0;
    uint32_t sampler_count = 1;
    uint32_t constant_register = 0;
};

struct Fence {
    Renderer* owner = nullptr;
    ComPtr<ID3D11Query> query;
    ComPtr<ID3D11CommandList> commands;
};

struct Buffer {
    Renderer* owner = nullptr;
    uint32_t size = 0;
    uint32_t usage = 0;
    ComPtr<ID3D11Buffer> gpu;
    ComPtr<ID3D11Buffer> staging;
};

struct Texture {
    Renderer* owner = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    ComPtr<ID3D11Texture2D> gpu;
    ComPtr<ID3D11Texture2D> staging;
};

HRESULT make_target(Renderer& renderer) {
    ComPtr<ID3D11Texture2D> back_buffer;
    HRESULT hr = renderer.presentation.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(hr)) return hr;
    return renderer.device->CreateRenderTargetView(back_buffer.Get(), nullptr, &renderer.presentation.target);
}

uint32_t create(const atmera_render_desc* desc, atmera_render_handle* out_handle) {
    last_shader_error[0] = '\0';
    if (!desc || !out_handle || desc->struct_size < sizeof(atmera_render_desc) ||
        !desc->hlsl_source || !desc->hlsl_source_size ||
        !desc->width || !desc->height) return static_cast<uint32_t>(E_INVALIDARG);
    *out_handle = nullptr;
    auto renderer = std::unique_ptr<Renderer>(new (std::nothrow) Renderer());
    if (!renderer) return static_cast<uint32_t>(E_OUTOFMEMORY);
    renderer->width = desc->width;
    renderer->height = desc->height;
    renderer->software = desc->use_warp != 0;
    renderer->capture = desc->capture_center_pixel != 0;
    HRESULT hr = D3D11CreateDevice(nullptr,
        renderer->software ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &renderer->device, &renderer->feature_level, &renderer->context);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    if (desc->window_handle) {
        DXGI_SWAP_CHAIN_DESC swap{};
        swap.BufferDesc.Width = desc->width;
        swap.BufferDesc.Height = desc->height;
        swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap.SampleDesc.Count = 1;
        swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap.BufferCount = 2;
        swap.OutputWindow = static_cast<HWND>(desc->window_handle);
        swap.Windowed = TRUE;
        swap.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<IDXGIDevice> dxgi_device;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory> factory;
        hr = renderer->device.As(&dxgi_device);
        if (SUCCEEDED(hr)) hr = dxgi_device->GetAdapter(&adapter);
        if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) hr = factory->CreateSwapChain(renderer->device.Get(), &swap,
            &renderer->presentation.swap_chain);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
    }
    if (renderer->presentation.swap_chain) {
        hr = make_target(*renderer);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
    }
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ComPtr<ID3DBlob> errors;
    hr = D3DCompile(desc->hlsl_source, desc->hlsl_source_size, "Triangle.hlsl", nullptr,
        nullptr, "VSMain", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs, &errors);
    if (FAILED(hr)) { save_shader_error(errors.Get()); return static_cast<uint32_t>(hr); }
    errors.Reset();
    hr = D3DCompile(desc->hlsl_source, desc->hlsl_source_size, "Triangle.hlsl", nullptr,
        nullptr, "PSMain", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps, &errors);
    if (FAILED(hr)) { save_shader_error(errors.Get()); return static_cast<uint32_t>(hr); }
    hr = renderer->device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &renderer->vertex_shader);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer->device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &renderer->pixel_shader);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    if (renderer->capture && renderer->presentation.swap_chain) {
        D3D11_TEXTURE2D_DESC readback_desc{};
        readback_desc.Width = 1;
        readback_desc.Height = 1;
        readback_desc.MipLevels = 1;
        readback_desc.ArraySize = 1;
        readback_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        readback_desc.SampleDesc.Count = 1;
        readback_desc.Usage = D3D11_USAGE_STAGING;
        readback_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = renderer->device->CreateTexture2D(&readback_desc, nullptr, &renderer->readback);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
    }
    *out_handle = renderer.release();
    return 0;
}

uint32_t get_caps(atmera_render_handle handle, atmera_render_caps* out_caps) {
    if (!handle || !out_caps || out_caps->struct_size < sizeof(atmera_render_caps)) return static_cast<uint32_t>(E_INVALIDARG);
    const auto& renderer = *static_cast<Renderer*>(handle);
    out_caps->api_major = 11;
    out_caps->feature_level = static_cast<uint32_t>(renderer.feature_level);
    out_caps->shader_model = 50;
    out_caps->is_software = renderer.software ? 1u : 0u;
    return 0;
}

uint32_t resize(atmera_render_handle handle, uint32_t width, uint32_t height) {
    if (!handle || !width || !height) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    if (renderer.recording || !renderer.presentation.swap_chain) return static_cast<uint32_t>(E_FAIL);
    renderer.context->OMSetRenderTargets(0, nullptr, nullptr);
    renderer.presentation.target.Reset();
    HRESULT hr = renderer.presentation.swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    renderer.width = width;
    renderer.height = height;
    return static_cast<uint32_t>(make_target(renderer));
}

uint32_t begin_frame(atmera_render_handle handle) {
    if (!handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    if (renderer.recording || !renderer.presentation.swap_chain) return static_cast<uint32_t>(E_FAIL);
    const float clear[] = {0.055f, 0.075f, 0.095f, 1.0f};
    renderer.context->OMSetRenderTargets(1, renderer.presentation.target.GetAddressOf(), nullptr);
    renderer.context->ClearRenderTargetView(renderer.presentation.target.Get(), clear);
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(renderer.width), static_cast<float>(renderer.height), 0, 1};
    renderer.context->RSSetViewports(1, &viewport);
    renderer.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11ShaderResourceView* no_views[16]{};
    ID3D11SamplerState* no_samplers[16]{};
    renderer.context->PSSetShaderResources(0, 16, no_views);
    renderer.context->PSSetSamplers(0, 16, no_samplers);
    renderer.recording = true;
    renderer.pipeline_bound = false;
    renderer.required_stride = 0;
    renderer.bound_stride = 0;
    renderer.bound_bytes = 0;
    renderer.required_stride1 = 0;
    renderer.bound_stride1 = 0;
    renderer.bound_bytes1 = 0;
    renderer.required_stride2 = 0;
    renderer.bound_stride2 = 0;
    renderer.bound_bytes2 = 0;
    renderer.index_element_bytes = 0;
    renderer.index_bound_bytes = 0;
    renderer.texture_register = 0;
    renderer.texture_count = 1;
    renderer.sampler_register = 0;
    renderer.sampler_count = 1;
    renderer.constant_register = 0;
    return 0;
}

uint32_t bind_pipeline(atmera_render_handle handle, atmera_pipeline_handle pipeline_handle) {
    if (!handle || !pipeline_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& pipeline = *static_cast<Pipeline*>(pipeline_handle);
    if (!renderer.recording || pipeline.owner != &renderer) return static_cast<uint32_t>(E_INVALIDARG);
    renderer.context->VSSetShader(pipeline.vertex.Get(), nullptr, 0);
    renderer.context->PSSetShader(pipeline.pixel.Get(), nullptr, 0);
    renderer.context->IASetInputLayout(pipeline.input_layout.Get());
    renderer.required_stride = pipeline.required_stride;
    renderer.required_stride1 = pipeline.required_stride1;
    renderer.required_stride2 = pipeline.required_stride2;
    renderer.texture_register = pipeline.texture_register;
    renderer.texture_count = pipeline.texture_count;
    renderer.sampler_register = pipeline.sampler_register;
    renderer.sampler_count = pipeline.sampler_count;
    renderer.constant_register = pipeline.constant_register;
    renderer.pipeline_bound = true;
    return 0;
}

uint32_t bind_vertex_buffer(atmera_render_handle handle, atmera_buffer_handle buffer_handle,
    uint32_t stride_bytes, uint32_t offset_bytes) {
    if (!handle || !buffer_handle || !stride_bytes) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (!renderer.recording || buffer.owner != &renderer || buffer.usage != ATMERA_BUFFER_VERTEX ||
        offset_bytes >= buffer.size || stride_bytes > buffer.size - offset_bytes ||
        (renderer.required_stride && stride_bytes < renderer.required_stride))
        return static_cast<uint32_t>(E_INVALIDARG);
    ID3D11Buffer* native = buffer.gpu.Get();
    renderer.context->IASetVertexBuffers(0, 1, &native, &stride_bytes, &offset_bytes);
    renderer.bound_stride = stride_bytes;
    renderer.bound_bytes = buffer.size - offset_bytes;
    return 0;
}

uint32_t bind_vertex_buffer_slot1(atmera_render_handle handle, atmera_buffer_handle buffer_handle,
    uint32_t stride_bytes, uint32_t offset_bytes) {
    if (!handle || !buffer_handle || !stride_bytes) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (!renderer.recording || buffer.owner != &renderer || buffer.usage != ATMERA_BUFFER_VERTEX ||
        offset_bytes >= buffer.size || stride_bytes > buffer.size - offset_bytes ||
        (renderer.required_stride1 && stride_bytes < renderer.required_stride1))
        return static_cast<uint32_t>(E_INVALIDARG);
    ID3D11Buffer* native = buffer.gpu.Get();
    renderer.context->IASetVertexBuffers(1, 1, &native, &stride_bytes, &offset_bytes);
    renderer.bound_stride1 = stride_bytes;
    renderer.bound_bytes1 = buffer.size - offset_bytes;
    return 0;
}

uint32_t bind_vertex_buffer_slot2(atmera_render_handle handle, atmera_buffer_handle buffer_handle,
    uint32_t stride_bytes, uint32_t offset_bytes) {
    if (!handle || !buffer_handle || !stride_bytes) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (!renderer.recording || buffer.owner != &renderer || buffer.usage != ATMERA_BUFFER_VERTEX ||
        offset_bytes >= buffer.size || stride_bytes > buffer.size - offset_bytes ||
        (renderer.required_stride2 && stride_bytes < renderer.required_stride2))
        return static_cast<uint32_t>(E_INVALIDARG);
    ID3D11Buffer* native = buffer.gpu.Get();
    renderer.context->IASetVertexBuffers(2, 1, &native, &stride_bytes, &offset_bytes);
    renderer.bound_stride2 = stride_bytes;
    renderer.bound_bytes2 = buffer.size - offset_bytes;
    return 0;
}

uint32_t bind_constant_buffer(atmera_render_handle handle, atmera_buffer_handle buffer_handle) {
    if (!handle || !buffer_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (!renderer.recording || buffer.owner != &renderer || buffer.usage != ATMERA_BUFFER_CONSTANT)
        return static_cast<uint32_t>(E_INVALIDARG);
    ID3D11Buffer* native = buffer.gpu.Get();
    renderer.context->VSSetConstantBuffers(renderer.constant_register, 1, &native);
    renderer.context->PSSetConstantBuffers(renderer.constant_register, 1, &native);
    return 0;
}

uint32_t bind_index_buffer(atmera_render_handle handle, atmera_buffer_handle buffer_handle,
    uint32_t index_format, uint32_t offset_bytes) {
    if (!handle || !buffer_handle ||
        (index_format != ATMERA_INDEX_UINT16 && index_format != ATMERA_INDEX_UINT32))
        return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    const uint32_t element_bytes = index_format == ATMERA_INDEX_UINT16 ? 2u : 4u;
    if (!renderer.recording || buffer.owner != &renderer || buffer.usage != ATMERA_BUFFER_INDEX ||
        offset_bytes >= buffer.size || offset_bytes % element_bytes ||
        buffer.size - offset_bytes < element_bytes)
        return static_cast<uint32_t>(E_INVALIDARG);
    renderer.context->IASetIndexBuffer(buffer.gpu.Get(),
        index_format == ATMERA_INDEX_UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT,
        offset_bytes);
    renderer.index_element_bytes = element_bytes;
    renderer.index_bound_bytes = buffer.size - offset_bytes;
    return 0;
}

uint32_t draw(atmera_render_handle handle, uint32_t vertex_count) {
    if (!handle || !vertex_count) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    if (!renderer.recording || !renderer.pipeline_bound ||
        (renderer.required_stride && (renderer.bound_stride < renderer.required_stride ||
            vertex_count > renderer.bound_bytes / renderer.bound_stride)) ||
        (renderer.required_stride1 && (renderer.bound_stride1 < renderer.required_stride1 ||
            vertex_count > renderer.bound_bytes1 / renderer.bound_stride1)) ||
        (renderer.required_stride2 && (renderer.bound_stride2 < renderer.required_stride2 ||
            vertex_count > renderer.bound_bytes2 / renderer.bound_stride2)))
        return static_cast<uint32_t>(E_FAIL);
    renderer.context->Draw(vertex_count, 0);
    return 0;
}

uint32_t draw_indexed(atmera_render_handle handle, uint32_t index_count) {
    if (!handle || !index_count) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    if (!renderer.recording || !renderer.pipeline_bound || !renderer.index_element_bytes ||
        index_count > renderer.index_bound_bytes / renderer.index_element_bytes ||
        (renderer.required_stride && (renderer.bound_stride < renderer.required_stride ||
            !renderer.bound_bytes)) ||
        (renderer.required_stride1 && (renderer.bound_stride1 < renderer.required_stride1 ||
            !renderer.bound_bytes1)) ||
        (renderer.required_stride2 && (renderer.bound_stride2 < renderer.required_stride2 ||
            !renderer.bound_bytes2))) return static_cast<uint32_t>(E_FAIL);
    renderer.context->DrawIndexed(index_count, 0, 0);
    return 0;
}

uint32_t end_frame(atmera_render_handle handle) {
    if (!handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    if (!renderer.recording) return static_cast<uint32_t>(E_FAIL);
    renderer.recording = false;
    if (renderer.capture) {
        ComPtr<ID3D11Texture2D> back_buffer;
        HRESULT hr = renderer.presentation.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        const D3D11_BOX box{renderer.width / 2, renderer.height / 2, 0,
            renderer.width / 2 + 1, renderer.height / 2 + 1, 1};
        renderer.context->CopySubresourceRegion(renderer.readback.Get(), 0, 0, 0, 0, back_buffer.Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = renderer.context->Map(renderer.readback.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        std::memcpy(renderer.center_pixel.data(), mapped.pData, renderer.center_pixel.size());
        renderer.context->Unmap(renderer.readback.Get(), 0);
    }
    return static_cast<uint32_t>(renderer.presentation.swap_chain->Present(0, 0));
}

uint32_t render_frame(atmera_render_handle handle) {
    const auto started = begin_frame(handle);
    if (started != 0) return started;
    auto& renderer = *static_cast<Renderer*>(handle);
    renderer.context->VSSetShader(renderer.vertex_shader.Get(), nullptr, 0);
    renderer.context->PSSetShader(renderer.pixel_shader.Get(), nullptr, 0);
    renderer.pipeline_bound = true;
    renderer.context->Draw(3, 0);
    return end_frame(handle);
}

uint32_t get_center_pixel(atmera_render_handle handle, uint8_t out_rgba[4]) {
    if (!handle || !out_rgba) return static_cast<uint32_t>(E_INVALIDARG);
    const auto& renderer = *static_cast<Renderer*>(handle);
    if (!renderer.capture || !renderer.presentation.swap_chain) return static_cast<uint32_t>(E_FAIL);
    std::memcpy(out_rgba, renderer.center_pixel.data(), 4);
    return 0;
}

uint32_t create_buffer(atmera_render_handle handle, const atmera_buffer_desc* desc,
    atmera_buffer_handle* out_buffer) {
    if (!handle || !desc || !out_buffer || desc->struct_size < sizeof(atmera_buffer_desc) ||
        !desc->size_bytes || (desc->usage != ATMERA_BUFFER_VERTEX &&
        desc->usage != ATMERA_BUFFER_INDEX && desc->usage != ATMERA_BUFFER_CONSTANT) ||
        (desc->usage == ATMERA_BUFFER_CONSTANT && desc->size_bytes % 256 != 0))
        return static_cast<uint32_t>(E_INVALIDARG);
    *out_buffer = nullptr;
    auto& renderer = *static_cast<Renderer*>(handle);
    auto buffer = std::unique_ptr<Buffer>(new (std::nothrow) Buffer());
    if (!buffer) return static_cast<uint32_t>(E_OUTOFMEMORY);
    buffer->owner = &renderer;
    buffer->size = desc->size_bytes;
    buffer->usage = desc->usage;
    D3D11_BUFFER_DESC gpu_desc{};
    gpu_desc.ByteWidth = desc->size_bytes;
    gpu_desc.Usage = D3D11_USAGE_DEFAULT;
    if (desc->usage & ATMERA_BUFFER_VERTEX) gpu_desc.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
    if (desc->usage & ATMERA_BUFFER_INDEX) gpu_desc.BindFlags |= D3D11_BIND_INDEX_BUFFER;
    if (desc->usage & ATMERA_BUFFER_CONSTANT) gpu_desc.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
    HRESULT hr = renderer.device->CreateBuffer(&gpu_desc, nullptr, &buffer->gpu);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    gpu_desc.Usage = D3D11_USAGE_STAGING;
    gpu_desc.BindFlags = 0;
    gpu_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = renderer.device->CreateBuffer(&gpu_desc, nullptr, &buffer->staging);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_buffer = buffer.release();
    return 0;
}

uint32_t write_buffer(atmera_render_handle handle, atmera_buffer_handle resource,
    uint32_t offset, const void* data, uint32_t size) {
    if (!handle || !resource || !data || !size) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(resource);
    if (renderer.recording) return static_cast<uint32_t>(E_FAIL);
    if (buffer.owner != &renderer || offset > buffer.size || size > buffer.size - offset)
        return static_cast<uint32_t>(E_INVALIDARG);
    if (buffer.usage == ATMERA_BUFFER_CONSTANT && (offset != 0 || size != buffer.size))
        return static_cast<uint32_t>(E_INVALIDARG);
    const D3D11_BOX box{offset, 0, 0, offset + size, 1, 1};
    renderer.context->UpdateSubresource(buffer.gpu.Get(), 0,
        buffer.usage == ATMERA_BUFFER_CONSTANT ? nullptr : &box, data, 0, 0);
    return 0;
}

uint32_t read_buffer(atmera_render_handle handle, atmera_buffer_handle resource,
    uint32_t offset, void* data, uint32_t size) {
    if (!handle || !resource || !data || !size) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(resource);
    if (renderer.recording) return static_cast<uint32_t>(E_FAIL);
    if (buffer.owner != &renderer || offset > buffer.size || size > buffer.size - offset)
        return static_cast<uint32_t>(E_INVALIDARG);
    renderer.context->CopyResource(buffer.staging.Get(), buffer.gpu.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = renderer.context->Map(buffer.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    std::memcpy(data, static_cast<const uint8_t*>(mapped.pData) + offset, size);
    renderer.context->Unmap(buffer.staging.Get(), 0);
    return 0;
}

void destroy_buffer(atmera_buffer_handle resource) { delete static_cast<Buffer*>(resource); }

uint32_t create_texture(atmera_render_handle handle, const atmera_texture_desc* desc,
    atmera_texture_handle* out_texture) {
    if (!handle || !desc || !out_texture || desc->struct_size < sizeof(atmera_texture_desc) ||
        !desc->width || !desc->height || desc->width > std::numeric_limits<uint32_t>::max() / 4)
        return static_cast<uint32_t>(E_INVALIDARG);
    *out_texture = nullptr;
    auto& renderer = *static_cast<Renderer*>(handle);
    auto texture = std::unique_ptr<Texture>(new (std::nothrow) Texture());
    if (!texture) return static_cast<uint32_t>(E_OUTOFMEMORY);
    texture->owner = &renderer;
    texture->width = desc->width;
    texture->height = desc->height;
    D3D11_TEXTURE2D_DESC gpu_desc{};
    gpu_desc.Width = desc->width;
    gpu_desc.Height = desc->height;
    gpu_desc.MipLevels = 1;
    gpu_desc.ArraySize = 1;
    gpu_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    gpu_desc.SampleDesc.Count = 1;
    gpu_desc.Usage = D3D11_USAGE_DEFAULT;
    gpu_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    HRESULT hr = renderer.device->CreateTexture2D(&gpu_desc, nullptr, &texture->gpu);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    gpu_desc.Usage = D3D11_USAGE_STAGING;
    gpu_desc.BindFlags = 0;
    gpu_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = renderer.device->CreateTexture2D(&gpu_desc, nullptr, &texture->staging);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_texture = texture.release();
    return 0;
}

bool valid_texture_io(const Texture& texture, uint32_t row_pitch, uint32_t byte_count) {
    const uint64_t row_bytes = static_cast<uint64_t>(texture.width) * 4;
    return row_pitch >= row_bytes &&
        byte_count >= static_cast<uint64_t>(texture.height - 1) * row_pitch + row_bytes;
}

uint32_t write_texture(atmera_render_handle handle, atmera_texture_handle resource,
    const uint8_t* pixels, uint32_t row_pitch, uint32_t byte_count) {
    if (!handle || !resource || !pixels) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& texture = *static_cast<Texture*>(resource);
    if (renderer.recording) return static_cast<uint32_t>(E_FAIL);
    if (texture.owner != &renderer || !valid_texture_io(texture, row_pitch, byte_count))
        return static_cast<uint32_t>(E_INVALIDARG);
    renderer.context->UpdateSubresource(texture.gpu.Get(), 0, nullptr, pixels, row_pitch, 0);
    return 0;
}

uint32_t read_texture(atmera_render_handle handle, atmera_texture_handle resource,
    uint8_t* pixels, uint32_t row_pitch, uint32_t byte_count) {
    if (!handle || !resource || !pixels) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& texture = *static_cast<Texture*>(resource);
    if (renderer.recording) return static_cast<uint32_t>(E_FAIL);
    if (texture.owner != &renderer || !valid_texture_io(texture, row_pitch, byte_count))
        return static_cast<uint32_t>(E_INVALIDARG);
    renderer.context->CopyResource(texture.staging.Get(), texture.gpu.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = renderer.context->Map(texture.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    const auto row_bytes = static_cast<size_t>(texture.width) * 4;
    for (uint32_t y = 0; y < texture.height; ++y)
        std::memcpy(pixels + static_cast<size_t>(y) * row_pitch,
            static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
            row_bytes);
    renderer.context->Unmap(texture.staging.Get(), 0);
    return 0;
}

void destroy_texture(atmera_texture_handle resource) { delete static_cast<Texture*>(resource); }

uint32_t create_shader(atmera_render_handle handle, const atmera_shader_desc* desc,
    atmera_shader_handle* out_shader) {
    if (!handle || !desc || !out_shader || desc->struct_size < sizeof(atmera_shader_desc) ||
        !desc->source || !desc->source_size || !desc->entry_point || !desc->entry_point[0] ||
        (desc->stage != ATMERA_SHADER_VERTEX && desc->stage != ATMERA_SHADER_PIXEL))
        return static_cast<uint32_t>(E_INVALIDARG);
    *out_shader = nullptr;
    auto shader = std::unique_ptr<Shader>(new (std::nothrow) Shader());
    if (!shader) return static_cast<uint32_t>(E_OUTOFMEMORY);
    auto& renderer = *static_cast<Renderer*>(handle);
    shader->owner = &renderer;
    shader->stage = desc->stage;
    auto& cache = renderer.shader_cache[desc->stage == ATMERA_SHADER_VERTEX ? 0 : 1];
    if (cache.valid && cache.source.size() == desc->source_size &&
        std::memcmp(cache.source.data(), desc->source, desc->source_size) == 0 &&
        cache.entry == desc->entry_point) {
        shader->bytecode = cache.bytecode;
        shader->reflection = cache.reflection;
        shader->constant_buffer_bindings = cache.constant_buffer_bindings;
        shader->texture_bindings = cache.texture_bindings;
        shader->sampler_bindings = cache.sampler_bindings;
        if (renderer.shader_cache_hits != (std::numeric_limits<uint32_t>::max)())
            ++renderer.shader_cache_hits;
        *out_shader = shader.release();
        return 0;
    }
    if (renderer.shader_cache_misses != (std::numeric_limits<uint32_t>::max)())
        ++renderer.shader_cache_misses;
    const char* profile = desc->stage == ATMERA_SHADER_VERTEX ? "vs_5_0" : "ps_5_0";
    HRESULT hr = D3DCompile(desc->source, desc->source_size, "Shader.hlsl", nullptr,
        nullptr, desc->entry_point, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0,
        &shader->bytecode, nullptr);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = D3DReflect(shader->bytecode->GetBufferPointer(), shader->bytecode->GetBufferSize(),
        IID_PPV_ARGS(&shader->reflection));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    D3D11_SHADER_DESC shader_desc{};
    hr = shader->reflection->GetDesc(&shader_desc);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    for (UINT i = 0; i < shader_desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        hr = shader->reflection->GetResourceBindingDesc(i, &binding);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        if (binding.Type == D3D_SIT_CBUFFER) ++shader->constant_buffer_bindings;
        else if (binding.Type == D3D_SIT_TEXTURE) ++shader->texture_bindings;
        else if (binding.Type == D3D_SIT_SAMPLER) ++shader->sampler_bindings;
    }
    try {
        std::string source(desc->source, desc->source_size);
        std::string entry(desc->entry_point);
        cache.source.swap(source);
        cache.entry.swap(entry);
        cache.bytecode = shader->bytecode;
        cache.reflection = shader->reflection;
        cache.constant_buffer_bindings = shader->constant_buffer_bindings;
        cache.texture_bindings = shader->texture_bindings;
        cache.sampler_bindings = shader->sampler_bindings;
        cache.valid = true;
    } catch (...) { /* Compilation succeeds even if caching runs out of memory. */ }
    *out_shader = shader.release();
    return 0;
}

void destroy_shader(atmera_shader_handle shader) { delete static_cast<Shader*>(shader); }

uint32_t get_shader_reflection(atmera_shader_handle handle,
    atmera_shader_reflection* out_reflection) {
    if (!handle || !out_reflection || out_reflection->struct_size < sizeof(atmera_shader_reflection))
        return static_cast<uint32_t>(E_INVALIDARG);
    const auto& shader = *static_cast<Shader*>(handle);
    *out_reflection = {sizeof(atmera_shader_reflection), shader.stage,
        shader.constant_buffer_bindings, shader.texture_bindings, shader.sampler_bindings};
    return 0;
}

uint32_t get_shader_bindings(atmera_shader_handle handle, atmera_shader_binding* out_bindings,
    uint32_t capacity, uint32_t* required_count) {
    if (!handle || !required_count) return static_cast<uint32_t>(E_INVALIDARG);
    const auto& shader = *static_cast<Shader*>(handle);
    const uint32_t count = shader.constant_buffer_bindings + shader.texture_bindings +
        shader.sampler_bindings;
    *required_count = count;
    if (!out_bindings && capacity == 0) return 0;
    if (!out_bindings || capacity < count) return static_cast<uint32_t>(E_INVALIDARG);
    for (uint32_t i = 0; i < count; ++i)
        if (out_bindings[i].struct_size < sizeof(atmera_shader_binding))
            return static_cast<uint32_t>(E_INVALIDARG);

    D3D11_SHADER_DESC shader_desc{};
    HRESULT hr = shader.reflection->GetDesc(&shader_desc);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    uint32_t written = 0;
    for (UINT i = 0; i < shader_desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        hr = shader.reflection->GetResourceBindingDesc(i, &binding);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        const uint32_t kind = binding.Type == D3D_SIT_CBUFFER ? ATMERA_SHADER_BINDING_CONSTANT_BUFFER :
            binding.Type == D3D_SIT_TEXTURE ? ATMERA_SHADER_BINDING_TEXTURE :
            binding.Type == D3D_SIT_SAMPLER ? ATMERA_SHADER_BINDING_SAMPLER : 0;
        if (kind != 0) out_bindings[written++] = {sizeof(atmera_shader_binding), kind,
            binding.BindPoint, 0, binding.BindCount};
    }
    return written == count ? 0 : static_cast<uint32_t>(E_FAIL);
}

uint32_t get_shader_cache_stats(atmera_render_handle handle,
    atmera_shader_cache_stats* out_stats) {
    if (!handle || !out_stats || out_stats->struct_size < sizeof(atmera_shader_cache_stats))
        return static_cast<uint32_t>(E_INVALIDARG);
    const auto& renderer = *static_cast<Renderer*>(handle);
    *out_stats = {sizeof(atmera_shader_cache_stats), renderer.shader_cache_hits,
        renderer.shader_cache_misses, static_cast<uint32_t>(renderer.shader_cache[0].valid) +
        static_cast<uint32_t>(renderer.shader_cache[1].valid)};
    return 0;
}

uint32_t get_shader_binding_name(atmera_shader_handle handle, uint32_t binding_index,
    char* out_utf8, uint32_t capacity, uint32_t* required_bytes) {
    if (!handle || !required_bytes) return static_cast<uint32_t>(E_INVALIDARG);
    *required_bytes = 0;
    const auto& shader = *static_cast<Shader*>(handle);
    D3D11_SHADER_DESC shader_desc{};
    HRESULT hr = shader.reflection->GetDesc(&shader_desc);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    uint32_t supported_index = 0;
    for (UINT i = 0; i < shader_desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        hr = shader.reflection->GetResourceBindingDesc(i, &binding);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        if (binding.Type != D3D_SIT_CBUFFER && binding.Type != D3D_SIT_TEXTURE &&
            binding.Type != D3D_SIT_SAMPLER) continue;
        if (supported_index++ != binding_index) continue;
        const auto bytes = std::strlen(binding.Name) + 1;
        if (bytes > UINT32_MAX) return static_cast<uint32_t>(E_FAIL);
        *required_bytes = static_cast<uint32_t>(bytes);
        if (!out_utf8 && capacity == 0) return 0;
        if (!out_utf8 || capacity < bytes) return static_cast<uint32_t>(E_INVALIDARG);
        std::memcpy(out_utf8, binding.Name, bytes);
        return 0;
    }
    return static_cast<uint32_t>(E_INVALIDARG);
}

uint32_t create_pipeline(atmera_render_handle handle, atmera_shader_handle vertex_handle,
    atmera_shader_handle pixel_handle, atmera_pipeline_handle* out_pipeline) {
    if (!handle || !vertex_handle || !pixel_handle || !out_pipeline)
        return static_cast<uint32_t>(E_INVALIDARG);
    *out_pipeline = nullptr;
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& vertex = *static_cast<Shader*>(vertex_handle);
    auto& pixel = *static_cast<Shader*>(pixel_handle);
    if (vertex.owner != &renderer || pixel.owner != &renderer ||
        vertex.stage != ATMERA_SHADER_VERTEX || pixel.stage != ATMERA_SHADER_PIXEL)
        return static_cast<uint32_t>(E_INVALIDARG);
    auto pipeline = std::unique_ptr<Pipeline>(new (std::nothrow) Pipeline());
    if (!pipeline) return static_cast<uint32_t>(E_OUTOFMEMORY);
    pipeline->owner = &renderer;
    HRESULT hr = renderer.device->CreateVertexShader(vertex.bytecode->GetBufferPointer(),
        vertex.bytecode->GetBufferSize(), nullptr, &pipeline->vertex);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer.device->CreatePixelShader(pixel.bytecode->GetBufferPointer(),
        pixel.bytecode->GetBufferSize(), nullptr, &pipeline->pixel);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_pipeline = pipeline.release();
    return 0;
}

uint32_t create_pipeline_with_vertex_layout(atmera_render_handle handle,
    atmera_shader_handle vertex_handle, atmera_shader_handle pixel_handle,
    const atmera_vertex_layout_desc* layout, atmera_pipeline_handle* out_pipeline) {
    if (!handle || !vertex_handle || !pixel_handle || !layout || !out_pipeline ||
        layout->struct_size < sizeof(atmera_vertex_layout_desc) || !layout->attributes ||
        !layout->attribute_count || layout->attribute_count > 16)
        return static_cast<uint32_t>(E_INVALIDARG);
    *out_pipeline = nullptr;
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& vertex = *static_cast<Shader*>(vertex_handle);
    auto& pixel = *static_cast<Shader*>(pixel_handle);
    if (vertex.owner != &renderer || pixel.owner != &renderer ||
        vertex.stage != ATMERA_SHADER_VERTEX || pixel.stage != ATMERA_SHADER_PIXEL)
        return static_cast<uint32_t>(E_INVALIDARG);
    std::array<D3D11_INPUT_ELEMENT_DESC, 16> elements{};
    uint32_t required_stride = 0;
    for (uint32_t i = 0; i < layout->attribute_count; ++i) {
        const auto& attr = layout->attributes[i];
        if (!attr.semantic_name || !attr.semantic_name[0]) return static_cast<uint32_t>(E_INVALIDARG);
        uint32_t bytes = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        switch (attr.format) {
        case ATMERA_VERTEX_FLOAT2: bytes = 8; format = DXGI_FORMAT_R32G32_FLOAT; break;
        case ATMERA_VERTEX_FLOAT3: bytes = 12; format = DXGI_FORMAT_R32G32B32_FLOAT; break;
        case ATMERA_VERTEX_FLOAT4: bytes = 16; format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
        case ATMERA_VERTEX_UNORM8X4: bytes = 4; format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case ATMERA_VERTEX_FLOAT1: bytes = 4; format = DXGI_FORMAT_R32_FLOAT; break;
        case ATMERA_VERTEX_HALF2: bytes = 4; format = DXGI_FORMAT_R16G16_FLOAT; break;
        case ATMERA_VERTEX_HALF4: bytes = 8; format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        case ATMERA_VERTEX_UNORM16X2: bytes = 4; format = DXGI_FORMAT_R16G16_UNORM; break;
        case ATMERA_VERTEX_UNORM8X2: bytes = 2; format = DXGI_FORMAT_R8G8_UNORM; break;
        default: return static_cast<uint32_t>(E_INVALIDARG);
        }
        if (attr.offset_bytes > UINT32_MAX - bytes) return static_cast<uint32_t>(E_INVALIDARG);
        if (attr.offset_bytes + bytes > required_stride)
            required_stride = attr.offset_bytes + bytes;
        elements[i] = {attr.semantic_name, attr.semantic_index, format, 0,
            attr.offset_bytes, D3D11_INPUT_PER_VERTEX_DATA, 0};
    }
    auto pipeline = std::unique_ptr<Pipeline>(new (std::nothrow) Pipeline());
    if (!pipeline) return static_cast<uint32_t>(E_OUTOFMEMORY);
    pipeline->owner = &renderer;
    pipeline->required_stride = required_stride;
    HRESULT hr = renderer.device->CreateVertexShader(vertex.bytecode->GetBufferPointer(),
        vertex.bytecode->GetBufferSize(), nullptr, &pipeline->vertex);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer.device->CreatePixelShader(pixel.bytecode->GetBufferPointer(),
        pixel.bytecode->GetBufferSize(), nullptr, &pipeline->pixel);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer.device->CreateInputLayout(elements.data(), layout->attribute_count,
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(), &pipeline->input_layout);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_pipeline = pipeline.release();
    return 0;
}

uint32_t create_pipeline_with_vertex_streams(atmera_render_handle handle,
    atmera_shader_handle vertex_handle, atmera_shader_handle pixel_handle,
    const atmera_vertex_layout_desc* slot0, const atmera_vertex_layout_desc* slot1,
    const atmera_vertex_layout_desc* slot2,
    atmera_pipeline_handle* out_pipeline) {
    if (!handle || !vertex_handle || !pixel_handle || !slot0 || !slot1 || !out_pipeline ||
        slot0->struct_size < sizeof(atmera_vertex_layout_desc) ||
        slot1->struct_size < sizeof(atmera_vertex_layout_desc) ||
        !slot0->attributes || !slot1->attributes || !slot0->attribute_count ||
        !slot1->attribute_count || slot0->attribute_count > 16 ||
        slot1->attribute_count > 16 - slot0->attribute_count ||
        (slot2 && (slot2->struct_size < sizeof(atmera_vertex_layout_desc) ||
            !slot2->attributes || !slot2->attribute_count ||
            slot2->attribute_count > 16 - slot0->attribute_count - slot1->attribute_count)))
        return static_cast<uint32_t>(E_INVALIDARG);
    *out_pipeline = nullptr;
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& vertex = *static_cast<Shader*>(vertex_handle);
    auto& pixel = *static_cast<Shader*>(pixel_handle);
    if (vertex.owner != &renderer || pixel.owner != &renderer ||
        vertex.stage != ATMERA_SHADER_VERTEX || pixel.stage != ATMERA_SHADER_PIXEL)
        return static_cast<uint32_t>(E_INVALIDARG);
    std::array<D3D11_INPUT_ELEMENT_DESC, 16> elements{};
    uint32_t required[3]{};
    uint32_t count = 0;
    for (uint32_t slot = 0; slot < (slot2 ? 3u : 2u); ++slot) {
        const auto& layout = slot == 0 ? *slot0 : slot == 1 ? *slot1 : *slot2;
        for (uint32_t i = 0; i < layout.attribute_count; ++i) {
            const auto& attr = layout.attributes[i];
            if (!attr.semantic_name || !attr.semantic_name[0]) return static_cast<uint32_t>(E_INVALIDARG);
            uint32_t bytes = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            switch (attr.format) {
            case ATMERA_VERTEX_FLOAT1: bytes = 4; format = DXGI_FORMAT_R32_FLOAT; break;
            case ATMERA_VERTEX_HALF2: bytes = 4; format = DXGI_FORMAT_R16G16_FLOAT; break;
            case ATMERA_VERTEX_HALF4: bytes = 8; format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
            case ATMERA_VERTEX_UNORM16X2: bytes = 4; format = DXGI_FORMAT_R16G16_UNORM; break;
            case ATMERA_VERTEX_UNORM8X2: bytes = 2; format = DXGI_FORMAT_R8G8_UNORM; break;
            case ATMERA_VERTEX_FLOAT2: bytes = 8; format = DXGI_FORMAT_R32G32_FLOAT; break;
            case ATMERA_VERTEX_FLOAT3: bytes = 12; format = DXGI_FORMAT_R32G32B32_FLOAT; break;
            case ATMERA_VERTEX_FLOAT4: bytes = 16; format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
            case ATMERA_VERTEX_UNORM8X4: bytes = 4; format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
            default: return static_cast<uint32_t>(E_INVALIDARG);
            }
            if (attr.offset_bytes > UINT32_MAX - bytes) return static_cast<uint32_t>(E_INVALIDARG);
            required[slot] = (std::max)(required[slot], attr.offset_bytes + bytes);
            elements[count++] = {attr.semantic_name, attr.semantic_index, format, slot,
                attr.offset_bytes, D3D11_INPUT_PER_VERTEX_DATA, 0};
        }
    }
    auto pipeline = std::unique_ptr<Pipeline>(new (std::nothrow) Pipeline());
    if (!pipeline) return static_cast<uint32_t>(E_OUTOFMEMORY);
    pipeline->owner = &renderer;
    pipeline->required_stride = required[0];
    pipeline->required_stride1 = required[1];
    pipeline->required_stride2 = required[2];
    HRESULT hr = renderer.device->CreateVertexShader(vertex.bytecode->GetBufferPointer(),
        vertex.bytecode->GetBufferSize(), nullptr, &pipeline->vertex);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer.device->CreatePixelShader(pixel.bytecode->GetBufferPointer(),
        pixel.bytecode->GetBufferSize(), nullptr, &pipeline->pixel);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer.device->CreateInputLayout(elements.data(), count,
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(), &pipeline->input_layout);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_pipeline = pipeline.release();
    return 0;
}

uint32_t create_pipeline_with_two_vertex_streams(atmera_render_handle handle,
    atmera_shader_handle vertex_handle, atmera_shader_handle pixel_handle,
    const atmera_vertex_layout_desc* slot0, const atmera_vertex_layout_desc* slot1,
    atmera_pipeline_handle* out_pipeline) {
    return create_pipeline_with_vertex_streams(handle, vertex_handle, pixel_handle,
        slot0, slot1, nullptr, out_pipeline);
}

uint32_t create_pipeline_with_three_vertex_streams(atmera_render_handle handle,
    atmera_shader_handle vertex_handle, atmera_shader_handle pixel_handle,
    const atmera_vertex_layout_desc* slot0, const atmera_vertex_layout_desc* slot1,
    const atmera_vertex_layout_desc* slot2, atmera_pipeline_handle* out_pipeline) {
    if (!slot2) return static_cast<uint32_t>(E_INVALIDARG);
    return create_pipeline_with_vertex_streams(handle, vertex_handle, pixel_handle,
        slot0, slot1, slot2, out_pipeline);
}

uint32_t create_binding_layout(atmera_render_handle handle,
    const atmera_binding_layout_desc* desc, atmera_binding_layout_handle* out_layout) {
    if (!handle || !desc || !out_layout || desc->struct_size < sizeof(atmera_binding_layout_desc) ||
        desc->texture_register > 15 || desc->sampler_register > 15 || desc->constant_register > 13)
        return static_cast<uint32_t>(E_INVALIDARG);
    *out_layout = nullptr;
    auto layout = std::unique_ptr<BindingLayout>(new (std::nothrow) BindingLayout());
    if (!layout) return static_cast<uint32_t>(E_OUTOFMEMORY);
    layout->owner = static_cast<Renderer*>(handle);
    layout->texture_register = desc->texture_register;
    layout->sampler_register = desc->sampler_register;
    layout->constant_register = desc->constant_register;
    *out_layout = layout.release();
    return 0;
}

void destroy_binding_layout(atmera_binding_layout_handle layout) { delete static_cast<BindingLayout*>(layout); }

uint32_t create_pipeline_with_binding_layout(atmera_render_handle handle,
    atmera_shader_handle vertex_handle, atmera_shader_handle pixel_handle,
    atmera_binding_layout_handle layout_handle, atmera_pipeline_handle* out_pipeline) {
    if (!handle || !vertex_handle || !pixel_handle || !layout_handle || !out_pipeline)
        return static_cast<uint32_t>(E_INVALIDARG);
    *out_pipeline = nullptr;
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& vertex = *static_cast<Shader*>(vertex_handle);
    auto& pixel = *static_cast<Shader*>(pixel_handle);
    auto& layout = *static_cast<BindingLayout*>(layout_handle);
    if (vertex.owner != &renderer || pixel.owner != &renderer || layout.owner != &renderer ||
        vertex.stage != ATMERA_SHADER_VERTEX || pixel.stage != ATMERA_SHADER_PIXEL)
        return static_cast<uint32_t>(E_INVALIDARG);
    auto pipeline = std::unique_ptr<Pipeline>(new (std::nothrow) Pipeline());
    if (!pipeline) return static_cast<uint32_t>(E_OUTOFMEMORY);
    pipeline->owner = &renderer;
    pipeline->texture_register = layout.texture_register;
    pipeline->sampler_register = layout.sampler_register;
    pipeline->constant_register = layout.constant_register;
    HRESULT hr = renderer.device->CreateVertexShader(vertex.bytecode->GetBufferPointer(),
        vertex.bytecode->GetBufferSize(), nullptr, &pipeline->vertex);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer.device->CreatePixelShader(pixel.bytecode->GetBufferPointer(),
        pixel.bytecode->GetBufferSize(), nullptr, &pipeline->pixel);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_pipeline = pipeline.release();
    return 0;
}

uint32_t create_pipeline_with_texture_pair(atmera_render_handle handle,
    atmera_shader_handle vertex_handle, atmera_shader_handle pixel_handle,
    atmera_binding_layout_handle layout_handle, atmera_pipeline_handle* out_pipeline) {
    if (!handle || !layout_handle || !out_pipeline) return static_cast<uint32_t>(E_INVALIDARG);
    auto& layout = *static_cast<BindingLayout*>(layout_handle);
    if (layout.owner != handle || layout.texture_register > 14)
        return static_cast<uint32_t>(E_INVALIDARG);
    const uint32_t result = create_pipeline_with_binding_layout(handle, vertex_handle,
        pixel_handle, layout_handle, out_pipeline);
    if (result == 0) static_cast<Pipeline*>(*out_pipeline)->texture_count = 2;
    return result;
}

uint32_t create_pipeline_with_texture_sampler_pair(atmera_render_handle handle,
    atmera_shader_handle vertex_handle, atmera_shader_handle pixel_handle,
    atmera_binding_layout_handle layout_handle, atmera_pipeline_handle* out_pipeline) {
    if (!handle || !layout_handle || !out_pipeline) return static_cast<uint32_t>(E_INVALIDARG);
    auto& layout = *static_cast<BindingLayout*>(layout_handle);
    if (layout.owner != handle || layout.sampler_register > 14)
        return static_cast<uint32_t>(E_INVALIDARG);
    const uint32_t result = create_pipeline_with_texture_pair(handle, vertex_handle,
        pixel_handle, layout_handle, out_pipeline);
    if (result == 0) static_cast<Pipeline*>(*out_pipeline)->sampler_count = 2;
    return result;
}

void destroy_pipeline(atmera_pipeline_handle pipeline) { delete static_cast<Pipeline*>(pipeline); }

uint32_t create_texture_view(atmera_render_handle handle, atmera_texture_handle texture_handle,
    atmera_texture_view_handle* out_view) {
    if (!handle || !texture_handle || !out_view) return static_cast<uint32_t>(E_INVALIDARG);
    *out_view = nullptr;
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& texture = *static_cast<Texture*>(texture_handle);
    if (texture.owner != &renderer) return static_cast<uint32_t>(E_INVALIDARG);
    auto view = std::unique_ptr<TextureView>(new (std::nothrow) TextureView());
    if (!view) return static_cast<uint32_t>(E_OUTOFMEMORY);
    view->owner = &renderer;
    view->texture = texture.gpu;
    const HRESULT hr = renderer.device->CreateShaderResourceView(texture.gpu.Get(), nullptr, &view->view);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_view = view.release();
    return 0;
}

void destroy_texture_view(atmera_texture_view_handle view) { delete static_cast<TextureView*>(view); }

uint32_t create_sampler(atmera_render_handle handle, const atmera_sampler_desc* desc,
    atmera_sampler_handle* out_sampler) {
    if (!handle || !desc || !out_sampler || desc->struct_size < sizeof(atmera_sampler_desc) ||
        (desc->filter != ATMERA_FILTER_POINT && desc->filter != ATMERA_FILTER_LINEAR) ||
        (desc->address_mode != ATMERA_ADDRESS_CLAMP && desc->address_mode != ATMERA_ADDRESS_WRAP))
        return static_cast<uint32_t>(E_INVALIDARG);
    *out_sampler = nullptr;
    auto& renderer = *static_cast<Renderer*>(handle);
    auto sampler = std::unique_ptr<Sampler>(new (std::nothrow) Sampler());
    if (!sampler) return static_cast<uint32_t>(E_OUTOFMEMORY);
    sampler->owner = &renderer;
    D3D11_SAMPLER_DESC native{};
    native.Filter = desc->filter == ATMERA_FILTER_POINT ?
        D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    native.AddressU = native.AddressV = native.AddressW =
        desc->address_mode == ATMERA_ADDRESS_CLAMP ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
    native.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    native.MaxAnisotropy = 1;
    native.MaxLOD = D3D11_FLOAT32_MAX;
    const HRESULT hr = renderer.device->CreateSamplerState(&native, &sampler->state);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_sampler = sampler.release();
    return 0;
}

void destroy_sampler(atmera_sampler_handle sampler) { delete static_cast<Sampler*>(sampler); }

uint32_t create_binding_set(atmera_render_handle handle, atmera_texture_view_handle view_handle,
    atmera_sampler_handle sampler_handle, atmera_binding_set_handle* out_set) {
    if (!handle || !view_handle || !sampler_handle || !out_set) return static_cast<uint32_t>(E_INVALIDARG);
    *out_set = nullptr;
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& view = *static_cast<TextureView*>(view_handle);
    auto& sampler = *static_cast<Sampler*>(sampler_handle);
    if (view.owner != &renderer || sampler.owner != &renderer) return static_cast<uint32_t>(E_INVALIDARG);
    auto set = std::unique_ptr<BindingSet>(new (std::nothrow) BindingSet());
    if (!set) return static_cast<uint32_t>(E_OUTOFMEMORY);
    set->owner = &renderer;
    set->texture = view.texture;
    set->view = view.view;
    set->sampler = sampler.state;
    *out_set = set.release();
    return 0;
}

uint32_t create_binding_set_with_layout(atmera_render_handle handle,
    atmera_binding_layout_handle layout_handle, atmera_texture_view_handle view_handle,
    atmera_sampler_handle sampler_handle, atmera_binding_set_handle* out_set) {
    if (!handle || !layout_handle || !out_set) return static_cast<uint32_t>(E_INVALIDARG);
    auto& layout = *static_cast<BindingLayout*>(layout_handle);
    if (layout.owner != handle) return static_cast<uint32_t>(E_INVALIDARG);
    const uint32_t result = create_binding_set(handle, view_handle, sampler_handle, out_set);
    if (result != 0) return result;
    auto& set = *static_cast<BindingSet*>(*out_set);
    set.texture_register = layout.texture_register;
    set.sampler_register = layout.sampler_register;
    return 0;
}

uint32_t create_binding_set_with_texture_pair(atmera_render_handle handle,
    atmera_binding_layout_handle layout_handle, atmera_texture_view_handle first_handle,
    atmera_texture_view_handle second_handle, atmera_sampler_handle sampler_handle,
    atmera_binding_set_handle* out_set) {
    if (!handle || !layout_handle || !first_handle || !second_handle || !out_set)
        return static_cast<uint32_t>(E_INVALIDARG);
    auto& layout = *static_cast<BindingLayout*>(layout_handle);
    auto& first = *static_cast<TextureView*>(first_handle);
    auto& second = *static_cast<TextureView*>(second_handle);
    if (layout.owner != handle || layout.texture_register > 14 ||
        first.owner != handle || second.owner != handle ||
        first.texture.Get() == second.texture.Get()) return static_cast<uint32_t>(E_INVALIDARG);
    const uint32_t result = create_binding_set_with_layout(handle, layout_handle,
        first_handle, sampler_handle, out_set);
    if (result != 0) return result;
    auto& set = *static_cast<BindingSet*>(*out_set);
    set.texture2 = second.texture;
    set.view2 = second.view;
    set.texture_count = 2;
    return 0;
}

uint32_t create_binding_set_with_texture_sampler_pair(atmera_render_handle handle,
    atmera_binding_layout_handle layout_handle, atmera_texture_view_handle first_handle,
    atmera_sampler_handle first_sampler, atmera_texture_view_handle second_handle,
    atmera_sampler_handle second_sampler_handle, atmera_binding_set_handle* out_set) {
    if (!handle || !layout_handle || !second_sampler_handle || !out_set)
        return static_cast<uint32_t>(E_INVALIDARG);
    auto& layout = *static_cast<BindingLayout*>(layout_handle);
    auto& second_sampler = *static_cast<Sampler*>(second_sampler_handle);
    if (layout.owner != handle || layout.sampler_register > 14 || second_sampler.owner != handle)
        return static_cast<uint32_t>(E_INVALIDARG);
    const uint32_t result = create_binding_set_with_texture_pair(handle, layout_handle,
        first_handle, second_handle, first_sampler, out_set);
    if (result != 0) return result;
    auto& set = *static_cast<BindingSet*>(*out_set);
    set.sampler2 = second_sampler.state;
    set.sampler_count = 2;
    return 0;
}

void destroy_binding_set(atmera_binding_set_handle set) { delete static_cast<BindingSet*>(set); }

uint32_t bind_binding_set(atmera_render_handle handle, atmera_binding_set_handle set_handle) {
    if (!handle || !set_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& set = *static_cast<BindingSet*>(set_handle);
    if (!renderer.recording || set.owner != &renderer ||
        set.texture_register != renderer.texture_register ||
        set.texture_count != renderer.texture_count ||
        set.sampler_register != renderer.sampler_register ||
        set.sampler_count != renderer.sampler_count)
        return static_cast<uint32_t>(E_INVALIDARG);
    ID3D11ShaderResourceView* views[] = {set.view.Get(), set.view2.Get()};
    renderer.context->PSSetShaderResources(set.texture_register, set.texture_count, views);
    ID3D11SamplerState* samplers[] = {set.sampler.Get(), set.sampler2.Get()};
    renderer.context->PSSetSamplers(set.sampler_register, set.sampler_count, samplers);
    return 0;
}

uint32_t get_graphics_queue(atmera_render_handle handle, atmera_command_queue_handle* out_queue) {
    if (!handle || !out_queue) return static_cast<uint32_t>(E_INVALIDARG);
    *out_queue = handle;
    return 0;
}

uint32_t create_command_list(atmera_render_handle handle, atmera_command_list_handle* out_list) {
    if (!handle || !out_list) return static_cast<uint32_t>(E_INVALIDARG);
    *out_list = nullptr;
    auto& renderer = *static_cast<Renderer*>(handle);
    auto list = std::unique_ptr<CommandList>(new (std::nothrow) CommandList());
    if (!list) return static_cast<uint32_t>(E_OUTOFMEMORY);
    list->owner = &renderer;
    const HRESULT hr = renderer.device->CreateDeferredContext(0, &list->deferred);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_list = list.release();
    return 0;
}

uint32_t command_copy_buffer(atmera_command_list_handle list_handle, atmera_buffer_handle destination,
    uint32_t destination_offset, atmera_buffer_handle source, uint32_t source_offset, uint32_t size) {
    if (!list_handle || !destination || !source || !size || destination == source)
        return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& dst = *static_cast<Buffer*>(destination);
    auto& src = *static_cast<Buffer*>(source);
    if (list.submitted || dst.owner != list.owner || src.owner != list.owner ||
        destination_offset > dst.size || size > dst.size - destination_offset ||
        source_offset > src.size || size > src.size - source_offset ||
        (dst.usage == ATMERA_BUFFER_CONSTANT && (destination_offset != 0 || size != dst.size)))
        return static_cast<uint32_t>(E_INVALIDARG);
    const D3D11_BOX box{source_offset, 0, 0, source_offset + size, 1, 1};
    list.deferred->CopySubresourceRegion(dst.gpu.Get(), 0, destination_offset, 0, 0,
        src.gpu.Get(), 0, &box);
    return 0;
}

uint32_t command_copy_texture(atmera_command_list_handle list_handle,
    atmera_texture_handle destination, atmera_texture_handle source) {
    if (!list_handle || !destination || !source || destination == source)
        return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& dst = *static_cast<Texture*>(destination);
    auto& src = *static_cast<Texture*>(source);
    if (list.submitted || list.render_target_open || dst.owner != list.owner ||
        src.owner != list.owner || dst.width != src.width || dst.height != src.height)
        return static_cast<uint32_t>(E_INVALIDARG);
    list.deferred->CopyResource(dst.gpu.Get(), src.gpu.Get());
    return 0;
}

uint32_t command_copy_texture_region(atmera_command_list_handle list_handle,
    atmera_texture_handle destination, uint32_t destination_x, uint32_t destination_y,
    atmera_texture_handle source, uint32_t source_x, uint32_t source_y,
    uint32_t width, uint32_t height) {
    if (!list_handle || !destination || !source || destination == source || !width || !height)
        return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& dst = *static_cast<Texture*>(destination);
    auto& src = *static_cast<Texture*>(source);
    if (list.submitted || list.render_target_open || dst.owner != list.owner || src.owner != list.owner ||
        destination_x > dst.width || width > dst.width - destination_x ||
        destination_y > dst.height || height > dst.height - destination_y ||
        source_x > src.width || width > src.width - source_x ||
        source_y > src.height || height > src.height - source_y)
        return static_cast<uint32_t>(E_INVALIDARG);
    const D3D11_BOX box{source_x, source_y, 0, source_x + width, source_y + height, 1};
    list.deferred->CopySubresourceRegion(dst.gpu.Get(), 0, destination_x, destination_y, 0,
        src.gpu.Get(), 0, &box);
    return 0;
}

uint32_t command_begin_render_target(atmera_command_list_handle list_handle, atmera_texture_handle target_handle) {
    if (!list_handle || !target_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& target = *static_cast<Texture*>(target_handle);
    if (list.submitted || list.render_target_open || target.owner != list.owner)
        return static_cast<uint32_t>(E_INVALIDARG);
    ComPtr<ID3D11RenderTargetView> view;
    const HRESULT hr = list.owner->device->CreateRenderTargetView(target.gpu.Get(), nullptr, &view);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    list.deferred->OMSetRenderTargets(1, view.GetAddressOf(), nullptr);
    const float clear[] = {0.055f, 0.075f, 0.095f, 1.0f};
    list.deferred->ClearRenderTargetView(view.Get(), clear);
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(target.width), static_cast<float>(target.height), 0, 1};
    list.deferred->RSSetViewports(1, &viewport);
    list.deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11ShaderResourceView* no_views[16]{};
    ID3D11SamplerState* no_samplers[16]{};
    list.deferred->PSSetShaderResources(0, 16, no_views);
    list.deferred->PSSetSamplers(0, 16, no_samplers);
    list.active_target = target.gpu;
    list.render_target_open = true;
    list.pipeline_bound = false;
    list.required_stride = 0;
    list.bound_stride = 0;
    list.bound_bytes = 0;
    list.required_stride1 = 0;
    list.bound_stride1 = 0;
    list.bound_bytes1 = 0;
    list.required_stride2 = 0;
    list.bound_stride2 = 0;
    list.bound_bytes2 = 0;
    list.index_element_bytes = 0;
    list.index_bound_bytes = 0;
    list.texture_register = 0;
    list.texture_count = 1;
    list.sampler_register = 0;
    list.sampler_count = 1;
    list.constant_register = 0;
    return 0;
}

uint32_t command_bind_pipeline(atmera_command_list_handle list_handle, atmera_pipeline_handle pipeline_handle) {
    if (!list_handle || !pipeline_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& pipeline = *static_cast<Pipeline*>(pipeline_handle);
    if (list.submitted || !list.render_target_open || pipeline.owner != list.owner)
        return static_cast<uint32_t>(E_INVALIDARG);
    list.deferred->VSSetShader(pipeline.vertex.Get(), nullptr, 0);
    list.deferred->PSSetShader(pipeline.pixel.Get(), nullptr, 0);
    list.deferred->IASetInputLayout(pipeline.input_layout.Get());
    list.required_stride = pipeline.required_stride;
    list.required_stride1 = pipeline.required_stride1;
    list.required_stride2 = pipeline.required_stride2;
    list.texture_register = pipeline.texture_register;
    list.texture_count = pipeline.texture_count;
    list.sampler_register = pipeline.sampler_register;
    list.sampler_count = pipeline.sampler_count;
    list.constant_register = pipeline.constant_register;
    list.pipeline_bound = true;
    return 0;
}

uint32_t command_bind_binding_set(atmera_command_list_handle list_handle,
    atmera_binding_set_handle set_handle) {
    if (!list_handle || !set_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& set = *static_cast<BindingSet*>(set_handle);
    if (list.submitted || !list.render_target_open || set.owner != list.owner ||
        set.texture.Get() == list.active_target.Get() ||
        set.texture2.Get() == list.active_target.Get() ||
        set.texture_register != list.texture_register ||
        set.texture_count != list.texture_count ||
        set.sampler_register != list.sampler_register ||
        set.sampler_count != list.sampler_count)
        return static_cast<uint32_t>(E_INVALIDARG);
    ID3D11ShaderResourceView* views[] = {set.view.Get(), set.view2.Get()};
    list.deferred->PSSetShaderResources(set.texture_register, set.texture_count, views);
    ID3D11SamplerState* samplers[] = {set.sampler.Get(), set.sampler2.Get()};
    list.deferred->PSSetSamplers(set.sampler_register, set.sampler_count, samplers);
    return 0;
}

uint32_t command_bind_constant_buffer(atmera_command_list_handle list_handle,
    atmera_buffer_handle buffer_handle) {
    if (!list_handle || !buffer_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (list.submitted || !list.render_target_open || buffer.owner != list.owner ||
        buffer.usage != ATMERA_BUFFER_CONSTANT)
        return static_cast<uint32_t>(E_INVALIDARG);
    ID3D11Buffer* native = buffer.gpu.Get();
    list.deferred->VSSetConstantBuffers(list.constant_register, 1, &native);
    list.deferred->PSSetConstantBuffers(list.constant_register, 1, &native);
    return 0;
}

uint32_t command_bind_vertex_buffer(atmera_command_list_handle list_handle,
    atmera_buffer_handle buffer_handle, uint32_t stride_bytes, uint32_t offset_bytes) {
    if (!list_handle || !buffer_handle || !stride_bytes) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (list.submitted || !list.render_target_open || buffer.owner != list.owner ||
        buffer.usage != ATMERA_BUFFER_VERTEX || offset_bytes >= buffer.size ||
        stride_bytes > buffer.size - offset_bytes ||
        (list.required_stride && stride_bytes < list.required_stride))
        return static_cast<uint32_t>(E_INVALIDARG);
    ID3D11Buffer* native = buffer.gpu.Get();
    list.deferred->IASetVertexBuffers(0, 1, &native, &stride_bytes, &offset_bytes);
    list.bound_stride = stride_bytes;
    list.bound_bytes = buffer.size - offset_bytes;
    return 0;
}

uint32_t command_bind_vertex_buffer_slot1(atmera_command_list_handle list_handle,
    atmera_buffer_handle buffer_handle, uint32_t stride_bytes, uint32_t offset_bytes) {
    if (!list_handle || !buffer_handle || !stride_bytes) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (list.submitted || !list.render_target_open || buffer.owner != list.owner ||
        buffer.usage != ATMERA_BUFFER_VERTEX || offset_bytes >= buffer.size ||
        stride_bytes > buffer.size - offset_bytes ||
        (list.required_stride1 && stride_bytes < list.required_stride1))
        return static_cast<uint32_t>(E_INVALIDARG);
    ID3D11Buffer* native = buffer.gpu.Get();
    list.deferred->IASetVertexBuffers(1, 1, &native, &stride_bytes, &offset_bytes);
    list.bound_stride1 = stride_bytes;
    list.bound_bytes1 = buffer.size - offset_bytes;
    return 0;
}

uint32_t command_bind_vertex_buffer_slot2(atmera_command_list_handle list_handle,
    atmera_buffer_handle buffer_handle, uint32_t stride_bytes, uint32_t offset_bytes) {
    if (!list_handle || !buffer_handle || !stride_bytes) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (list.submitted || !list.render_target_open || buffer.owner != list.owner ||
        buffer.usage != ATMERA_BUFFER_VERTEX || offset_bytes >= buffer.size ||
        stride_bytes > buffer.size - offset_bytes ||
        (list.required_stride2 && stride_bytes < list.required_stride2))
        return static_cast<uint32_t>(E_INVALIDARG);
    ID3D11Buffer* native = buffer.gpu.Get();
    list.deferred->IASetVertexBuffers(2, 1, &native, &stride_bytes, &offset_bytes);
    list.bound_stride2 = stride_bytes;
    list.bound_bytes2 = buffer.size - offset_bytes;
    return 0;
}

uint32_t command_bind_index_buffer(atmera_command_list_handle list_handle,
    atmera_buffer_handle buffer_handle, uint32_t index_format, uint32_t offset_bytes) {
    if (!list_handle || !buffer_handle ||
        (index_format != ATMERA_INDEX_UINT16 && index_format != ATMERA_INDEX_UINT32))
        return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    const uint32_t element_bytes = index_format == ATMERA_INDEX_UINT16 ? 2u : 4u;
    if (list.submitted || !list.render_target_open || buffer.owner != list.owner ||
        buffer.usage != ATMERA_BUFFER_INDEX || offset_bytes >= buffer.size ||
        offset_bytes % element_bytes || buffer.size - offset_bytes < element_bytes)
        return static_cast<uint32_t>(E_INVALIDARG);
    list.deferred->IASetIndexBuffer(buffer.gpu.Get(),
        index_format == ATMERA_INDEX_UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT,
        offset_bytes);
    list.index_element_bytes = element_bytes;
    list.index_bound_bytes = buffer.size - offset_bytes;
    return 0;
}

uint32_t command_draw(atmera_command_list_handle list_handle, uint32_t vertex_count) {
    if (!list_handle || !vertex_count) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    if (list.submitted || !list.render_target_open || !list.pipeline_bound ||
        (list.required_stride && (list.bound_stride < list.required_stride ||
            vertex_count > list.bound_bytes / list.bound_stride)) ||
        (list.required_stride1 && (list.bound_stride1 < list.required_stride1 ||
            vertex_count > list.bound_bytes1 / list.bound_stride1)) ||
        (list.required_stride2 && (list.bound_stride2 < list.required_stride2 ||
            vertex_count > list.bound_bytes2 / list.bound_stride2)))
        return static_cast<uint32_t>(E_FAIL);
    list.deferred->Draw(vertex_count, 0);
    return 0;
}

uint32_t command_draw_indexed(atmera_command_list_handle list_handle, uint32_t index_count) {
    if (!list_handle || !index_count) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    if (list.submitted || !list.render_target_open || !list.pipeline_bound ||
        !list.index_element_bytes || index_count > list.index_bound_bytes / list.index_element_bytes ||
        (list.required_stride && (list.bound_stride < list.required_stride || !list.bound_bytes)) ||
        (list.required_stride1 && (list.bound_stride1 < list.required_stride1 || !list.bound_bytes1)) ||
        (list.required_stride2 && (list.bound_stride2 < list.required_stride2 || !list.bound_bytes2)))
        return static_cast<uint32_t>(E_FAIL);
    list.deferred->DrawIndexed(index_count, 0, 0);
    return 0;
}

uint32_t command_end_render_target(atmera_command_list_handle list_handle) {
    if (!list_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    if (list.submitted || !list.render_target_open) return static_cast<uint32_t>(E_FAIL);
    list.deferred->OMSetRenderTargets(0, nullptr, nullptr);
    list.active_target.Reset();
    list.render_target_open = false;
    list.pipeline_bound = false;
    return 0;
}

uint32_t submit_command_list(atmera_command_queue_handle queue_handle, atmera_command_list_handle list_handle,
    atmera_fence_handle* out_fence) {
    if (!queue_handle || !list_handle || !out_fence) return static_cast<uint32_t>(E_INVALIDARG);
    *out_fence = nullptr;
    auto& renderer = *static_cast<Renderer*>(queue_handle);
    auto& list = *static_cast<CommandList*>(list_handle);
    if (renderer.recording || list.submitted || list.render_target_open || list.owner != &renderer)
        return static_cast<uint32_t>(E_INVALIDARG);
    auto fence = std::unique_ptr<Fence>(new (std::nothrow) Fence());
    if (!fence) return static_cast<uint32_t>(E_OUTOFMEMORY);
    fence->owner = &renderer;
    D3D11_QUERY_DESC query_desc{D3D11_QUERY_EVENT, 0};
    HRESULT hr = renderer.device->CreateQuery(&query_desc, &fence->query);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = list.deferred->FinishCommandList(FALSE, &list.compiled);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    renderer.context->ExecuteCommandList(list.compiled.Get(), TRUE);
    renderer.context->End(fence->query.Get());
    renderer.context->Flush();
    list.submitted = true;
    fence->commands = list.compiled;
    *out_fence = fence.release();
    return 0;
}

uint32_t wait_fence(atmera_command_queue_handle queue_handle, atmera_fence_handle fence_handle,
    uint32_t timeout_ms) {
    if (!queue_handle || !fence_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(queue_handle);
    auto& fence = *static_cast<Fence*>(fence_handle);
    if (fence.owner != &renderer) return static_cast<uint32_t>(E_INVALIDARG);
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        BOOL done = FALSE;
        const HRESULT hr = renderer.context->GetData(fence.query.Get(), &done, sizeof(done),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_OK && done) return 0;
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        if (GetTickCount64() >= deadline) return static_cast<uint32_t>(HRESULT_FROM_WIN32(WAIT_TIMEOUT));
        Sleep(1);
    }
}

void destroy_fence(atmera_fence_handle fence) { delete static_cast<Fence*>(fence); }
void destroy_command_list(atmera_command_list_handle list) { delete static_cast<CommandList*>(list); }

void destroy(atmera_render_handle handle) { delete static_cast<Renderer*>(handle); }
const atmera_render_api api{sizeof(atmera_render_api), ATMERA_RENDER_ABI_VERSION,
    "atmera.d3d11", &create, &get_caps, &resize, &render_frame, &get_center_pixel, &destroy,
    &create_buffer, &write_buffer, &read_buffer, &destroy_buffer,
    &create_texture, &write_texture, &read_texture, &destroy_texture,
    &create_shader, &destroy_shader, &create_pipeline, &destroy_pipeline,
    &begin_frame, &bind_pipeline, &draw, &end_frame,
    &create_texture_view, &destroy_texture_view, &create_sampler, &destroy_sampler,
    &create_binding_set, &destroy_binding_set, &bind_binding_set,
    &get_graphics_queue, &create_command_list, &command_copy_buffer,
    &submit_command_list, &wait_fence, &destroy_fence, &destroy_command_list,
    &command_begin_render_target, &command_bind_pipeline, &command_draw, &command_end_render_target,
    &command_copy_texture, &command_copy_texture_region,
    &create_pipeline_with_vertex_layout, &command_bind_vertex_buffer, &bind_vertex_buffer,
    &bind_index_buffer, &draw_indexed, &command_bind_index_buffer, &command_draw_indexed,
    &command_bind_binding_set, &bind_constant_buffer, &command_bind_constant_buffer,
    &create_binding_layout, &destroy_binding_layout, &create_pipeline_with_binding_layout,
    &create_binding_set_with_layout, &create_pipeline_with_two_vertex_streams,
    &bind_vertex_buffer_slot1, &command_bind_vertex_buffer_slot1,
    &create_pipeline_with_texture_pair, &create_binding_set_with_texture_pair,
    &create_pipeline_with_texture_sampler_pair, &create_binding_set_with_texture_sampler_pair,
    &create_pipeline_with_three_vertex_streams, &bind_vertex_buffer_slot2,
    &command_bind_vertex_buffer_slot2, &get_shader_reflection, &get_shader_bindings,
    &get_shader_cache_stats, &get_shader_binding_name};
}

extern "C" ATMERA_RENDER_EXPORT uint32_t atmera_get_render_api(uint32_t version,
    const atmera_render_api** out_api) {
    if (!out_api || version != ATMERA_RENDER_ABI_VERSION) return 1;
    *out_api = &api;
    return 0;
}

extern "C" ATMERA_RENDER_EXPORT uint32_t atmera_get_last_shader_error(uint32_t version,
    char* out_utf8, uint32_t capacity, uint32_t* required_bytes) {
    if (version != ATMERA_RENDER_ABI_VERSION || !required_bytes) return 1;
    const uint32_t required = static_cast<uint32_t>(std::strlen(last_shader_error) + 1);
    *required_bytes = required;
    if (!out_utf8 && capacity == 0) return 0;
    if (!out_utf8 || capacity < required) return 2;
    std::memcpy(out_utf8, last_shader_error, required);
    return 0;
}
