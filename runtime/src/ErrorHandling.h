#pragma once
#include <string>

namespace mzzplork {

// Shared status vocabulary for every service's initialization result.
// NotImplemented is a distinct, honest outcome -- not a failure -- for
// subsystems that don't exist yet (Network, Resources, GTA V integration,
// Server Connection). The runtime boot sequence treats it as "continue,
// but report truthfully," while Failed is a hard stop for services that
// are supposed to work now (Configuration, Logging, Cache) but didn't.
enum class ServiceStatus {
    Ok,
    NotImplemented,
    Failed
};

inline const char* ToString(ServiceStatus status) {
    switch (status) {
        case ServiceStatus::Ok: return "ok";
        case ServiceStatus::NotImplemented: return "not_implemented";
        case ServiceStatus::Failed: return "failed";
    }
    return "unknown";
}

struct ServiceResult {
    ServiceStatus status = ServiceStatus::Ok;
    std::string message;

    static ServiceResult Ok(std::string message = "") {
        return ServiceResult{ServiceStatus::Ok, std::move(message)};
    }
    static ServiceResult NotImplemented(std::string message) {
        return ServiceResult{ServiceStatus::NotImplemented, std::move(message)};
    }
    static ServiceResult Failed(std::string message) {
        return ServiceResult{ServiceStatus::Failed, std::move(message)};
    }
};

// Installs process-wide handlers for unhandled C++ exceptions
// (std::set_terminate) and unhandled SEH exceptions (access violations,
// etc. via SetUnhandledExceptionFilter) that write a crash report to
// <appDataRoot>\logs\crash-<timestamp>.log before the process exits.
// This is real: it is exercised by tests/CrashHandlerTests.cpp, which
// spawns MzzPlorkClient.exe with a hidden --debug-crash flag and checks
// that a crash log actually gets written.
class CrashHandler {
public:
    static void Install(const std::string& appDataRoot);
    static std::string LogDirectory();

private:
    static std::string logDirectory_;
};

}

