// =============================================================================
// CHROMODYNAMIC — tests/test_vehicle_jolt.cpp
// Phase 692 — cd::physics::vehicle Sprint-2: JoltAdapter unit tests.
//
// Tests in this file are Jolt-gated: they call
// cd::physics_jolt::is_stub_backend() and issue GTEST_SKIP when the real
// Jolt library is not yet linked (Sprint-2 default). The adapter shape
// (configure / sync_to / sync_from lifecycle) is still exercised against the
// stub backend's IPhysicsWorld so the plumbing compiles and runs correctly.
//
// Coverage (3 cases):
//   1. JoltAdapter::configure() registers chassis body in the world.
//   2. Vehicle::tick() with use_jolt=true progresses chassis position over
//      60 frames (1 second at dt=1/60) under full throttle.
//   3. Fallback: use_jolt=true without configure_jolt() still uses the
//      bicycle model (speed_kph > 0 from bicycle path).
//
// Test methodology:
//   * Arrange / Act / Assert. No sleep_for — deterministic dt steps.
//   * Uses cd::physics::make_builtin_physics_world() so no Jolt port is
//     required for this file to link. The GTEST_SKIP guard documents the
//     future intent clearly.
// =============================================================================

#include <cd/physics/vehicle/JoltAdapter.hpp>
#include <cd/physics/vehicle/Vehicle.hpp>
#include <cd/physics/IPhysicsWorld.hpp>
#include <cd/physics_jolt/JoltWorld.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

namespace
{

using cd::physics::vehicle::JoltAdapter;
using cd::physics::vehicle::Vehicle;
using cd::physics::vehicle::VehicleConfig;
using cd::physics::vehicle::WheelConfig;
using cd::physics::vehicle::EngineConfig;

/// Standard RWD test car reused from test_vehicle.cpp for consistency.
VehicleConfig make_jolt_test_config(bool use_jolt = true)
{
    VehicleConfig cfg {};
    cfg.chassis_mass_kg    = 1200.0F;
    cfg.chassis_dimensions = { 2.3F, 0.95F, 0.65F };
    cfg.use_jolt           = use_jolt;

    WheelConfig front {};
    front.radius             = 0.32F;
    front.mass               = 20.0F;
    front.suspension_rest_length = 0.25F;
    front.suspension_stiffness   = 22000.0F;
    front.damping            = 3800.0F;
    front.steering_angle_max = 0.524F;
    front.is_driven          = false;

    WheelConfig rear {};
    rear.radius              = 0.32F;
    rear.mass                = 20.0F;
    rear.suspension_rest_length = 0.25F;
    rear.suspension_stiffness   = 22000.0F;
    rear.damping             = 3800.0F;
    rear.steering_angle_max  = 0.0F;
    rear.is_driven           = true;

    cfg.wheels = { front, front, rear, rear };

    EngineConfig eng {};
    eng.max_torque_nm = 320.0F;
    eng.gear_ratios   = { 3.5F, 2.1F, 1.4F, 1.0F, 0.8F, 0.67F };
    eng.final_drive   = 3.7F;
    eng.idle_rpm      = 800.0F;
    eng.max_rpm       = 6500.0F;
    cfg.engine        = eng;

    return cfg;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. JoltAdapter::configure() registers a chassis body in the world.
// ---------------------------------------------------------------------------
TEST(VehicleJolt, AdapterConfigureRegistersChassisBody)
{
    // Arrange
    auto world   = cd::physics::make_builtin_physics_world();
    ASSERT_NE(world, nullptr);
    ASSERT_EQ(world->body_count(), 0U);

    const VehicleConfig cfg = make_jolt_test_config();

    JoltAdapter adapter;
    EXPECT_FALSE(adapter.is_configured())
        << "Pre-condition: adapter must not be configured yet.";

    // Act
    const bool ok = adapter.configure(cfg, *world);

    // Assert
    EXPECT_TRUE(ok) << "configure() must return true for a valid VehicleConfig.";
    EXPECT_TRUE(adapter.is_configured());
    EXPECT_EQ(world->body_count(), 1U)
        << "Exactly one chassis body must be registered after configure().";

    // Verify wheel attachment descriptors were populated.
    const auto& wheels = adapter.wheel_descs();
    EXPECT_EQ(static_cast<int>(wheels.size()), 4);
    for (const auto& w : wheels)
    {
        EXPECT_TRUE(w.grounded)
            << "Sprint-2: all wheels grounded by default.";
    }
}

// ---------------------------------------------------------------------------
// 2. Vehicle::tick() with use_jolt=true progresses chassis position.
//
//    Stub-backend note: the inner Euler world integrates velocity with
//    gravity (-Y) and our drive impulse (+Z). After 60 ticks the chassis
//    body's Z-velocity should be non-zero (impulse was applied). We do NOT
//    assert a specific Jolt rigid-body result here — that belongs to the
//    real-backend sprint-3 test.  The point is: the plumbing compiles, runs,
//    and the VehicleState reflects a progressing simulation.
// ---------------------------------------------------------------------------
TEST(VehicleJolt, TickWithJoltFlagProgressesSpeed)
{
    // This test exercises the adapter plumbing. Skip with an informational
    // message when the real Jolt backend is not yet linked, to distinguish
    // "skip" from "fail" in CI output.
    if (cd::physics_jolt::is_stub_backend())
    {
        // Note: we do NOT GTEST_SKIP here because the stub world still
        // exercises the full plumbing (configure, sync_to, step, sync_from).
        // GTEST_SKIP is reserved for tests that physically require real Jolt
        // collision geometry (e.g. terrain normal queries, JPH::Shape contact).
        // This test passes even on the stub.
    }

    // Arrange
    auto world = cd::physics::make_builtin_physics_world();
    ASSERT_NE(world, nullptr);

    Vehicle v;
    v.configure(make_jolt_test_config(/*use_jolt=*/true));

    const bool armed = v.configure_jolt(*world);
    ASSERT_TRUE(armed) << "configure_jolt() must succeed with a live world.";

    v.set_input(/*throttle=*/1.0F, /*brake=*/0.0F, /*steer=*/0.0F);

    // Act — simulate ~1 second (60 frames at dt=1/60).
    constexpr float kDt = 1.0F / 60.0F;
    for (int i = 0; i < 60; ++i)
    {
        v.tick(kDt);
    }

    // Assert — the Jolt path should have pushed a non-zero drive impulse
    // and world->step() integrated it into the chassis velocity.
    // speed_kph is derived from world->linear_velocity() in sync_from_jolt.
    const float speed = v.state().speed_kph;
    EXPECT_GT(speed, 0.0F)
        << "Chassis speed must be positive after 60 ticks of full throttle "
           "via the JoltAdapter path.";
}

// ---------------------------------------------------------------------------
// 3. use_jolt=true without configure_jolt() falls back to the bicycle model.
// ---------------------------------------------------------------------------
TEST(VehicleJolt, UseJoltWithoutConfigureFallsBackToBicycle)
{
    // Arrange: use_jolt=true but configure_jolt() is never called.
    Vehicle v;
    v.configure(make_jolt_test_config(/*use_jolt=*/true));

    v.set_input(/*throttle=*/1.0F, /*brake=*/0.0F, /*steer=*/0.0F);

    // Act — simulate 2 seconds with bicycle model fallback.
    for (int i = 0; i < 20; ++i)
    {
        v.tick(0.1F);
    }

    // Assert — the bicycle model should still produce non-zero speed.
    EXPECT_GT(v.state().speed_kph, 0.0F)
        << "Bicycle model fallback must produce non-zero speed when "
           "configure_jolt() was never called.";
}
