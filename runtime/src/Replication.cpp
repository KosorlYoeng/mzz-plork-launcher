#include "Replication.h"

#include <chrono>

namespace mzzplork {

Replication::Replication(EntityManager& entityManager, ProtocolClient& protocol, EventSystem& events, std::string localPlayerId, std::string localDisplayName)
    : entityManager_(entityManager), protocol_(protocol), events_(events),
      localPlayerId_(std::move(localPlayerId)), localDisplayName_(std::move(localDisplayName)) {}

int64_t Replication::NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Replication::Start() {
    entityManager_.CreatePlayer(localPlayerId_, localDisplayName_, /*isLocal=*/true);

    events_.Subscribe("player_joined", [this](const json::Object& data) { OnPlayerJoined(data); });
    events_.Subscribe("player_left", [this](const json::Object& data) { OnPlayerLeft(data); });
    events_.Subscribe("player_state", [this](const json::Object& data) { OnPlayerState(data); });
}

void Replication::SetLocalPosition(const Vector3& position) {
    if (auto* player = entityManager_.GetPlayer(localPlayerId_)) {
        player->SetPosition(position);
        localStateDirty_ = true;
    }
}

void Replication::SetLocalRotation(const Rotation3& rotation) {
    if (auto* player = entityManager_.GetPlayer(localPlayerId_)) {
        player->SetRotation(rotation);
        localStateDirty_ = true;
    }
}

void Replication::Tick(int64_t nowMs) {
    if (!localStateDirty_) return;
    if (nowMs - lastSentMs_ < syncIntervalMs_) return;

    Player* player = entityManager_.GetPlayer(localPlayerId_);
    if (!player) return;

    NetworkState state;
    state.position = player->Position();
    state.rotation = player->Rotation();
    state.basicState = player->BasicState();
    state.timestampMs = nowMs;

    protocol_.SendPlayerState(json::StringifyObject(ToJson(state)));
    lastSentMs_ = nowMs;
    localStateDirty_ = false;
}

void Replication::OnPlayerJoined(const json::Object& data) {
    auto it = data.find("playerId");
    if (it == data.end()) return;
    std::string playerId = it->second.AsString();
    if (playerId.empty() || playerId == localPlayerId_ || entityManager_.Contains(playerId)) return;

    auto nameIt = data.find("displayName");
    std::string displayName = nameIt != data.end() ? nameIt->second.AsString() : "";
    entityManager_.CreatePlayer(playerId, displayName, /*isLocal=*/false);
}

void Replication::OnPlayerLeft(const json::Object& data) {
    auto it = data.find("playerId");
    if (it == data.end()) return;
    std::string playerId = it->second.AsString();
    if (playerId.empty()) return;

    entityManager_.DestroyEntity(playerId);
    std::lock_guard<std::mutex> lock(mutex_);
    interpolators_.erase(playerId);
}

void Replication::OnPlayerState(const json::Object& data) {
    auto it = data.find("playerId");
    if (it == data.end()) return;
    std::string playerId = it->second.AsString();
    if (playerId.empty() || playerId == localPlayerId_) return;

    NetworkState state = FromJson(data);

    Player* player = entityManager_.GetPlayer(playerId);
    if (!player) {
        // Defensive: a state update for a player we don't have an entity
        // for yet (e.g. this update raced ahead of player_joined) --
        // create one rather than silently discarding real data.
        player = &entityManager_.CreatePlayer(playerId, "", /*isLocal=*/false);
    }
    player->SetPosition(state.position);
    player->SetRotation(state.rotation);
    player->SetBasicState(state.basicState);

    int64_t receivedAtMs = NowMs();
    std::lock_guard<std::mutex> lock(mutex_);
    auto& interpolator = interpolators_[playerId];
    interpolator.SetInterpolationDurationMs(interpolationDurationMs_);
    interpolator.SetTarget(state, receivedAtMs);
}

bool Replication::GetInterpolatedState(const std::string& entityId, NetworkState& out, int64_t nowMs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = interpolators_.find(entityId);
    if (it == interpolators_.end()) return false;
    out = it->second.Current(nowMs);
    return true;
}

}
