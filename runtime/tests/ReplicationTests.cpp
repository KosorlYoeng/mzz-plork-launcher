#include <cmath>
#include <vector>

#include "TestFramework.h"
#include "../src/EntityManager.h"
#include "../src/EventSystem.h"
#include "../src/ProtocolClient.h"
#include "../src/Replication.h"
#include "../src/ServerConnection.h"

using namespace mzzplork;

namespace {

// Records every message this Replication instance tries to send, without
// a real socket -- mirrors RuntimeTests.cpp's FakeServerConnection, scoped
// down to what Replication actually needs (Send(), never Connect() or the
// request/response handshake types).
class RecordingServerConnection : public IServerConnection {
public:
    struct SentMessage {
        std::string type;
        std::string payloadJson;
    };
    std::vector<SentMessage> sent;

    bool Connect(const std::string&, unsigned short) override { return true; }
    void Disconnect() override {}
    bool IsConnected() const override { return true; }
    bool SendHandshake() override { return true; }
    bool Send(const std::string& type, const std::string& payloadJson) override {
        sent.push_back({type, payloadJson});
        return true;
    }
    void SetMessageHandler(std::function<void(const ServerMessage&)>) override {}
};

json::Object PlayerJoinedData(const std::string& playerId, const std::string& displayName) {
    json::Object data;
    data["playerId"] = json::Value::MakeString(playerId);
    data["displayName"] = json::Value::MakeString(displayName);
    return data;
}

json::Object PlayerStateData(const std::string& playerId, Vector3 pos, int64_t timestampMs) {
    json::Object data;
    data["playerId"] = json::Value::MakeString(playerId);
    json::Object position;
    position["x"] = json::Value::MakeNumber(pos.x);
    position["y"] = json::Value::MakeNumber(pos.y);
    position["z"] = json::Value::MakeNumber(pos.z);
    data["position"] = json::Value::MakeObject(position);
    data["rotation"] = json::Value::MakeObject(json::Object{});
    data["basicState"] = json::Value::MakeObject(json::Object{});
    data["timestamp"] = json::Value::MakeNumber(static_cast<double>(timestampMs));
    return data;
}

}  // namespace

TEST_CASE(Replication_StartCreatesTheLocalPlayerEntity) {
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");

    replication.Start();

    Player* local = entities.GetPlayer("local-1");
    ASSERT_TRUE(local != nullptr);
    ASSERT_TRUE(local->IsLocal());
    ASSERT_EQ(local->DisplayName(), std::string("Me"));
}

TEST_CASE(Replication_TickWaitsForTheSyncIntervalBeforeSendingAnything) {
    // The local player's state starts dirty (an initial snapshot is owed
    // to the server), but Tick() still won't send before syncIntervalMs_
    // has elapsed since construction.
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.SetSyncIntervalMs(100);
    replication.Start();

    replication.Tick(0);
    replication.Tick(99);

    ASSERT_EQ(connection.sent.size(), static_cast<size_t>(0));
}

TEST_CASE(Replication_TickSendsTheInitialLocalStateOnceTheSyncIntervalElapses) {
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.SetSyncIntervalMs(100);
    replication.Start();

    replication.Tick(100);
    ASSERT_EQ(connection.sent.size(), static_cast<size_t>(1));
    ASSERT_EQ(connection.sent[0].type, std::string("player_state"));

    // Ticking again with nothing newly dirty sends nothing more.
    replication.Tick(110);
    ASSERT_EQ(connection.sent.size(), static_cast<size_t>(1));
}

TEST_CASE(Replication_TickThrottlesRepeatedDirtyUpdatesToTheSyncInterval) {
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.SetSyncIntervalMs(100);
    replication.Start();

    // Consume the initial dirty-on-construction send so the rest of this
    // test is only exercising SetLocalPosition()'s effect.
    replication.Tick(100);
    ASSERT_EQ(connection.sent.size(), static_cast<size_t>(1));

    replication.SetLocalPosition(Vector3{1.0, 0.0, 0.0});
    // Too soon since the last actual send (100) -- throttled, not sent.
    replication.Tick(150);
    ASSERT_EQ(connection.sent.size(), static_cast<size_t>(1));

    // Now a full interval past the last send -- the still-pending dirty
    // state goes out.
    replication.Tick(200);
    ASSERT_EQ(connection.sent.size(), static_cast<size_t>(2));
}

