#include "Interpolation.h"

#include <algorithm>
#include <cmath>

namespace mzzplork {

namespace {
double Lerp(double from, double to, double t) {
    return from + (to - from) * t;
}

// Shortest-path angle interpolation in degrees -- without this, going
// from 350 degrees to 10 degrees would spin the long way around (340
// degrees) instead of the short way (20 degrees).
double LerpAngle(double from, double to, double t) {
    double delta = std::fmod(to - from + 180.0, 360.0);
    if (delta < 0) delta += 360.0;
    delta -= 180.0;
    return from + delta * t;
}
}  // namespace

void InterpolatedEntity::SetTarget(const NetworkState& newState, int64_t nowMs) {
    if (!hasTarget_) {
        // First update ever for this entity -- nothing to blend from yet.
        previous_ = newState;
        target_ = newState;
        transitionStartMs_ = nowMs;
        hasTarget_ = true;
        return;
    }
    previous_ = Current(nowMs);
    target_ = newState;
    transitionStartMs_ = nowMs;
}

NetworkState InterpolatedEntity::Current(int64_t nowMs) const {
    if (!hasTarget_) return NetworkState{};

    int64_t elapsed = nowMs - transitionStartMs_;
    double t = durationMs_ > 0 ? static_cast<double>(elapsed) / static_cast<double>(durationMs_) : 1.0;
    t = std::clamp(t, 0.0, 1.0);

    NetworkState result;
    result.position.x = Lerp(previous_.position.x, target_.position.x, t);
    result.position.y = Lerp(previous_.position.y, target_.position.y, t);
    result.position.z = Lerp(previous_.position.z, target_.position.z, t);
    result.rotation.pitch = LerpAngle(previous_.rotation.pitch, target_.rotation.pitch, t);
    result.rotation.yaw = LerpAngle(previous_.rotation.yaw, target_.rotation.yaw, t);
    result.rotation.roll = LerpAngle(previous_.rotation.roll, target_.rotation.roll, t);
    // Basic state (flags/health/etc.) snaps rather than interpolates --
    // arbitrary key/value data isn't generally numeric-lerp-safe.
    result.basicState = target_.basicState;
    result.timestampMs = nowMs;
    return result;
}

}

