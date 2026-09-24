#pragma once
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Entity.h"
#include "Player.h"

namespace mzzplork {

// Owns every synchronized entity, keyed by id. Generic over EntityType --
// Vehicle/Ped/Object (docs/synchronization.md's "expand to" phase) will
// use the same CreateEntity/DestroyEntity/Get machinery Player already
// does, not a parallel system.
//
// Thread-safe: in the wired runtime, the network reader thread creates/
// destroys remote entities (via Replication's player_joined/player_left
// handlers) while the replication tick thread concurrently reads the
// local player -- both touch the same underlying map, so every method
// here takes a lock around the map access itself.
class EntityManager {
public:
    Player& CreatePlayer(const std::string& playerId, const std::string& displayName, bool isLocal);
    bool DestroyEntity(const std::string& id);

    Entity* Get(const std::string& id);
    const Entity* Get(const std::string& id) const;
    Player* GetPlayer(const std::string& id);

    std::vector<Entity*> GetAll();
    size_t Count() const;
    bool Contains(const std::string& id) const;

    // Observability for Replication and tests -- fired synchronously from
    // CreatePlayer()/DestroyEntity().
    void SetOnEntityCreated(std::function<void(Entity&)> callback) { onCreated_ = std::move(callback); }
    void SetOnEntityDestroyed(std::function<void(const std::string& id, EntityType type)> callback) { onDestroyed_ = std::move(callback); }

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Entity>> entities_;
    std::function<void(Entity&)> onCreated_;
    std::function<void(const std::string&, EntityType)> onDestroyed_;
};

}
