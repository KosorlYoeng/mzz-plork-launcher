#pragma once
#include <mutex>
#include <string>

#include "Json.h"

namespace mzzplork {

struct Vector3 {
    double x = 0;
    double y = 0;
    double z = 0;
};

struct Rotation3 {
    double pitch = 0;
    double yaw = 0;
    double roll = 0;
};

enum class EntityType {
    Player
    // Vehicle, Ped, Object -- added when synchronization expands to them
    // (see docs/synchronization.md); EntityManager and Entity are already
    // generic enough not to need changes when that happens.
};

const char* ToString(EntityType type);

// The base of every synchronized thing. Deliberately minimal: id,
// ownership (is this the local client's own entity, or a remote one
// replicated from the server), and the state that's actually
// synchronized -- position, rotation, and a small generic "basic state"
// bag (docs/synchronization.md's "5. Basic state"). Nothing here reads
// from or writes to GTA V -- there is no consumer for that yet (see
// docs/game-integration.md); this is the network replication model only.
//
// Thread-safe for position/rotation/basicState: the local player's
// fields are written from the control-command thread (Runtime.cpp's
// HandleControlCommand -> Replication::SetLocalPosition/Rotation) and
// read from the replication tick thread concurrently; a remote player's
// fields are written from the network reader thread. Id/Type/IsLocal are
// set once at construction and never mutated, so they need no locking.
class Entity {
public:
    Entity(std::string id, EntityType type, bool isLocal);
    virtual ~Entity() = default;

    const std::string& Id() const { return id_; }
    EntityType Type() const { return type_; }
    bool IsLocal() const { return isLocal_; }

    Vector3 Position() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return position_;
    }
    void SetPosition(const Vector3& position) {
        std::lock_guard<std::mutex> lock(mutex_);
        position_ = position;
    }

    Rotation3 Rotation() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return rotation_;
    }
    void SetRotation(const Rotation3& rotation) {
        std::lock_guard<std::mutex> lock(mutex_);
        rotation_ = rotation;
    }

    // Returned by value (rather than by reference, as most getters in
    // this codebase prefer) specifically because it's locked -- a
    // reference into basicState_ could be read after another thread
    // mutates it post-unlock.
    json::Object BasicState() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return basicState_;
    }
    void SetBasicState(json::Object state) {
        std::lock_guard<std::mutex> lock(mutex_);
        basicState_ = std::move(state);
    }

private:
    std::string id_;
    EntityType type_;
    bool isLocal_;
    mutable std::mutex mutex_;
    Vector3 position_;
    Rotation3 rotation_;
    json::Object basicState_;
};

}
