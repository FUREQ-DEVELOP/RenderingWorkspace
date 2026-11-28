#pragma once
#include <ATMERA/Logger.hpp>
#include <ATMERA/ModuleRegistry.hpp>
#include <filesystem>
#include <string>

namespace atmera {
class Engine {
public:
    explicit Engine(std::filesystem::path project_root, LogSink sink = {});
    bool add_module(ModuleSpec spec, std::string& error);
    bool install_module(const std::filesystem::path& source_dll, std::string id,
        std::vector<std::string> dependencies, std::string& error);
    bool install_module_versioned(const std::filesystem::path& source_dll, std::string id,
        std::vector<std::string> dependencies, std::string version, std::string& error);
    bool update_module_versioned(const std::filesystem::path& source_dll, std::string_view id,
        std::string version, std::string& error);
    bool rollback_module(std::string_view id, std::string& error);
    bool load_installed_modules(std::string& error);
    bool set_module_enabled(std::string_view id, bool enabled, std::string& error);
    bool start(std::string& error);
    bool start_with_rollback(std::string& error, std::string& rolled_back_id);
    void stop() noexcept;
    bool running() const noexcept { return modules_.running(); }
    ModuleState module_state(std::string_view id) const { return modules_.state(id); }
    std::string module_version(std::string_view id) const { return modules_.version(id); }
    std::vector<std::string> active_modules() const { return modules_.active_modules(); }
    ServiceRegistry& services() noexcept { return services_; }
    EventBus& events() noexcept { return events_; }
private:
    Logger logger_;
    ServiceRegistry services_;
    EventBus events_;
    ModuleRegistry modules_;
};
}