TEST_CASE(Replication_PlayerJoinedEventCreatesARemotePlayerEntity) {
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.Start();

    events.Publish("player_joined", PlayerJoinedData("remote-1", "Alice"));

    Player* remote = entities.GetPlayer("remote-1");
    ASSERT_TRUE(remote != nullptr);
    ASSERT_FALSE(remote->IsLocal());
    ASSERT_EQ(remote->DisplayName(), std::string("Alice"));
}

TEST_CASE(Replication_PlayerJoinedEventForTheLocalPlayerIdIsIgnored) {
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.Start();

    events.Publish("player_joined", PlayerJoinedData("local-1", "Me"));

    // Still exactly one entity (the local player Start() created) -- a
    // stray echo of our own join must not create a second, remote copy.
    ASSERT_EQ(entities.Count(), static_cast<size_t>(1));
}

TEST_CASE(Replication_PlayerLeftEventDestroysTheEntity) {
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.Start();
    events.Publish("player_joined", PlayerJoinedData("remote-1", "Alice"));
    ASSERT_TRUE(entities.Contains("remote-1"));

    json::Object leftData;
    leftData["playerId"] = json::Value::MakeString("remote-1");
    events.Publish("player_left", leftData);

    ASSERT_FALSE(entities.Contains("remote-1"));
}

TEST_CASE(Replication_PlayerStateEventUpdatesAnExistingRemoteEntity) {
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.Start();
    events.Publish("player_joined", PlayerJoinedData("remote-1", "Alice"));

    events.Publish("player_state", PlayerStateData("remote-1", Vector3{5.0, 6.0, 7.0}, 12345));

    Player* remote = entities.GetPlayer("remote-1");
    ASSERT_TRUE(remote != nullptr);
    ASSERT_EQ(remote->Position().x, 5.0);
    ASSERT_EQ(remote->Position().y, 6.0);
    ASSERT_EQ(remote->Position().z, 7.0);
}

TEST_CASE(Replication_PlayerStateEventForAnUnknownPlayerDefensivelyCreatesTheEntity) {
    // A state update can race ahead of the player_joined event for the
    // same player -- real network delivery gives no ordering guarantee
    // between them. Discarding the update would lose real data.
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.Start();

    events.Publish("player_state", PlayerStateData("remote-never-joined", Vector3{1.0, 1.0, 1.0}, 1));

    Player* remote = entities.GetPlayer("remote-never-joined");
    ASSERT_TRUE(remote != nullptr);
    ASSERT_EQ(remote->Position().x, 1.0);
}

TEST_CASE(Replication_PlayerStateEventForTheLocalPlayerIdIsIgnored) {
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.Start();
    replication.SetLocalPosition(Vector3{9.0, 9.0, 9.0});

    // An echo of our own state (should never happen server-side, since
    // the server excludes the sender -- but Replication must not trust
    // that blindly) must not overwrite the locally authoritative position.
    events.Publish("player_state", PlayerStateData("local-1", Vector3{0.0, 0.0, 0.0}, 1));

    Player* local = entities.GetPlayer("local-1");
    ASSERT_EQ(local->Position().x, 9.0);
}

TEST_CASE(Replication_GetInterpolatedStateReturnsFalseForAnUnknownEntity) {
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.Start();

    NetworkState out;
    bool found = replication.GetInterpolatedState("no-such-entity", out, 0);
    ASSERT_FALSE(found);
}

TEST_CASE(Replication_GetInterpolatedStateReachesTheReceivedSnapshotAfterTheInterpolationDuration) {
    EntityManager entities;
    RecordingServerConnection connection;
    ProtocolClient protocol(connection);
    EventSystem events;
    Replication replication(entities, protocol, events, "local-1", "Me");
    replication.SetInterpolationDurationMs(100);
    replication.Start();

    events.Publish("player_state", PlayerStateData("remote-1", Vector3{40.0, 0.0, 0.0}, 0));

    NetworkState out;
    bool found = replication.GetInterpolatedState("remote-1", out, Replication::NowMs() + 1000);
    ASSERT_TRUE(found);
    ASSERT_TRUE(std::abs(out.position.x - 40.0) < 0.001);
}
