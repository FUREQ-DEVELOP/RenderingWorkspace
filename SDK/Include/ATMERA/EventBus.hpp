#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace atmera {
class EventBus {
public:
    using Callback = std::function<void(std::string_view)>;
    using Token = uint64_t;
    Token subscribe(std::string name, Callback callback);
    void unsubscribe(Token token);
    void publish(std::string_view name, std::string_view payload) const;
private:
    struct Subscription { Token token; std::string name; Callback callback; };
    std::vector<Subscription> subscriptions_;
    Token next_token_ = 1;
};
}
