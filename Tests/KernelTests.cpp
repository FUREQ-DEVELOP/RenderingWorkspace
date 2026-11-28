#include <ATMERA/DependencyResolver.hpp>
#include <ATMERA/Engine.hpp>
#include <ATMERA/EventBus.hpp>
#include <ATMERA/PluginABI.h>
#include <ATMERA/ServiceRegistry.hpp>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TestProject {
    fs::path root = fs::temp_directory_path() / (L"atmera-kernel-tests-" + std::to_wstring(GetCurrentProcessId()));
    TestProject(const fs::path& first, const fs::path& second) {
        fs::create_directories(root / L"Modules" / L"Primary");
        fs::create_directories(root / L"Modules" / L"Secondary");
        fs::copy_file(first, root / L"Modules" / L"Primary" / L"ATMERA.TestPlugin.dll", fs::copy_options::overwrite_existing);
        fs::copy_file(second, root / L"Modules" / L"Secondary" / L"ATMERA.SecondPlugin.dll", fs::copy_options::overwrite_existing);
    }
    ~TestProject() { std::error_code ignored; fs::remove_all(root, ignored); }
};

void test_resolver() {
    using atmera::ModuleSpec;
    std::vector<size_t> order;
    std::string error;
    std::vector<ModuleSpec> specs{
        {"b", "b.dll", {"a"}}, {"a", "a.dll", {}}, {"off", "off.dll", {}, false}};
    check(atmera::DependencyResolver::resolve(specs, order, error), "Dependency order failed");
    check(order == std::vector<size_t>({1, 0}), "Dependencies were not ordered first");
    specs[1].dependencies = {"b"};
    check(!atmera::DependencyResolver::resolve(specs, order, error) && error.find("cycle") != std::string::npos,
        "Cycle was accepted");
    specs[1].dependencies.clear();
    specs[0].dependencies = {"off"};
    check(!atmera::DependencyResolver::resolve(specs, order, error), "Disabled dependency was accepted");
    specs[0].dependencies = {"missing"};
    check(!atmera::DependencyResolver::resolve(specs, order, error), "Missing dependency was accepted");
}

void test_services_and_events() {
    atmera::ServiceRegistry services;
    int service = 42;
    check(services.add("sample", "owner", &service), "Service registration failed");
    check(!services.add("sample", "other", &service), "Duplicate service was accepted");
    check(services.find("sample") == &service, "Service lookup failed");
    services.remove_owner("owner");
    check(services.find("sample") == nullptr, "Service was not removed");

    atmera::EventBus events;
    int calls = 0;
    atmera::EventBus::Token token = 0;
    token = events.subscribe("sample", [&](std::string_view payload) {
        check(payload == "data", "Event payload mismatch");
        ++calls;
        events.unsubscribe(token);
    });
    check(token != 0, "Event subscription failed");
    events.publish("sample", "data");
    events.publish("sample", "data");
    check(calls == 1, "Event unsubscribe failed");
}

