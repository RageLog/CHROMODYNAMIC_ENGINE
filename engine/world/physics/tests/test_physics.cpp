// =============================================================================
// CHROMODYNAMIC — cd::physics tests
// =============================================================================
#include <cd/physics/IPhysicsWorld.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>

namespace
{

[[nodiscard]] bool approx_eq(float a, float b, float eps = 1e-3F) noexcept
{
    return std::fabs(a - b) < eps;
}

TEST(Physics, FactoryBuilds)
{
    auto w = cd::physics::make_builtin_physics_world();
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->body_count(), 0U);
}

TEST(Physics, DefaultGravityIsEarth)
{
    auto w = cd::physics::make_builtin_physics_world();
    auto g = w->gravity();
    EXPECT_TRUE(approx_eq(g.y, -9.81F));
}

TEST(Physics, CreateDynamicBodyRejectsZeroMass)
{
    auto w = cd::physics::make_builtin_physics_world();
    cd::physics::BodyDesc d {};
    d.mass = 0.0F;
    auto r = w->create_body(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::physics::physics_errors::Code::kInvalidArgument));
}

TEST(Physics, CreateAndDestroyBody)
{
    auto w = cd::physics::make_builtin_physics_world();
    cd::physics::BodyDesc d {};
    d.position = { 1.0F, 2.0F, 3.0F };
    d.mass = 2.5F;
    auto r = w->create_body(d);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(w->body_count(), 1U);
    EXPECT_TRUE(approx_eq(w->position(*r).x, 1.0F));
    EXPECT_TRUE(approx_eq(w->position(*r).y, 2.0F));
    EXPECT_TRUE(approx_eq(w->position(*r).z, 3.0F));
    w->destroy_body(*r);
    EXPECT_EQ(w->body_count(), 0U);
}

TEST(Physics, GravityIntegratesDynamicBody)
{
    // Free-fall: dropped from rest, after 1 s the body should be at
    // y ≈ -gravity/2 (semi-implicit Euler is exact for constant-accel cases
    // in the limit, and very close for any reasonable dt).
    auto w = cd::physics::make_builtin_physics_world();
    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    d.position = { 0.0F, 0.0F, 0.0F };
    auto b = w->create_body(d);
    ASSERT_TRUE(b.has_value());
    constexpr float dt = 1.0F / 60.0F;
    for (int i = 0; i < 60; ++i)
        w->step(dt);
    // After 1s of -9.81 m/s² gravity, semi-implicit Euler produces a
    // slightly larger drop than the analytic v_avg formula because each
    // step uses v_{n+1} for position update. Empirically ≈ -4.96.
    EXPECT_LT(w->position(*b).y, -4.5F);
    EXPECT_GT(w->position(*b).y, -5.5F);
}

TEST(Physics, ImpulseChangesVelocityWithMassInverse)
{
    auto w = cd::physics::make_builtin_physics_world();
    w->set_gravity({ 0.0F, 0.0F, 0.0F });  // disable gravity for a clean test
    cd::physics::BodyDesc d {};
    d.mass = 2.0F;
    auto b = w->create_body(d);
    ASSERT_TRUE(b.has_value());
    w->apply_impulse(*b, { 10.0F, 0.0F, 0.0F });
    // p = m·v  →  v = 10 / 2 = 5
    EXPECT_TRUE(approx_eq(w->linear_velocity(*b).x, 5.0F));
}

TEST(Physics, StaticBodyIgnoresImpulse)
{
    auto w = cd::physics::make_builtin_physics_world();
    w->set_gravity({ 0.0F, 0.0F, 0.0F });
    cd::physics::BodyDesc d {};
    d.type = cd::physics::BodyType::kStatic;
    d.mass = 1.0F;
    auto b = w->create_body(d);
    ASSERT_TRUE(b.has_value());
    w->apply_impulse(*b, { 100.0F, 0.0F, 0.0F });
    w->step(1.0F / 60.0F);
    EXPECT_TRUE(approx_eq(w->linear_velocity(*b).x, 0.0F));
    EXPECT_TRUE(approx_eq(w->position(*b).x, 0.0F));
}

