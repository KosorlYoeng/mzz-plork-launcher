#include "TestFramework.h"
#include "../src/NetworkState.h"

using namespace mzzplork;

TEST_CASE(NetworkState_ToJsonThenFromJsonRoundTripsPositionAndRotation) {
    NetworkState state;
    state.position = Vector3{1.25, -2.5, 3.75};
    state.rotation = Rotation3{10.0, 350.0, -45.0};
    state.timestampMs = 123456789;

    json::Object json = ToJson(state);
    NetworkState roundTripped = FromJson(json);

    ASSERT_EQ(roundTripped.position.x, state.position.x);
    ASSERT_EQ(roundTripped.position.y, state.position.y);
    ASSERT_EQ(roundTripped.position.z, state.position.z);
    ASSERT_EQ(roundTripped.rotation.pitch, state.rotation.pitch);
    ASSERT_EQ(roundTripped.rotation.yaw, state.rotation.yaw);
    ASSERT_EQ(roundTripped.rotation.roll, state.rotation.roll);
    ASSERT_EQ(roundTripped.timestampMs, state.timestampMs);
}

TEST_CASE(NetworkState_ToJsonThenFromJsonRoundTripsBasicState) {
    NetworkState state;
    state.basicState["health"] = json::Value::MakeNumber(75);
    state.basicState["isAiming"] = json::Value::MakeBool(true);

    json::Object json = ToJson(state);
    NetworkState roundTripped = FromJson(json);

    ASSERT_TRUE(roundTripped.basicState.count("health") == 1);
    ASSERT_EQ(roundTripped.basicState.at("health").AsNumber(), 75.0);
    ASSERT_TRUE(roundTripped.basicState.count("isAiming") == 1);
    ASSERT_TRUE(roundTripped.basicState.at("isAiming").AsBool());
}

TEST_CASE(NetworkState_ToJsonUsesTimestampAsTheWireFieldName) {
    // Matches server/src/protocol.js's player_state payload shape --
    // "timestamp", not "timestampMs" (that's the in-memory C++ field name).
    NetworkState state;
    state.timestampMs = 42;

    json::Object json = ToJson(state);
    ASSERT_TRUE(json.count("timestamp") == 1);
    ASSERT_EQ(json.at("timestamp").AsNumber(), 42.0);
}

TEST_CASE(NetworkState_ToJsonThenStringifyThenParseThenFromJsonSurvivesTheWire) {
    NetworkState state;
    state.position = Vector3{5.0, 6.0, 7.0};
    state.rotation = Rotation3{1.0, 2.0, 3.0};
    state.timestampMs = 999;

    std::string wire = json::StringifyObject(ToJson(state));
    auto parsed = json::ParseObject(wire);
    ASSERT_TRUE(parsed.ok);

    NetworkState roundTripped = FromJson(parsed.value);
    ASSERT_EQ(roundTripped.position.x, 5.0);
    ASSERT_EQ(roundTripped.position.y, 6.0);
    ASSERT_EQ(roundTripped.position.z, 7.0);
    ASSERT_EQ(roundTripped.rotation.yaw, 2.0);
    ASSERT_EQ(roundTripped.timestampMs, 999);
}

TEST_CASE(NetworkState_FromJsonOfAnEmptyObjectYieldsDefaults) {
    json::Object empty;
    NetworkState state = FromJson(empty);

    ASSERT_EQ(state.position.x, 0.0);
    ASSERT_EQ(state.rotation.yaw, 0.0);
    ASSERT_EQ(state.timestampMs, static_cast<int64_t>(0));
}
