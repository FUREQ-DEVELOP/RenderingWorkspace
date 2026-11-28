#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace atmera {
// Borrowed pointers: the owner must unregister before destroying the service.
class ServiceRegistry {
public:
    bool add(std::string id, std::string owner, void* instance);
    void* find(std::string_view id) const;
    void remove_owner(std::string_view owner) noexcept;
    size_t size() const noexcept { return services_.size(); }
private:
    struct Entry { std::string owner; void* instance; };
    std::unordered_map<std::string, Entry> services_;
};
}
