#include <ATMERA/RenderABI.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <new>
#include <limits>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
thread_local char last_shader_error[8192]{};

void save_shader_error(IDxcResult* result) {
    ComPtr<IDxcBlobUtf8> errors;
    if (!result || FAILED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) || !errors) return;
    const size_t count = (std::min)(errors->GetStringLength(), sizeof(last_shader_error) - 1);
    std::memcpy(last_shader_error, errors->GetStringPointer(), count);
    last_shader_error[count] = '\0';
}

constexpr UINT buffer_count = 2;
struct Presentation {
    ComPtr<IDXGISwapChain3> swap_chain;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    std::array<ComPtr<ID3D12Resource>, buffer_count> back_buffers;
    UINT rtv_stride = 0;
    UINT frame_index = 0;
};

struct ShaderCacheEntry {
    std::string source;
    std::string entry;
    ComPtr<IDxcBlob> bytecode;
    ComPtr<ID3D12ShaderReflection> reflection;
    uint32_t constant_buffer_bindings = 0;
    uint32_t texture_bindings = 0;
    uint32_t sampler_bindings = 0;
    bool valid = false;
};

struct Renderer {
    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    Presentation presentation;
    std::array<ComPtr<ID3D12CommandAllocator>, buffer_count> allocators;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12Resource> readback;
    ComPtr<ID3D12Resource> bound_texture;
    ComPtr<ID3D12Resource> bound_texture2;
    ComPtr<ID3D12Resource> bound_vertex;
    ComPtr<ID3D12Resource> bound_vertex1;
    ComPtr<ID3D12Resource> bound_vertex2;
    ComPtr<ID3D12Resource> bound_index;
    ComPtr<ID3D12Resource> bound_constant;
    std::array<uint8_t, 4> center_pixel{};
    bool capture = false;
    HANDLE fence_event = nullptr;
    UINT64 fence_value = 0;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    D3D_SHADER_MODEL shader_model = D3D_SHADER_MODEL_6_0;
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
    ~Renderer() { if (fence_event) CloseHandle(fence_event); }
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
    ComPtr<IDxcBlob> bytecode;
    ComPtr<ID3D12ShaderReflection> reflection;
    uint32_t constant_buffer_bindings = 0;
    uint32_t texture_bindings = 0;
    uint32_t sampler_bindings = 0;
};

struct Pipeline {
    Renderer* owner = nullptr;
    ComPtr<ID3D12PipelineState> state;
    ComPtr<ID3D12RootSignature> root_signature;
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
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12DescriptorHeap> heap;
};

struct Sampler {
    Renderer* owner = nullptr;
    ComPtr<ID3D12DescriptorHeap> heap;
};

struct BindingSet {
    Renderer* owner = nullptr;
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> texture2;
    ComPtr<ID3D12DescriptorHeap> view_heap;
    ComPtr<ID3D12DescriptorHeap> sampler_heap;
    uint32_t texture_register = 0;
    uint32_t texture_count = 1;
    uint32_t sampler_register = 0;
    uint32_t sampler_count = 1;
};

struct CommandList {
    Renderer* owner = nullptr;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    std::vector<ComPtr<ID3D12Resource>> resources;
    std::vector<ComPtr<ID3D12PipelineState>> pipelines;
    std::vector<ComPtr<ID3D12RootSignature>> root_signatures;
    std::vector<ComPtr<ID3D12DescriptorHeap>> render_targets;
    std::vector<ComPtr<ID3D12DescriptorHeap>> binding_heaps;
    ComPtr<ID3D12Resource> active_target;
    ComPtr<ID3D12Resource> active_texture;
    ComPtr<ID3D12Resource> active_texture2;
    ComPtr<ID3D12Resource> active_vertex;
    ComPtr<ID3D12Resource> active_vertex1;
    ComPtr<ID3D12Resource> active_vertex2;
    ComPtr<ID3D12Resource> active_index;
    ComPtr<ID3D12Resource> active_constant;
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
    UINT64 value = 0;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    std::vector<ComPtr<ID3D12Resource>> resources;
    std::vector<ComPtr<ID3D12PipelineState>> pipelines;
    std::vector<ComPtr<ID3D12RootSignature>> root_signatures;
    std::vector<ComPtr<ID3D12DescriptorHeap>> render_targets;
    std::vector<ComPtr<ID3D12DescriptorHeap>> binding_heaps;
};

struct Buffer {
    Renderer* owner = nullptr;
    uint32_t size = 0;
    uint32_t usage = 0;
    ComPtr<ID3D12Resource> gpu;
};

struct Texture {
    Renderer* owner = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    ComPtr<ID3D12Resource> gpu;
};

HRESULT compile_shader(const char* source_text, uint32_t source_size, const wchar_t* entry,
    const wchar_t* profile, ComPtr<IDxcBlob>& blob) {
    ComPtr<IDxcCompiler3> compiler;
    HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    if (FAILED(hr)) return hr;
    DxcBuffer source{source_text, source_size, DXC_CP_UTF8};
    const wchar_t* arguments[] = {L"-E", entry, L"-T", profile, L"-HV", L"2021"};
    ComPtr<IDxcResult> result;
    hr = compiler->Compile(&source, arguments, _countof(arguments), nullptr, IID_PPV_ARGS(&result));
    if (FAILED(hr)) return hr;
    HRESULT compile_status = S_OK;
    hr = result->GetStatus(&compile_status);
    if (FAILED(hr)) return hr;
    if (FAILED(compile_status)) { save_shader_error(result.Get()); return compile_status; }
    return result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&blob), nullptr);
}

HRESULT make_root_signature(Renderer& renderer, uint32_t texture_register,
    uint32_t sampler_register, uint32_t constant_register,
    ComPtr<ID3D12RootSignature>& out_signature, uint32_t texture_count = 1,
    uint32_t sampler_count = 1) {
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = texture_count;
    ranges[0].BaseShaderRegister = texture_register;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors = sampler_count;
    ranges[1].BaseShaderRegister = sampler_register;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER parameters[3]{};
    for (UINT i = 0; i < 2; ++i) {
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[i].DescriptorTable.NumDescriptorRanges = 1;
        parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
        parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[2].Descriptor.ShaderRegister = constant_register;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = 3;
    root_desc.pParameters = parameters;
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> root_blob;
    HRESULT hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
        &root_blob, nullptr);
    if (FAILED(hr)) return hr;
    return renderer.device->CreateRootSignature(0, root_blob->GetBufferPointer(),
        root_blob->GetBufferSize(), IID_PPV_ARGS(&out_signature));
}

HRESULT make_pipeline(Renderer& renderer, IDxcBlob* vertex, IDxcBlob* pixel,
    ComPtr<ID3D12PipelineState>& out_pipeline,
    const D3D12_INPUT_ELEMENT_DESC* elements = nullptr, UINT element_count = 0,
    ID3D12RootSignature* signature = nullptr) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = signature ? signature : renderer.root_signature.Get();
    pso.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    pso.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    pso.InputLayout = {elements, element_count};
    pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    return renderer.device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&out_pipeline));
}

