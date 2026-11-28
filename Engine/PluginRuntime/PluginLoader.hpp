#pragma once
#include <ATMERA/Logger.hpp>
#include <ATMERA/PluginABI.h>
#include <ATMERA/EventBus.hpp>
#include <ATMERA/ServiceRegistry.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace atmera {
class PluginLoader {
public:
    PluginLoader(const Logger& logger, ServiceRegistry& services, EventBus& events) noexcept;
    ~PluginLoader();
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    bool open(const std::filesystem::path& path, std::string& error);
    bool initialize(std::string& error);
    bool load(const std::filesystem::path& path, std::string& error);
    void unload() noexcept;
    bool loaded() const noexcept { return library_ != nullptr; }
    bool active() const noexcept { return handle_ != nullptr; }
    const std::string& module_id() const noexcept { return module_id_; }
private:
    static void host_log(void* context, uint32_t level, const char* message);
    static uint32_t host_register_service(void* context, const char* id, void* instance);
    static void* host_find_service(void* context, const char* id);
    static uint64_t host_subscribe_event(void* context, const char* name,
        atmera_event_fn callback, void* user_context);
    static void host_unsubscribe_event(void* context, uint64_t token);
    static void host_publish_event(void* context, const char* name, const char* payload);
    const Logger& logger_;
    ServiceRegistry& services_;
    EventBus& events_;
    std::vector<EventBus::Token> subscriptions_;
    void* library_ = nullptr;
    const atmera_plugin_api* api_ = nullptr;
    atmera_plugin_handle handle_ = nullptr;
    std::string module_id_;
};
}