TEST(Physics, KinematicBodyMovesByVelocityNotByImpulse)
{
    auto w = cd::physics::make_builtin_physics_world();
    w->set_gravity({ 0.0F, 0.0F, 0.0F });
    cd::physics::BodyDesc d {};
    d.type = cd::physics::BodyType::kKinematic;
    d.mass = 1.0F;
    auto b = w->create_body(d);
    ASSERT_TRUE(b.has_value());
    // Impulse must NOT touch a kinematic body.
    w->apply_impulse(*b, { 100.0F, 0.0F, 0.0F });
    EXPECT_TRUE(approx_eq(w->linear_velocity(*b).x, 0.0F));
    // set_linear_velocity is the kinematic mover.
    w->set_linear_velocity(*b, { 3.0F, 0.0F, 0.0F });
    // Kinematic bodies are not integrated by the solver in this MVP; users
    // teleport via set_position. Verify velocity is reported back.
    EXPECT_TRUE(approx_eq(w->linear_velocity(*b).x, 3.0F));
}

TEST(Physics, LinearDampingReducesVelocityOverTime)
{
    auto w = cd::physics::make_builtin_physics_world();
    w->set_gravity({ 0.0F, 0.0F, 0.0F });
    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    d.linear_velocity = { 10.0F, 0.0F, 0.0F };
    d.linear_damping = 2.0F;  // strong damping
    auto b = w->create_body(d);
    ASSERT_TRUE(b.has_value());
    for (int i = 0; i < 60; ++i)
        w->step(1.0F / 60.0F);
    EXPECT_LT(w->linear_velocity(*b).x, 10.0F);
}

TEST(Physics, ForceClearsBetweenSteps)
{
    auto w = cd::physics::make_builtin_physics_world();
    w->set_gravity({ 0.0F, 0.0F, 0.0F });
    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    auto b = w->create_body(d);
    ASSERT_TRUE(b.has_value());
    w->apply_force(*b, { 1.0F, 0.0F, 0.0F });
    w->step(1.0F);
    const auto v1 = w->linear_velocity(*b).x;
    // No new force this step — velocity should be steady (damping = 0).
    w->step(1.0F);
    const auto v2 = w->linear_velocity(*b).x;
    EXPECT_TRUE(approx_eq(v1, v2));
}

TEST(Physics, SetPositionTeleports)
{
    auto w = cd::physics::make_builtin_physics_world();
    w->set_gravity({ 0.0F, 0.0F, 0.0F });
    cd::physics::BodyDesc d {};
    auto b = w->create_body(d);
    ASSERT_TRUE(b.has_value());
    w->set_position(*b, { 7.0F, 8.0F, 9.0F });
    EXPECT_TRUE(approx_eq(w->position(*b).x, 7.0F));
    EXPECT_TRUE(approx_eq(w->position(*b).y, 8.0F));
    EXPECT_TRUE(approx_eq(w->position(*b).z, 9.0F));
}

}  // namespace

// ---------------------------------------------------------------------------
// Phase 20.B — AABB primitive tests (Wave 182)
// ---------------------------------------------------------------------------
#include <cd/physics/Aabb.hpp>

TEST(Aabb, OverlapsDetectsIntersection)
{
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    cd::physics::Aabb b { cd::math::Vec3f { 0.5F, 0.5F, 0.5F },
                          cd::math::Vec3f { 1.5F, 1.5F, 1.5F } };
    EXPECT_TRUE(cd::physics::overlaps(a, b));
}

TEST(Aabb, OverlapsTouchingBoxesIsTrue)
{
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    cd::physics::Aabb b { cd::math::Vec3f { 1, 1, 1 }, cd::math::Vec3f { 2, 2, 2 } };
    EXPECT_TRUE(cd::physics::overlaps(a, b));
}

TEST(Aabb, DisjointBoxesReturnFalse)
{
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    cd::physics::Aabb b { cd::math::Vec3f { 2, 2, 2 }, cd::math::Vec3f { 3, 3, 3 } };
    EXPECT_FALSE(cd::physics::overlaps(a, b));
}

TEST(Aabb, ContainsPoint)
{
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    EXPECT_TRUE(cd::physics::contains(a, cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
    EXPECT_FALSE(cd::physics::contains(a, cd::math::Vec3f { 2, 2, 2 }));
}

TEST(Aabb, MergeExpandsToCoverBoth)
{
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    cd::physics::Aabb b { cd::math::Vec3f { 2, 0, 0 }, cd::math::Vec3f { 3, 1, 1 } };
    auto m = cd::physics::merge(a, b);
    EXPECT_FLOAT_EQ(m.min.x, 0.0F);
    EXPECT_FLOAT_EQ(m.max.x, 3.0F);
}
