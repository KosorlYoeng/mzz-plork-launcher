#pragma once
#include <cstdint>

#include "Entity.h"
#include "Json.h"

namespace mzzplork {

// The wire-format snapshot of an entity's synchronized state -- what
// actually goes over the network, as distinct from Entity (the client's
// in-memory model of it). Kept separate so serialization concerns don't
// leak into Entity, and so Interpolation can work with plain snapshots
// without needing a whole Entity object.
struct NetworkState {
    Vector3 position;
    Rotation3 rotation;
    json::Object basicState;
    int64_t timestampMs = 0;
};

json::Object ToJson(const NetworkState& state);
NetworkState FromJson(const json::Object& obj);

}
