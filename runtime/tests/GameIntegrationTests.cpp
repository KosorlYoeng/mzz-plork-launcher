#include <fstream>

#include "TestFramework.h"
#include "TestUtil.h"
#include "../src/GameIntegration.h"

using namespace mzzplork;

// LaunchGame() actually spawning a process is deliberately not exercised
// here -- doing so in an automated test would spawn a real (possibly
// visible) process, which is poor test hygiene. Its failure path (called
// before a successful DetectGame()) is safe and is tested. The manual
// end-to-end verification in docs/game-integration.md covers what these
// unit tests intentionally don't.

TEST_CASE(GameIntegration_DetectGameFindsAConfiguredOverridePath) {
    std::string dir = mzzplork::test::MakeTempAppData();
    std::string fakeExe = dir + "\\GTA5.exe";
    std::ofstream(fakeExe) << "not a real executable, just a test fixture";

    auto game = MakeGameIntegration(fakeExe);
    auto result = game->DetectGame();

    ASSERT_TRUE(result.status == ServiceStatus::Ok);
    ASSERT_TRUE(game->GetGameState().installDetected);
    ASSERT_EQ(game->GetGameState().installPath, fakeExe);
}

TEST_CASE(GameIntegration_DetectGameFailsHonestlyWhenTheOverridePathDoesNotExist) {
    auto game = MakeGameIntegration("C:\\definitely\\does\\not\\exist\\GTA5.exe");
    auto result = game->DetectGame();

    ASSERT_TRUE(result.status == ServiceStatus::Failed);
    ASSERT_FALSE(game->GetGameState().installDetected);
}

TEST_CASE(GameIntegration_LaunchGameFailsCleanlyWithoutAPriorDetection) {
    auto game = MakeGameIntegration("");
    auto result = game->LaunchGame();
    ASSERT_TRUE(result.status == ServiceStatus::Failed);
}

TEST_CASE(GameIntegration_ConnectIsHonestlyNotImplemented) {
    std::string dir = mzzplork::test::MakeTempAppData();
    std::string fakeExe = dir + "\\GTA5.exe";
    std::ofstream(fakeExe) << "x";
    auto game = MakeGameIntegration(fakeExe);
    game->Initialize();
    game->DetectGame();

    auto result = game->Connect();
    ASSERT_TRUE(result.status == ServiceStatus::NotImplemented);
    ASSERT_FALSE(game->GetGameState().integrated);
}

TEST_CASE(GameIntegration_GetGameStateNeverClaimsIntegration) {
    auto game = MakeGameIntegration("");
    game->Initialize();
    ASSERT_FALSE(game->GetGameState().integrated);
    game->Shutdown();
    ASSERT_FALSE(game->GetGameState().integrated);
}

TEST_CASE(GameIntegration_DisconnectAndShutdownAreSafeWithoutAnyPriorState) {
    auto game = MakeGameIntegration("");
    game->Disconnect();
    game->Shutdown();
    // No crash is the assertion.
}
