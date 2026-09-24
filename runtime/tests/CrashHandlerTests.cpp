#include <windows.h>

#include <filesystem>
#include <vector>

#include "TestFramework.h"
#include "TestUtil.h"

// Exercises the real MzzPlorkClient.exe binary via the hidden
// --debug-crash flag (see main.cpp), rather than crashing the test
// process itself -- a real crash can't be caught in-process without
// losing every other test's result, so this spawns it as a child and
// inspects what it left behind.
//
// Uses CreateProcess directly rather than std::system(): system() shells
// out through cmd.exe, whose command-line requoting is unreliable once
// the command itself contains quoted arguments (it can mangle the
// executable path so cmd.exe fails to find it at all -- which still
// returns a nonzero exit code, making a bare "exit code != 0" assertion
// pass for the wrong reason). CreateProcess passes the command line
// straight to the child with no shell in between.
//
// Run this test suite from runtime\tests so the relative path below
// resolves; see docs/RUNTIME.md.

namespace {

bool AnyCrashLogExists(const std::string& logsDir) {
    std::error_code ec;
    if (!std::filesystem::exists(logsDir, ec)) return false;
    for (const auto& entry : std::filesystem::directory_iterator(logsDir, ec)) {
        if (entry.path().filename().string().rfind("crash-", 0) == 0) {
            return true;
        }
    }
    return false;
}

bool RunProcessAndWait(const std::string& exePath, const std::string& args, DWORD& exitCodeOut) {
    std::string commandLine = "\"" + exePath + "\" " + args;
    std::vector<char> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back('\0');

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    BOOL started = CreateProcessA(
        nullptr, buffer.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo);
    if (!started) return false;

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    GetExitCodeProcess(processInfo.hProcess, &exitCodeOut);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    return true;
}

}  // namespace

TEST_CASE(CrashHandler_WritesACrashReportWhenAnUnhandledExceptionEscapes) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    std::string exePath = "..\\..\\dist\\runtime\\MzzPlorkClient.exe";
    ASSERT_TRUE(std::filesystem::exists(exePath));

    DWORD exitCode = 0;
    bool spawned = RunProcessAndWait(exePath, "--appdata \"" + appDataRoot + "\" --debug-crash throw", exitCode);

    ASSERT_TRUE(spawned);
    ASSERT_TRUE(exitCode != 0);
    ASSERT_TRUE(AnyCrashLogExists(appDataRoot + "\\logs"));
}

TEST_CASE(CrashHandler_WritesACrashReportOnANullDereference) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    std::string exePath = "..\\..\\dist\\runtime\\MzzPlorkClient.exe";

    DWORD exitCode = 0;
    bool spawned = RunProcessAndWait(exePath, "--appdata \"" + appDataRoot + "\" --debug-crash nullderef", exitCode);

    ASSERT_TRUE(spawned);
    ASSERT_TRUE(exitCode != 0);
    ASSERT_TRUE(AnyCrashLogExists(appDataRoot + "\\logs"));
}
