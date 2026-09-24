#pragma once
#include <cstdio>
#include <string>

namespace mzzplork {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

LogLevel ParseLogLevel(const std::string& text, LogLevel fallback = LogLevel::Info);

class Logger {
public:
    explicit Logger(const std::string& logFilePath, LogLevel level = LogLevel::Info);
    ~Logger();

    void SetLevel(LogLevel level);

    void Debug(const std::string& message);
    void Info(const std::string& message);
    void Warn(const std::string& message);
    void Error(const std::string& message);

private:
    void Write(LogLevel level, const char* levelName, const std::string& message);
    FILE* file_ = nullptr;
    LogLevel level_ = LogLevel::Info;
};

}
