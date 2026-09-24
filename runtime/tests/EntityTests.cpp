#include "TestFramework.h"
#include "../src/Entity.h"
#include "../src/Player.h"

using namespace mzzplork;

TEST_CASE(Entity_ConstructorSetsIdTypeAndOwnership) {
    Entity entity("e1", EntityType::Player, /*isLocal=*/true);
    ASSERT_EQ(entity.Id(), std::string("e1"));
    ASSERT_TRUE(entity.Type() == EntityType::Player);
    ASSERT_TRUE(entity.IsLocal());
}

TEST_CASE(Entity_DefaultPositionAndRotationAreZero) {
    Entity entity("e1", EntityType::Player, false);
    Vector3 pos = entity.Position();
    Rotation3 rot = entity.Rotation();
    ASSERT_EQ(pos.x, 0.0);
    ASSERT_EQ(pos.y, 0.0);
    ASSERT_EQ(pos.z, 0.0);
    ASSERT_EQ(rot.pitch, 0.0);
    ASSERT_EQ(rot.yaw, 0.0);
    ASSERT_EQ(rot.roll, 0.0);
}

TEST_CASE(Entity_SetPositionAndRotationAreReadBack) {
    Entity entity("e1", EntityType::Player, false);
    entity.SetPosition(Vector3{1.5, -2.5, 3.0});
    entity.SetRotation(Rotation3{10.0, 20.0, 30.0});

    Vector3 pos = entity.Position();
    Rotation3 rot = entity.Rotation();
    ASSERT_EQ(pos.x, 1.5);
    ASSERT_EQ(pos.y, -2.5);
    ASSERT_EQ(pos.z, 3.0);
    ASSERT_EQ(rot.pitch, 10.0);
    ASSERT_EQ(rot.yaw, 20.0);
    ASSERT_EQ(rot.roll, 30.0);
}

TEST_CASE(Entity_SetBasicStateIsReadBack) {
    Entity entity("e1", EntityType::Player, false);
    json::Object state;
    state["health"] = json::Value::MakeNumber(100);
    entity.SetBasicState(state);

    ASSERT_TRUE(entity.BasicState().count("health") == 1);
    ASSERT_EQ(entity.BasicState().at("health").AsNumber(), 100.0);
}

TEST_CASE(Entity_ToStringCoversTheOnlyCurrentEntityType) {
    ASSERT_EQ(std::string(ToString(EntityType::Player)), std::string("player"));
}

TEST_CASE(Player_IsAnEntityWithDisplayName) {
    Player player("p1", "Alice", /*isLocal=*/true);
    ASSERT_EQ(player.Id(), std::string("p1"));
    ASSERT_EQ(player.DisplayName(), std::string("Alice"));
    ASSERT_TRUE(player.Type() == EntityType::Player);
    ASSERT_TRUE(player.IsLocal());
}

TEST_CASE(Player_RemotePlayerIsNotLocal) {
    Player player("p2", "Bob", /*isLocal=*/false);
    ASSERT_FALSE(player.IsLocal());
}
