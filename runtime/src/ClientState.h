#pragma once
#include <string>

namespace mzzplork {

// The client's session state machine, distinct from RuntimeState
// (Lifecycle.h), which only tracks process boot/shutdown. This is the
// state machine requested for Phase 6+: it reflects where the client
// actually is in connecting to a MzzPlork Server and getting ready to
// play -- entirely independent of GTA V integration, which remains a
// separate concern (GameIntegration.h).
//
// PLAYING must never be entered by pretending GTA V integration exists.
// As of this phase, GameIntegration::connect() is honestly
// NotImplemented, so nothing in this runtime ever transitions to
// Playing -- READY is as far as the state machine goes today.
enum class ClientState {
    Starting,
    Initializing,
    CheckingUpdate,
    Connecting,
    Authenticating,
    DownloadingResources,
    LoadingResources,
    Ready,
    Playing,
    Disconnecting,
    Stopped
};

const char* ToString(ClientState state);

class ClientStateStore {
public:
    ClientState Get() const;
    void Set(ClientState state);

private:
    ClientState state_ = ClientState::Starting;
};

}
