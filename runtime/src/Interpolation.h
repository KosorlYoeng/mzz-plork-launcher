#pragma once
#include <cstdint>

#include "NetworkState.h"

namespace mzzplork {

// Smooths a remote entity's movement between two received network
// snapshots rather than snapping to each one as it arrives. Pure math --
// no network, no game -- so it's directly unit-testable.
class InterpolatedEntity {
public:
    // Call whenever a fresh snapshot arrives for this entity (e.g. from a
    // "player_state" event). Blends from wherever the entity currently
    // *appears* to be (not the previous raw snapshot) toward the new
    // target, so a late-arriving update never causes a visible jump back.
    void SetTarget(const NetworkState& newState, int64_t nowMs);

    // Call every tick; returns the current smoothed state.
    NetworkState Current(int64_t nowMs) const;

    void SetInterpolationDurationMs(int64_t ms) { durationMs_ = ms; }
    bool HasTarget() const { return hasTarget_; }

private:
    NetworkState previous_;
    NetworkState target_;
    int64_t transitionStartMs_ = 0;
    int64_t durationMs_ = 100;
    bool hasTarget_ = false;
};

}

