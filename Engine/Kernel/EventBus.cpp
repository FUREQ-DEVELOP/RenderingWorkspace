#include <ATMERA/EventBus.hpp>
#include <algorithm>

namespace atmera {
EventBus::Token EventBus::subscribe(std::string name, Callback callback) {
    if (name.empty() || !callback) return 0;
    const auto token = next_token_++;
    subscriptions_.push_back({token, std::move(name), std::move(callback)});
    return token;
}
void EventBus::unsubscribe(Token token) {
    std::erase_if(subscriptions_, [=](const Subscription& item) { return item.token == token; });
}
void EventBus::publish(std::string_view name, std::string_view payload) const {
    // Copy callbacks so a subscriber may safely change the subscription list.
    std::vector<Callback> callbacks;
    for (const auto& item : subscriptions_)
        if (item.name == name) callbacks.push_back(item.callback);
    for (const auto& callback : callbacks) callback(payload);
}
}
