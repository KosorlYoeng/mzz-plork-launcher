#pragma once
#include <memory>
#include <string>

#include "ErrorHandling.h"

namespace mzzplork {

// What the runtime actually knows about the game process. installDetected
// and processRunning are real, independently-observable facts (a file
// exists; a process by that name is running) -- integrated is always
// false today, by construction: nothing in this codebase has ever
// established an in-process connection to a running game, since that
// mechanism is the blocker documented in docs/game-integration.md.
struct GameState {
    bool installDetected = false;
    std::string installPath;
    bool processRunning = false;
    unsigned long processId = 0;
    bool integrated = false;
};

// The GTA V integration boundary, per docs/game-integration.md: the rest
// of the runtime depends only on this interface (dependency inversion),
// never on a specific integration mechanism. detectGame()/launchGame()
// are legitimate, real, standard OS operations (locating an installed
// game, starting a process) and are implemented for real below. connect()
// -- establishing an in-process integration with a running game -- has no
// legitimate mechanism identified as of this phase (see
// docs/game-integration.md) and stays honestly NotImplemented. Do not
// give connect() a body that pretends to succeed.
class IGameIntegration {
public:
    virtual ~IGameIntegration() = default;

    virtual ServiceResult Initialize() = 0;
    virtual ServiceResult DetectGame() = 0;
    virtual ServiceResult LaunchGame() = 0;
    virtual ServiceResult Connect() = 0;
    virtual void Disconnect() = 0;
    virtual GameState GetGameState() const = 0;
    virtual void Shutdown() = 0;
};

// pathOverride: if non-empty, DetectGame() checks exactly this path and
// nothing else -- the only mechanism actually verified in this
// environment (there is no real GTA V/Rockstar Games Launcher install
// available to test registry-based detection against). If empty,
// DetectGame() falls back to a Rockstar Games Launcher registry lookup
// based on community-documented key names -- UNVERIFIED, since it has
// never been run against a real installation. See
// docs/game-integration.md for the full caveat.
std::unique_ptr<IGameIntegration> MakeGameIntegration(std::string pathOverride);

}
