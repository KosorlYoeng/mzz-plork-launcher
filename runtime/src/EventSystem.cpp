#include "EventSystem.h"

namespace mzzplork {

void EventSystem::Subscribe(const std::string& eventType, Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.emplace_back(eventType, std::move(handler));
}

void EventSystem::Publish(const std::string& eventType, const json::Object& data) {
    std::vector<Handler> toInvoke;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [type, handler] : handlers_) {
            if (type == eventType) toInvoke.push_back(handler);
        }
    }
    for (auto& handler : toInvoke) handler(data);
}

}
