#pragma once
#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mzzplork::test {

struct SpawnedProcess {
    HANDLE hProcess = nullptr;
    HANDLE hThread = nullptr;
    HANDLE hStdoutRead = nullptr;
    HANDLE hStdinWrite = nullptr;  // only set by SpawnCapturingStdoutWithStdin()
    DWORD pid = 0;
};

// Spawns commandLine with its stdout redirected to a pipe this process
// can read from (stdin/stderr are left inherited). Used by E2ETests.cpp
// to launch the real server and the real MzzPlorkClient.exe as actual
// subprocesses and observe what they report.
inline SpawnedProcess SpawnCapturingStdout(const std::string& commandLine, const std::string& workingDir) {
    SpawnedProcess result;

    SECURITY_ATTRIBUTES saAttr{};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr, stdoutWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &saAttr, 0)) return result;
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = stdoutWrite;
    si.hStdError = stdoutWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::vector<char> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back('\0');

    BOOL started = CreateProcessA(
        nullptr, buffer.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, workingDir.empty() ? nullptr : workingDir.c_str(), &si, &pi);

    CloseHandle(stdoutWrite);  // parent doesn't write; child's inherited copy is what matters

    if (!started) {
        CloseHandle(stdoutRead);
        return result;
    }

    result.hProcess = pi.hProcess;
    result.hThread = pi.hThread;
    result.hStdoutRead = stdoutRead;
    result.pid = pi.dwProcessId;
    return result;
}

// Same as SpawnCapturingStdout(), but also gives the parent a writable
// pipe for the child's stdin instead of inheriting the parent's --
// SyncE2ETests.cpp uses this to drive a specific client's local player
// via the "setpos"/"setrot" control commands (Runtime.cpp's
// HandleControlCommand) without affecting any other spawned process.
inline SpawnedProcess SpawnCapturingStdoutWithStdin(const std::string& commandLine, const std::string& workingDir) {
    SpawnedProcess result;

    SECURITY_ATTRIBUTES saAttr{};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr, stdoutWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &saAttr, 0)) return result;
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE stdinRead = nullptr, stdinWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &saAttr, 0)) {
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        return result;
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = stdoutWrite;
    si.hStdError = stdoutWrite;
    si.hStdInput = stdinRead;

    PROCESS_INFORMATION pi{};
    std::vector<char> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back('\0');

    BOOL started = CreateProcessA(
        nullptr, buffer.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, workingDir.empty() ? nullptr : workingDir.c_str(), &si, &pi);

    CloseHandle(stdoutWrite);
    CloseHandle(stdinRead);

    if (!started) {
        CloseHandle(stdoutRead);
        CloseHandle(stdinWrite);
        return result;
    }

    result.hProcess = pi.hProcess;
    result.hThread = pi.hThread;
    result.hStdoutRead = stdoutRead;
    result.hStdinWrite = stdinWrite;
    result.pid = pi.dwProcessId;
    return result;
}

inline bool WriteLine(HANDLE hStdinWrite, const std::string& line) {
    std::string withNewline = line + "\n";
    DWORD written = 0;
    return WriteFile(hStdinWrite, withNewline.data(), static_cast<DWORD>(withNewline.size()), &written, nullptr) != 0;
}

// Reads from the pipe until `predicate(accumulatedOutput)` returns true or
// timeoutMs elapses. Runs the blocking ReadFile on a helper thread so the
// caller can still enforce a wall-clock timeout.
//
// On a timeout, the reader thread may still be blocked in ReadFile (Windows
// anonymous pipes have no portable non-blocking read), so it's detached
// rather than joined -- it will unblock once the caller closes pipeRead.
// Everything the detached thread touches (the accumulator, the done flag,
// the mutex, and the predicate itself) therefore has to survive past
// ReadUntil() returning, which a plain stack local or a reference
// parameter does not: this previously captured them all by reference,
// which is a use-after-return the moment a timed-out thread later wakes
// up with more data and calls a predicate/accumulator that no longer
// exists. Owning them via a shared_ptr the thread keeps alive, and taking
// predicate by value, fixes that for real rather than just making it less
// likely to be observed.
inline bool ReadUntil(HANDLE pipeRead, std::function<bool(const std::string&)> predicate, std::string& outAccumulated, int timeoutMs) {
    struct SharedState {
        std::string accumulated;
        std::atomic<bool> done{false};
        std::mutex mutex;
    };
    auto state = std::make_shared<SharedState>();

    std::thread reader([pipeRead, predicate, state] {
        char buf[4096];
        DWORD bytesRead = 0;
        while (!state->done) {
            if (!ReadFile(pipeRead, buf, sizeof(buf), &bytesRead, nullptr) || bytesRead == 0) break;
            std::lock_guard<std::mutex> lock(state->mutex);
            state->accumulated.append(buf, bytesRead);
            if (predicate(state->accumulated)) {
                state->done = true;
                break;
            }
        }
    });

    auto start = std::chrono::steady_clock::now();
    while (!state->done) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    bool succeeded = state->done.load();
    reader.detach();

    std::lock_guard<std::mutex> lock(state->mutex);
    outAccumulated = state->accumulated;
    return succeeded;
}

inline void TerminateAndClose(SpawnedProcess& p) {
    if (p.hProcess) {
        TerminateProcess(p.hProcess, 0);
        WaitForSingleObject(p.hProcess, 2000);
        CloseHandle(p.hProcess);
    }
    if (p.hThread) CloseHandle(p.hThread);
    if (p.hStdoutRead) CloseHandle(p.hStdoutRead);
    if (p.hStdinWrite) CloseHandle(p.hStdinWrite);
    p = SpawnedProcess{};
}

}
