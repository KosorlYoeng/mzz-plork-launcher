#include "Lifecycle.h"

namespace mzzplork {

const char* ToString(RuntimeState state) {
    switch (state) {
        case RuntimeState::Initializing: return "initializing";
        case RuntimeState::Ready: return "ready";
        case RuntimeState::ShuttingDown: return "shutting_down";
        case RuntimeState::Stopped: return "stopped";
    }
    return "unknown";
}

RuntimeState Lifecycle::GetState() const {
    return state_;
}

void Lifecycle::TransitionTo(RuntimeState next) {
    state_ = next;
}

}
