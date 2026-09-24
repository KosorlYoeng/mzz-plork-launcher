#include "TestFramework.h"
#include "../src/ClientState.h"

using mzzplork::ClientState;
using mzzplork::ClientStateStore;
using mzzplork::ToString;

TEST_CASE(ClientState_StartsInStarting) {
    ClientStateStore store;
    ASSERT_TRUE(store.Get() == ClientState::Starting);
}

TEST_CASE(ClientState_SetChangesTheStoredState) {
    ClientStateStore store;
    store.Set(ClientState::Connecting);
    ASSERT_TRUE(store.Get() == ClientState::Connecting);
    store.Set(ClientState::Ready);
    ASSERT_TRUE(store.Get() == ClientState::Ready);
}

TEST_CASE(ClientState_ToStringCoversTheFullStateMachine) {
    ASSERT_EQ(std::string(ToString(ClientState::Starting)), std::string("starting"));
    ASSERT_EQ(std::string(ToString(ClientState::Initializing)), std::string("initializing"));
    ASSERT_EQ(std::string(ToString(ClientState::CheckingUpdate)), std::string("checking_update"));
    ASSERT_EQ(std::string(ToString(ClientState::Connecting)), std::string("connecting"));
    ASSERT_EQ(std::string(ToString(ClientState::Authenticating)), std::string("authenticating"));
    ASSERT_EQ(std::string(ToString(ClientState::DownloadingResources)), std::string("downloading_resources"));
    ASSERT_EQ(std::string(ToString(ClientState::LoadingResources)), std::string("loading_resources"));
    ASSERT_EQ(std::string(ToString(ClientState::Ready)), std::string("ready"));
    ASSERT_EQ(std::string(ToString(ClientState::Playing)), std::string("playing"));
    ASSERT_EQ(std::string(ToString(ClientState::Disconnecting)), std::string("disconnecting"));
    ASSERT_EQ(std::string(ToString(ClientState::Stopped)), std::string("stopped"));
}
