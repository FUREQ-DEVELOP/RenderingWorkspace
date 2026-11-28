#include <ATMERA/ModuleRegistry.hpp>
#include "PluginLoader.hpp"
#include <algorithm>
#include <array>
#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#pragma comment(lib, "bcrypt.lib")

namespace atmera {
namespace {
bool valid_module_id(std::string_view id) {
    if (id.empty() || id == "." || id == "..") return false;
    return std::all_of(id.begin(), id.end(), [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
    });
}
bool valid_package_version(std::string_view version) {
    if (version.empty()) return true;
    if (version.size() > 64) return false;
    unsigned dots = 0;
    bool has_digit = false;
    for (const char ch : version) {
        if (ch == '.') {
            if (!has_digit) return false;
            ++dots;
            has_digit = false;
        } else if (ch >= '0' && ch <= '9') has_digit = true;
        else return false;
    }
    return dots == 2 && has_digit;
}
bool file_sha256(const std::filesystem::path& path, std::string& hex) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 64 * 1024> block{};
    while (ok && file) {
        file.read(reinterpret_cast<char*>(block.data()), block.size());
        const auto count = file.gcount();
        if (count > 0) ok = BCryptHashData(hash, block.data(),
            static_cast<ULONG>(count), 0) >= 0;
    }
    ok = ok && file.eof();
    std::array<unsigned char, 32> digest{};
    if (ok) ok = BCryptFinishHash(hash, digest.data(),
        static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return false;
    constexpr char digits[] = "0123456789abcdef";
    hex.clear();
    for (const auto byte : digest) {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    return true;
}
}
ModuleRegistry::ModuleRegistry(std::filesystem::path project_root, const Logger& logger,
    ServiceRegistry& services, EventBus& events)
    : project_root_(std::filesystem::weakly_canonical(std::move(project_root))),
      logger_(logger), services_(services), events_(events) {}
ModuleRegistry::~ModuleRegistry() { stop(); }

bool ModuleRegistry::add(ModuleSpec spec, std::string& error) {
    if (running()) { error = "Cannot register modules while running"; return false; }
    if (spec.id.empty() || spec.relative_path.empty()) {
        error = "Module ID and path are required"; return false;
    }
    if (!valid_package_version(spec.version)) {
        error = "Invalid package version"; return false;
    }
    if (std::any_of(specs_.begin(), specs_.end(), [&](const auto& item) { return item.id == spec.id; })) {
        error = "Duplicate module ID: " + spec.id; return false;
    }
    specs_.push_back(std::move(spec));
    states_.push_back(ModuleState::Discovered);
    loaders_.push_back(nullptr);
    error.clear();
    return true;
}

bool ModuleRegistry::install(const std::filesystem::path& source_dll, std::string id,
    std::vector<std::string> dependencies, std::string& error, std::string version) {
    if (running()) { error = "Cannot install modules while running"; return false; }
    if (!valid_package_version(version)) { error = "Invalid package version"; return false; }
    if (specs_.empty() && std::filesystem::exists(project_root_ / "Modules.lock") &&
        !load_lockfile(error)) return false;
    if (!valid_module_id(id)) {
        error = "Invalid module ID"; return false;
    }
    if (std::any_of(specs_.begin(), specs_.end(), [&](const auto& spec) { return spec.id == id; })) {
        error = "Duplicate module ID: " + id; return false;
    }
    try {
        if (!std::filesystem::is_regular_file(source_dll) || source_dll.extension() != L".dll") {
            error = "Source must be a DLL file"; return false;
        }
        const auto modules = project_root_ / "Modules";
        if (std::filesystem::weakly_canonical(modules) != modules.lexically_normal()) {
            error = "Project Modules/ redirects outside the project"; return false;
        }
        const auto destination = modules / id;
        const auto staging = modules / (id + ".partial");
        if (std::filesystem::exists(destination) || std::filesystem::exists(staging)) {
            error = "Module installation already exists: " + id; return false;
        }
        PluginLoader loader(logger_, services_, events_);
        if (!loader.open(source_dll, error)) return false;
        if (loader.module_id() != id) {
            error = "Module ID mismatch: expected " + id + ", got " + loader.module_id();
            return false;
        }
        std::filesystem::create_directories(modules);
        if (!std::filesystem::create_directory(staging)) {
            error = "Module staging directory already exists: " + id; return false;
        }
        const auto staged_dll = staging / source_dll.filename();
        try {
            std::filesystem::copy_file(source_dll, staged_dll);
            std::filesystem::rename(staging, destination);
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove(staged_dll, ignored);
            std::filesystem::remove(staging, ignored);
            throw;
        }
        ModuleSpec spec{std::move(id), (std::filesystem::path(destination.filename()) /
            source_dll.filename()).generic_string(), std::move(dependencies), true, std::move(version)};
        if (!add(std::move(spec), error)) {
            std::error_code ignored;
            std::filesystem::remove(destination / source_dll.filename(), ignored);
            std::filesystem::remove(destination, ignored);
            return false;
        }
        if (!save_lockfile(error)) {
            specs_.pop_back();
            states_.pop_back();
            loaders_.pop_back();
            std::error_code ignored;
            std::filesystem::remove(destination / source_dll.filename(), ignored);
            std::filesystem::remove(destination, ignored);
            return false;
        }
        return true;
    } catch (const std::filesystem::filesystem_error& ex) {
        error = ex.what(); return false;
    }
}

bool ModuleRegistry::update(const std::filesystem::path& source_dll, std::string_view id,
    std::string version, std::string& error) {
    if (running()) { error = "Cannot update modules while running"; return false; }
    if (!valid_module_id(id) || version.empty() || !valid_package_version(version)) {
        error = "Invalid module ID or package version"; return false;
    }
    if (specs_.empty() && std::filesystem::exists(project_root_ / "Modules.lock") &&
        !load_lockfile(error)) return false;
    const auto it = std::find_if(specs_.begin(), specs_.end(),
        [id](const ModuleSpec& spec) { return spec.id == id; });
    if (it == specs_.end()) { error = "Unknown module ID"; return false; }
    if (it->version == version) { error = "Package version is already installed"; return false; }
    try {
        std::filesystem::path current;
        if (!module_path(*it, current, error)) return false;
        std::string current_digest;
        if (!file_sha256(current, current_digest)) {
            error = "Cannot hash current module"; return false;
        }
        const auto relative = std::filesystem::path(it->relative_path);
        const auto module_dir = project_root_ / "Modules" / std::string(id);
        if (relative.begin() == relative.end() || *relative.begin() != std::filesystem::path(id) ||
            std::filesystem::weakly_canonical(module_dir) != module_dir.lexically_normal()) {
            error = "Module is not installed under its project ID"; return false;
        }
        if (!std::filesystem::is_regular_file(source_dll) || source_dll.extension() != L".dll") {
            error = "Source must be a DLL file"; return false;
        }
        const auto destination = module_dir / version;
        const auto staging = module_dir / (version + ".partial");
        if (std::filesystem::exists(destination) || std::filesystem::exists(staging)) {
            error = "Package version directory already exists"; return false;
        }
        PluginLoader loader(logger_, services_, events_);
        if (!loader.open(source_dll, error)) return false;
        if (loader.module_id() != id) {
            error = "Module ID mismatch during update"; return false;
        }
        if (!std::filesystem::create_directory(staging)) {
            error = "Package staging directory already exists"; return false;
        }
        const auto staged_dll = staging / source_dll.filename();
        try {
            std::filesystem::copy_file(source_dll, staged_dll);
            std::filesystem::rename(staging, destination);
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove(staged_dll, ignored);
            std::filesystem::remove(staging, ignored);
            throw;
        }
        const ModuleSpec previous = *it;
        it->previous_relative_path = it->relative_path;
        it->previous_version = it->version;
        it->previous_sha256 = current_digest;
        it->relative_path = (std::filesystem::path(id) / version / source_dll.filename()).generic_string();
        it->version = std::move(version);
        if (!save_lockfile(error)) {
            *it = previous;
            std::error_code ignored;
            std::filesystem::remove(destination / source_dll.filename(), ignored);
            std::filesystem::remove(destination, ignored);
            return false;
        }
        return true;
    } catch (const std::filesystem::filesystem_error& ex) {
        error = ex.what(); return false;
    }
}

bool ModuleRegistry::rollback(std::string_view id, std::string& error) {
    if (running()) { error = "Cannot roll back modules while running"; return false; }
    if (specs_.empty() && std::filesystem::exists(project_root_ / "Modules.lock") &&
        !load_lockfile(error)) return false;
    const auto it = std::find_if(specs_.begin(), specs_.end(),
        [id](const ModuleSpec& spec) { return spec.id == id; });
    if (it == specs_.end() || it->previous_relative_path.empty()) {
        error = "No previous module version is recorded"; return false;
    }
    ModuleSpec previous = *it;
    previous.relative_path = it->previous_relative_path;
    previous.version = it->previous_version;
    std::filesystem::path old_file, current_file;
    if (!module_path(previous, old_file, error) || !module_path(*it, current_file, error)) return false;
    std::string old_digest, current_digest;
    if (!file_sha256(old_file, old_digest) || old_digest != it->previous_sha256 ||
        !file_sha256(current_file, current_digest)) {
        error = "Previous module hash mismatch"; return false;
    }
    const ModuleSpec before = *it;
    it->relative_path = before.previous_relative_path;
    it->version = before.previous_version;
    it->previous_relative_path = before.relative_path;
    it->previous_version = before.version;
    it->previous_sha256 = current_digest;
    if (!save_lockfile(error)) { *it = before; return false; }
    return true;
}

bool ModuleRegistry::save_lockfile(std::string& error) const {
    std::ostringstream content;
    content << "ATMERA_LOCK 3\n";
    for (const auto& spec : specs_) {
        std::filesystem::path file;
        if (!module_path(spec, file, error)) return false;
        std::string digest;
        if (!file_sha256(file, digest)) { error = "Cannot hash module: " + spec.id; return false; }
        if (!spec.previous_relative_path.empty()) {
            ModuleSpec previous = spec;
            previous.relative_path = spec.previous_relative_path;
            std::filesystem::path old_file;
            if (!module_path(previous, old_file, error)) return false;
            std::string old_digest;
            if (!file_sha256(old_file, old_digest) || old_digest != spec.previous_sha256) {
                error = "Previous module hash mismatch: " + spec.id; return false;
            }
        } else if (!spec.previous_version.empty() || !spec.previous_sha256.empty()) {
            error = "Incomplete previous module record: " + spec.id; return false;
        }
        content << std::quoted(spec.id) << ' ' << std::quoted(spec.relative_path) << ' '
            << static_cast<unsigned>(spec.enabled) << ' ' << std::quoted(spec.version) << ' '
            << std::quoted(spec.previous_relative_path) << ' '
            << std::quoted(spec.previous_version) << ' '
            << std::quoted(spec.previous_sha256) << ' '
            << std::quoted(digest) << ' '
            << spec.dependencies.size();
        for (const auto& dependency : spec.dependencies) content << ' ' << std::quoted(dependency);
        content << '\n';
    }
    const auto data = content.str();
    if (data.size() > MAXDWORD) { error = "Module lockfile is too large"; return false; }
    const auto lock = project_root_ / "Modules.lock";
    const auto staging = project_root_ / "Modules.lock.partial";
    HANDLE output = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) { error = "Cannot create module lockfile staging file"; return false; }
    DWORD written = 0;
    const bool saved = WriteFile(output, data.data(), static_cast<DWORD>(data.size()),
        &written, nullptr) && written == data.size() && FlushFileBuffers(output);
    CloseHandle(output);
    if (!saved || !MoveFileExW(staging.c_str(), lock.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(staging.c_str());
        error = "Cannot commit module lockfile";
        return false;
    }
    error.clear();
    return true;
}

bool ModuleRegistry::load_lockfile(std::string& error) {
    if (running() || !specs_.empty()) {
        error = "Load lockfile before registering or starting modules"; return false;
    }
    std::ifstream input(project_root_ / "Modules.lock", std::ios::binary);
    if (!input) { error = "Module lockfile is missing"; return false; }
    std::string line;
    if (!std::getline(input, line)) { error = "Unsupported module lockfile"; return false; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "ATMERA_LOCK 1" && line != "ATMERA_LOCK 2" && line != "ATMERA_LOCK 3") {
        error = "Unsupported module lockfile"; return false;
    }
    const bool versioned = line != "ATMERA_LOCK 1";
    const bool with_previous = line == "ATMERA_LOCK 3";
    std::vector<ModuleSpec> loaded;
    while (std::getline(input, line)) {
        ModuleSpec spec;
        std::string expected;
        unsigned enabled = 0;
        std::size_t dependency_count = 0;
        std::istringstream row(line);
        const bool fields_ok = static_cast<bool>(row >> std::quoted(spec.id) >>
            std::quoted(spec.relative_path) >> enabled) &&
            (!versioned || static_cast<bool>(row >> std::quoted(spec.version))) &&
            (!with_previous || static_cast<bool>(row >>
                std::quoted(spec.previous_relative_path) >>
                std::quoted(spec.previous_version) >>
                std::quoted(spec.previous_sha256))) &&
            static_cast<bool>(row >> std::quoted(expected) >> dependency_count);
        if (!fields_ok || enabled > 1 || dependency_count > 128 ||
            expected.size() != 64 || spec.id.empty() || !valid_package_version(spec.version) ||
            !valid_package_version(spec.previous_version)) {
            error = "Invalid module lockfile entry"; return false;
        }
        spec.enabled = enabled != 0;
        for (std::size_t i = 0; i < dependency_count; ++i) {
            std::string dependency;
            if (!(row >> std::quoted(dependency)) || dependency.empty()) {
                error = "Invalid module dependency entry"; return false;
            }
            spec.dependencies.push_back(std::move(dependency));
        }
        row >> std::ws;
        if (!row.eof() || std::any_of(loaded.begin(), loaded.end(),
                [&](const auto& item) { return item.id == spec.id; })) {
            error = "Duplicate or trailing module lockfile data"; return false;
        }
        std::filesystem::path file;
        if (!module_path(spec, file, error)) return false;
        std::string actual;
        if (!file_sha256(file, actual) || actual != expected) {
            error = "Module hash mismatch: " + spec.id; return false;
        }
        if (!spec.previous_relative_path.empty()) {
            const auto previous_path = std::filesystem::path(spec.previous_relative_path);
            if (previous_path.begin() == previous_path.end() ||
                *previous_path.begin() != std::filesystem::path(spec.id) ||
                spec.previous_sha256.size() != 64) {
                error = "Invalid previous module record: " + spec.id; return false;
            }
            ModuleSpec previous = spec;
            previous.relative_path = spec.previous_relative_path;
            std::filesystem::path old_file;
            if (!module_path(previous, old_file, error) ||
                !file_sha256(old_file, actual) || actual != spec.previous_sha256) {
                error = "Previous module hash mismatch: " + spec.id; return false;
            }
        } else if (!spec.previous_version.empty() || !spec.previous_sha256.empty()) {
            error = "Incomplete previous module record: " + spec.id; return false;
        }
        loaded.push_back(std::move(spec));
    }
    if (!input.eof()) { error = "Cannot read module lockfile"; return false; }
    specs_ = std::move(loaded);
    states_.assign(specs_.size(), ModuleState::Discovered);
    loaders_.resize(specs_.size());
    error.clear();
    return true;
}

bool ModuleRegistry::set_enabled(std::string_view id, bool enabled, std::string& error) {
    if (running()) { error = "Cannot change modules while running"; return false; }
    if (specs_.empty() && std::filesystem::exists(project_root_ / "Modules.lock") &&
        !load_lockfile(error)) return false;
    const auto it = std::find_if(specs_.begin(), specs_.end(),
        [id](const ModuleSpec& spec) { return spec.id == id; });
    if (it == specs_.end()) { error = "Unknown module ID"; return false; }
    if (it->enabled == enabled) { error.clear(); return true; }
    const bool previous = it->enabled;
    it->enabled = enabled;
    std::vector<std::size_t> order;
    if (!DependencyResolver::resolve(specs_, order, error) || !save_lockfile(error)) {
        it->enabled = previous;
        return false;
    }
    return true;
}

bool ModuleRegistry::module_path(const ModuleSpec& spec, std::filesystem::path& result, std::string& error) const {
    try {
    const std::filesystem::path relative(spec.relative_path);
    if (relative.is_absolute() || relative.empty()) {
        error = "Module path must be relative to project Modules/: " + spec.id; return false;
    }
    const auto modules_root = std::filesystem::weakly_canonical(project_root_ / "Modules");
    if (modules_root != (project_root_ / "Modules").lexically_normal()) {
        error = "Project Modules/ redirects outside the project"; return false;
    }
    result = std::filesystem::weakly_canonical(modules_root / relative);
    auto root_it = modules_root.begin();
    auto file_it = result.begin();
    for (; root_it != modules_root.end(); ++root_it, ++file_it) {
        if (file_it == result.end() || *root_it != *file_it) {
            error = "Module path escapes project Modules/: " + spec.id; return false;
        }
    }
    if (!std::filesystem::is_regular_file(result) || result.extension() != L".dll") {
        error = "Module DLL is missing: " + spec.id; return false;
    }
    return true;
    } catch (const std::filesystem::filesystem_error& ex) {
        error = "Invalid module path for " + spec.id + ": " + ex.what();
        return false;
    }
}

bool ModuleRegistry::start(std::string& error) {
    failed_initialization_index_.reset();
    if (running()) { error = "Modules are already active"; return false; }
    std::vector<size_t> order;
    if (!DependencyResolver::resolve(specs_, order, error)) return false;
    std::vector<std::filesystem::path> paths(specs_.size());
    for (const auto index : order) {
        if (!module_path(specs_[index], paths[index], error)) {
            states_[index] = ModuleState::Failed;
            return false;
        }
        states_[index] = ModuleState::Validated;
    }
    for (const auto index : order) states_[index] = ModuleState::DependenciesResolved;
    active_.reserve(order.size());

    for (const auto index : order) {
        auto loader = std::make_unique<PluginLoader>(logger_, services_, events_);
        if (!loader->open(paths[index], error)) {
            states_[index] = ModuleState::Failed;
            stop();
            return false;
        }
        states_[index] = ModuleState::Loaded;
        if (loader->module_id() != specs_[index].id) {
            error = "Module ID mismatch: expected " + specs_[index].id + ", got " + loader->module_id();
            states_[index] = ModuleState::Failed;
            stop();
            return false;
        }
        if (!loader->initialize(error)) {
            states_[index] = ModuleState::Failed;
            failed_initialization_index_ = index;
            stop();
            return false;
        }
        states_[index] = ModuleState::Initialized;
        loaders_[index] = std::move(loader);
        states_[index] = ModuleState::Active;
        active_.push_back(index);
        try { events_.publish("module.active", specs_[index].id); }
        catch (...) {
            error = "Module activation event failed: " + specs_[index].id;
            states_[index] = ModuleState::Failed;
            stop();
            return false;
        }
    }
    error.clear();
    return true;
}

bool ModuleRegistry::start_with_rollback(std::string& error, std::string& rolled_back_id) {
    rolled_back_id.clear();
    if (start(error)) return true;
    if (!failed_initialization_index_) return false;
    const auto& spec = specs_[*failed_initialization_index_];
    if (spec.previous_relative_path.empty()) return false;
    const std::string first_error = error;
    const std::string id = spec.id;
    std::string rollback_error;
    if (!rollback(id, rollback_error)) {
        error = first_error + "; rollback failed: " + rollback_error;
        return false;
    }
    rolled_back_id = id;
    return start(error);
}

void ModuleRegistry::stop() noexcept {
    for (auto it = active_.rbegin(); it != active_.rend(); ++it) {
        const auto index = *it;
        if (states_[index] != ModuleState::Failed) states_[index] = ModuleState::Deactivated;
        try { events_.publish("module.deactivated", specs_[index].id); } catch (...) {}
        if (states_[index] != ModuleState::Failed) states_[index] = ModuleState::Shutdown;
        loaders_[index].reset();
        if (states_[index] != ModuleState::Failed) states_[index] = ModuleState::Unloaded;
    }
    active_.clear();
}

ModuleState ModuleRegistry::state(std::string_view id) const {
    for (size_t i = 0; i < specs_.size(); ++i)
        if (specs_[i].id == id) return states_[i];
    throw std::out_of_range("Unknown module ID");
}

std::string ModuleRegistry::version(std::string_view id) const {
    for (const auto& spec : specs_)
        if (spec.id == id) return spec.version;
    throw std::out_of_range("Unknown module ID");
}

std::vector<std::string> ModuleRegistry::active_modules() const {
    std::vector<std::string> result;
    result.reserve(active_.size());
    for (const auto index : active_) result.push_back(specs_[index].id);
    return result;
}
}