void test_runtime(const fs::path& first, const fs::path& second) {
    TestProject project(first, second);
    std::vector<std::string> log;
    atmera::Engine engine(project.root, [&](atmera::LogLevel, std::string_view message) {
        log.emplace_back(message);
    });
    engine.events().subscribe("module.active", [&](std::string_view id) { log.emplace_back("active " + std::string(id)); });
    engine.events().subscribe("module.deactivated", [&](std::string_view id) { log.emplace_back("deactivated " + std::string(id)); });
    std::string error;
    check(engine.add_module({"atmera.second", "Secondary/ATMERA.SecondPlugin.dll", {"atmera.test"}}, error),
        "Second module registration failed");
    check(engine.add_module({"atmera.test", "Primary/ATMERA.TestPlugin.dll", {}}, error),
        "First module registration failed");
    check(!engine.add_module({"atmera.test", "duplicate.dll", {}}, error), "Duplicate module accepted");

    SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_SECOND", L"1");
    const bool failed = !engine.start(error);
    SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_SECOND", nullptr);
    check(failed && !engine.running() && !error.empty(), "Initialization failure did not roll back");
    check(engine.services().find("atmera.test.state") == nullptr,
        "Rollback retained a plugin service");
    check(engine.module_state("atmera.second") == atmera::ModuleState::Failed, "Failed state missing");
    check(engine.module_state("atmera.test") == atmera::ModuleState::Unloaded, "Dependency not unloaded");
    check(log == std::vector<std::string>({"Test plugin initialized", "active atmera.test",
        "deactivated atmera.test", "atmera.test shutdown"}), "Rollback order incorrect");

    log.clear();
    check(engine.start(error), "Restart after failure failed");
    check(engine.active_modules() == std::vector<std::string>({"atmera.test", "atmera.second"}),
        "Activation order incorrect");
    check(!engine.add_module({"late", "late.dll", {}}, error), "Registered module during runtime");
    int service = 7;
    check(engine.services().add("sample", "atmera.test", &service), "Runtime service registration failed");
    engine.stop();
    check(!engine.running() && engine.services().find("sample") == nullptr &&
        engine.services().find("atmera.test.state") == nullptr, "Stop did not clear services");
    check(log == std::vector<std::string>({"Test plugin initialized", "active atmera.test",
        "atmera.test received ping", "atmera.second initialized", "active atmera.second", "deactivated atmera.second",
        "atmera.second shutdown", "deactivated atmera.test", "atmera.test shutdown"}),
        "Shutdown was not in reverse order");
    const auto log_size = log.size();
    engine.events().publish("atmera.ping", "after unload");
    check(log.size() == log_size, "Unloaded plugin retained an event subscription");

    const auto other_root = project.root / "OtherProject";
    fs::create_directories(other_root / "Modules" / "Primary");
    fs::copy_file(first, other_root / "Modules" / "Primary" / "ATMERA.TestPlugin.dll");
    atmera::Engine other(other_root);
    check(other.add_module({"atmera.test", "Primary/ATMERA.TestPlugin.dll", {}}, error),
        "Second project registration failed");
    check(other.start(error) && other.active_modules() == std::vector<std::string>({"atmera.test"}),
        "Second project failed to start independently");
    other.stop();
    check(engine.start(error), "Original project did not restart after project switch");
    engine.stop();

    atmera::Engine escaped(project.root);
    check(escaped.add_module({"outside", "../ATMERA.TestPlugin.dll", {}}, error), "Path test registration failed");
    check(!escaped.start(error) && error.find("escapes") != std::string::npos, "Project path escape was accepted");

    atmera::Engine mismatch(project.root);
    check(mismatch.add_module({"wrong.id", "Primary/ATMERA.TestPlugin.dll", {}}, error), "Mismatch registration failed");
    check(!mismatch.start(error) && !mismatch.running(), "Module ID mismatch was accepted");

    atmera::Engine init_failure(project.root);
    check(init_failure.add_module({"atmera.test", "Primary/ATMERA.TestPlugin.dll", {}}, error),
        "Init failure registration failed");
    SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_INIT", L"1");
    const bool first_failed = !init_failure.start(error);
    SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_INIT", nullptr);
    check(first_failed && !init_failure.running() && init_failure.services().size() == 0,
        "Initial module failure did not clean up");
    std::string rolled_back_id;
    SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_INIT", L"1");
    const bool unversioned_failed = !init_failure.start_with_rollback(error, rolled_back_id);
    SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_INIT", nullptr);
    check(unversioned_failed && rolled_back_id.empty() && !init_failure.running(),
        "Unversioned module unexpectedly rolled back");
}

