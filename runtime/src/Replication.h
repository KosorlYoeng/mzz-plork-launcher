#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "EntityManager.h"
#include "EventSystem.h"
#include "Interpolation.h"
#include "NetworkState.h"
#include "ProtocolClient.h"

namespace mzzplork {

// Ties EntityManager + ProtocolClient + EventSystem + Interpolation
// together: subscribes to the server's player_joined/player_left/
// player_state events (already routed through EventSystem by
// Runtime.cpp -- see ProtocolClient's event sink), applies them to the
// EntityManager, and periodically sends the local player's own state.
// See docs/synchronization.md.
//
// Clock note: NowMs() is this class's own local clock (steady_clock-based,
// relative -- not wall-clock, not synchronized with the server or any
// peer). A remote player_state's embedded `timestamp` is the *sender's*
// clock and is never used as the local interpolation anchor -- doing so
// would silently assume synchronized clocks, which nothing here
// establishes. Every interpolation transition is anchored to when *this*
// process received the update, via NowMs().
class Replication {
public:
    Replication(EntityManager& entityManager, ProtocolClient& protocol, EventSystem& events, std::string localPlayerId, std::string localDisplayName);

    // Subscribes to server events and creates the local player entity.
    // Call once, after authentication succeeds.
    void Start();

    // Sets the local player's current state -- called by whatever drives
    // it (a local dev test tool today; GTA V integration once that
    // exists, see docs/game-integration.md). Marks it dirty so the next
    // Tick() sends it.
    void SetLocalPosition(const Vector3& position);
    void SetLocalRotation(const Rotation3& rotation);

    // Call periodically (Runtime.cpp's replication thread): sends the
    // local player's state if it changed since the last send.
    void Tick(int64_t nowMs);

    void SetSyncIntervalMs(int64_t ms) { syncIntervalMs_ = ms; }
    void SetInterpolationDurationMs(int64_t ms) { interpolationDurationMs_ = ms; }

    // The smoothed (interpolated) state of a remote entity right now --
    // what an eventual consumer (GTA V integration) would apply to the
    // game's representation of that player. Pass a NowMs()-domain
    // timestamp.
    bool GetInterpolatedState(const std::string& entityId, NetworkState& out, int64_t nowMs) const;

    static int64_t NowMs();

private:
    void OnPlayerJoined(const json::Object& data);
    void OnPlayerLeft(const json::Object& data);
    void OnPlayerState(const json::Object& data);

    EntityManager& entityManager_;
    ProtocolClient& protocol_;
    EventSystem& events_;
    std::string localPlayerId_;
    std::string localDisplayName_;

    mutable std::mutex mutex_;
    std::map<std::string, InterpolatedEntity> interpolators_;

    // Written by the control-command thread (SetLocalPosition/Rotation)
    // and read/cleared by the replication tick thread (Tick()) --
    // plain bool/int64_t would be a data race across those threads.
    std::atomic<bool> localStateDirty_{true};
    std::atomic<int64_t> lastSentMs_{0};
    int64_t syncIntervalMs_ = 100;   // 10Hz default -- see docs/synchronization.md
    int64_t interpolationDurationMs_ = 100;
};

}
