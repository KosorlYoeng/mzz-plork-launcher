#include <cstring>
#include <iostream>
#include <string>

#include "ClientRuntimeInterface.h"
#include "ErrorHandling.h"
#include "Runtime.h"

namespace {

std::string ParseAppDataArg(int argc, char** argv) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--appdata") == 0) {
            return argv[i + 1];
        }
    }
    return "";
}

// Hidden diagnostic flag, not part of the bootstrapper contract in
// ClientRuntimeInterface.h: used by tests/CrashHandlerTests.cpp to verify
// CrashHandler actually writes a crash report before the process dies.
// "throw" escapes an unhandled C++ exception; "nullderef" triggers a real
// SEH access violation.
std::string ParseDebugCrashArg(int argc, char** argv) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--debug-crash") == 0) {
            return argv[i + 1];
        }
    }
    return "";
}

}  // namespace

int main(int argc, char** argv) {
    using namespace mzzplork;

    std::string appDataRoot = ParseAppDataArg(argc, argv);
    if (appDataRoot.empty()) {
        EmitErrorEvent("missing required --appdata argument");
        return static_cast<int>(ExitCode::InvalidArguments);
    }

    CrashHandler::Install(appDataRoot);

    std::string debugCrash = ParseDebugCrashArg(argc, argv);
    if (debugCrash == "throw") {
        throw std::runtime_error("--debug-crash throw");
    }
    if (debugCrash == "nullderef") {
        volatile int* p = nullptr;
        *p = 1;
    }

    ClientRuntime runtime(appDataRoot);
    return runtime.Run(std::cin);
}
