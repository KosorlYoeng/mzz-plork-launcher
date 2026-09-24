#include "ResourceState.h"

namespace mzzplork {

const char* ToString(ResourceState state) {
    switch (state) {
        case ResourceState::Pending: return "pending";
        case ResourceState::Downloading: return "downloading";
        case ResourceState::Verifying: return "verifying";
        case ResourceState::Cached: return "cached";
        case ResourceState::Loading: return "loading";
        case ResourceState::Loaded: return "loaded";
        case ResourceState::Started: return "started";
        case ResourceState::Stopped: return "stopped";
        case ResourceState::Failed: return "failed";
    }
    return "unknown";
}

bool IsValidTransition(ResourceState from, ResourceState to) {
    switch (from) {
        case ResourceState::Pending:
            return to == ResourceState::Downloading || to == ResourceState::Cached || to == ResourceState::Failed;
        case ResourceState::Downloading:
            return to == ResourceState::Verifying || to == ResourceState::Failed;
        case ResourceState::Verifying:
            return to == ResourceState::Cached || to == ResourceState::Failed;
        case ResourceState::Cached:
            return to == ResourceState::Loading || to == ResourceState::Failed;
        case ResourceState::Loading:
            return to == ResourceState::Loaded || to == ResourceState::Failed;
        case ResourceState::Loaded:
            return to == ResourceState::Started || to == ResourceState::Failed;
        case ResourceState::Started:
            return to == ResourceState::Stopped || to == ResourceState::Loading || to == ResourceState::Failed;
        case ResourceState::Stopped:
            return to == ResourceState::Started || to == ResourceState::Loading || to == ResourceState::Failed;
        case ResourceState::Failed:
            return false;  // terminal -- a retry re-applies the manifest and creates a fresh record
    }
    return false;
}

}
