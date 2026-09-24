#include <fstream>
#include <sstream>

#include "TestFramework.h"
#include "TestUtil.h"
#include "SpawnHelper.h"

// The end-to-end test explicitly requested for this phase: spawns the
// REAL MzzPlork Server (node) and the REAL MzzPlorkClient.exe as actual
// subprocesses -- not mocks -- and verifies the full chain:
//   MzzPlorkClient.exe -> MzzPlork Server -> Authentication ->
//   Resource manifest -> Resource download -> Resource verification -> READY
//
// Run from runtime\tests so relative paths resolve (same convention as
// CrashHandlerTests.cpp); requires `node` on PATH.

using namespace mzzplork::test;

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

TEST_CASE(E2E_RealClientReachesReadyAgainstARealServer) {
    // 1. Launch a real MzzPlork Server on ephemeral ports.
    SpawnedProcess server = SpawnCapturingStdout(
        "node test-support\\launch-ephemeral.js",
        "..\\..\\server");
    ASSERT_TRUE(server.hProcess != nullptr);

    std::string serverOutput;
    bool gotPorts = ReadUntil(
        server.hStdoutRead,
        [](const std::string& acc) { return acc.find("TCP_PORT=") != std::string::npos && acc.find("HTTP_PORT=") != std::string::npos; },
        serverOutput,
        10000);
    ASSERT_TRUE(gotPorts);

    auto extractPort = [&](const std::string& key) -> int {
        auto pos = serverOutput.find(key);
        if (pos == std::string::npos) return -1;
        pos += key.size();
        return std::atoi(serverOutput.c_str() + pos);
    };
    int tcpPort = extractPort("TCP_PORT=");
    int httpPort = extractPort("HTTP_PORT=");
    ASSERT_TRUE(tcpPort > 0);
    ASSERT_TRUE(httpPort > 0);

    // 2. Point a real client config at that server and run the real exe
    // to completion (its stdin is inherited from this process and not
    // written to, so on most CI-style invocations it will be closed/EOF
    // quickly; if not, the test's own timeout below still bounds the wait
    // via the client's log file rather than blocking on the pipe).
    std::string appDataRoot = MakeTempAppData();
    {
        std::ofstream configFile(appDataRoot + "\\config\\config.json");
        configFile << "{\"serverHost\":\"127.0.0.1\",\"serverPort\":" << tcpPort << "}";
    }

    SpawnedProcess client = SpawnCapturingStdout(
        "..\\..\\dist\\runtime\\MzzPlorkClient.exe --appdata \"" + appDataRoot + "\"",
        "");
    ASSERT_TRUE(client.hProcess != nullptr);

    std::string clientOutput;
    bool reachedReady = ReadUntil(
        client.hStdoutRead,
        [](const std::string& acc) { return acc.find("\"state\":\"ready\"") != std::string::npos; },
        clientOutput,
        10000);

    TerminateAndClose(client);
    TerminateAndClose(server);

    ASSERT_TRUE(reachedReady);
    ASSERT_TRUE(clientOutput.find("\"server-connection\",\"status\":\"ok\"") != std::string::npos);
    ASSERT_TRUE(clientOutput.find("\"authentication\",\"status\":\"ok\"") != std::string::npos);
    ASSERT_TRUE(clientOutput.find("\"resources\",\"status\":\"ok\"") != std::string::npos);

    std::string log = ReadFile(appDataRoot + "\\logs\\client.log");
    ASSERT_TRUE(log.find("client runtime ready") != std::string::npos);
}
