// =============================================================================
// CHROMODYNAMIC — tests/test_vehicle.cpp
// Phase 670 — cd::physics::vehicle Sprint-1 unit tests.
//
// Brief-mandated coverage (5+ cases):
//   1. configure() + tick() produces non-zero speed under full throttle.
//   2. Brake decelerates a moving vehicle.
//   3. Gear shifts upward when RPM exceeds max_rpm threshold.
//   4. Steer input is reflected in VehicleState::steer.
//   5. Idle (no input) decays vehicle to rest.
//
// Extras:
//   6. Zero dt tick does not change state.
//   7. configure() resets dynamic state even after several ticks.
//
// Test methodology:
//   * Arrange / Act / Assert pattern.
//   * No sleep_for — purely deterministic tick(dt) calls.
//   * VehicleConfig built with a helper so all tests share consistent tuning.
// =============================================================================

#include <cd/physics/vehicle/Vehicle.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace
{

using cd::physics::vehicle::EngineConfig;
using cd::physics::vehicle::Vehicle;
using cd::physics::vehicle::VehicleConfig;
using cd::physics::vehicle::WheelConfig;

/// Build a standard 4-wheel RWD test car.
///   FL/FR: steer-only (not driven).
///   RL/RR: driven (not steered).
VehicleConfig make_test_config()
{
    VehicleConfig cfg {};
    cfg.chassis_mass_kg       = 1200.0F;
    cfg.chassis_dimensions    = { 2.3F, 0.95F, 0.65F };

    // Front wheels: steerable, not driven.
    WheelConfig front {};
    front.radius              = 0.32F;
    front.mass                = 20.0F;
    front.suspension_rest_length = 0.25F;
    front.suspension_stiffness   = 22000.0F;
    front.damping             = 3800.0F;
    front.steering_angle_max  = 0.524F; // ~30 degrees
    front.is_driven           = false;

    // Rear wheels: driven, no steer.
    WheelConfig rear {};
    rear.radius               = 0.32F;
    rear.mass                 = 20.0F;
    rear.suspension_rest_length = 0.25F;
    rear.suspension_stiffness   = 22000.0F;
    rear.damping              = 3800.0F;
    rear.steering_angle_max   = 0.0F;
    rear.is_driven            = true;

    cfg.wheels = { front, front, rear, rear };

    EngineConfig eng {};
    eng.max_torque_nm = 320.0F;
    eng.gear_ratios   = { 3.5F, 2.1F, 1.4F, 1.0F, 0.8F, 0.67F };
    eng.final_drive   = 3.7F;
    eng.idle_rpm      = 800.0F;
    eng.max_rpm       = 6500.0F;
    cfg.engine = eng;

    return cfg;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. configure() + tick() produces non-zero speed under full throttle.
// ---------------------------------------------------------------------------
TEST(Vehicle, FullThrottleProducesNonZeroSpeed)
{
    // Arrange
    Vehicle v;
    v.configure(make_test_config());
    v.set_input(/*throttle=*/1.0F, /*brake=*/0.0F, /*steer=*/0.0F);

    // Act — simulate 2 seconds at full throttle.
    for (int i = 0; i < 20; ++i)
    {
        v.tick(0.1F);
    }

    // Assert
    EXPECT_GT(v.state().speed_kph, 0.0F)
        << "Vehicle should have accelerated under full throttle.";
}

// ---------------------------------------------------------------------------
// 2. Brake decelerates a moving vehicle.
// ---------------------------------------------------------------------------
TEST(Vehicle, BrakeDeceleratesMovingVehicle)
{
    // Arrange — spin up to some speed first.
    Vehicle v;
    v.configure(make_test_config());
    v.set_input(/*throttle=*/1.0F, 0.0F, 0.0F);
    for (int i = 0; i < 20; ++i)
    {
        v.tick(0.1F);  // 2 s of acceleration
    }

    const float speed_before = v.state().speed_kph;
    ASSERT_GT(speed_before, 0.0F) << "Pre-condition: vehicle must be moving.";

    // Act — apply full brakes for 3 seconds.
    v.set_input(/*throttle=*/0.0F, /*brake=*/1.0F, /*steer=*/0.0F);
    for (int i = 0; i < 30; ++i)
    {
        v.tick(0.1F);
    }

    // Assert
    EXPECT_LT(v.state().speed_kph, speed_before)
        << "Braking should reduce speed.";
}

// ---------------------------------------------------------------------------
// 3. Gear shifts upward when RPM exceeds max_rpm.
// ---------------------------------------------------------------------------
TEST(Vehicle, GearShiftsUpAtMaxRpm)
{
    // Arrange — high-torque engine so RPM rises quickly.
    VehicleConfig cfg = make_test_config();
    cfg.engine.max_torque_nm = 1200.0F; // exaggerated to ensure fast RPM climb
    cfg.engine.max_rpm       = 3000.0F; // lower redline so shift happens sooner

    Vehicle v;
    v.configure(cfg);
    v.set_input(/*throttle=*/1.0F, 0.0F, 0.0F);

    const uint8_t initial_gear = v.state().gear;

    // Act — run long enough for at least one gear shift.
    for (int i = 0; i < 100; ++i)
    {
        v.tick(0.1F);
    }

    // Assert
    EXPECT_GT(v.state().gear, initial_gear)
        << "Gear should have shifted up as RPM exceeded max_rpm.";
}

// ---------------------------------------------------------------------------
// 4. Steer input is reflected in VehicleState::steer.
// ---------------------------------------------------------------------------
TEST(Vehicle, SteerInputReflectedInState)
{
    // Arrange
    Vehicle v;
    v.configure(make_test_config());

    // Act
    v.set_input(0.0F, 0.0F, /*steer=*/0.75F);
    v.tick(0.016F); // one frame

    // Assert
    EXPECT_NEAR(v.state().steer, 0.75F, 1e-5F)
        << "VehicleState::steer must match set_input steer argument.";
}

// ---------------------------------------------------------------------------
// 5. Idle (no input) decays vehicle to rest.
// ---------------------------------------------------------------------------
TEST(Vehicle, IdleDecaysToRest)
{
    // Arrange — give the vehicle a brief kick (5 ticks ≈ 0.5 s, ~5 m/s / 18 km/h).
    // Rolling resistance decelerates at ~0.15 m/s² so ~33 s suffices from
    // that initial speed; 500 ticks × 0.1 s = 50 s gives comfortable margin.
    Vehicle v;
    v.configure(make_test_config());
    v.set_input(1.0F, 0.0F, 0.0F);
    for (int i = 0; i < 5; ++i)
    {
        v.tick(0.1F);
    }
    ASSERT_GT(v.state().speed_kph, 0.0F) << "Pre-condition: vehicle must be moving.";

    // Act — release all inputs and simulate 50 seconds of coast.
    v.set_input(0.0F, 0.0F, 0.0F);
    for (int i = 0; i < 500; ++i)
    {
        v.tick(0.1F);
    }

    // Assert — speed should be very close to zero (< 0.5 km/h).
    EXPECT_NEAR(v.state().speed_kph, 0.0F, 0.5F)
        << "Vehicle should coast to rest with no inputs (drag + rolling resistance).";
}

// ---------------------------------------------------------------------------
// 6. Zero-dt tick does not change state.
// ---------------------------------------------------------------------------
TEST(Vehicle, ZeroDtTickDoesNotChangeState)
{
    // Arrange
    Vehicle v;
    v.configure(make_test_config());
    v.set_input(1.0F, 0.0F, 0.0F);
    v.tick(0.1F);  // produce some non-zero state

    const float speed_snapshot = v.state().speed_kph;
    const float rpm_snapshot   = v.state().rpm;

    // Act
    v.tick(0.0F);

    // Assert — state must be unchanged.
    EXPECT_FLOAT_EQ(v.state().speed_kph, speed_snapshot)
        << "Zero-dt tick must not change speed.";
    EXPECT_FLOAT_EQ(v.state().rpm, rpm_snapshot)
        << "Zero-dt tick must not change RPM.";
}

// ---------------------------------------------------------------------------
// 7. configure() resets dynamic state even after several ticks.
// ---------------------------------------------------------------------------
TEST(Vehicle, ConfigureResetsState)
{
    // Arrange — run for 5 seconds.
    Vehicle v;
    v.configure(make_test_config());
    v.set_input(1.0F, 0.0F, 0.0F);
    for (int i = 0; i < 50; ++i)
    {
        v.tick(0.1F);
    }
    ASSERT_GT(v.state().speed_kph, 0.0F) << "Pre-condition: vehicle must be moving.";

    // Act — re-configure with the same config.
    v.configure(make_test_config());

    // Assert — all dynamic state is zeroed.
    EXPECT_FLOAT_EQ(v.state().speed_kph, 0.0F)
        << "configure() must reset speed.";
    EXPECT_EQ(v.state().gear, 0U)
        << "configure() must reset gear.";
}

// ---------------------------------------------------------------------------
// 8. Lateral kinematics: at speed with steer the body develops a yaw rate
//    and a centripetal lateral acceleration; straight-line driving does not.
//    Verifies the kinematic-bicycle term (v * tan(delta) / wheelbase).
// ---------------------------------------------------------------------------
TEST(Vehicle, SteeringAtSpeedProducesYawAndLateralAccel)
{
    Vehicle v;
    v.configure(make_test_config());

    // Build up speed driving straight first.
    v.set_input(/*throttle=*/1.0F, 0.0F, /*steer=*/0.0F);
    for (int i = 0; i < 20; ++i)
    {
        v.tick(0.1F);
    }
    const float speed = v.state().speed_kph;
    ASSERT_GT(speed, 0.0F) << "Pre-condition: vehicle must be moving.";

    // Straight-line: yaw rate and lateral acceleration are ~zero.
    EXPECT_NEAR(v.state().yaw_rate_rad_s, 0.0F, 1e-5F)
        << "No steer → no yaw rate.";
    EXPECT_NEAR(v.state().lateral_accel_ms2, 0.0F, 1e-5F)
        << "No steer → no lateral acceleration.";

    // Now apply right steer; one tick is enough — the term is algebraic.
    v.set_input(/*throttle=*/1.0F, 0.0F, /*steer=*/1.0F);
    v.tick(0.1F);

    EXPECT_GT(v.state().yaw_rate_rad_s, 0.0F)
        << "Right steer at speed must produce a positive yaw rate.";
    EXPECT_GT(v.state().lateral_accel_ms2, 0.0F)
        << "Right steer at speed must produce centripetal lateral acceleration.";

    // Cross-check the closed-form kinematic relation a_lat = v * yaw_rate.
    const float v_ms = v.state().speed_kph / 3.6F;
    EXPECT_NEAR(v.state().lateral_accel_ms2,
                v_ms * v.state().yaw_rate_rad_s, 1e-3F)
        << "lateral_accel must equal v * yaw_rate (kinematic bicycle).";
}

// ---------------------------------------------------------------------------
// 9. Lateral kinematics are sign-symmetric (left steer mirrors right steer at
//    the same speed) and yaw rate scales with speed (faster ⇒ more yaw for the
//    same steer angle — the v factor in v*tan(delta)/L).
// ---------------------------------------------------------------------------
TEST(Vehicle, LateralIsSignSymmetricAndScalesWithSpeed)
{
    // Drive to a given speed, then apply `steer` for one tick and read state.
    auto state_after_steer = [](int spin_ticks, float steer) {
        Vehicle v;
        v.configure(make_test_config());
        v.set_input(1.0F, 0.0F, 0.0F);
        for (int i = 0; i < spin_ticks; ++i)
        {
            v.tick(0.1F);
        }
        v.set_input(1.0F, 0.0F, steer);
        v.tick(0.1F);
        return v.state();
    };

    // Sign symmetry: same speed, opposite steer → opposite yaw + lateral accel.
    const auto right = state_after_steer(20, +1.0F);
    const auto left  = state_after_steer(20, -1.0F);

    EXPECT_NEAR(right.yaw_rate_rad_s, -left.yaw_rate_rad_s, 1e-4F)
        << "Left/right yaw rates must be opposite for symmetric steer.";
    EXPECT_NEAR(right.lateral_accel_ms2, -left.lateral_accel_ms2, 1e-3F)
        << "Left/right lateral accelerations must be opposite.";

    // Speed scaling: at higher speed the same steer angle yields a larger yaw
    // rate (the linear v factor in the kinematic-bicycle relation).
    const auto slow = state_after_steer(5,  +1.0F);
    const auto fast = state_after_steer(40, +1.0F);
    ASSERT_GT(fast.speed_kph, slow.speed_kph)
        << "Pre-condition: the 'fast' run must be travelling faster.";
    EXPECT_GT(fast.yaw_rate_rad_s, slow.yaw_rate_rad_s)
        << "Higher speed must produce a larger yaw rate for the same steer.";
}