void test_project_install(const fs::path& first, const fs::path& second) {
    TestProject project(first, second);
    std::string error;
    atmera::Engine engine(project.root);
    check(!engine.install_module(first, "../escape", {}, error), "Unsafe module ID was accepted");
    check(!fs::exists(project.root / "Modules" / "escape"), "Unsafe install wrote a file");
    check(!engine.install_module(first, "wrong.id", {}, error), "Mismatched module ID was accepted");
    check(!fs::exists(project.root / "Modules" / "wrong.id"), "Mismatched install wrote a file");
    check(!engine.install_module_versioned(first, "atmera.test", {}, "1..2", error),
        "Invalid package version was accepted");
    check(engine.install_module_versioned(first, "atmera.test", {}, "1.2.3", error),
        "Versioned project-local install failed");
    const auto installed = project.root / "Modules" / "atmera.test" / first.filename();
    check(fs::is_regular_file(installed), "Installed DLL is missing");
    check(!engine.install_module(first, "atmera.test", {}, error), "Duplicate install was accepted");
    check(!fs::exists(project.root / "Modules" / "atmera.test.partial"),
        "Installation staging directory remained");
    check(fs::is_regular_file(project.root / "Modules.lock"), "Module lockfile was not written");
    const auto lock_staging = project.root / "Modules.lock.partial";
    { std::ofstream blocked(lock_staging); blocked << "occupied"; }
    check(!engine.install_module(second, "atmera.second", {"atmera.test"}, error),
        "Install succeeded without committing lockfile");
    check(!fs::exists(project.root / "Modules" / "atmera.second"),
        "Failed lockfile commit left an installed DLL");
    fs::remove(lock_staging);
    check(engine.start(error), "Installed module failed to start");
    check(!engine.install_module(second, "atmera.second", {"atmera.test"}, error),
        "Install while running was accepted");
    engine.stop();
    atmera::Engine update(project.root);
    check(update.install_module(second, "atmera.second", {"atmera.test"}, error),
        "Dependent module install did not preserve existing lockfile entries");
    check(update.start(error) && update.active_modules() ==
        std::vector<std::string>({"atmera.test", "atmera.second"}),
        "Installed dependency order failed");
    update.stop();
    atmera::Engine restored(project.root);
    check(restored.load_installed_modules(error), "Module lockfile reload failed");
    check(restored.module_version("atmera.test") == "1.2.3" &&
        restored.module_version("atmera.second").empty(),
        "Package versions did not survive lockfile reload");
    check(restored.start(error) && restored.active_modules() ==
        std::vector<std::string>({"atmera.test", "atmera.second"}),
        "Reloaded modules did not start in dependency order");
    check(!restored.set_module_enabled("atmera.second", false, error),
        "Active module was disabled");
    check(!restored.update_module_versioned(first, "atmera.test", "1.2.4", error),
        "Active module was updated");
    restored.stop();
    check(!restored.update_module_versioned(second, "atmera.test", "1.2.4", error),
        "Mismatched plugin was accepted for update");
    const auto new_version = project.root / "Modules" / "atmera.test" / "1.2.4";
    check(!fs::exists(new_version), "Rejected update wrote a new version");
    { std::ofstream blocked(lock_staging); blocked << "occupied"; }
    check(!restored.update_module_versioned(first, "atmera.test", "1.2.4", error),
        "Update succeeded without committing lockfile");
    check(!fs::exists(new_version) && restored.module_version("atmera.test") == "1.2.3",
        "Failed update did not roll back");
    fs::remove(lock_staging);
    check(restored.update_module_versioned(first, "atmera.test", "1.2.4", error) &&
        fs::is_regular_file(new_version / first.filename()) && fs::is_regular_file(installed),
        "Versioned update did not retain both versions");
    atmera::Engine updated(project.root);
    check(updated.load_installed_modules(error) && updated.module_version("atmera.test") == "1.2.4" &&
        updated.start(error), "Updated version did not reload and start");
    updated.stop();
    { std::ofstream blocked(lock_staging); blocked << "occupied"; }
    check(!restored.set_module_enabled("atmera.second", false, error),
        "Enable state changed without committing lockfile");
    fs::remove(lock_staging);
    check(!restored.set_module_enabled("atmera.test", false, error),
        "Dependency of an enabled module was disabled");
    check(restored.set_module_enabled("atmera.second", false, error),
        "Dependent module could not be disabled");
    check(restored.set_module_enabled("atmera.test", false, error),
        "Unneeded module could not be disabled");
    atmera::Engine disabled(project.root);
    check(disabled.load_installed_modules(error) && disabled.start(error) &&
        disabled.active_modules().empty(), "Disabled modules activated after reload");
    disabled.stop();
    check(disabled.set_module_enabled("atmera.test", true, error) &&
        disabled.set_module_enabled("atmera.second", true, error),
        "Modules could not be re-enabled in dependency order");
    atmera::Engine reenabled(project.root);
    check(reenabled.load_installed_modules(error) && reenabled.start(error) &&
        reenabled.active_modules() == std::vector<std::string>({"atmera.test", "atmera.second"}),
        "Re-enabled modules did not activate after reload");
    reenabled.stop();
    atmera::Engine selected(project.root);
    check(selected.load_installed_modules(error), "Updated lockfile did not reload");
    { std::ofstream blocked(lock_staging); blocked << "occupied"; }
    check(!selected.rollback_module("atmera.test", error) &&
        selected.module_version("atmera.test") == "1.2.4",
        "Failed rollback changed the selected module");
    fs::remove(lock_staging);
    check(selected.rollback_module("atmera.test", error) &&
        selected.module_version("atmera.test") == "1.2.3" && selected.start(error),
        "Previous version could not be selected");
    selected.stop();
    atmera::Engine redo(project.root);
    check(redo.load_installed_modules(error) && redo.module_version("atmera.test") == "1.2.3" &&
        redo.rollback_module("atmera.test", error) && redo.module_version("atmera.test") == "1.2.4" &&
        redo.start(error), "Rolled-back version could not be restored");
    redo.stop();

    atmera::Engine automatic(project.root);
    check(automatic.load_installed_modules(error), "Automatic rollback lockfile did not reload");
    std::string rolled_back_id;
    SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_INIT", L"1");
    SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_ONCE", L"1");
    const bool recovered = automatic.start_with_rollback(error, rolled_back_id);
    SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_INIT", nullptr);
    SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_ONCE", nullptr);
    check(recovered && rolled_back_id == "atmera.test" &&
        automatic.module_version("atmera.test") == "1.2.3" &&
        automatic.active_modules() == std::vector<std::string>({"atmera.test", "atmera.second"}),
        "Initialization failure did not start the previous version");
    automatic.stop();
    atmera::Engine recovered_reload(project.root);
    check(recovered_reload.load_installed_modules(error) &&
        recovered_reload.module_version("atmera.test") == "1.2.3" &&
        recovered_reload.start(error), "Automatic rollback was not persisted");
    recovered_reload.stop();
    check(automatic.rollback_module("atmera.test", error) &&
        automatic.module_version("atmera.test") == "1.2.4",
        "Updated version could not be reselected after automatic rollback");

    const auto other_root = project.root / "OtherInstall";
    fs::create_directories(other_root);
    atmera::Engine other(other_root);
    check(!fs::exists(other_root / "Modules" / "atmera.test"),
        "Module was shared between projects");
    check(other.install_module(first, "atmera.test", {}, error) && other.start(error),
        "Independent project installation failed");
    other.stop();
    const auto old_lock = other_root / "Modules.lock";
    std::string entry;
    { std::ifstream source(old_lock); std::string header; std::getline(source, header); std::getline(source, entry); }
    std::istringstream current(entry);
    std::string legacy_id, legacy_path, legacy_version, legacy_hash;
    std::string legacy_previous_path, legacy_previous_version, legacy_previous_hash;
    unsigned legacy_enabled = 0;
    std::size_t legacy_dependencies = 0;
    check(static_cast<bool>(current >> std::quoted(legacy_id) >> std::quoted(legacy_path) >>
        legacy_enabled >> std::quoted(legacy_version) >> std::quoted(legacy_previous_path) >>
        std::quoted(legacy_previous_version) >> std::quoted(legacy_previous_hash) >>
        std::quoted(legacy_hash) >>
        legacy_dependencies) && legacy_version.empty() && legacy_dependencies == 0,
        "Could not prepare legacy lockfile test");
    { std::ofstream old(old_lock, std::ios::trunc); old << "ATMERA_LOCK 2\n" <<
        std::quoted(legacy_id) << ' ' << std::quoted(legacy_path) << ' ' << legacy_enabled <<
        ' ' << std::quoted(legacy_version) << ' ' << std::quoted(legacy_hash) << " 0\n"; }
    atmera::Engine legacy_v2(other_root);
    check(legacy_v2.load_installed_modules(error) && legacy_v2.start(error),
        "Version 2 lockfile was not accepted");
    legacy_v2.stop();
    { std::ofstream old(old_lock, std::ios::trunc); old << "ATMERA_LOCK 1\n" <<
        std::quoted(legacy_id) << ' ' << std::quoted(legacy_path) << ' ' << legacy_enabled <<
        ' ' << std::quoted(legacy_hash) << " 0\n"; }
    atmera::Engine legacy(other_root);
    const bool legacy_loaded = legacy.load_installed_modules(error);
    if (!legacy_loaded) std::cerr << "Legacy load: " << error << '\n';
    check(legacy_loaded, "Legacy lockfile was not accepted");
    check(legacy.module_version("atmera.test").empty(), "Legacy version was not empty");
    check(legacy.start(error), "Legacy module did not start");
    legacy.stop();
    check(fs::is_regular_file(installed), "Other project modified original installation");
    { std::ofstream tamper(new_version / first.filename(), std::ios::binary | std::ios::app); tamper.put('x'); }
    atmera::Engine changed(project.root);
    check(!changed.load_installed_modules(error) && error.find("hash mismatch") != std::string::npos,
        "Modified module passed lockfile hash validation");
}

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "Expected two test DLL paths\n"; return 2; }
    try {
        const auto first = fs::absolute(argv[1]);
        const auto second = fs::absolute(argv[2]);
        HMODULE dll = LoadLibraryExW(first.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        check(dll != nullptr, "Direct DLL load failed");
        auto entry = reinterpret_cast<atmera_get_plugin_api_fn>(GetProcAddress(dll, ATMERA_PLUGIN_ENTRY));
        const atmera_plugin_api* api = nullptr;
        check(entry && entry(ATMERA_PLUGIN_ABI_VERSION + 1, &api) != 0, "ABI mismatch was accepted");
        FreeLibrary(dll);
        test_resolver();
        test_services_and_events();
        test_runtime(first, second);
        test_project_install(first, second);
        std::cout << "Resolver, services, events, project install/isolation, lifecycle and rollback passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
