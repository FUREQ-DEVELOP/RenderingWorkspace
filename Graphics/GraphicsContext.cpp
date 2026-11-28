#include <ATMERA/GraphicsContext.hpp>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>
#include <new>

namespace atmera {
std::unique_ptr<GraphicsContext> GraphicsContext::open(const std::filesystem::path& backend_dll,
    const atmera_render_desc& desc, std::string& error) {
    error.clear();
    std::filesystem::path absolute;
    try { absolute = std::filesystem::absolute(backend_dll); }
    catch (const std::filesystem::filesystem_error& ex) { error = ex.what(); return {}; }
    HMODULE library = LoadLibraryExW(absolute.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library) { error = "Backend DLL load failed: " + std::to_string(GetLastError()); return {}; }

    const auto entry = reinterpret_cast<atmera_get_render_api_fn>(GetProcAddress(library, ATMERA_RENDER_ENTRY));
    const atmera_render_api* api = nullptr;
    if (!entry || entry(ATMERA_RENDER_ABI_VERSION, &api) != 0 || !api ||
        api->struct_size < offsetof(atmera_render_api, get_shader_reflection) ||
        api->abi_version != ATMERA_RENDER_ABI_VERSION ||
        !api->create || !api->destroy) {
        error = "Backend Render ABI v1 is incompatible";
        FreeLibrary(library);
        return {};
    }
    atmera_render_handle renderer = nullptr;
    const uint32_t result = api->create(&desc, &renderer);
    if (result != 0 || !renderer) {
        error = "Renderer creation failed: " + std::to_string(result);
        FreeLibrary(library);
        return {};
    }
    auto context = std::unique_ptr<GraphicsContext>(new (std::nothrow) GraphicsContext(library, api, renderer));
    if (!context) {
        api->destroy(renderer);
        FreeLibrary(library);
        error = "Graphics context allocation failed";
    }
    return context;
}

GraphicsContext::~GraphicsContext() {
    if (renderer_) api_->destroy(renderer_);
    if (library_) FreeLibrary(static_cast<HMODULE>(library_));
}
}
