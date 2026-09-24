#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

#include "TestFramework.h"
#include "TestUtil.h"
#include "SpawnHelper.h"

// The multiplayer synchronization end-to-end test explicitly requested
// for this phase: spawns a REAL MzzPlork Server and TWO REAL
// MzzPlorkClient.exe processes (not mocks, not a single process talking
// to itself) and proves that player A's locally-driven position and
// rotation actually reach player B's Replication/EntityManager over a
// real socket. See docs/synchronization.md.
//
// Run from runtime\tests (same convention as E2ETests.cpp); requires
// `node` on PATH.

using namespace mzzplork::test;

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Extracts the playerId a client's own "authentication" service event
// reported, e.g. {"event":"service","service":"authentication","status":"ok","message":"player <id>"}.
std::string ExtractPlayerId(const std::string& clientOutput) {
    const std::string marker = "\"authentication\",\"status\":\"ok\",\"message\":\"player ";
    auto pos = clientOutput.find(marker);
    if (pos == std::string::npos) return "";
    pos += marker.size();
    auto end = clientOutput.find('"', pos);
    if (end == std::string::npos) return "";
    return clientOutput.substr(pos, end - pos);
}

// The player_state log line (Runtime.cpp) lands in the log file, not
// stdout -- polls it directly since there is no pipe-based signal for it.
bool PollFileForSubstring(const std::string& path, const std::string& needle, int timeoutMs) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
        if (ReadFile(path).find(needle) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

}  // namespace

TEST_CASE(SyncE2E_PositionAndRotationSetOnOneClientReplicateToTheOther) {
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
    ASSERT_TRUE(tcpPort > 0);

    // 2. Spawn two independent real clients against the same server, each
    // with its own app data root (and therefore its own generated
    // clientId/playerId -- see Runtime.cpp's LoadOrCreateClientId()).
    // replicationIntervalMs is shortened so the test doesn't have to wait
    // out the 100ms production default.
    std::string appDataA = MakeTempAppData();
    {
        std::ofstream configFile(appDataA + "\\config\\config.json");
        configFile << "{\"serverHost\":\"127.0.0.1\",\"serverPort\":" << tcpPort << ",\"replicationIntervalMs\":50}";
    }
    std::string appDataB = MakeTempAppData();
    {
        std::ofstream configFile(appDataB + "\\config\\config.json");
        configFile << "{\"serverHost\":\"127.0.0.1\",\"serverPort\":" << tcpPort << ",\"replicationIntervalMs\":50}";
    }

    SpawnedProcess clientA = SpawnCapturingStdoutWithStdin(
        "..\\..\\dist\\runtime\\MzzPlorkClient.exe --appdata \"" + appDataA + "\"", "");
    ASSERT_TRUE(clientA.hProcess != nullptr);
    std::string clientAOutput;
    bool aReady = ReadUntil(
        clientA.hStdoutRead,
        [](const std::string& acc) { return acc.find("\"state\":\"ready\"") != std::string::npos; },
        clientAOutput, 10000);
    ASSERT_TRUE(aReady);

    std::string playerIdA = ExtractPlayerId(clientAOutput);
    ASSERT_TRUE(!playerIdA.empty());

    SpawnedProcess clientB = SpawnCapturingStdoutWithStdin(
        "..\\..\\dist\\runtime\\MzzPlorkClient.exe --appdata \"" + appDataB + "\"", "");
    ASSERT_TRUE(clientB.hProcess != nullptr);
    std::string clientBOutput;
    bool bReady = ReadUntil(
        clientB.hStdoutRead,
        [](const std::string& acc) { return acc.find("\"state\":\"ready\"") != std::string::npos; },
        clientBOutput, 10000);
    ASSERT_TRUE(bReady);

    // 3. Drive client A's local player through the control channel this
    // phase adds specifically for local development/testing (no real GTA V
    // binding exists to source this from -- see docs/synchronization.md).
    ASSERT_TRUE(WriteLine(clientA.hStdinWrite, "setpos 111 222 333"));
    ASSERT_TRUE(WriteLine(clientA.hStdinWrite, "setrot 5 15 25"));

    // 4. Confirm B's Replication actually received and applied it -- the
    // real assertion of this test. Runtime.cpp logs both entity lifecycle
    // and every received player_state with its values specifically so
    // this is observable from the log file without reaching into the
    // process. A generous timeout absorbs real subprocess/OS scheduling
    // variance (spawn, auth, resource round trip, then the update itself)
    // rather than racing a tight one.
    std::string needle = "player_state received for " + playerIdA + " pos=(111,222,333) rot=(5,15,25)";
    bool replicated = PollFileForSubstring(appDataB + "\\logs\\client.log", needle, 8000);
    std::string finalClientBLog = ReadFile(appDataB + "\\logs\\client.log");

    TerminateAndClose(clientA);
    TerminateAndClose(clientB);
    TerminateAndClose(server);

    ASSERT_TRUE(replicated);
    // player_state is defensively applied even without a prior
    // player_joined (Replication::OnPlayerState), so a successful
    // replication above already implies an entity for A exists on B --
    // this confirms it was actually recorded as such, not just logged.
    ASSERT_TRUE(finalClientBLog.find("entity created: " + playerIdA + " (player, remote)") != std::string::npos);
}
