#include <ATMERA/Engine.hpp>

namespace atmera {
Engine::Engine(std::filesystem::path project_root, LogSink sink)
    : logger_(std::move(sink)), modules_(std::move(project_root), logger_, services_, events_) {}
bool Engine::add_module(ModuleSpec spec, std::string& error) {
    return modules_.add(std::move(spec), error);
}

bool Engine::install_module(const std::filesystem::path& source_dll, std::string id,
    std::vector<std::string> dependencies, std::string& error) {
    return modules_.install(source_dll, std::move(id), std::move(dependencies), error);
}
bool Engine::install_module_versioned(const std::filesystem::path& source_dll, std::string id,
    std::vector<std::string> dependencies, std::string version, std::string& error) {
    if (version.empty()) { error = "Package version is required"; return false; }
    return modules_.install(source_dll, std::move(id), std::move(dependencies), error,
        std::move(version));
}
bool Engine::update_module_versioned(const std::filesystem::path& source_dll, std::string_view id,
    std::string version, std::string& error) {
    return modules_.update(source_dll, id, std::move(version), error);
}
bool Engine::rollback_module(std::string_view id, std::string& error) {
    return modules_.rollback(id, error);
}
bool Engine::load_installed_modules(std::string& error) { return modules_.load_lockfile(error); }
bool Engine::set_module_enabled(std::string_view id, bool enabled, std::string& error) {
    return modules_.set_enabled(id, enabled, error);
}
bool Engine::start(std::string& error) { return modules_.start(error); }
bool Engine::start_with_rollback(std::string& error, std::string& rolled_back_id) {
    return modules_.start_with_rollback(error, rolled_back_id);
}
void Engine::stop() noexcept { modules_.stop(); }
}
