#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "Json.h"

namespace mzzplork {

// Simple typed pub/sub for client/server events. ProtocolClient feeds
// server-pushed "event" messages in here via Publish(); anything in the
// runtime that cares about a given event type calls Subscribe().
// Thread-safe, since events currently arrive on the connection's
// background reader thread.
class EventSystem {
public:
    using Handler = std::function<void(const json::Object& data)>;

    void Subscribe(const std::string& eventType, Handler handler);
    void Publish(const std::string& eventType, const json::Object& data);

private:
    std::mutex mutex_;
    std::vector<std::pair<std::string, Handler>> handlers_;
};

}
