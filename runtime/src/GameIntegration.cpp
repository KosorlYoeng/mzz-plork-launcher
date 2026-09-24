#include "GameIntegration.h"

#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>

#pragma comment(lib, "advapi32.lib")

namespace mzzplork {

namespace fs = std::filesystem;

namespace {

const wchar_t* kProcessName = L"GTA5.exe";

// Community-documented Rockstar Games Launcher registry layout for an
// installed GTA V. NOT experimentally verified against a real
// installation in this environment -- see GameIntegration.h and
// docs/game-integration.md.
bool TryReadRegistryInstallPath(std::string& outPath) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Rockstar Games\\Grand Theft Auto V", 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t buffer[MAX_PATH]{};
    DWORD size = sizeof(buffer);
    LONG rc = RegQueryValueExW(key, L"InstallFolder", nullptr, nullptr, reinterpret_cast<BYTE*>(buffer), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) return false;

    int len = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    std::string path(len > 0 ? len - 1 : 0, '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, buffer, -1, path.data(), len, nullptr, nullptr);
    outPath = path + "\\GTA5.exe";
    return true;
}

bool FindRunningProcess(unsigned long& outPid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, kProcessName) == 0) {
                outPid = entry.th32ProcessID;
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

class GameIntegration : public IGameIntegration {
public:
    explicit GameIntegration(std::string pathOverride) : pathOverride_(std::move(pathOverride)) {}

    ~GameIntegration() override { Shutdown(); }

    ServiceResult Initialize() override {
        initialized_ = true;
        return ServiceResult::Ok("game integration boundary initialized (detection/launch only -- see docs/game-integration.md)");
    }

    ServiceResult DetectGame() override {
        std::string candidatePath;
        bool fromOverride = !pathOverride_.empty();
        if (fromOverride) {
            candidatePath = pathOverride_;
        } else if (!TryReadRegistryInstallPath(candidatePath)) {
            state_.installDetected = false;
            return ServiceResult::Failed("no GTA V install path configured and registry auto-detection found nothing");
        }

        std::error_code ec;
        if (!fs::exists(candidatePath, ec) || ec) {
            state_.installDetected = false;
            return ServiceResult::Failed(
                (fromOverride ? "configured gtaInstallPathOverride does not exist: " : "registry-reported install path does not exist: ")
                + candidatePath);
        }

        state_.installDetected = true;
        state_.installPath = candidatePath;
        return ServiceResult::Ok(
            (fromOverride ? "detected via configured override: " : "detected via registry (unverified mechanism): ") + candidatePath);
    }

    ServiceResult LaunchGame() override {
        if (!state_.installDetected) {
            return ServiceResult::Failed("cannot launch: DetectGame() has not found an install yet");
        }

        unsigned long existingPid;
        if (FindRunningProcess(existingPid)) {
            state_.processRunning = true;
            state_.processId = existingPid;
            return ServiceResult::Ok("GTA5.exe is already running (pid " + std::to_string(existingPid) + "), not relaunching");
        }

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        BOOL started = CreateProcessA(
            state_.installPath.c_str(), nullptr, nullptr, nullptr, FALSE,
            0, nullptr, nullptr, &si, &pi);
        if (!started) {
            return ServiceResult::Failed("CreateProcess failed (error " + std::to_string(GetLastError()) + ")");
        }

        if (launchedProcessHandle_) CloseHandle(launchedProcessHandle_);
        launchedProcessHandle_ = pi.hProcess;
        CloseHandle(pi.hThread);
        state_.processRunning = true;
        state_.processId = pi.dwProcessId;
        return ServiceResult::Ok("launched GTA5.exe (pid " + std::to_string(pi.dwProcessId) + ")");
    }

    ServiceResult Connect() override {
        // The actual blocker: see docs/game-integration.md. No legitimate
        // in-process integration mechanism has been identified, so this
        // stays honestly unimplemented rather than pretending to hook
        // anything.
        return ServiceResult::NotImplemented(
            "in-process GTA V integration is not implemented -- no legitimate mechanism identified (docs/game-integration.md)");
    }

    void Disconnect() override {
        // Nothing to tear down -- Connect() has never succeeded, by
        // construction. Present for interface symmetry and for a future
        // real implementation to fill in.
    }

    GameState GetGameState() const override {
        GameState current = state_;
        unsigned long pid;
        current.processRunning = FindRunningProcess(pid);
        if (current.processRunning) current.processId = pid;
        current.integrated = false;
        return current;
    }

    void Shutdown() override {
        if (launchedProcessHandle_) {
            CloseHandle(launchedProcessHandle_);
            launchedProcessHandle_ = nullptr;
        }
        initialized_ = false;
    }

private:
    std::string pathOverride_;
    bool initialized_ = false;
    GameState state_;
    HANDLE launchedProcessHandle_ = nullptr;
};

}  // namespace

std::unique_ptr<IGameIntegration> MakeGameIntegration(std::string pathOverride) {
    return std::make_unique<GameIntegration>(std::move(pathOverride));
}

}