HRESULT make_buffers(Renderer& renderer) {
    auto handle = renderer.presentation.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < buffer_count; ++i) {
        HRESULT hr = renderer.presentation.swap_chain->GetBuffer(i, IID_PPV_ARGS(&renderer.presentation.back_buffers[i]));
        if (FAILED(hr)) return hr;
        renderer.device->CreateRenderTargetView(renderer.presentation.back_buffers[i].Get(), nullptr, handle);
        handle.ptr += renderer.presentation.rtv_stride;
    }
    renderer.presentation.frame_index = renderer.presentation.swap_chain->GetCurrentBackBufferIndex();
    return S_OK;
}

HRESULT wait_gpu(Renderer& renderer) {
    const UINT64 value = ++renderer.fence_value;
    HRESULT hr = renderer.queue->Signal(renderer.fence.Get(), value);
    if (FAILED(hr)) return hr;
    if (renderer.fence->GetCompletedValue() < value) {
        hr = renderer.fence->SetEventOnCompletion(value, renderer.fence_event);
        if (FAILED(hr)) return hr;
        if (WaitForSingleObject(renderer.fence_event, 10000) != WAIT_OBJECT_0)
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    return S_OK;
}

HRESULT make_resource(Renderer& renderer, D3D12_HEAP_TYPE heap_type,
    const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initial_state,
    ComPtr<ID3D12Resource>& out_resource) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heap_type;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    return renderer.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &desc, initial_state, nullptr, IID_PPV_ARGS(&out_resource));
}

D3D12_RESOURCE_DESC buffer_desc(uint64_t size) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

HRESULT begin_copy(Renderer& renderer) {
    if (renderer.recording) return E_FAIL;
    HRESULT hr = wait_gpu(renderer);
    if (FAILED(hr)) return hr;
    hr = renderer.allocators[renderer.presentation.frame_index]->Reset();
    if (FAILED(hr)) return hr;
    return renderer.commands->Reset(renderer.allocators[renderer.presentation.frame_index].Get(), nullptr);
}

void record_transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    commands->ResourceBarrier(1, &barrier);
}

void transition(Renderer& renderer, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    record_transition(renderer.commands.Get(), resource, before, after);
}

HRESULT end_copy(Renderer& renderer) {
    HRESULT hr = renderer.commands->Close();
    if (FAILED(hr)) return hr;
    ID3D12CommandList* lists[] = {renderer.commands.Get()};
    renderer.queue->ExecuteCommandLists(1, lists);
    return wait_gpu(renderer);
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
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&renderer->factory));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    ComPtr<IDXGIAdapter> warp_adapter;
    IUnknown* adapter = nullptr;
    if (renderer->software) {
        hr = renderer->factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter));
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        adapter = warp_adapter.Get();
    }
    hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&renderer->device));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D12_FEATURE_DATA_FEATURE_LEVELS feature_data{_countof(levels), levels, D3D_FEATURE_LEVEL_11_0};
    if (SUCCEEDED(renderer->device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
            &feature_data, sizeof(feature_data))))
        renderer->feature_level = feature_data.MaxSupportedFeatureLevel;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_data{D3D_SHADER_MODEL_6_6};
    if (FAILED(renderer->device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
            &shader_data, sizeof(shader_data)))) shader_data.HighestShaderModel = D3D_SHADER_MODEL_6_0;
    renderer->shader_model = shader_data.HighestShaderModel;
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = renderer->device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&renderer->queue));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    if (desc->window_handle) {
        DXGI_SWAP_CHAIN_DESC1 swap_desc{};
        swap_desc.Width = desc->width;
        swap_desc.Height = desc->height;
        swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.SampleDesc.Count = 1;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.BufferCount = buffer_count;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> swap1;
        hr = renderer->factory->CreateSwapChainForHwnd(renderer->queue.Get(),
            static_cast<HWND>(desc->window_handle), &swap_desc, nullptr, nullptr, &swap1);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        hr = swap1.As(&renderer->presentation.swap_chain);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        renderer->factory->MakeWindowAssociation(static_cast<HWND>(desc->window_handle), DXGI_MWA_NO_ALT_ENTER);
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.NumDescriptors = buffer_count;
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hr = renderer->device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&renderer->presentation.rtv_heap));
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        renderer->presentation.rtv_stride = renderer->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        hr = make_buffers(*renderer);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
    }
    for (auto& allocator : renderer->allocators) {
        hr = renderer->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
    }
    hr = renderer->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        renderer->allocators[0].Get(), nullptr, IID_PPV_ARGS(&renderer->commands));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer->commands->Close();
    if (FAILED(hr)) return static_cast<uint32_t>(hr);

    hr = make_root_signature(*renderer, 0, 0, 0, renderer->root_signature);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    ComPtr<IDxcBlob> vs;
    ComPtr<IDxcBlob> ps;
    hr = compile_shader(desc->hlsl_source, desc->hlsl_source_size, L"VSMain", L"vs_6_0", vs);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = compile_shader(desc->hlsl_source, desc->hlsl_source_size, L"PSMain", L"ps_6_0", ps);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = make_pipeline(*renderer, vs.Get(), ps.Get(), renderer->pipeline);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&renderer->fence));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    renderer->fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!renderer->fence_event) return static_cast<uint32_t>(HRESULT_FROM_WIN32(GetLastError()));
    if (renderer->capture && renderer->presentation.swap_chain) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = renderer->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&renderer->readback));
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
    }
    *out_handle = renderer.release();
    return 0;
}

uint32_t get_caps(atmera_render_handle handle, atmera_render_caps* out_caps) {
    if (!handle || !out_caps || out_caps->struct_size < sizeof(atmera_render_caps)) return static_cast<uint32_t>(E_INVALIDARG);
    const auto& renderer = *static_cast<Renderer*>(handle);
    out_caps->api_major = 12;
    out_caps->feature_level = static_cast<uint32_t>(renderer.feature_level);
    const auto shader_code = static_cast<uint32_t>(renderer.shader_model);
    out_caps->shader_model = (shader_code >> 4) * 10 + (shader_code & 0xf);
    out_caps->is_software = renderer.software ? 1u : 0u;
    return 0;
}

