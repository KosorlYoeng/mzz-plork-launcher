#include "TestFramework.h"
#include "../src/ResourceState.h"

using namespace mzzplork;

TEST_CASE(ResourceState_ToStringCoversEveryState) {
    ASSERT_EQ(std::string(ToString(ResourceState::Pending)), std::string("pending"));
    ASSERT_EQ(std::string(ToString(ResourceState::Downloading)), std::string("downloading"));
    ASSERT_EQ(std::string(ToString(ResourceState::Verifying)), std::string("verifying"));
    ASSERT_EQ(std::string(ToString(ResourceState::Cached)), std::string("cached"));
    ASSERT_EQ(std::string(ToString(ResourceState::Loading)), std::string("loading"));
    ASSERT_EQ(std::string(ToString(ResourceState::Loaded)), std::string("loaded"));
    ASSERT_EQ(std::string(ToString(ResourceState::Started)), std::string("started"));
    ASSERT_EQ(std::string(ToString(ResourceState::Stopped)), std::string("stopped"));
    ASSERT_EQ(std::string(ToString(ResourceState::Failed)), std::string("failed"));
}

TEST_CASE(ResourceState_TheHappyPathIsEntirelyValid) {
    ASSERT_TRUE(IsValidTransition(ResourceState::Pending, ResourceState::Downloading));
    ASSERT_TRUE(IsValidTransition(ResourceState::Downloading, ResourceState::Verifying));
    ASSERT_TRUE(IsValidTransition(ResourceState::Verifying, ResourceState::Cached));
    ASSERT_TRUE(IsValidTransition(ResourceState::Cached, ResourceState::Loading));
    ASSERT_TRUE(IsValidTransition(ResourceState::Loading, ResourceState::Loaded));
    ASSERT_TRUE(IsValidTransition(ResourceState::Loaded, ResourceState::Started));
}

TEST_CASE(ResourceState_TheCacheHitShortcutIsValid) {
    ASSERT_TRUE(IsValidTransition(ResourceState::Pending, ResourceState::Cached));
}

TEST_CASE(ResourceState_LoadingIsOnlyEverReachableFromCached) {
    // The structural enforcement behind "do not allow unverified files to
    // load": every other state must NOT be able to jump straight to
    // Loading.
    ASSERT_FALSE(IsValidTransition(ResourceState::Pending, ResourceState::Loading));
    ASSERT_FALSE(IsValidTransition(ResourceState::Downloading, ResourceState::Loading));
    ASSERT_FALSE(IsValidTransition(ResourceState::Verifying, ResourceState::Loading));
    ASSERT_FALSE(IsValidTransition(ResourceState::Loaded, ResourceState::Loading));
    ASSERT_FALSE(IsValidTransition(ResourceState::Failed, ResourceState::Loading));
}

TEST_CASE(ResourceState_StartStopReloadCycle) {
    ASSERT_TRUE(IsValidTransition(ResourceState::Loaded, ResourceState::Started));
    ASSERT_TRUE(IsValidTransition(ResourceState::Started, ResourceState::Stopped));
    ASSERT_TRUE(IsValidTransition(ResourceState::Stopped, ResourceState::Started));
    ASSERT_TRUE(IsValidTransition(ResourceState::Started, ResourceState::Loading));   // reload while running
    ASSERT_TRUE(IsValidTransition(ResourceState::Stopped, ResourceState::Loading));   // reload while stopped
}

TEST_CASE(ResourceState_FailedIsTerminal) {
    ASSERT_FALSE(IsValidTransition(ResourceState::Failed, ResourceState::Pending));
    ASSERT_FALSE(IsValidTransition(ResourceState::Failed, ResourceState::Cached));
    ASSERT_FALSE(IsValidTransition(ResourceState::Failed, ResourceState::Loaded));
}

TEST_CASE(ResourceState_EveryStateCanReachFailedExceptFailedItself) {
    ResourceState allExceptFailed[] = {
        ResourceState::Pending, ResourceState::Downloading, ResourceState::Verifying,
        ResourceState::Cached, ResourceState::Loading, ResourceState::Loaded,
        ResourceState::Started, ResourceState::Stopped
    };
    for (auto s : allExceptFailed) {
        ASSERT_TRUE(IsValidTransition(s, ResourceState::Failed));
    }
}
