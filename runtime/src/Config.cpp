#include "Config.h"
#include "Json.h"

#include <fstream>
#include <sstream>

namespace mzzplork {

namespace {
std::string ConfigFilePath(const std::string& appDataRoot) {
    return appDataRoot + "\\config\\config.json";
}
}

ConfigLoadResult LoadConfig(const std::string& appDataRoot) {
    ConfigLoadResult out;
    std::string path = ConfigFilePath(appDataRoot);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        out.result = ServiceResult::Ok("no config.json present, using defaults");
        return out;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    auto parsed = json::ParseObject(buffer.str());
    if (!parsed.ok) {
        out.result = ServiceResult::Failed("config.json is malformed: " + parsed.error);
        return out;
    }

    if (auto it = parsed.value.find("logLevel"); it != parsed.value.end()) {
        out.config.logLevel = it->second.AsString(out.config.logLevel);
    }
    if (auto it = parsed.value.find("serverHost"); it != parsed.value.end()) {
        out.config.serverHost = it->second.AsString(out.config.serverHost);
    }
    if (auto it = parsed.value.find("serverPort"); it != parsed.value.end()) {
        out.config.serverPort = static_cast<int>(it->second.AsNumber(out.config.serverPort));
    }
    if (auto it = parsed.value.find("gtaInstallPathOverride"); it != parsed.value.end()) {
        out.config.gtaInstallPathOverride = it->second.AsString(out.config.gtaInstallPathOverride);
    }
    if (auto it = parsed.value.find("replicationIntervalMs"); it != parsed.value.end()) {
        out.config.replicationIntervalMs = static_cast<int>(it->second.AsNumber(out.config.replicationIntervalMs));
    }
    if (auto it = parsed.value.find("interpolationDurationMs"); it != parsed.value.end()) {
        out.config.interpolationDurationMs = static_cast<int>(it->second.AsNumber(out.config.interpolationDurationMs));
    }

    out.result = ServiceResult::Ok("config.json loaded from " + path);
    return out;
}

}

