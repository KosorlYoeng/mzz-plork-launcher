#pragma once
#include <string>

#include "Entity.h"

namespace mzzplork {

// A player entity: identity (playerId + display name) plus the Entity
// base's replicated state (position/rotation/basic state). Local player
// identity comes from authentication (Runtime.cpp's playerId); remote
// players are created from the server's "player_joined" event.
class Player : public Entity {
public:
    Player(std::string playerId, std::string displayName, bool isLocal);

    const std::string& DisplayName() const { return displayName_; }

private:
    std::string displayName_;
};

}
