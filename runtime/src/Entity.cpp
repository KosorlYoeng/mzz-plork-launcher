#include "Entity.h"

namespace mzzplork {

const char* ToString(EntityType type) {
    switch (type) {
        case EntityType::Player: return "player";
    }
    return "unknown";
}

Entity::Entity(std::string id, EntityType type, bool isLocal)
    : id_(std::move(id)), type_(type), isLocal_(isLocal) {}

}
