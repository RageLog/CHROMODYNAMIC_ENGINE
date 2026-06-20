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
#include <cstdint>

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

    // Steer scaling at a FIXED, LOW speed: a larger steer angle yields a larger
    // yaw rate. Monotonic in steer for both the kinematic and dynamic single-
    // track model — but only below the grip limit. At higher speed the lateral
    // acceleration saturates (grip-clamped), so both steers give the SAME yaw;
    // and yaw-vs-speed is itself NON-monotonic for an understeering vehicle (the
    // steady-state yaw gain falls above the characteristic speed). A low speed
    // keeps the response unsaturated so the steer→yaw monotonicity is visible.
    const auto gentle = state_after_steer(5, +0.4F);
    const auto sharp  = state_after_steer(5, +1.0F);
    EXPECT_GT(sharp.yaw_rate_rad_s, gentle.yaw_rate_rad_s)
        << "Larger steer must produce a larger yaw rate at the same (low) speed.";
    EXPECT_GT(gentle.yaw_rate_rad_s, 0.0F)
        << "Positive steer must produce a positive yaw rate.";
}

// ---------------------------------------------------------------------------
// 10. Dynamic linear-tyre model: at modest speed (below the grip limit) the
//     lateral acceleration matches the kinematic demand and is NOT traction-
//     limited, while the steady-state slip angles are populated and non-zero.
// ---------------------------------------------------------------------------
TEST(Vehicle, DynamicTyreSlipAnglesBelowGripLimit)
{
    Vehicle v;
    v.configure(make_test_config());

    // Build up a modest speed driving straight (~5 ticks → low speed so the
    // v^2 lateral demand stays well below mu*g ≈ 9.81 m/s²).
    v.set_input(/*throttle=*/1.0F, 0.0F, 0.0F);
    for (int i = 0; i < 5; ++i)
    {
        v.tick(0.1F);
    }
    // Gentle steer for one algebraic tick.
    v.set_input(1.0F, 0.0F, /*steer=*/0.2F);
    v.tick(0.1F);

    const auto& s = v.state();
    ASSERT_LT(s.lateral_grip_ratio, 1.0F)
        << "Pre-condition: demand must be below the grip limit at low speed.";
    EXPECT_FALSE(s.is_traction_limited)
        << "Below the friction circle the tyres are not saturated.";
    // Below the limit lateral_accel equals the unclamped kinematic demand.
    const float v_ms = s.speed_kph / 3.6F;
    EXPECT_NEAR(s.lateral_accel_ms2, v_ms * s.yaw_rate_rad_s, 1e-3F)
        << "Below grip, a_lat = v * yaw_rate (kinematic identity preserved).";
    // Linear-tyre slip angles are populated and have the demanded sign.
    EXPECT_GT(s.slip_angle_front_rad, 0.0F)
        << "Right steer develops a positive front slip angle.";
    EXPECT_GT(s.slip_angle_rear_rad, 0.0F)
        << "Right steer develops a positive rear slip angle.";
}

// ---------------------------------------------------------------------------
// 11. Friction circle: a hard steer at high speed saturates lateral
//     acceleration at mu*g (the demand exceeds available grip → sliding).
// ---------------------------------------------------------------------------
TEST(Vehicle, HardSteerAtSpeedSaturatesAtGripLimit)
{
    Vehicle v;
    v.configure(make_test_config());

    // Reach high speed, then slam full steer (huge v^2 * tan(delta)/L demand).
    v.set_input(1.0F, 0.0F, 0.0F);
    for (int i = 0; i < 80; ++i)
    {
        v.tick(0.1F);
    }
    v.set_input(1.0F, 0.0F, /*steer=*/1.0F);
    v.tick(0.1F);

    const auto& s = v.state();
    ASSERT_GE(s.lateral_grip_ratio, 1.0F)
        << "Pre-condition: full steer at high speed must exceed the grip limit.";
    EXPECT_TRUE(s.is_traction_limited)
        << "Saturated cornering must set the traction-limited flag.";
    // Clamped magnitude equals the friction-circle ceiling mu*g (mu=1 default).
    EXPECT_NEAR(std::fabs(s.lateral_accel_ms2), s.max_lateral_accel_ms2, 1e-3F)
        << "At the limit |a_lat| must equal mu * g.";
    EXPECT_NEAR(s.max_lateral_accel_ms2, 9.81F, 1e-2F)
        << "Default mu=1 gives a_lat_max = g.";
}

