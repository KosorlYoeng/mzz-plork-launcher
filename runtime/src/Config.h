#pragma once
#include <string>

#include "ErrorHandling.h"

namespace mzzplork {

// Everything the runtime needs to know before it can do anything else.
// Deliberately small: fields are only added here once something in the
// runtime actually consumes them, to avoid config plumbing for features
// that don't exist yet.
struct RuntimeConfig {
    std::string logLevel = "info";     // "debug" | "info" | "warn" | "error"
    std::string serverHost;            // empty = not configured -- no MzzPlork Server connection is attempted
    int serverPort = 0;
    std::string gtaInstallPathOverride;  // empty = rely on (unverified) registry auto-detection; see GameIntegration.h
    int replicationIntervalMs = 100;     // how often a dirty local player state is sent -- see docs/synchronization.md
    int interpolationDurationMs = 100;   // how long a remote player takes to blend to a newly received state
};

struct ConfigLoadResult {
    ServiceResult result;
    RuntimeConfig config;
};

// Loads <appDataRoot>\config\config.json if present; falls back to
// defaults (Ok) if the file doesn't exist -- that's the normal case until
// something actually writes one. A file that exists but fails to parse is
// a real ServiceStatus::Failed, since that likely means user/deployment
// error rather than "nothing configured yet."
ConfigLoadResult LoadConfig(const std::string& appDataRoot);

}

