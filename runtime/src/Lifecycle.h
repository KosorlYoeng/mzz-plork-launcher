#pragma once

namespace mzzplork {

enum class RuntimeState {
    Initializing,
    Ready,
    ShuttingDown,
    Stopped
};

const char* ToString(RuntimeState state);

class Lifecycle {
public:
    RuntimeState GetState() const;
    void TransitionTo(RuntimeState next);

private:
    RuntimeState state_ = RuntimeState::Initializing;
};

}
