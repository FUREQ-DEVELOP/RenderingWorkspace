#include <ATMERA/ServiceRegistry.hpp>

namespace atmera {
bool ServiceRegistry::add(std::string id, std::string owner, void* instance) {
    if (id.empty() || owner.empty() || !instance) return false;
    return services_.emplace(std::move(id), Entry{std::move(owner), instance}).second;
}
void* ServiceRegistry::find(std::string_view id) const {
    const auto it = services_.find(std::string(id));
    return it == services_.end() ? nullptr : it->second.instance;
}
void ServiceRegistry::remove_owner(std::string_view owner) noexcept {
    for (auto it = services_.begin(); it != services_.end();) {
        if (it->second.owner == owner) it = services_.erase(it);
        else ++it;
    }
}
}
