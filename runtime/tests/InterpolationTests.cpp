#include <cmath>

#include "TestFramework.h"
#include "../src/Interpolation.h"

using namespace mzzplork;

namespace {
bool Near(double a, double b, double epsilon = 0.001) {
    return std::abs(a - b) < epsilon;
}
}  // namespace

TEST_CASE(Interpolation_NoTargetYieldsDefaultState) {
    InterpolatedEntity entity;
    ASSERT_FALSE(entity.HasTarget());

    NetworkState current = entity.Current(1000);
    ASSERT_EQ(current.position.x, 0.0);
}

TEST_CASE(Interpolation_FirstTargetSnapsImmediatelyRatherThanBlendingFromZero) {
    InterpolatedEntity entity;
    entity.SetInterpolationDurationMs(100);

    NetworkState target;
    target.position = Vector3{10.0, 20.0, 30.0};
    entity.SetTarget(target, /*nowMs=*/1000);

    ASSERT_TRUE(entity.HasTarget());
    // At the very instant the first snapshot arrives, elapsed == 0, so
    // Current() must already report the target -- not a blend from the
    // zero-valued "previous" state, which would visibly snap the entity
    // to the origin for one frame.
    NetworkState current = entity.Current(1000);
    ASSERT_TRUE(Near(current.position.x, 10.0));
    ASSERT_TRUE(Near(current.position.y, 20.0));
    ASSERT_TRUE(Near(current.position.z, 30.0));
}

TEST_CASE(Interpolation_HalfwayThroughTheDurationIsHalfwayBetweenPositions) {
    InterpolatedEntity entity;
    entity.SetInterpolationDurationMs(100);

    NetworkState first;
    first.position = Vector3{0.0, 0.0, 0.0};
    entity.SetTarget(first, 0);

    NetworkState second;
    second.position = Vector3{100.0, 0.0, 0.0};
    entity.SetTarget(second, 0);

    NetworkState current = entity.Current(50);
    ASSERT_TRUE(Near(current.position.x, 50.0));
}

TEST_CASE(Interpolation_PastTheDurationClampsAtTheTargetRatherThanOvershooting) {
    InterpolatedEntity entity;
    entity.SetInterpolationDurationMs(100);

    NetworkState first;
    first.position = Vector3{0.0, 0.0, 0.0};
    entity.SetTarget(first, 0);

    NetworkState second;
    second.position = Vector3{100.0, 0.0, 0.0};
    entity.SetTarget(second, 0);

    NetworkState current = entity.Current(10000);
    ASSERT_TRUE(Near(current.position.x, 100.0));
}

TEST_CASE(Interpolation_RotationTakesTheShortestPathAcrossTheThreeHundredSixtyWrap) {
    InterpolatedEntity entity;
    entity.SetInterpolationDurationMs(100);

    NetworkState first;
    first.rotation.yaw = 350.0;
    entity.SetTarget(first, 0);

    NetworkState second;
    second.rotation.yaw = 10.0;
    entity.SetTarget(second, 0);

    // Going the short way (350 -> 360/0 -> 10, a 20-degree arc) the
    // halfway point is 0 degrees, not 180 (the long way around).
    NetworkState halfway = entity.Current(50);
    double normalized = std::fmod(halfway.rotation.yaw + 360.0, 360.0);
    ASSERT_TRUE(Near(normalized, 0.0, 0.5) || Near(normalized, 360.0, 0.5));
}

TEST_CASE(Interpolation_RotationReachesTheExactTargetAtTheEndOfTheDuration) {
    InterpolatedEntity entity;
    entity.SetInterpolationDurationMs(100);

    NetworkState first;
    first.rotation.yaw = 350.0;
    entity.SetTarget(first, 0);

    NetworkState second;
    second.rotation.yaw = 10.0;
    entity.SetTarget(second, 0);

    NetworkState atEnd = entity.Current(100);
    double normalized = std::fmod(atEnd.rotation.yaw + 360.0, 360.0);
    ASSERT_TRUE(Near(normalized, 10.0, 0.5));
}

TEST_CASE(Interpolation_ANewTargetBlendsFromTheCurrentVisualPositionNotTheOldRawTarget) {
    InterpolatedEntity entity;
    entity.SetInterpolationDurationMs(100);

    NetworkState first;
    first.position = Vector3{0.0, 0.0, 0.0};
    entity.SetTarget(first, 0);

    NetworkState second;
    second.position = Vector3{100.0, 0.0, 0.0};
    entity.SetTarget(second, 0);

    // Halfway through the first transition, a fresh snapshot arrives.
    // The entity currently *appears* to be at x=50 -- the new blend must
    // start from there, not snap back to the x=0 starting point.
    NetworkState third;
    third.position = Vector3{200.0, 0.0, 0.0};
    entity.SetTarget(third, 50);

    NetworkState immediatelyAfterRetarget = entity.Current(50);
    ASSERT_TRUE(Near(immediatelyAfterRetarget.position.x, 50.0));
}

TEST_CASE(Interpolation_BasicStateSnapsToTheTargetRatherThanBlending) {
    InterpolatedEntity entity;
    entity.SetInterpolationDurationMs(100);

    NetworkState first;
    first.basicState["health"] = json::Value::MakeNumber(100);
    entity.SetTarget(first, 0);

    NetworkState second;
    second.basicState["health"] = json::Value::MakeNumber(50);
    entity.SetTarget(second, 0);

    NetworkState current = entity.Current(50);
    ASSERT_EQ(current.basicState.at("health").AsNumber(), 50.0);
}
