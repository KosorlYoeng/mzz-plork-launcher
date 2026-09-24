#include "ClientRuntimeInterface.h"
#include <cstdio>

namespace mzzplork {

namespace {
std::string EscapeJson(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out;
}
}

void EmitStateEvent(const std::string& state) {
    std::printf("{\"event\":\"state\",\"state\":\"%s\"}\n", EscapeJson(state).c_str());
    std::fflush(stdout);
}

void EmitServiceEvent(const std::string& service, const std::string& status, const std::string& message) {
    std::printf(
        "{\"event\":\"service\",\"service\":\"%s\",\"status\":\"%s\",\"message\":\"%s\"}\n",
        EscapeJson(service).c_str(), EscapeJson(status).c_str(), EscapeJson(message).c_str());
    std::fflush(stdout);
}

void EmitLogEvent(const std::string& message) {
    std::printf("{\"event\":\"log\",\"message\":\"%s\"}\n", EscapeJson(message).c_str());
    std::fflush(stdout);
}

void EmitErrorEvent(const std::string& message) {
    std::printf("{\"event\":\"error\",\"message\":\"%s\"}\n", EscapeJson(message).c_str());
    std::fflush(stdout);
}

}

