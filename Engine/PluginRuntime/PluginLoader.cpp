#include "PluginLoader.hpp"
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <exception>

namespace atmera {
PluginLoader::PluginLoader(const Logger& logger, ServiceRegistry& services, EventBus& events) noexcept
    : logger_(logger), services_(services), events_(events) {}
PluginLoader::~PluginLoader() { unload(); }

void PluginLoader::host_log(void* context, uint32_t level, const char* message) {
    auto* self = static_cast<PluginLoader*>(context);
    if (!self || !message) return;
    try { self->logger_.log(static_cast<LogLevel>(level), message); } catch (...) { /* C ABI boundary */ }
}

uint32_t PluginLoader::host_register_service(void* context, const char* id, void* instance) {
    auto* self = static_cast<PluginLoader*>(context);
    if (!self || !id || !instance) return 1;
    try { return self->services_.add(id, self->module_id_, instance) ? 0u : 2u; }
    catch (...) { return 3; }
}

void* PluginLoader::host_find_service(void* context, const char* id) {
    auto* self = static_cast<PluginLoader*>(context);
    if (!self || !id) return nullptr;
    try { return self->services_.find(id); } catch (...) { return nullptr; }
}

uint64_t PluginLoader::host_subscribe_event(void* context, const char* name,
    atmera_event_fn callback, void* user_context) {
    auto* self = static_cast<PluginLoader*>(context);
    if (!self || !name || !callback) return 0;
    try {
        const auto token = self->events_.subscribe(name, [callback, user_context](std::string_view payload) {
            const std::string terminated(payload);
            callback(user_context, terminated.c_str());
        });
        if (token) {
            try { self->subscriptions_.push_back(token); }
            catch (...) { self->events_.unsubscribe(token); return 0; }
        }
        return token;
    } catch (...) { return 0; }
}

void PluginLoader::host_unsubscribe_event(void* context, uint64_t token) {
    auto* self = static_cast<PluginLoader*>(context);
    if (!self) return;
    try {
        self->events_.unsubscribe(token);
        std::erase(self->subscriptions_, token);
    } catch (...) {}
}

void PluginLoader::host_publish_event(void* context, const char* name, const char* payload) {
    auto* self = static_cast<PluginLoader*>(context);
    if (!self || !name || !payload) return;
    try { self->events_.publish(name, payload); } catch (...) {}
}

bool PluginLoader::open(const std::filesystem::path& path, std::string& error) {
    if (loaded()) { error = "A plugin is already loaded"; return false; }
    std::filesystem::path full_path;
    try { full_path = std::filesystem::absolute(path); }
    catch (const std::filesystem::filesystem_error& ex) { error = ex.what(); return false; }
    auto* library = LoadLibraryExW(full_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library) { error = "LoadLibraryExW failed: " + std::to_string(GetLastError()); return false; }
    auto fail = [&](const char* reason) {
        error = reason;
        FreeLibrary(library);
        return false;
    };
    const auto entry = reinterpret_cast<atmera_get_plugin_api_fn>(GetProcAddress(library, ATMERA_PLUGIN_ENTRY));
    if (!entry) return fail("Plugin entry point is missing");
    const atmera_plugin_api* api = nullptr;
    try {
        if (entry(ATMERA_PLUGIN_ABI_VERSION, &api) != 0 || !api) return fail("Plugin rejected host ABI");
        if (api->struct_size < sizeof(atmera_plugin_api) || api->abi_version != ATMERA_PLUGIN_ABI_VERSION ||
            !api->module_id || !api->initialize || !api->shutdown) return fail("Invalid plugin API table");
        module_id_ = api->module_id;
        if (module_id_.empty()) return fail("Plugin module ID is empty");
        library_ = library;
        api_ = api;
        error.clear();
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        FreeLibrary(library);
        return false;
    } catch (...) { return fail("Unknown plugin load failure"); }
}

bool PluginLoader::initialize(std::string& error) {
    if (!loaded()) { error = "Plugin is not loaded"; return false; }
    if (active()) { error = "Plugin is already initialized"; return false; }
    atmera_host_api host{ sizeof(atmera_host_api), ATMERA_PLUGIN_ABI_VERSION,
        this, &host_log, &host_register_service, &host_find_service,
        &host_subscribe_event, &host_unsubscribe_event, &host_publish_event };
    atmera_plugin_handle handle = nullptr;
    try {
        const auto result = api_->initialize(&host, &handle);
        if (result != 0 || !handle) {
            if (handle) {
                const auto failed_handle = handle;
                handle = nullptr;
                try { api_->shutdown(failed_handle); } catch (...) {}
            }
            error = "Plugin initialization failed: " + std::to_string(result);
            return false;
        }
        handle_ = handle;
        error.clear();
        return true;
    } catch (...) {
        if (handle) { try { api_->shutdown(handle); } catch (...) {} }
        error = "Plugin threw during initialization";
        return false;
    }
}

bool PluginLoader::load(const std::filesystem::path& path, std::string& error) {
    if (!open(path, error)) return false;
    if (initialize(error)) return true;
    unload();
    return false;
}

void PluginLoader::unload() noexcept {
    if (!library_) return;
    try { if (api_ && handle_) api_->shutdown(handle_); } catch (...) { /* no exception across ABI */ }
    for (const auto token : subscriptions_) events_.unsubscribe(token);
    subscriptions_.clear();
    services_.remove_owner(module_id_);
    FreeLibrary(static_cast<HMODULE>(library_));
    library_ = nullptr;
    api_ = nullptr;
    handle_ = nullptr;
    module_id_.clear();
}
}
