#include "TestFramework.h"
#include "TestUtil.h"
#include "../src/CacheService.h"

using mzzplork::FileCacheService;
using mzzplork::ServiceStatus;

TEST_CASE(CacheService_InitializeCreatesTheCacheDirectory) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    FileCacheService cache;
    auto result = cache.Initialize(appDataRoot);
    ASSERT_TRUE(result.status == ServiceStatus::Ok);
}

TEST_CASE(CacheService_PutThenGetRoundTripsTheSameData) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    FileCacheService cache;
    cache.Initialize(appDataRoot);

    ASSERT_FALSE(cache.Has("manifest"));
    ASSERT_TRUE(cache.Put("manifest", "hello world"));
    ASSERT_TRUE(cache.Has("manifest"));

    auto value = cache.Get("manifest");
    ASSERT_TRUE(value.has_value());
    ASSERT_EQ(*value, std::string("hello world"));
}

TEST_CASE(CacheService_GetOnMissingKeyReturnsNullopt) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    FileCacheService cache;
    cache.Initialize(appDataRoot);
    ASSERT_FALSE(cache.Get("does-not-exist").has_value());
}

TEST_CASE(CacheService_RemoveDeletesTheEntry) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    FileCacheService cache;
    cache.Initialize(appDataRoot);
    cache.Put("temp-key", "data");
    ASSERT_TRUE(cache.Has("temp-key"));
    ASSERT_TRUE(cache.Remove("temp-key"));
    ASSERT_FALSE(cache.Has("temp-key"));
}

TEST_CASE(CacheService_SanitizesKeysWithUnsafeCharacters) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    FileCacheService cache;
    cache.Initialize(appDataRoot);
    ASSERT_TRUE(cache.Put("../../evil", "payload"));
    auto value = cache.Get("../../evil");
    ASSERT_TRUE(value.has_value());
    ASSERT_EQ(*value, std::string("payload"));
}
