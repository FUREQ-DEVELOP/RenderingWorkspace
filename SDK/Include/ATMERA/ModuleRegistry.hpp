#pragma once
#include <ATMERA/DependencyResolver.hpp>
#include <ATMERA/EventBus.hpp>
#include <ATMERA/Logger.hpp>
#include <ATMERA/ServiceRegistry.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atmera {
class PluginLoader;
enum class ModuleState {
    Discovered, Validated, DependenciesResolved, Loaded, Initialized,
    Active, Deactivated, Shutdown, Unloaded, Failed
};

class ModuleRegistry {
public:
    ModuleRegistry(std::filesystem::path project_root, const Logger& logger,
        ServiceRegistry& services, EventBus& events);
    ~ModuleRegistry();
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    bool add(ModuleSpec spec, std::string& error);
    // Installs a local DLL under this project's Modules/ and registers it.
    bool install(const std::filesystem::path& source_dll, std::string id,
        std::vector<std::string> dependencies, std::string& error,
        std::string version = {});
    bool update(const std::filesystem::path& source_dll, std::string_view id,
        std::string version, std::string& error);
    bool rollback(std::string_view id, std::string& error);
    bool load_lockfile(std::string& error);
    bool set_enabled(std::string_view id, bool enabled, std::string& error);
    bool start(std::string& error);
    // On initialization failure, select the retained version and retry once.
    bool start_with_rollback(std::string& error, std::string& rolled_back_id);
    void stop() noexcept;
    bool running() const noexcept { return !active_.empty(); }
    ModuleState state(std::string_view id) const;
    std::string version(std::string_view id) const;
    std::vector<std::string> active_modules() const;
    const std::filesystem::path& project_root() const noexcept { return project_root_; }
private:
    bool module_path(const ModuleSpec& spec, std::filesystem::path& result, std::string& error) const;
    bool save_lockfile(std::string& error) const;
    std::filesystem::path project_root_;
    const Logger& logger_;
    ServiceRegistry& services_;
    EventBus& events_;
    std::vector<ModuleSpec> specs_;
    std::vector<ModuleState> states_;
    std::vector<std::unique_ptr<PluginLoader>> loaders_;
    std::vector<size_t> active_;
    std::optional<size_t> failed_initialization_index_;
};
}