// ---------------------------------------------------------------------------
// 12. Lower friction coefficient lowers the grip ceiling and saturates sooner
//     (wet/ice tyres slide at a smaller lateral acceleration than dry).
// ---------------------------------------------------------------------------
TEST(Vehicle, LowerFrictionLowersGripCeiling)
{
    auto run_with_mu = [](float mu) {
        VehicleConfig cfg = make_test_config();
        for (auto& w : cfg.wheels)
        {
            w.friction_coefficient = mu;
        }
        Vehicle v;
        v.configure(cfg);
        v.set_input(1.0F, 0.0F, 0.0F);
        for (int i = 0; i < 40; ++i)
        {
            v.tick(0.1F);
        }
        v.set_input(1.0F, 0.0F, /*steer=*/0.5F);
        v.tick(0.1F);
        return v.state();
    };

    const auto dry = run_with_mu(1.0F);
    const auto wet = run_with_mu(0.4F);

    EXPECT_GT(dry.max_lateral_accel_ms2, wet.max_lateral_accel_ms2)
        << "Higher friction must raise the grip ceiling (mu * g).";
    EXPECT_NEAR(wet.max_lateral_accel_ms2, 0.4F * 9.81F, 1e-2F)
        << "Grip ceiling must equal mu * g.";
    // The achievable lateral accel on the slippery surface cannot exceed its
    // (lower) ceiling.
    EXPECT_LE(std::fabs(wet.lateral_accel_ms2),
              wet.max_lateral_accel_ms2 + 1e-3F)
        << "Lateral accel must never exceed the friction-circle ceiling.";
}

// ---------------------------------------------------------------------------
// 13. Understeer gradient sign: a front-biased cornering stiffness (softer
//     front tyres → understeer) yields K > 0; the mirror config yields K < 0
//     (oversteer). K is independent of speed/steer (a static chassis property).
// ---------------------------------------------------------------------------
TEST(Vehicle, UndersteerGradientSignTracksAxleStiffness)
{
    auto k_for = [](float c_front, float c_rear) {
        VehicleConfig cfg = make_test_config();
        // Front wheels steer; rear wheels drive (per make_test_config order).
        cfg.wheels[0].cornering_stiffness = c_front;  // FL (steer)
        cfg.wheels[1].cornering_stiffness = c_front;  // FR (steer)
        cfg.wheels[2].cornering_stiffness = c_rear;   // RL (rear)
        cfg.wheels[3].cornering_stiffness = c_rear;   // RR (rear)
        Vehicle v;
        v.configure(cfg);
        // One tick at speed so compute_lateral runs (K does not depend on it).
        v.set_input(1.0F, 0.0F, 0.1F);
        v.tick(0.1F);
        return v.state().understeer_gradient;
    };

    // Softer front (lower C_f) → understeer → K > 0 (l_f=l_r so K ∝ 1/C_f-1/C_r).
    EXPECT_GT(k_for(/*c_front=*/40000.0F, /*c_rear=*/90000.0F), 0.0F)
        << "Soft front tyres must produce understeer (K > 0).";
    // Softer rear → oversteer → K < 0.
    EXPECT_LT(k_for(/*c_front=*/90000.0F, /*c_rear=*/40000.0F), 0.0F)
        << "Soft rear tyres must produce oversteer (K < 0).";
    // Balanced → neutral (K ≈ 0).
    EXPECT_NEAR(k_for(/*c_front=*/60000.0F, /*c_rear=*/60000.0F), 0.0F, 1e-4F)
        << "Balanced cornering stiffness must be neutral (K ≈ 0).";
}

// ---------------------------------------------------------------------------
// 14. Zero cornering stiffness degrades gracefully: the dynamic correction is
//     skipped (neutral K, zero slip angles) but the kinematic yaw still runs
//     and no NaN/Inf is produced (divide-by-zero guard).
// ---------------------------------------------------------------------------
TEST(Vehicle, ZeroCorneringStiffnessFallsBackCleanly)
{
    VehicleConfig cfg = make_test_config();
    for (auto& w : cfg.wheels)
    {
        w.cornering_stiffness = 0.0F;  // degenerate tyre
    }
    Vehicle v;
    v.configure(cfg);

    v.set_input(1.0F, 0.0F, 0.0F);
    for (int i = 0; i < 20; ++i)
    {
        v.tick(0.1F);
    }
    v.set_input(1.0F, 0.0F, /*steer=*/1.0F);
    v.tick(0.1F);

    const auto& s = v.state();
    EXPECT_FALSE(std::isnan(s.understeer_gradient));
    EXPECT_FALSE(std::isnan(s.slip_angle_front_rad));
    EXPECT_FALSE(std::isnan(s.lateral_accel_ms2));
    EXPECT_FLOAT_EQ(s.understeer_gradient, 0.0F)
        << "Degenerate stiffness must yield a neutral (zero) gradient.";
    EXPECT_FLOAT_EQ(s.slip_angle_front_rad, 0.0F)
        << "Degenerate stiffness must yield zero slip angle.";
    // Kinematic yaw must still be produced.
    EXPECT_GT(s.yaw_rate_rad_s, 0.0F)
        << "Kinematic yaw must survive when the tyre model degrades.";
}

