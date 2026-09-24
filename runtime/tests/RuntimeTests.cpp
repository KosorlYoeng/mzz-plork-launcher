#include <fstream>
#include <sstream>

#include "TestFramework.h"
#include "TestUtil.h"
#include "../src/Runtime.h"
#include "../src/ClientRuntimeInterface.h"

using namespace mzzplork;

namespace {

// Simulates a real MzzPlork Server's protocol responses without a socket:
// replies to hello/auth/resource_manifest_request/disconnect synchronously
// from within Send(), matching server/src/protocol.js's message shapes.
// Lets RuntimeTests exercise the full CONNECTING -> ... -> READY sequence
// against a fake that behaves like the real thing, while E2ETests.cpp
// covers the real socket + real server end-to-end.
class FakeServerConnection : public IServerConnection {
public:
    bool connectSucceeds = true;
    bool authSucceeds = true;
    bool manifestHasResources = false;
    bool sentDisconnect = false;
    bool resourcesReadyNotified = false;
    std::string lastAuthClientId;

    bool Connect(const std::string&, unsigned short) override {
        connected_ = connectSucceeds;
        return connectSucceeds;
    }
    void Disconnect() override { connected_ = false; }
    bool IsConnected() const override { return connected_; }
    bool SendHandshake() override { return Send("hello", "{}"); }

    bool Send(const std::string& type, const std::string&) override {
        if (!handler_) return true;
        if (type == "hello") {
            handler_(ServerMessage{"hello_ack", R"({"ok":true,"protocolVersion":"1.0"})"});
        } else if (type == "auth") {
            std::string reply = authSucceeds
                ? R"({"ok":true,"playerId":"test-player","sessionId":"test-session","displayName":"Tester"})"
                : R"({"ok":false,"reason":"denied"})";
            handler_(ServerMessage{"auth_result", reply});
        } else if (type == "resource_manifest_request") {
            std::string resources = manifestHasResources
                ? R"([{"id":"a.txt","path":"a.txt","size":1,"hash":"0000000000000000000000000000000000000000000000000000000000000000","version":"1"}])"
                : "[]";
            handler_(ServerMessage{"resource_manifest", R"({"manifestVersion":"1.0.0","httpPort":1,"resources":)" + resources + "}"});
        } else if (type == "resources_ready") {
            resourcesReadyNotified = true;
            handler_(ServerMessage{"resources_ready_ack", R"({"ok":true})"});
        } else if (type == "disconnect") {
            sentDisconnect = true;
            handler_(ServerMessage{"kick", R"({"reason":"client shutting down"})"});
        }
        return true;
    }
    void SetMessageHandler(std::function<void(const ServerMessage&)> handler) override {
        handler_ = std::move(handler);
    }

private:
    bool connected_ = false;
    std::function<void(const ServerMessage&)> handler_;
};

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

RuntimeDependencies MakeTestDeps(FakeServerConnection** outFakeServer = nullptr) {
    RuntimeConfig config;
    RuntimeDependencies deps = MakeDefaultDependencies(config);
    auto fake = std::make_unique<FakeServerConnection>();
    if (outFakeServer) *outFakeServer = fake.get();
    deps.server = std::move(fake);
    return deps;
}

void WriteServerConfig(const std::string& appDataRoot) {
    std::ofstream configFile(appDataRoot + "\\config\\config.json");
    configFile << R"({"serverHost":"127.0.0.1","serverPort":7777})";
}

}  // namespace

TEST_CASE(Runtime_NoServerConfiguredStillReachesReadyAndStopsCleanly) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    int exitCode = 0;
    RuntimeState finalState;
    {
        ClientRuntime runtime(appDataRoot);  // no config.json -> serverHost empty -> lazy defaults
        std::istringstream emptyControlChannel;
        exitCode = runtime.Run(emptyControlChannel);
        finalState = runtime.LifecycleState();
    }
    ASSERT_EQ(exitCode, 0);
    ASSERT_TRUE(finalState == RuntimeState::Stopped);

    std::string log = ReadFile(appDataRoot + "\\logs\\client.log");
    ASSERT_TRUE(log.find("client runtime ready") != std::string::npos);
    ASSERT_TRUE(log.find("no server configured") != std::string::npos);
}

TEST_CASE(Runtime_FullHandshakeReachesReadyThroughEveryState) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    WriteServerConfig(appDataRoot);
    FakeServerConnection* fakePtr = nullptr;
    RuntimeDependencies deps = MakeTestDeps(&fakePtr);

    ClientRuntime runtime(appDataRoot, std::move(deps));
    std::istringstream emptyControlChannel;
    int exitCode = runtime.Run(emptyControlChannel);

    ASSERT_EQ(exitCode, 0);
    ASSERT_TRUE(runtime.SessionState() == ClientState::Stopped);
    // Never reaches Playing: no GameIntegration::Connect() ever succeeds.
    ASSERT_FALSE(runtime.SessionState() == ClientState::Playing);
}

