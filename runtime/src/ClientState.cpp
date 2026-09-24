#include "ClientState.h"

namespace mzzplork {

const char* ToString(ClientState state) {
    switch (state) {
        case ClientState::Starting: return "starting";
        case ClientState::Initializing: return "initializing";
        case ClientState::CheckingUpdate: return "checking_update";
        case ClientState::Connecting: return "connecting";
        case ClientState::Authenticating: return "authenticating";
        case ClientState::DownloadingResources: return "downloading_resources";
        case ClientState::LoadingResources: return "loading_resources";
        case ClientState::Ready: return "ready";
        case ClientState::Playing: return "playing";
        case ClientState::Disconnecting: return "disconnecting";
        case ClientState::Stopped: return "stopped";
    }
    return "unknown";
}

ClientState ClientStateStore::Get() const { return state_; }
void ClientStateStore::Set(ClientState state) { state_ = state; }

}