// ---------------------------------------------------------------------------
// 15. Zero-mass guard: a degenerate chassis_mass=0 with no wheel mass must not
//     divide-by-zero or NaN; the vehicle stays inert under throttle.
// ---------------------------------------------------------------------------
TEST(Vehicle, ZeroMassIsInertNotNaN)
{
    VehicleConfig cfg = make_test_config();
    cfg.chassis_mass_kg = 0.0F;
    for (auto& w : cfg.wheels)
    {
        w.mass = 0.0F;
    }
    Vehicle v;
    v.configure(cfg);

    v.set_input(/*throttle=*/1.0F, 0.0F, /*steer=*/0.5F);
    for (int i = 0; i < 10; ++i)
    {
        v.tick(0.1F);
    }

    const auto& s = v.state();
    EXPECT_FALSE(std::isnan(s.speed_kph)) << "Zero-mass must not NaN the speed.";
    EXPECT_FALSE(std::isinf(s.speed_kph)) << "Zero-mass must not blow up speed.";
    EXPECT_FLOAT_EQ(s.speed_kph, 0.0F)
        << "A massless body is treated as inert (no acceleration).";
}

// ---------------------------------------------------------------------------
// 16. Top-speed asymptote: under sustained full throttle the speed converges
//     to a finite terminal velocity (drive force balanced by aerodynamic drag
//     ~ v^2), it does not increase without bound.
// ---------------------------------------------------------------------------
TEST(Vehicle, TopSpeedConvergesToFiniteAsymptote)
{
    Vehicle v;
    v.configure(make_test_config());
    v.set_input(/*throttle=*/1.0F, 0.0F, 0.0F);

    // Long run to approach terminal velocity.
    for (int i = 0; i < 600; ++i)
    {
        v.tick(0.1F);  // 60 s
    }
    const float speed_a = v.state().speed_kph;

    // A further 30 s must add only a tiny increment (asymptotic plateau).
    for (int i = 0; i < 300; ++i)
    {
        v.tick(0.1F);
    }
    const float speed_b = v.state().speed_kph;

    EXPECT_GT(speed_a, 0.0F);
    EXPECT_LT(speed_b, 500.0F)
        << "Terminal speed must be finite (drag-limited), not unbounded.";
    EXPECT_NEAR(speed_b, speed_a, std::max(speed_a * 0.05F, 1.0F))
        << "Speed must plateau: <5% growth over the last 30 s.";
}

// ---------------------------------------------------------------------------
// 17. Degenerate wheelbase guard: a near-zero chassis length must not divide
//     by zero in the lateral formula (wheelbase() is floored strictly
//     positive); yaw/lateral stay finite.
// ---------------------------------------------------------------------------
TEST(Vehicle, DegenerateWheelbaseDoesNotDivideByZero)
{
    VehicleConfig cfg = make_test_config();
    cfg.chassis_dimensions = { 0.0F, 0.95F, 0.65F };  // zero length

    Vehicle v;
    v.configure(cfg);

    v.set_input(1.0F, 0.0F, 0.0F);
    for (int i = 0; i < 20; ++i)
    {
        v.tick(0.1F);
    }
    v.set_input(1.0F, 0.0F, /*steer=*/1.0F);
    v.tick(0.1F);

    const auto& s = v.state();
    EXPECT_FALSE(std::isnan(s.yaw_rate_rad_s)) << "Wheelbase floor must hold.";
    EXPECT_FALSE(std::isinf(s.yaw_rate_rad_s));
    EXPECT_FALSE(std::isnan(s.lateral_accel_ms2));
    EXPECT_FALSE(std::isnan(s.understeer_gradient));
    // Friction circle still caps the lateral acceleration to a finite value.
    EXPECT_LE(std::fabs(s.lateral_accel_ms2), s.max_lateral_accel_ms2 + 1e-3F);
}

// ---------------------------------------------------------------------------
// 18. Gear-down on deceleration: after coasting down from a high gear the
//     gearbox steps back toward 1st as RPM falls below idle (auto_shift
//     downshift branch), not just upshifts.
// ---------------------------------------------------------------------------
TEST(Vehicle, GearDownshiftsAsSpeedFalls)
{
    VehicleConfig cfg = make_test_config();
    cfg.engine.max_torque_nm = 1200.0F;  // climb RPM quickly to upshift
    cfg.engine.max_rpm       = 3000.0F;  // low redline so shifts happen soon

    Vehicle v;
    v.configure(cfg);

    // Accelerate to upshift past 1st gear.
    v.set_input(1.0F, 0.0F, 0.0F);
    for (int i = 0; i < 200; ++i)
    {
        v.tick(0.1F);
    }
    const uint8_t high_gear = v.state().gear;
    ASSERT_GT(high_gear, 0U) << "Pre-condition: must have upshifted at least once.";

    // Brake to a near-stop; RPM falls below idle → downshift back toward 1st.
    v.set_input(/*throttle=*/0.0F, /*brake=*/1.0F, 0.0F);
    for (int i = 0; i < 200; ++i)
    {
        v.tick(0.1F);
    }

    EXPECT_LT(v.state().gear, high_gear)
        << "Gear must step down as RPM falls below idle on deceleration.";
}
