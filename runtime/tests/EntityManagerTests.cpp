#include "TestFramework.h"
#include "../src/EntityManager.h"

using namespace mzzplork;

TEST_CASE(EntityManager_CreatePlayerIsRetrievableByGetAndGetPlayer) {
    EntityManager manager;
    Player& created = manager.CreatePlayer("p1", "Alice", true);
    ASSERT_EQ(created.Id(), std::string("p1"));

    Entity* asEntity = manager.Get("p1");
    ASSERT_TRUE(asEntity != nullptr);
    ASSERT_EQ(asEntity->Id(), std::string("p1"));

    Player* asPlayer = manager.GetPlayer("p1");
    ASSERT_TRUE(asPlayer != nullptr);
    ASSERT_EQ(asPlayer->DisplayName(), std::string("Alice"));

    ASSERT_TRUE(manager.Contains("p1"));
    ASSERT_EQ(manager.Count(), static_cast<size_t>(1));
}

TEST_CASE(EntityManager_GetUnknownIdReturnsNull) {
    EntityManager manager;
    ASSERT_TRUE(manager.Get("missing") == nullptr);
    ASSERT_TRUE(manager.GetPlayer("missing") == nullptr);
    ASSERT_FALSE(manager.Contains("missing"));
}

TEST_CASE(EntityManager_DestroyEntityRemovesItAndReturnsTrue) {
    EntityManager manager;
    manager.CreatePlayer("p1", "Alice", true);

    bool destroyed = manager.DestroyEntity("p1");
    ASSERT_TRUE(destroyed);
    ASSERT_FALSE(manager.Contains("p1"));
    ASSERT_EQ(manager.Count(), static_cast<size_t>(0));
}

TEST_CASE(EntityManager_DestroyUnknownIdReturnsFalseAndDoesNothing) {
    EntityManager manager;
    manager.CreatePlayer("p1", "Alice", true);

    bool destroyed = manager.DestroyEntity("does-not-exist");
    ASSERT_FALSE(destroyed);
    ASSERT_EQ(manager.Count(), static_cast<size_t>(1));
}

TEST_CASE(EntityManager_GetAllReturnsEveryEntity) {
    EntityManager manager;
    manager.CreatePlayer("p1", "Alice", true);
    manager.CreatePlayer("p2", "Bob", false);

    std::vector<Entity*> all = manager.GetAll();
    ASSERT_EQ(all.size(), static_cast<size_t>(2));
}

TEST_CASE(EntityManager_OnEntityCreatedCallbackFiresSynchronously) {
    EntityManager manager;
    std::string createdId;
    manager.SetOnEntityCreated([&createdId](Entity& e) { createdId = e.Id(); });

    manager.CreatePlayer("p1", "Alice", true);
    ASSERT_EQ(createdId, std::string("p1"));
}

TEST_CASE(EntityManager_OnEntityDestroyedCallbackFiresWithIdAndType) {
    EntityManager manager;
    manager.CreatePlayer("p1", "Alice", true);

    std::string destroyedId;
    EntityType destroyedType = EntityType::Player;
    manager.SetOnEntityDestroyed([&](const std::string& id, EntityType type) {
        destroyedId = id;
        destroyedType = type;
    });

    manager.DestroyEntity("p1");
    ASSERT_EQ(destroyedId, std::string("p1"));
    ASSERT_TRUE(destroyedType == EntityType::Player);
}

TEST_CASE(EntityManager_OnEntityDestroyedDoesNotFireForAnUnknownId) {
    EntityManager manager;
    bool fired = false;
    manager.SetOnEntityDestroyed([&fired](const std::string&, EntityType) { fired = true; });

    manager.DestroyEntity("does-not-exist");
    ASSERT_FALSE(fired);
}

TEST_CASE(EntityManager_LocalAndRemotePlayersAreDistinguishedByIsLocal) {
    EntityManager manager;
    manager.CreatePlayer("local-player", "Me", true);
    manager.CreatePlayer("remote-player", "Someone Else", false);

    ASSERT_TRUE(manager.GetPlayer("local-player")->IsLocal());
    ASSERT_FALSE(manager.GetPlayer("remote-player")->IsLocal());
}