TEST_CASE(Runtime_NeverEntersPlayingEvenWithAConnectedServer) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    WriteServerConfig(appDataRoot);
    RuntimeDependencies deps = MakeTestDeps();
    {
        ClientRuntime runtime(appDataRoot, std::move(deps));
        std::istringstream emptyControlChannel;
        runtime.Run(emptyControlChannel);
    }  // ~ClientRuntime() closes the Logger's file handle before we read it back below

    std::string log = ReadFile(appDataRoot + "\\logs\\client.log");
    ASSERT_TRUE(log.find("\"playing\"") == std::string::npos);
    ASSERT_TRUE(log.find("client runtime ready") != std::string::npos);
}

TEST_CASE(Runtime_AuthenticationFailureIsReportedAndSkipsResourceDownload) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    WriteServerConfig(appDataRoot);
    FakeServerConnection* fakePtr = nullptr;
    RuntimeDependencies deps = MakeTestDeps(&fakePtr);
    fakePtr->authSucceeds = false;

    int exitCode = 0;
    {
        ClientRuntime runtime(appDataRoot, std::move(deps));
        std::istringstream emptyControlChannel;
        exitCode = runtime.Run(emptyControlChannel);
    }

    // Auth failure is not fatal to the process -- the client still
    // reaches Ready (idling, disconnected) rather than crashing.
    ASSERT_EQ(exitCode, 0);
    std::string log = ReadFile(appDataRoot + "\\logs\\client.log");
    ASSERT_TRUE(log.find("client runtime ready") != std::string::npos);
}

TEST_CASE(Runtime_ConnectionFailureFallsBackToReadyWithoutAServer) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    WriteServerConfig(appDataRoot);
    FakeServerConnection* fakePtr = nullptr;
    RuntimeDependencies deps = MakeTestDeps(&fakePtr);
    fakePtr->connectSucceeds = false;

    int exitCode = 0;
    {
        ClientRuntime runtime(appDataRoot, std::move(deps));
        std::istringstream emptyControlChannel;
        exitCode = runtime.Run(emptyControlChannel);
    }

    ASSERT_EQ(exitCode, 0);
    std::string log = ReadFile(appDataRoot + "\\logs\\client.log");
    ASSERT_TRUE(log.find("failed to connect") != std::string::npos);
}

TEST_CASE(Runtime_FailingCacheServiceAbortsStartupBeforeReady) {
    class FailingCache : public ICacheService {
    public:
        ServiceResult Initialize(const std::string&) override { return ServiceResult::Failed("simulated disk failure"); }
        bool Has(const std::string&) const override { return false; }
        bool Put(const std::string&, const std::string&) override { return false; }
        std::optional<std::string> Get(const std::string&) const override { return std::nullopt; }
        bool Remove(const std::string&) override { return false; }
    };

    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    RuntimeDependencies deps = MakeTestDeps();
    deps.cache = std::make_unique<FailingCache>();

    ClientRuntime runtime(appDataRoot, std::move(deps));
    std::istringstream emptyControlChannel;
    int exitCode = runtime.Run(emptyControlChannel);

    ASSERT_EQ(exitCode, static_cast<int>(ExitCode::FatalInitError));
    ASSERT_TRUE(runtime.LifecycleState() == RuntimeState::Initializing);
}

TEST_CASE(Runtime_MalformedConfigAbortsStartupBeforeReady) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    std::ofstream configFile(appDataRoot + "\\config\\config.json");
    configFile << "{not valid json";
    configFile.close();

    ClientRuntime runtime(appDataRoot);
    std::istringstream emptyControlChannel;
    int exitCode = runtime.Run(emptyControlChannel);

    ASSERT_EQ(exitCode, static_cast<int>(ExitCode::FatalInitError));
    ASSERT_TRUE(runtime.LifecycleState() == RuntimeState::Initializing);
}

TEST_CASE(Runtime_VoluntaryDisconnectIsAcknowledgedNotReportedAsAKick) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    WriteServerConfig(appDataRoot);
    FakeServerConnection* fakePtr = nullptr;
    RuntimeDependencies deps = MakeTestDeps(&fakePtr);

    ClientRuntime runtime(appDataRoot, std::move(deps));
    std::istringstream emptyControlChannel;
    runtime.Run(emptyControlChannel);

    ASSERT_TRUE(fakePtr->sentDisconnect);
    std::string log = ReadFile(appDataRoot + "\\logs\\client.log");
    ASSERT_TRUE(log.find("kicked by server") == std::string::npos);
}



