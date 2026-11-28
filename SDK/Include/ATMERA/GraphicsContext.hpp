#pragma once
#include <ATMERA/RenderABI.h>
#include <filesystem>
#include <memory>
#include <string>

namespace atmera {
// Owns one backend DLL and one renderer. Destroy all renderer-owned resources before this object.
class GraphicsContext final {
public:
    static std::unique_ptr<GraphicsContext> open(const std::filesystem::path& backend_dll,
        const atmera_render_desc& desc, std::string& error);
    ~GraphicsContext();
    GraphicsContext(const GraphicsContext&) = delete;
    GraphicsContext& operator=(const GraphicsContext&) = delete;

    const atmera_render_api& api() const noexcept { return *api_; }
    atmera_render_handle handle() const noexcept { return renderer_; }
private:
    GraphicsContext(void* library, const atmera_render_api* api, atmera_render_handle renderer) noexcept
        : library_(library), api_(api), renderer_(renderer) {}
    void* library_ = nullptr;
    const atmera_render_api* api_ = nullptr;
    atmera_render_handle renderer_ = nullptr;
};
}
