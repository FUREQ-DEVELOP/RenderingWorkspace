#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace atmera {
struct ModuleSpec {
    std::string id;
    std::string relative_path;
    std::vector<std::string> dependencies;
    bool enabled = true;
    std::string version; // Empty for an unversioned module.
    std::string previous_relative_path;
    std::string previous_version;
    std::string previous_sha256;
};

class DependencyResolver {
public:
    // Produces dependency-first indexes into specs. Disabled modules are excluded.
    static bool resolve(const std::vector<ModuleSpec>& specs,
        std::vector<size_t>& order, std::string& error);
};
}
