#include "Log.h"
#include <ctime>
#include <algorithm>
#include <cctype>
#include <share.h>

namespace mzzplork {

LogLevel ParseLogLevel(const std::string& text, LogLevel fallback) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower == "debug") return LogLevel::Debug;
    if (lower == "info") return LogLevel::Info;
    if (lower == "warn" || lower == "warning") return LogLevel::Warn;
    if (lower == "error") return LogLevel::Error;
    return fallback;
}

namespace {
std::string Timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmBuf);
    return buf;
}
}

Logger::Logger(const std::string& logFilePath, LogLevel level) : level_(level) {
    // Plain fopen_s()/fopen() opens exclusively on Windows (no sharing),
    // which blocks any other process -- a log tail tool, a monitoring
    // agent, or an automated test polling a live client's log for an
    // async event -- from even reading the file while this process is
    // still running. _SH_DENYWR still reserves write access to this
    // process (nothing else should be appending to our own log) but
    // allows concurrent readers.
    file_ = _fsopen(logFilePath.c_str(), "a", _SH_DENYWR);
}

Logger::~Logger() {
    if (file_) {
        std::fclose(file_);
    }
}

void Logger::SetLevel(LogLevel level) { level_ = level; }

void Logger::Write(LogLevel level, const char* levelName, const std::string& message) {
    if (level < level_) return;
    if (!file_) return;
    std::fprintf(file_, "[%s] %s %s\n", Timestamp().c_str(), levelName, message.c_str());
    std::fflush(file_);
}

void Logger::Debug(const std::string& message) { Write(LogLevel::Debug, "DEBUG", message); }
void Logger::Info(const std::string& message) { Write(LogLevel::Info, "INFO", message); }
void Logger::Warn(const std::string& message) { Write(LogLevel::Warn, "WARN", message); }
void Logger::Error(const std::string& message) { Write(LogLevel::Error, "ERROR", message); }

}
