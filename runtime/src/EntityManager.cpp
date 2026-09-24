#include "EntityManager.h"

namespace mzzplork {

Player& EntityManager::CreatePlayer(const std::string& playerId, const std::string& displayName, bool isLocal) {
    Player* ref = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto player = std::make_unique<Player>(playerId, displayName, isLocal);
        ref = player.get();
        entities_[playerId] = std::move(player);
    }
    // Invoked outside the lock -- a callback that calls back into this
    // EntityManager (Get/Contains/etc.) must not deadlock on its own lock.
    if (onCreated_) onCreated_(*ref);
    return *ref;
}

bool EntityManager::DestroyEntity(const std::string& id) {
    EntityType type;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entities_.find(id);
        if (it == entities_.end()) return false;
        type = it->second->Type();
        entities_.erase(it);
    }
    if (onDestroyed_) onDestroyed_(id, type);
    return true;
}

Entity* EntityManager::Get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(id);
    return it != entities_.end() ? it->second.get() : nullptr;
}

const Entity* EntityManager::Get(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(id);
    return it != entities_.end() ? it->second.get() : nullptr;
}

Player* EntityManager::GetPlayer(const std::string& id) {
    Entity* e = Get(id);
    return (e && e->Type() == EntityType::Player) ? static_cast<Player*>(e) : nullptr;
}

std::vector<Entity*> EntityManager::GetAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Entity*> all;
    all.reserve(entities_.size());
    for (auto& [id, entity] : entities_) all.push_back(entity.get());
    return all;
}

size_t EntityManager::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entities_.size();
}

bool EntityManager::Contains(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entities_.count(id) > 0;
}

}
