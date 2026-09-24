#pragma once
#include <atomic>
#include <istream>
#include <memory>
#include <string>
#include <thread>

#include "CacheService.h"
#include "ClientState.h"
#include "Config.h"
#include "EntityManager.h"
#include "EventSystem.h"
#include "GameIntegration.h"
#include "Lifecycle.h"
#include "Log.h"
#include "NetworkService.h"
#include "ProtocolClient.h"
#include "Replication.h"
#include "ResourceManager.h"
#include "ServerConnection.h"

namespace mzzplork {

// The concrete services a ClientRuntime drives through the lifecycle.
// Defaults to real implementations for everything except GameIntegration,
// which is real for detectGame()/launchGame() but honestly
// NotImplemented for connect() -- see docs/game-integration.md. Tests
// inject alternatives to exercise Runtime::Run() without a real network,
// server, or game process.
struct RuntimeDependencies {
    std::unique_ptr<ICacheService> cache;
    std::unique_ptr<INetworkService> network;
    std::unique_ptr<IServerConnection> server;
    std::unique_ptr<IResourceManager> resources;
    std::unique_ptr<IGameIntegration> game;
};

RuntimeDependencies MakeDefaultDependencies(const RuntimeConfig& config);

// Implements the client state machine:
//   STARTING -> INITIALIZING -> CHECKING_UPDATE -> CONNECTING ->
//   AUTHENTICATING -> DOWNLOADING_RESOURCES -> LOADING_RESOURCES ->
//   READY -> (idle, never PLAYING -- see GameIntegration.h) ->
//   DISCONNECTING -> STOPPED
// See docs/runtime.md for the detailed contract.
class ClientRuntime {
public:
    ClientRuntime(std::string appDataRoot, RuntimeDependencies deps);
    explicit ClientRuntime(std::string appDataRoot);

    // Runs the full lifecycle to completion. `controlInput` is read until
    // EOF as the client loop's control channel -- production passes
    // std::cin (blocks until the bootstrapper closes the pipe); tests pass
    // an already-exhausted stream so Run() completes immediately while
    // still exercising every step for real. Returns a process exit code.
    int Run(std::istream& controlInput);

    RuntimeState LifecycleState() const;
    ClientState SessionState() const;
    EventSystem& Events();

private:
    void SetState(ClientState state);
    int Shutdown();
    std::string LoadOrCreateClientId();
    // Parses local development test commands from the control channel --
    // "setpos x y z" / "setrot pitch yaw roll" -- since there is no real
    // GTA V binding to source a local player's position from (see
    // docs/synchronization.md). Unrecognized lines are ignored.
    void HandleControlCommand(const std::string& line);

    std::string appDataRoot_;
    RuntimeDependencies deps_;
    RuntimeConfig config_;
    std::unique_ptr<Logger> logger_;
    Lifecycle lifecycle_;
    ClientStateStore clientState_;
    EventSystem events_;
    std::unique_ptr<ProtocolClient> protocol_;
    bool connectedToServer_ = false;
    std::atomic<bool> disconnectRequested_{false};
    std::atomic<bool> kicked_{false};
    std::string kickReason_;
    std::atomic<bool> heartbeatShouldStop_{false};
    std::thread heartbeatThread_;

    // Multiplayer synchronization -- see docs/synchronization.md. Only
    // constructed once authentication succeeds, since both need the
    // authoritative playerId the server assigned.
    EntityManager entityManager_;
    std::unique_ptr<Replication> replication_;
    std::atomic<bool> replicationShouldStop_{false};
    std::thread replicationThread_;
};

}

