#include "TestFramework.h"
#include "../src/NetworkService.h"
#include "../src/ServerConnectionStub.h"

using mzzplork::ServiceStatus;

// GameIntegration is covered in GameIntegrationTests.cpp and
// ResourceManager in ResourceManagerTests.cpp, now that both have grown
// real functionality beyond a plain NotImplemented stub.

TEST_CASE(NetworkService_WinsockIsAvailableOnThisMachine) {
    auto network = mzzplork::MakeWinsockNetworkService();
    auto result = network->Initialize();
    ASSERT_TRUE(result.status == ServiceStatus::Ok);
    ASSERT_TRUE(network->IsInitialized());
}

TEST_CASE(ServerConnectionStub_NeverClaimsToConnect) {
    auto server = mzzplork::MakeNotImplementedServerConnection();
    ASSERT_FALSE(server->Connect("127.0.0.1", 7777));
    ASSERT_FALSE(server->IsConnected());
    ASSERT_FALSE(server->SendHandshake());
    ASSERT_FALSE(server->Send("hello", "{}"));
    // Disconnect() on a connection that was never established must not throw.
    server->Disconnect();
}

TEST_CASE(ServerConnectionStub_MessageHandlerIsNeverInvoked) {
    auto server = mzzplork::MakeNotImplementedServerConnection();
    bool invoked = false;
    server->SetMessageHandler([&invoked](const mzzplork::ServerMessage&) { invoked = true; });
    server->Connect("127.0.0.1", 7777);
    server->Send("hello", "{}");
    ASSERT_FALSE(invoked);
}