uint32_t resize(atmera_render_handle handle, uint32_t width, uint32_t height) {
    if (!handle || !width || !height) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    if (renderer.recording || !renderer.presentation.swap_chain) return static_cast<uint32_t>(E_FAIL);
    HRESULT hr = wait_gpu(renderer);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    for (auto& buffer : renderer.presentation.back_buffers) buffer.Reset();
    hr = renderer.presentation.swap_chain->ResizeBuffers(buffer_count, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    renderer.width = width;
    renderer.height = height;
    return static_cast<uint32_t>(make_buffers(renderer));
}

uint32_t begin_frame(atmera_render_handle handle) {
    if (!handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    if (renderer.recording || !renderer.presentation.swap_chain) return static_cast<uint32_t>(E_FAIL);
    const UINT index = renderer.presentation.frame_index;
    HRESULT hr = renderer.allocators[index]->Reset();
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer.commands->Reset(renderer.allocators[index].Get(), nullptr);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = renderer.presentation.back_buffers[index].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    renderer.commands->ResourceBarrier(1, &barrier);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.presentation.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(index) * renderer.presentation.rtv_stride;
    const float clear[] = {0.055f, 0.075f, 0.095f, 1.0f};
    renderer.commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    renderer.commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(renderer.width), static_cast<float>(renderer.height), 0, 1};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(renderer.width), static_cast<LONG>(renderer.height)};
    renderer.commands->RSSetViewports(1, &viewport);
    renderer.commands->RSSetScissorRects(1, &scissor);
    renderer.commands->SetGraphicsRootSignature(renderer.root_signature.Get());
    renderer.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderer.bound_texture.Reset();
    renderer.bound_texture2.Reset();
    renderer.bound_vertex.Reset();
    renderer.bound_vertex1.Reset();
    renderer.bound_vertex2.Reset();
    renderer.bound_index.Reset();
    renderer.bound_constant.Reset();
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
    renderer.commands->SetGraphicsRootSignature(pipeline.root_signature.Get());
    renderer.commands->SetPipelineState(pipeline.state.Get());
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
        renderer.bound_vertex1.Get() == buffer.gpu.Get() ||
        renderer.bound_vertex2.Get() == buffer.gpu.Get() ||
        (renderer.required_stride && stride_bytes < renderer.required_stride))
        return static_cast<uint32_t>(E_INVALIDARG);
    if (renderer.bound_vertex)
        transition(renderer, renderer.bound_vertex.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
    transition(renderer, buffer.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer.gpu->GetGPUVirtualAddress() + offset_bytes;
    view.SizeInBytes = buffer.size - offset_bytes;
    view.StrideInBytes = stride_bytes;
    renderer.commands->IASetVertexBuffers(0, 1, &view);
    renderer.bound_vertex = buffer.gpu;
    renderer.bound_stride = stride_bytes;
    renderer.bound_bytes = view.SizeInBytes;
    return 0;
}

uint32_t bind_vertex_buffer_slot1(atmera_render_handle handle, atmera_buffer_handle buffer_handle,
    uint32_t stride_bytes, uint32_t offset_bytes) {
    if (!handle || !buffer_handle || !stride_bytes) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (!renderer.recording || buffer.owner != &renderer || buffer.usage != ATMERA_BUFFER_VERTEX ||
        offset_bytes >= buffer.size || stride_bytes > buffer.size - offset_bytes ||
        renderer.bound_vertex.Get() == buffer.gpu.Get() ||
        renderer.bound_vertex2.Get() == buffer.gpu.Get() ||
        (renderer.required_stride1 && stride_bytes < renderer.required_stride1))
        return static_cast<uint32_t>(E_INVALIDARG);
    if (renderer.bound_vertex1)
        transition(renderer, renderer.bound_vertex1.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
    transition(renderer, buffer.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer.gpu->GetGPUVirtualAddress() + offset_bytes;
    view.SizeInBytes = buffer.size - offset_bytes;
    view.StrideInBytes = stride_bytes;
    renderer.commands->IASetVertexBuffers(1, 1, &view);
    renderer.bound_vertex1 = buffer.gpu;
    renderer.bound_stride1 = stride_bytes;
    renderer.bound_bytes1 = view.SizeInBytes;
    return 0;
}

uint32_t bind_vertex_buffer_slot2(atmera_render_handle handle, atmera_buffer_handle buffer_handle,
    uint32_t stride_bytes, uint32_t offset_bytes) {
    if (!handle || !buffer_handle || !stride_bytes) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (!renderer.recording || buffer.owner != &renderer || buffer.usage != ATMERA_BUFFER_VERTEX ||
        offset_bytes >= buffer.size || stride_bytes > buffer.size - offset_bytes ||
        renderer.bound_vertex.Get() == buffer.gpu.Get() ||
        renderer.bound_vertex1.Get() == buffer.gpu.Get() ||
        (renderer.required_stride2 && stride_bytes < renderer.required_stride2))
        return static_cast<uint32_t>(E_INVALIDARG);
    if (renderer.bound_vertex2)
        transition(renderer, renderer.bound_vertex2.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
    transition(renderer, buffer.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer.gpu->GetGPUVirtualAddress() + offset_bytes;
    view.SizeInBytes = buffer.size - offset_bytes;
    view.StrideInBytes = stride_bytes;
    renderer.commands->IASetVertexBuffers(2, 1, &view);
    renderer.bound_vertex2 = buffer.gpu;
    renderer.bound_stride2 = stride_bytes;
    renderer.bound_bytes2 = view.SizeInBytes;
    return 0;
}

uint32_t bind_constant_buffer(atmera_render_handle handle, atmera_buffer_handle buffer_handle) {
    if (!handle || !buffer_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(buffer_handle);
    if (!renderer.recording || buffer.owner != &renderer || buffer.usage != ATMERA_BUFFER_CONSTANT)
        return static_cast<uint32_t>(E_INVALIDARG);
    if (renderer.bound_constant.Get() != buffer.gpu.Get()) {
        if (renderer.bound_constant)
            transition(renderer, renderer.bound_constant.Get(),
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        transition(renderer, buffer.gpu.Get(),
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        renderer.bound_constant = buffer.gpu;
    }
    renderer.commands->SetGraphicsRootConstantBufferView(2, buffer.gpu->GetGPUVirtualAddress());
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
    if (renderer.bound_index)
        transition(renderer, renderer.bound_index.Get(),
            D3D12_RESOURCE_STATE_INDEX_BUFFER, D3D12_RESOURCE_STATE_COMMON);
    transition(renderer, buffer.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    D3D12_INDEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer.gpu->GetGPUVirtualAddress() + offset_bytes;
    view.SizeInBytes = buffer.size - offset_bytes;
    view.Format = index_format == ATMERA_INDEX_UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    renderer.commands->IASetIndexBuffer(&view);
    renderer.bound_index = buffer.gpu;
    renderer.index_element_bytes = element_bytes;
    renderer.index_bound_bytes = view.SizeInBytes;
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
    renderer.commands->DrawInstanced(vertex_count, 1, 0, 0);
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
    renderer.commands->DrawIndexedInstanced(index_count, 1, 0, 0, 0);
    return 0;
}

uint32_t end_frame(atmera_render_handle handle) {
    if (!handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    if (!renderer.recording) return static_cast<uint32_t>(E_FAIL);
    renderer.recording = false;
    const UINT index = renderer.presentation.frame_index;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = renderer.presentation.back_buffers[index].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    HRESULT hr = S_OK;
    if (renderer.bound_vertex) {
        transition(renderer, renderer.bound_vertex.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        renderer.bound_vertex.Reset();
    }
    if (renderer.bound_vertex1) {
        transition(renderer, renderer.bound_vertex1.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        renderer.bound_vertex1.Reset();
    }
    if (renderer.bound_vertex2) {
        transition(renderer, renderer.bound_vertex2.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        renderer.bound_vertex2.Reset();
    }
    if (renderer.bound_index) {
        transition(renderer, renderer.bound_index.Get(),
            D3D12_RESOURCE_STATE_INDEX_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        renderer.bound_index.Reset();
    }
    if (renderer.bound_constant) {
        transition(renderer, renderer.bound_constant.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        renderer.bound_constant.Reset();
    }
    if (renderer.bound_texture) {
        transition(renderer, renderer.bound_texture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        renderer.bound_texture.Reset();
    }
    if (renderer.bound_texture2) {
        transition(renderer, renderer.bound_texture2.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        renderer.bound_texture2.Reset();
    }
    if (renderer.capture) {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        renderer.commands->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = renderer.readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        destination.PlacedFootprint.Footprint.Width = 1;
        destination.PlacedFootprint.Footprint.Height = 1;
        destination.PlacedFootprint.Footprint.Depth = 1;
        destination.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = renderer.presentation.back_buffers[index].Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_BOX box{renderer.width / 2, renderer.height / 2, 0,
            renderer.width / 2 + 1, renderer.height / 2 + 1, 1};
        renderer.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    } else {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    renderer.commands->ResourceBarrier(1, &barrier);
    hr = renderer.commands->Close();
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    ID3D12CommandList* lists[] = {renderer.commands.Get()};
    renderer.queue->ExecuteCommandLists(1, lists);
    const HRESULT present_result = renderer.presentation.swap_chain->Present(0, 0);
    hr = wait_gpu(renderer);
    renderer.presentation.frame_index = renderer.presentation.swap_chain->GetCurrentBackBufferIndex();
    if (FAILED(present_result) || FAILED(hr)) return static_cast<uint32_t>(FAILED(present_result) ? present_result : hr);
    if (renderer.capture) {
        const D3D12_RANGE read_range{0, 4};
        void* mapped = nullptr;
        hr = renderer.readback->Map(0, &read_range, &mapped);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        std::memcpy(renderer.center_pixel.data(), mapped, 4);
        const D3D12_RANGE written_range{0, 0};
        renderer.readback->Unmap(0, &written_range);
    }
    return 0;
}

uint32_t render_frame(atmera_render_handle handle) {
    const auto started = begin_frame(handle);
    if (started != 0) return started;
    auto& renderer = *static_cast<Renderer*>(handle);
    renderer.commands->SetPipelineState(renderer.pipeline.Get());
    renderer.pipeline_bound = true;
    renderer.commands->DrawInstanced(3, 1, 0, 0);
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
    const auto resource_desc = buffer_desc(desc->size_bytes);
    const HRESULT hr = make_resource(renderer, D3D12_HEAP_TYPE_DEFAULT, resource_desc,
        D3D12_RESOURCE_STATE_COMMON, buffer->gpu);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_buffer = buffer.release();
    return 0;
}

uint32_t write_buffer(atmera_render_handle handle, atmera_buffer_handle resource,
    uint32_t offset, const void* data, uint32_t size) {
    if (!handle || !resource || !data || !size) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(resource);
    if (buffer.owner != &renderer || offset > buffer.size || size > buffer.size - offset)
        return static_cast<uint32_t>(E_INVALIDARG);
    if (buffer.usage == ATMERA_BUFFER_CONSTANT && (offset != 0 || size != buffer.size))
        return static_cast<uint32_t>(E_INVALIDARG);
    ComPtr<ID3D12Resource> upload;
    HRESULT hr = make_resource(renderer, D3D12_HEAP_TYPE_UPLOAD, buffer_desc(size),
        D3D12_RESOURCE_STATE_GENERIC_READ, upload);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    hr = upload->Map(0, &no_read, &mapped);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    std::memcpy(mapped, data, size);
    const D3D12_RANGE written{0, size};
    upload->Unmap(0, &written);
    hr = begin_copy(renderer);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    transition(renderer, buffer.gpu.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    renderer.commands->CopyBufferRegion(buffer.gpu.Get(), offset, upload.Get(), 0, size);
    transition(renderer, buffer.gpu.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    return static_cast<uint32_t>(end_copy(renderer));
}

uint32_t read_buffer(atmera_render_handle handle, atmera_buffer_handle resource,
    uint32_t offset, void* data, uint32_t size) {
    if (!handle || !resource || !data || !size) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& buffer = *static_cast<Buffer*>(resource);
    if (buffer.owner != &renderer || offset > buffer.size || size > buffer.size - offset)
        return static_cast<uint32_t>(E_INVALIDARG);
    ComPtr<ID3D12Resource> readback;
    HRESULT hr = make_resource(renderer, D3D12_HEAP_TYPE_READBACK, buffer_desc(size),
        D3D12_RESOURCE_STATE_COPY_DEST, readback);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = begin_copy(renderer);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    transition(renderer, buffer.gpu.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    renderer.commands->CopyBufferRegion(readback.Get(), 0, buffer.gpu.Get(), offset, size);
    transition(renderer, buffer.gpu.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    hr = end_copy(renderer);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    const D3D12_RANGE read_range{0, size};
    void* mapped = nullptr;
    hr = readback->Map(0, &read_range, &mapped);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    std::memcpy(data, mapped, size);
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
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
    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = desc->width;
    resource_desc.Height = desc->height;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const HRESULT hr = make_resource(renderer, D3D12_HEAP_TYPE_DEFAULT, resource_desc,
        D3D12_RESOURCE_STATE_COMMON, texture->gpu);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_texture = texture.release();
    return 0;
}

bool valid_texture_io(const Texture& texture, uint32_t row_pitch, uint32_t byte_count) {
    const uint64_t row_bytes = static_cast<uint64_t>(texture.width) * 4;
    return row_pitch >= row_bytes &&
        byte_count >= static_cast<uint64_t>(texture.height - 1) * row_pitch + row_bytes;
}

bool texture_layout(const Texture& texture, uint32_t& aligned_pitch, uint64_t& total) {
    const uint64_t row_bytes = static_cast<uint64_t>(texture.width) * 4;
    const uint64_t pitch = (row_bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
        ~(static_cast<uint64_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) - 1);
    if (pitch > std::numeric_limits<uint32_t>::max()) return false;
    if (texture.height > std::numeric_limits<uint64_t>::max() / pitch) return false;
    aligned_pitch = static_cast<uint32_t>(pitch);
    total = pitch * texture.height;
    return total >= pitch && total <= std::numeric_limits<size_t>::max();
}

D3D12_TEXTURE_COPY_LOCATION texture_location(ID3D12Resource* resource) {
    D3D12_TEXTURE_COPY_LOCATION location{};
    location.pResource = resource;
    location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    return location;
}

D3D12_TEXTURE_COPY_LOCATION buffer_location(ID3D12Resource* resource,
    const Texture& texture, uint32_t aligned_pitch) {
    D3D12_TEXTURE_COPY_LOCATION location{};
    location.pResource = resource;
    location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    location.PlacedFootprint.Footprint.Width = texture.width;
    location.PlacedFootprint.Footprint.Height = texture.height;
    location.PlacedFootprint.Footprint.Depth = 1;
    location.PlacedFootprint.Footprint.RowPitch = aligned_pitch;
    return location;
}

uint32_t write_texture(atmera_render_handle handle, atmera_texture_handle resource,
    const uint8_t* pixels, uint32_t row_pitch, uint32_t byte_count) {
    if (!handle || !resource || !pixels) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& texture = *static_cast<Texture*>(resource);
    if (texture.owner != &renderer || !valid_texture_io(texture, row_pitch, byte_count))
        return static_cast<uint32_t>(E_INVALIDARG);
    uint32_t aligned_pitch = 0;
    uint64_t total = 0;
    if (!texture_layout(texture, aligned_pitch, total)) return static_cast<uint32_t>(E_INVALIDARG);
    ComPtr<ID3D12Resource> upload;
    HRESULT hr = make_resource(renderer, D3D12_HEAP_TYPE_UPLOAD, buffer_desc(total),
        D3D12_RESOURCE_STATE_GENERIC_READ, upload);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    hr = upload->Map(0, &no_read, &mapped);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    const auto row_bytes = static_cast<size_t>(texture.width) * 4;
    for (uint32_t y = 0; y < texture.height; ++y)
        std::memcpy(static_cast<uint8_t*>(mapped) + static_cast<size_t>(y) * aligned_pitch,
            pixels + static_cast<size_t>(y) * row_pitch, row_bytes);
    const D3D12_RANGE written{0, static_cast<SIZE_T>(total)};
    upload->Unmap(0, &written);
    hr = begin_copy(renderer);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    transition(renderer, texture.gpu.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    const auto destination = texture_location(texture.gpu.Get());
    const auto source = buffer_location(upload.Get(), texture, aligned_pitch);
    renderer.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    transition(renderer, texture.gpu.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    return static_cast<uint32_t>(end_copy(renderer));
}

uint32_t read_texture(atmera_render_handle handle, atmera_texture_handle resource,
    uint8_t* pixels, uint32_t row_pitch, uint32_t byte_count) {
    if (!handle || !resource || !pixels) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& texture = *static_cast<Texture*>(resource);
    if (texture.owner != &renderer || !valid_texture_io(texture, row_pitch, byte_count))
        return static_cast<uint32_t>(E_INVALIDARG);
    uint32_t aligned_pitch = 0;
    uint64_t total = 0;
    if (!texture_layout(texture, aligned_pitch, total)) return static_cast<uint32_t>(E_INVALIDARG);
    ComPtr<ID3D12Resource> readback;
    HRESULT hr = make_resource(renderer, D3D12_HEAP_TYPE_READBACK, buffer_desc(total),
        D3D12_RESOURCE_STATE_COPY_DEST, readback);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = begin_copy(renderer);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    transition(renderer, texture.gpu.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    const auto destination = buffer_location(readback.Get(), texture, aligned_pitch);
    const auto source = texture_location(texture.gpu.Get());
    renderer.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    transition(renderer, texture.gpu.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    hr = end_copy(renderer);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(total)};
    void* mapped = nullptr;
    hr = readback->Map(0, &read_range, &mapped);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    const auto row_bytes = static_cast<size_t>(texture.width) * 4;
    for (uint32_t y = 0; y < texture.height; ++y)
        std::memcpy(pixels + static_cast<size_t>(y) * row_pitch,
            static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * aligned_pitch,
            row_bytes);
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
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
    wchar_t entry[128]{};
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, desc->entry_point, -1, entry, _countof(entry)))
        return static_cast<uint32_t>(E_INVALIDARG);
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
    const wchar_t* profile = desc->stage == ATMERA_SHADER_VERTEX ? L"vs_6_0" : L"ps_6_0";
    HRESULT hr = compile_shader(desc->source, desc->source_size, entry, profile, shader->bytecode);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    ComPtr<IDxcUtils> utils;
    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    const DxcBuffer reflection_data{shader->bytecode->GetBufferPointer(),
        shader->bytecode->GetBufferSize(), 0};
    hr = utils->CreateReflection(&reflection_data, IID_PPV_ARGS(&shader->reflection));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    D3D12_SHADER_DESC shader_desc{};
    hr = shader->reflection->GetDesc(&shader_desc);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    for (UINT i = 0; i < shader_desc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        hr = shader->reflection->GetResourceBindingDesc(i, &binding);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        if (binding.Type == D3D_SIT_CBUFFER) ++shader->constant_buffer_bindings;
        else if (binding.Type == D3D_SIT_TEXTURE) ++shader->texture_bindings;
        else if (binding.Type == D3D_SIT_SAMPLER) ++shader->sampler_bindings;
    }
    try {
        std::string source(desc->source, desc->source_size);
        std::string cached_entry(desc->entry_point);
        cache.source.swap(source);
        cache.entry.swap(cached_entry);
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

    D3D12_SHADER_DESC shader_desc{};
    HRESULT hr = shader.reflection->GetDesc(&shader_desc);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    uint32_t written = 0;
    for (UINT i = 0; i < shader_desc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        hr = shader.reflection->GetResourceBindingDesc(i, &binding);
        if (FAILED(hr)) return static_cast<uint32_t>(hr);
        const uint32_t kind = binding.Type == D3D_SIT_CBUFFER ? ATMERA_SHADER_BINDING_CONSTANT_BUFFER :
            binding.Type == D3D_SIT_TEXTURE ? ATMERA_SHADER_BINDING_TEXTURE :
            binding.Type == D3D_SIT_SAMPLER ? ATMERA_SHADER_BINDING_SAMPLER : 0;
        if (kind != 0) out_bindings[written++] = {sizeof(atmera_shader_binding), kind,
            binding.BindPoint, binding.Space, binding.BindCount};
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
    D3D12_SHADER_DESC shader_desc{};
    HRESULT hr = shader.reflection->GetDesc(&shader_desc);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    uint32_t supported_index = 0;
    for (UINT i = 0; i < shader_desc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
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
    pipeline->root_signature = renderer.root_signature;
    const HRESULT hr = make_pipeline(renderer, vertex.bytecode.Get(), pixel.bytecode.Get(), pipeline->state);
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
    std::array<D3D12_INPUT_ELEMENT_DESC, 16> elements{};
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
            attr.offset_bytes, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
    }
    auto pipeline = std::unique_ptr<Pipeline>(new (std::nothrow) Pipeline());
    if (!pipeline) return static_cast<uint32_t>(E_OUTOFMEMORY);
    pipeline->owner = &renderer;
    pipeline->root_signature = renderer.root_signature;
    pipeline->required_stride = required_stride;
    const HRESULT hr = make_pipeline(renderer, vertex.bytecode.Get(), pixel.bytecode.Get(),
        pipeline->state, elements.data(), layout->attribute_count);
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
    std::array<D3D12_INPUT_ELEMENT_DESC, 16> elements{};
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
                attr.offset_bytes, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
        }
    }
    auto pipeline = std::unique_ptr<Pipeline>(new (std::nothrow) Pipeline());
    if (!pipeline) return static_cast<uint32_t>(E_OUTOFMEMORY);
    pipeline->owner = &renderer;
    pipeline->root_signature = renderer.root_signature;
    pipeline->required_stride = required[0];
    pipeline->required_stride1 = required[1];
    pipeline->required_stride2 = required[2];
    const HRESULT hr = make_pipeline(renderer, vertex.bytecode.Get(), pixel.bytecode.Get(),
        pipeline->state, elements.data(), count);
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
    HRESULT hr = make_root_signature(renderer, layout.texture_register, layout.sampler_register,
        layout.constant_register, pipeline->root_signature);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = make_pipeline(renderer, vertex.bytecode.Get(), pixel.bytecode.Get(), pipeline->state,
        nullptr, 0, pipeline->root_signature.Get());
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_pipeline = pipeline.release();
    return 0;
}

uint32_t create_pipeline_with_texture_pair(atmera_render_handle handle,
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
        layout.texture_register > 14 || vertex.stage != ATMERA_SHADER_VERTEX ||
        pixel.stage != ATMERA_SHADER_PIXEL) return static_cast<uint32_t>(E_INVALIDARG);
    auto pipeline = std::unique_ptr<Pipeline>(new (std::nothrow) Pipeline());
    if (!pipeline) return static_cast<uint32_t>(E_OUTOFMEMORY);
    pipeline->owner = &renderer;
    pipeline->texture_register = layout.texture_register;
    pipeline->sampler_register = layout.sampler_register;
    pipeline->constant_register = layout.constant_register;
    pipeline->texture_count = 2;
    HRESULT hr = make_root_signature(renderer, layout.texture_register, layout.sampler_register,
        layout.constant_register, pipeline->root_signature, 2);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = make_pipeline(renderer, vertex.bytecode.Get(), pixel.bytecode.Get(), pipeline->state,
        nullptr, 0, pipeline->root_signature.Get());
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_pipeline = pipeline.release();
    return 0;
}

uint32_t create_pipeline_with_texture_sampler_pair(atmera_render_handle handle,
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
        layout.texture_register > 14 || layout.sampler_register > 14 ||
        vertex.stage != ATMERA_SHADER_VERTEX || pixel.stage != ATMERA_SHADER_PIXEL)
        return static_cast<uint32_t>(E_INVALIDARG);
    auto pipeline = std::unique_ptr<Pipeline>(new (std::nothrow) Pipeline());
    if (!pipeline) return static_cast<uint32_t>(E_OUTOFMEMORY);
    pipeline->owner = &renderer;
    pipeline->texture_register = layout.texture_register;
    pipeline->sampler_register = layout.sampler_register;
    pipeline->constant_register = layout.constant_register;
    pipeline->texture_count = 2;
    pipeline->sampler_count = 2;
    HRESULT hr = make_root_signature(renderer, layout.texture_register, layout.sampler_register,
        layout.constant_register, pipeline->root_signature, 2, 2);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = make_pipeline(renderer, vertex.bytecode.Get(), pixel.bytecode.Get(), pipeline->state,
        nullptr, 0, pipeline->root_signature.Get());
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    *out_pipeline = pipeline.release();
    return 0;
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
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 1;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    const HRESULT hr = renderer.device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&view->heap));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    renderer.device->CreateShaderResourceView(texture.gpu.Get(), &srv,
        view->heap->GetCPUDescriptorHandleForHeapStart());
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
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    heap_desc.NumDescriptors = 1;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    const HRESULT hr = renderer.device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&sampler->heap));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    D3D12_SAMPLER_DESC native{};
    native.Filter = desc->filter == ATMERA_FILTER_POINT ?
        D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    native.AddressU = native.AddressV = native.AddressW =
        desc->address_mode == ATMERA_ADDRESS_CLAMP ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    native.MaxAnisotropy = 1;
    native.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    native.MaxLOD = D3D12_FLOAT32_MAX;
    renderer.device->CreateSampler(&native, sampler->heap->GetCPUDescriptorHandleForHeapStart());
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
    set->view_heap = view.heap;
    set->sampler_heap = sampler.heap;
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
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& layout = *static_cast<BindingLayout*>(layout_handle);
    auto& first = *static_cast<TextureView*>(first_handle);
    auto& second = *static_cast<TextureView*>(second_handle);
    if (layout.owner != &renderer || layout.texture_register > 14 ||
        first.owner != &renderer || second.owner != &renderer ||
        first.texture.Get() == second.texture.Get()) return static_cast<uint32_t>(E_INVALIDARG);
    const uint32_t result = create_binding_set_with_layout(handle, layout_handle,
        first_handle, sampler_handle, out_set);
    if (result != 0) return result;
    auto& set = *static_cast<BindingSet*>(*out_set);
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 2;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> pair_heap;
    const HRESULT hr = renderer.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pair_heap));
    if (FAILED(hr)) {
        delete static_cast<BindingSet*>(*out_set);
        *out_set = nullptr;
        return static_cast<uint32_t>(hr);
    }
    auto destination = pair_heap->GetCPUDescriptorHandleForHeapStart();
    renderer.device->CopyDescriptorsSimple(1, destination,
        first.heap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    destination.ptr += renderer.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    renderer.device->CopyDescriptorsSimple(1, destination,
        second.heap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    set.view_heap = pair_heap;
    set.texture2 = second.texture;
    set.texture_count = 2;
    return 0;
}

uint32_t create_binding_set_with_texture_sampler_pair(atmera_render_handle handle,
    atmera_binding_layout_handle layout_handle, atmera_texture_view_handle first_handle,
    atmera_sampler_handle first_sampler, atmera_texture_view_handle second_handle,
    atmera_sampler_handle second_sampler_handle, atmera_binding_set_handle* out_set) {
    if (!handle || !layout_handle || !second_sampler_handle || !out_set)
        return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(handle);
    auto& layout = *static_cast<BindingLayout*>(layout_handle);
    auto& second_sampler = *static_cast<Sampler*>(second_sampler_handle);
    if (layout.owner != &renderer || layout.sampler_register > 14 ||
        second_sampler.owner != &renderer) return static_cast<uint32_t>(E_INVALIDARG);
    const uint32_t result = create_binding_set_with_texture_pair(handle, layout_handle,
        first_handle, second_handle, first_sampler, out_set);
    if (result != 0) return result;
    auto& set = *static_cast<BindingSet*>(*out_set);
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    desc.NumDescriptors = 2;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> pair_heap;
    const HRESULT hr = renderer.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pair_heap));
    if (FAILED(hr)) {
        delete static_cast<BindingSet*>(*out_set);
        *out_set = nullptr;
        return static_cast<uint32_t>(hr);
    }
    auto destination = pair_heap->GetCPUDescriptorHandleForHeapStart();
    renderer.device->CopyDescriptorsSimple(1, destination,
        set.sampler_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    destination.ptr += renderer.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    renderer.device->CopyDescriptorsSimple(1, destination,
        second_sampler.heap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    set.sampler_heap = pair_heap;
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
    auto* old_first = renderer.bound_texture.Get();
    auto* old_second = renderer.bound_texture2.Get();
    auto* new_first = set.texture.Get();
    auto* new_second = set.texture2.Get();
    if (old_first && old_first != new_first && old_first != new_second)
        transition(renderer, old_first, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    if (old_second && old_second != new_first && old_second != new_second)
        transition(renderer, old_second, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    if (new_first != old_first && new_first != old_second)
        transition(renderer, new_first, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (new_second && new_second != old_first && new_second != old_second)
        transition(renderer, new_second, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    renderer.bound_texture = set.texture;
    renderer.bound_texture2 = set.texture2;
    ID3D12DescriptorHeap* heaps[] = {set.view_heap.Get(), set.sampler_heap.Get()};
    renderer.commands->SetDescriptorHeaps(2, heaps);
    renderer.commands->SetGraphicsRootDescriptorTable(0, set.view_heap->GetGPUDescriptorHandleForHeapStart());
    renderer.commands->SetGraphicsRootDescriptorTable(1, set.sampler_heap->GetGPUDescriptorHandleForHeapStart());
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
    HRESULT hr = renderer.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&list->allocator));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    hr = renderer.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        list->allocator.Get(), nullptr, IID_PPV_ARGS(&list->commands));
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
    try {
        list.resources.push_back(dst.gpu);
        list.resources.push_back(src.gpu);
    } catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    record_transition(list.commands.Get(), src.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    record_transition(list.commands.Get(), dst.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    list.commands->CopyBufferRegion(dst.gpu.Get(), destination_offset, src.gpu.Get(), source_offset, size);
    record_transition(list.commands.Get(), dst.gpu.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    record_transition(list.commands.Get(), src.gpu.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
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
    try {
        list.resources.push_back(dst.gpu);
        list.resources.push_back(src.gpu);
    } catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    record_transition(list.commands.Get(), src.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    record_transition(list.commands.Get(), dst.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    const auto target = texture_location(dst.gpu.Get());
    const auto origin = texture_location(src.gpu.Get());
    list.commands->CopyTextureRegion(&target, 0, 0, 0, &origin, nullptr);
    record_transition(list.commands.Get(), dst.gpu.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    record_transition(list.commands.Get(), src.gpu.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
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
    try {
        list.resources.push_back(dst.gpu);
        list.resources.push_back(src.gpu);
    } catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    record_transition(list.commands.Get(), src.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    record_transition(list.commands.Get(), dst.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    const auto target = texture_location(dst.gpu.Get());
    const auto origin = texture_location(src.gpu.Get());
    const D3D12_BOX box{source_x, source_y, 0, source_x + width, source_y + height, 1};
    list.commands->CopyTextureRegion(&target, destination_x, destination_y, 0, &origin, &box);
    record_transition(list.commands.Get(), dst.gpu.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    record_transition(list.commands.Get(), src.gpu.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    return 0;
}

uint32_t command_begin_render_target(atmera_command_list_handle list_handle, atmera_texture_handle target_handle) {
    if (!list_handle || !target_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    auto& target = *static_cast<Texture*>(target_handle);
    if (list.submitted || list.render_target_open || target.owner != list.owner)
        return static_cast<uint32_t>(E_INVALIDARG);
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> heap;
    const HRESULT hr = list.owner->device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    try {
        list.resources.push_back(target.gpu);
        list.render_targets.push_back(heap);
    } catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
    list.owner->device->CreateRenderTargetView(target.gpu.Get(), nullptr, rtv);
    record_transition(list.commands.Get(), target.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
    list.commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const float clear[] = {0.055f, 0.075f, 0.095f, 1.0f};
    list.commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(target.width), static_cast<float>(target.height), 0, 1};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(target.width), static_cast<LONG>(target.height)};
    list.commands->RSSetViewports(1, &viewport);
    list.commands->RSSetScissorRects(1, &scissor);
    list.commands->SetGraphicsRootSignature(list.owner->root_signature.Get());
    list.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list.active_target = target.gpu;
    list.active_texture.Reset();
    list.active_texture2.Reset();
    list.active_constant.Reset();
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
    try {
        list.pipelines.push_back(pipeline.state);
        list.root_signatures.push_back(pipeline.root_signature);
    }
    catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    list.commands->SetGraphicsRootSignature(pipeline.root_signature.Get());
    list.commands->SetPipelineState(pipeline.state.Get());
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
    try {
        list.resources.push_back(set.texture);
        if (set.texture2) list.resources.push_back(set.texture2);
        list.binding_heaps.push_back(set.view_heap);
        list.binding_heaps.push_back(set.sampler_heap);
    } catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    auto* old_first = list.active_texture.Get();
    auto* old_second = list.active_texture2.Get();
    auto* new_first = set.texture.Get();
    auto* new_second = set.texture2.Get();
    if (old_first && old_first != new_first && old_first != new_second)
        record_transition(list.commands.Get(), old_first,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    if (old_second && old_second != new_first && old_second != new_second)
        record_transition(list.commands.Get(), old_second,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    if (new_first != old_first && new_first != old_second)
        record_transition(list.commands.Get(), new_first,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (new_second && new_second != old_first && new_second != old_second)
        record_transition(list.commands.Get(), new_second,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    list.active_texture = set.texture;
    list.active_texture2 = set.texture2;
    ID3D12DescriptorHeap* heaps[] = {set.view_heap.Get(), set.sampler_heap.Get()};
    list.commands->SetDescriptorHeaps(2, heaps);
    list.commands->SetGraphicsRootDescriptorTable(0, set.view_heap->GetGPUDescriptorHandleForHeapStart());
    list.commands->SetGraphicsRootDescriptorTable(1, set.sampler_heap->GetGPUDescriptorHandleForHeapStart());
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
    try { list.resources.push_back(buffer.gpu); }
    catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    if (list.active_constant.Get() != buffer.gpu.Get()) {
        if (list.active_constant)
            record_transition(list.commands.Get(), list.active_constant.Get(),
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        record_transition(list.commands.Get(), buffer.gpu.Get(),
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        list.active_constant = buffer.gpu;
    }
    list.commands->SetGraphicsRootConstantBufferView(2, buffer.gpu->GetGPUVirtualAddress());
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
        list.active_vertex1.Get() == buffer.gpu.Get() ||
        list.active_vertex2.Get() == buffer.gpu.Get() ||
        (list.required_stride && stride_bytes < list.required_stride))
        return static_cast<uint32_t>(E_INVALIDARG);
    try { list.resources.push_back(buffer.gpu); }
    catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    if (list.active_vertex)
        record_transition(list.commands.Get(), list.active_vertex.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
    record_transition(list.commands.Get(), buffer.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer.gpu->GetGPUVirtualAddress() + offset_bytes;
    view.SizeInBytes = buffer.size - offset_bytes;
    view.StrideInBytes = stride_bytes;
    list.commands->IASetVertexBuffers(0, 1, &view);
    list.active_vertex = buffer.gpu;
    list.bound_stride = stride_bytes;
    list.bound_bytes = view.SizeInBytes;
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
        list.active_vertex.Get() == buffer.gpu.Get() ||
        list.active_vertex2.Get() == buffer.gpu.Get() ||
        (list.required_stride1 && stride_bytes < list.required_stride1))
        return static_cast<uint32_t>(E_INVALIDARG);
    try { list.resources.push_back(buffer.gpu); }
    catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    if (list.active_vertex1)
        record_transition(list.commands.Get(), list.active_vertex1.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
    record_transition(list.commands.Get(), buffer.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer.gpu->GetGPUVirtualAddress() + offset_bytes;
    view.SizeInBytes = buffer.size - offset_bytes;
    view.StrideInBytes = stride_bytes;
    list.commands->IASetVertexBuffers(1, 1, &view);
    list.active_vertex1 = buffer.gpu;
    list.bound_stride1 = stride_bytes;
    list.bound_bytes1 = view.SizeInBytes;
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
        list.active_vertex.Get() == buffer.gpu.Get() ||
        list.active_vertex1.Get() == buffer.gpu.Get() ||
        (list.required_stride2 && stride_bytes < list.required_stride2))
        return static_cast<uint32_t>(E_INVALIDARG);
    try { list.resources.push_back(buffer.gpu); }
    catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    if (list.active_vertex2)
        record_transition(list.commands.Get(), list.active_vertex2.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
    record_transition(list.commands.Get(), buffer.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer.gpu->GetGPUVirtualAddress() + offset_bytes;
    view.SizeInBytes = buffer.size - offset_bytes;
    view.StrideInBytes = stride_bytes;
    list.commands->IASetVertexBuffers(2, 1, &view);
    list.active_vertex2 = buffer.gpu;
    list.bound_stride2 = stride_bytes;
    list.bound_bytes2 = view.SizeInBytes;
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
    try { list.resources.push_back(buffer.gpu); }
    catch (...) { return static_cast<uint32_t>(E_OUTOFMEMORY); }
    if (list.active_index)
        record_transition(list.commands.Get(), list.active_index.Get(),
            D3D12_RESOURCE_STATE_INDEX_BUFFER, D3D12_RESOURCE_STATE_COMMON);
    record_transition(list.commands.Get(), buffer.gpu.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    D3D12_INDEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer.gpu->GetGPUVirtualAddress() + offset_bytes;
    view.SizeInBytes = buffer.size - offset_bytes;
    view.Format = index_format == ATMERA_INDEX_UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    list.commands->IASetIndexBuffer(&view);
    list.active_index = buffer.gpu;
    list.index_element_bytes = element_bytes;
    list.index_bound_bytes = view.SizeInBytes;
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
    list.commands->DrawInstanced(vertex_count, 1, 0, 0);
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
    list.commands->DrawIndexedInstanced(index_count, 1, 0, 0, 0);
    return 0;
}

uint32_t command_end_render_target(atmera_command_list_handle list_handle) {
    if (!list_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& list = *static_cast<CommandList*>(list_handle);
    if (list.submitted || !list.render_target_open) return static_cast<uint32_t>(E_FAIL);
    if (list.active_texture) {
        record_transition(list.commands.Get(), list.active_texture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        list.active_texture.Reset();
    }
    if (list.active_texture2) {
        record_transition(list.commands.Get(), list.active_texture2.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        list.active_texture2.Reset();
    }
    if (list.active_constant) {
        record_transition(list.commands.Get(), list.active_constant.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        list.active_constant.Reset();
    }
    if (list.active_vertex) {
        record_transition(list.commands.Get(), list.active_vertex.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        list.active_vertex.Reset();
    }
    if (list.active_vertex1) {
        record_transition(list.commands.Get(), list.active_vertex1.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        list.active_vertex1.Reset();
    }
    if (list.active_vertex2) {
        record_transition(list.commands.Get(), list.active_vertex2.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        list.active_vertex2.Reset();
    }
    if (list.active_index) {
        record_transition(list.commands.Get(), list.active_index.Get(),
            D3D12_RESOURCE_STATE_INDEX_BUFFER, D3D12_RESOURCE_STATE_COMMON);
        list.active_index.Reset();
    }
    record_transition(list.commands.Get(), list.active_target.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
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
    HRESULT hr = list.commands->Close();
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    ID3D12CommandList* lists[] = {list.commands.Get()};
    renderer.queue->ExecuteCommandLists(1, lists);
    const UINT64 value = ++renderer.fence_value;
    hr = renderer.queue->Signal(renderer.fence.Get(), value);
    list.submitted = true;
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    fence->owner = &renderer;
    fence->value = value;
    fence->allocator = list.allocator;
    fence->commands = list.commands;
    fence->resources = std::move(list.resources);
    fence->pipelines = std::move(list.pipelines);
    fence->root_signatures = std::move(list.root_signatures);
    fence->render_targets = std::move(list.render_targets);
    fence->binding_heaps = std::move(list.binding_heaps);
    *out_fence = fence.release();
    return 0;
}

uint32_t wait_fence(atmera_command_queue_handle queue_handle, atmera_fence_handle fence_handle,
    uint32_t timeout_ms) {
    if (!queue_handle || !fence_handle) return static_cast<uint32_t>(E_INVALIDARG);
    auto& renderer = *static_cast<Renderer*>(queue_handle);
    auto& fence = *static_cast<Fence*>(fence_handle);
    if (fence.owner != &renderer) return static_cast<uint32_t>(E_INVALIDARG);
    if (renderer.fence->GetCompletedValue() >= fence.value) return 0;
    const HRESULT hr = renderer.fence->SetEventOnCompletion(fence.value, renderer.fence_event);
    if (FAILED(hr)) return static_cast<uint32_t>(hr);
    const DWORD waited = WaitForSingleObject(renderer.fence_event, timeout_ms);
    if (waited == WAIT_OBJECT_0) return 0;
    return static_cast<uint32_t>(HRESULT_FROM_WIN32(waited == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError()));
}

void destroy_fence(atmera_fence_handle fence) { delete static_cast<Fence*>(fence); }
void destroy_command_list(atmera_command_list_handle list) { delete static_cast<CommandList*>(list); }

void destroy(atmera_render_handle handle) { delete static_cast<Renderer*>(handle); }
const atmera_render_api api{sizeof(atmera_render_api), ATMERA_RENDER_ABI_VERSION,
    "atmera.d3d12", &create, &get_caps, &resize, &render_frame, &get_center_pixel, &destroy,
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
