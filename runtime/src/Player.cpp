#include "Player.h"

namespace mzzplork {

Player::Player(std::string playerId, std::string displayName, bool isLocal)
    : Entity(std::move(playerId), EntityType::Player, isLocal), displayName_(std::move(displayName)) {}

}
