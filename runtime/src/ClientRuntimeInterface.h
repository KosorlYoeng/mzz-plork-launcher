#pragma once
//
// Contract between the MzzPlork bootstrapper and MzzPlorkClient.exe.
// See docs/FLOWS.md ("Startup flow") and docs/RUNTIME.md (the client's
// internal lifecycle) for the full sequence.
//
// Invocation:
//   MzzPlorkClient.exe --appdata "<MzzPlork Application Data path>"
//
// Output protocol (stdout, JSON Lines -- one JSON object per line):
//   {"event":"state","state":"initializing"}
//   {"event":"state","state":"ready"}
//   {"event":"state","state":"shutting_down"}
//   {"event":"state","state":"stopped"}
//   {"event":"service","service":"<name>","status":"ok"|"not_implemented"|"failed","message":"..."}
//   {"event":"log","message":"..."}
//   {"event":"error","message":"..."}
//
// "service" events report each lifecycle step's real outcome (see
// Runtime.h) -- notably "not_implemented" for Network, Resources, GTA V
// integration, and Server Connection, which is expected and not an error
// until their respective phases land.
//
// Exit codes:
//   0  Success           - clean shutdown
//   1  FatalInitError     - fatal initialization error (e.g. malformed config.json)
//   2  InvalidArguments   - invalid/missing --appdata argument
//
// bootstrapper/electron/clientLauncher.js reads this stream line-by-line.
// Anything on stdout that isn't valid JSON is forwarded as a plain log
// line instead of crashing the bootstrapper.
//
#include <string>

namespace mzzplork {

enum class ExitCode {
    Success = 0,
    FatalInitError = 1,
    InvalidArguments = 2
};

void EmitStateEvent(const std::string& state);
void EmitServiceEvent(const std::string& service, const std::string& status, const std::string& message);
void EmitLogEvent(const std::string& message);
void EmitErrorEvent(const std::string& message);

}
