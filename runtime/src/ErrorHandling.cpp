#include "ErrorHandling.h"

#include <windows.h>
#include <ctime>
#include <cstdio>
#include <exception>
#include <fstream>

namespace mzzplork {

std::string CrashHandler::logDirectory_;

namespace {

std::string Timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmBuf);
    return buf;
}

void WriteCrashReport(const std::string& logDirectory, const std::string& reason) {
    std::string path = logDirectory + "\\crash-" + Timestamp() + ".log";
    std::ofstream out(path, std::ios::app);
    if (out) {
        out << "MzzPlorkClient crash report\n";
        out << "reason: " << reason << "\n";
    }
}

void TerminateHandler() {
    std::string reason = "unhandled C++ exception";
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            reason = std::string("unhandled std::exception: ") + e.what();
        } catch (...) {
            reason = "unhandled non-std exception";
        }
    }
    WriteCrashReport(CrashHandler::LogDirectory(), reason);
    std::abort();
}

LONG WINAPI SehFilter(EXCEPTION_POINTERS* info) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "SEH exception code 0x%08lX",
                  info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0);
    WriteCrashReport(CrashHandler::LogDirectory(), buf);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

std::string CrashHandler::LogDirectory() { return logDirectory_; }

void CrashHandler::Install(const std::string& appDataRoot) {
    logDirectory_ = appDataRoot + "\\logs";
    std::set_terminate(TerminateHandler);
    SetUnhandledExceptionFilter(SehFilter);
}

}  // namespace mzzplork
