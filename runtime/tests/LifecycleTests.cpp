#include "TestFramework.h"
#include "../src/Lifecycle.h"

using mzzplork::Lifecycle;
using mzzplork::RuntimeState;
using mzzplork::ToString;

TEST_CASE(Lifecycle_StartsInitializing) {
    Lifecycle lifecycle;
    ASSERT_TRUE(lifecycle.GetState() == RuntimeState::Initializing);
}

TEST_CASE(Lifecycle_TransitionsThroughFullSequence) {
    Lifecycle lifecycle;
    lifecycle.TransitionTo(RuntimeState::Ready);
    ASSERT_TRUE(lifecycle.GetState() == RuntimeState::Ready);
    lifecycle.TransitionTo(RuntimeState::ShuttingDown);
    ASSERT_TRUE(lifecycle.GetState() == RuntimeState::ShuttingDown);
    lifecycle.TransitionTo(RuntimeState::Stopped);
    ASSERT_TRUE(lifecycle.GetState() == RuntimeState::Stopped);
}

TEST_CASE(Lifecycle_ToStringMatchesTheDocumentedProtocol) {
    ASSERT_EQ(std::string(ToString(RuntimeState::Initializing)), std::string("initializing"));
    ASSERT_EQ(std::string(ToString(RuntimeState::Ready)), std::string("ready"));
    ASSERT_EQ(std::string(ToString(RuntimeState::ShuttingDown)), std::string("shutting_down"));
    ASSERT_EQ(std::string(ToString(RuntimeState::Stopped)), std::string("stopped"));
}
