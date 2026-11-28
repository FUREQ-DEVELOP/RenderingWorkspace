#include <ATMERA/DependencyResolver.hpp>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace atmera {
bool DependencyResolver::resolve(const std::vector<ModuleSpec>& specs,
    std::vector<size_t>& order, std::string& error) {
    order.clear();
    error.clear();
    std::unordered_map<std::string, size_t> ids;
    for (size_t i = 0; i < specs.size(); ++i) {
        if (specs[i].id.empty()) { error = "Module ID is empty"; return false; }
        if (!ids.emplace(specs[i].id, i).second) {
            error = "Duplicate module ID: " + specs[i].id;
            return false;
        }
    }
    std::vector<uint8_t> visit(specs.size(), 0);
    std::function<bool(size_t)> dfs = [&](size_t index) {
        if (visit[index] == 2) return true;
        if (visit[index] == 1) { error = "Dependency cycle at " + specs[index].id; return false; }
        visit[index] = 1;
        for (const auto& dependency : specs[index].dependencies) {
            const auto it = ids.find(dependency);
            if (it == ids.end() || !specs[it->second].enabled) {
                error = "Missing or disabled dependency " + dependency + " for " + specs[index].id;
                return false;
            }
            if (!dfs(it->second)) return false;
        }
        visit[index] = 2;
        order.push_back(index);
        return true;
    };
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].enabled && !dfs(i)) { order.clear(); return false; }
    return true;
}
}
