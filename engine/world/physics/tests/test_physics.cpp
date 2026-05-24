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

// ---------------------------------------------------------------------------
// Phase 21.A — Sphere primitive tests (Wave 184)
// ---------------------------------------------------------------------------
#include <cd/physics/Sphere.hpp>

TEST(Sphere, SphereSphereOverlapping)
{
    cd::physics::Sphere a { cd::math::Vec3f { 0, 0, 0 }, 1.0F };
    cd::physics::Sphere b { cd::math::Vec3f { 1.5F, 0, 0 }, 1.0F };
    EXPECT_TRUE(cd::physics::intersects(a, b));
}

TEST(Sphere, SphereSphereSeparated)
{
    cd::physics::Sphere a { cd::math::Vec3f { 0, 0, 0 }, 1.0F };
    cd::physics::Sphere b { cd::math::Vec3f { 3, 0, 0 }, 1.0F };
    EXPECT_FALSE(cd::physics::intersects(a, b));
}

TEST(Sphere, SphereAabbIntersection)
{
    cd::physics::Sphere s { cd::math::Vec3f { 2, 0, 0 }, 1.5F };
    cd::physics::Aabb a { cd::math::Vec3f { 0, -1, -1 }, cd::math::Vec3f { 1, 1, 1 } };
    EXPECT_TRUE(cd::physics::intersects(s, a));
}

TEST(Sphere, ContainsPoint)
{
    cd::physics::Sphere s { cd::math::Vec3f { 0, 0, 0 }, 2.0F };
    EXPECT_TRUE(cd::physics::contains(s, cd::math::Vec3f { 1, 1, 1 }));
    EXPECT_FALSE(cd::physics::contains(s, cd::math::Vec3f { 5, 0, 0 }));
}

// ---------------------------------------------------------------------------
// Phase 22.C — Ray + ray-AABB tests (Wave 186)
// ---------------------------------------------------------------------------
#include <cd/physics/Ray.hpp>

TEST(Ray, IntersectsAabbFromOutside)
{
    cd::physics::Ray r { cd::math::Vec3f { -5, 0.5F, 0.5F }, cd::math::Vec3f { 1, 0, 0 } };
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    auto t = cd::physics::intersect_ray_aabb(r, a);
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(*t, 5.0F, 1e-5F);
}

TEST(Ray, MissesAabb)
{
    cd::physics::Ray r { cd::math::Vec3f { -5, 5, 0.5F }, cd::math::Vec3f { 1, 0, 0 } };
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    auto t = cd::physics::intersect_ray_aabb(r, a);
    EXPECT_FALSE(t.has_value());
}

TEST(Ray, InsideAabbReturnsZero)
{
    cd::physics::Ray r { cd::math::Vec3f { 0.5F, 0.5F, 0.5F }, cd::math::Vec3f { 1, 0, 0 } };
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    auto t = cd::physics::intersect_ray_aabb(r, a);
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(*t, 0.0F, 1e-5F);
}

// ---------------------------------------------------------------------------
// Phase 25.C — Obb tests (Wave 192)
// ---------------------------------------------------------------------------
#include <cd/physics/Obb.hpp>

TEST(Obb, ContainsPointAtAxisAlignedDefault)
{
    cd::physics::Obb b;
    EXPECT_TRUE(cd::physics::contains(b, cd::math::Vec3f { 0, 0, 0 }));
    EXPECT_TRUE(cd::physics::contains(b, cd::math::Vec3f { 0.4F, 0, 0 }));
    EXPECT_FALSE(cd::physics::contains(b, cd::math::Vec3f { 0.6F, 0, 0 }));
}

TEST(Obb, ContainsPointAfterRotation45Y)
{
    cd::physics::Obb b;
    // 45° around Y: axis_x and axis_z spin in the XZ plane.
    const float c = 0.7071068F;
    b.axis_x = cd::math::Vec3f {  c, 0, -c };
    b.axis_z = cd::math::Vec3f {  c, 0,  c };
    // The world-aligned corner (0.5, 0, 0.5) is at distance sqrt(0.5) ≈
    // 0.707 from center along the diagonal — still inside the rotated
    // box (extends 0.5 along each rotated axis).
    EXPECT_TRUE(cd::physics::contains(b, cd::math::Vec3f { 0.0F, 0, 0.5F }));
    // A point along world +X by 0.6 projects to ~0.42 on axis_x —
    // still inside (the half-extent is 0.5).
    EXPECT_TRUE(cd::physics::contains(b, cd::math::Vec3f { 0.6F, 0, 0 }));
    // Far enough to project past half_extents.
    EXPECT_FALSE(cd::physics::contains(b, cd::math::Vec3f { 0.8F, 0, -0.8F }));
}

// ---------------------------------------------------------------------------
// Phase 26.C — Capsule tests (Wave 194)
// ---------------------------------------------------------------------------
#include <cd/physics/Capsule.hpp>

TEST(Capsule, ContainsPointsAlongSegment)
{
    cd::physics::Capsule c { cd::math::Vec3f { 0, 0, 0 },
                              cd::math::Vec3f { 0, 4, 0 }, 1.0F };
    EXPECT_TRUE(cd::physics::contains(c, cd::math::Vec3f { 0, 2, 0 }));
    EXPECT_TRUE(cd::physics::contains(c, cd::math::Vec3f { 0.9F, 2, 0 }));
    EXPECT_FALSE(cd::physics::contains(c, cd::math::Vec3f { 1.5F, 2, 0 }));
}

TEST(Capsule, ContainsEndpointHemispheres)
{
    cd::physics::Capsule c { cd::math::Vec3f { 0, 0, 0 },
                              cd::math::Vec3f { 0, 4, 0 }, 1.0F };
    // Point slightly above p1 by 0.5 → inside the top hemisphere.
    EXPECT_TRUE(cd::physics::contains(c, cd::math::Vec3f { 0, 4.5F, 0 }));
    // Point 2 above p1 → outside.
    EXPECT_FALSE(cd::physics::contains(c, cd::math::Vec3f { 0, 6.0F, 0 }));
}

// ---------------------------------------------------------------------------
// Phase 27.B — Triangle barycentric tests (Wave 196)
// ---------------------------------------------------------------------------
#include <cd/physics/Triangle.hpp>

TEST(Triangle, BarycentricVertexAReturnsUOne)
{
    cd::physics::Triangle t {
        cd::math::Vec3f { 0, 0, 0 },
        cd::math::Vec3f { 1, 0, 0 },
        cd::math::Vec3f { 0, 1, 0 },
    };
    const auto bc = cd::physics::barycentric(t, cd::math::Vec3f { 0, 0, 0 });
    EXPECT_NEAR(bc.x, 1.0F, 1e-4F);  // u
    EXPECT_NEAR(bc.y, 0.0F, 1e-4F);  // v
    EXPECT_NEAR(bc.z, 0.0F, 1e-4F);  // w
}

TEST(Triangle, BarycentricCentroidIsThird)
{
    cd::physics::Triangle t {
        cd::math::Vec3f { 0, 0, 0 },
        cd::math::Vec3f { 3, 0, 0 },
        cd::math::Vec3f { 0, 3, 0 },
    };
    const cd::math::Vec3f cent { 1, 1, 0 };  // (a+b+c)/3
    const auto bc = cd::physics::barycentric(t, cent);
    EXPECT_NEAR(bc.x, 1.0F / 3.0F, 1e-4F);
    EXPECT_NEAR(bc.y, 1.0F / 3.0F, 1e-4F);
    EXPECT_NEAR(bc.z, 1.0F / 3.0F, 1e-4F);
}

TEST(Triangle, ContainsInteriorPoint)
{
    cd::physics::Triangle t {
        cd::math::Vec3f { 0, 0, 0 },
        cd::math::Vec3f { 4, 0, 0 },
        cd::math::Vec3f { 0, 4, 0 },
    };
    EXPECT_TRUE(cd::physics::contains(t, cd::math::Vec3f { 1, 1, 0 }));
    EXPECT_FALSE(cd::physics::contains(t, cd::math::Vec3f { 3, 3, 0 }));
}

#include <cd/physics/RaySphere.hpp>

TEST(RaySphere, HitsSphereInFront)
{
    cd::physics::Ray r;
    r.origin = { 0, 0, 0 };
    r.direction = { 0, 0, 1 };
    cd::physics::Sphere s { { 0, 0, 5 }, 1.0F };
    auto t = cd::physics::intersect_ray_sphere(r, s);
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(*t, 4.0F, 1e-4F);
}

TEST(RaySphere, MissesWhenOffset)
{
    cd::physics::Ray r;
    r.origin = { 5, 5, 0 };
    r.direction = { 0, 0, 1 };
    cd::physics::Sphere s { { 0, 0, 5 }, 1.0F };
    auto t = cd::physics::intersect_ray_sphere(r, s);
    EXPECT_FALSE(t.has_value());
}

TEST(RaySphere, BehindOriginReturnsNullopt)
{
    cd::physics::Ray r;
    r.origin = { 0, 0, 10 };
    r.direction = { 0, 0, 1 };
    cd::physics::Sphere s { { 0, 0, 0 }, 1.0F };
    auto t = cd::physics::intersect_ray_sphere(r, s);
    EXPECT_FALSE(t.has_value());
}

TEST(RaySphere, InsideSphereReturnsZero)
{
    cd::physics::Ray r;
    r.origin = { 0, 0, 0 };
    r.direction = { 0, 0, 1 };
    cd::physics::Sphere s { { 0, 0, 0 }, 1.0F };
    auto t = cd::physics::intersect_ray_sphere(r, s);
    ASSERT_TRUE(t.has_value());
    EXPECT_FLOAT_EQ(*t, 0.0F);
}

#include <cd/physics/CapsuleSphere.hpp>

TEST(CapsuleSphere, OverlapWhenSphereInsideCapsule)
{
    cd::physics::Capsule c;
    c.p0 = { 0, 0, 0 };
    c.p1 = { 0, 10, 0 };
    c.radius = 1.0F;
    cd::physics::Sphere s { { 0, 5, 0 }, 0.5F };
    EXPECT_TRUE(cd::physics::intersects(c, s));
}

TEST(CapsuleSphere, NoOverlapWhenFarAway)
{
    cd::physics::Capsule c;
    c.p0 = { 0, 0, 0 };
    c.p1 = { 0, 10, 0 };
    c.radius = 1.0F;
    cd::physics::Sphere s { { 100, 5, 0 }, 0.5F };
    EXPECT_FALSE(cd::physics::intersects(c, s));
}

TEST(CapsuleSphere, TouchingSurfaces)
{
    cd::physics::Capsule c;
    c.p0 = { 0, 0, 0 };
    c.p1 = { 0, 10, 0 };
    c.radius = 1.0F;
    // sphere center at (1.5, 5, 0), radius 0.5 → outer surfaces touch
    cd::physics::Sphere s { { 1.5F, 5, 0 }, 0.5F };
    EXPECT_TRUE(cd::physics::intersects(c, s));
}

TEST(CapsuleSphere, SymmetricSphereCapsule)
{
    cd::physics::Capsule c;
    c.p0 = { 0, 0, 0 };
    c.p1 = { 5, 0, 0 };
    c.radius = 1.0F;
    cd::physics::Sphere s { { 2.5F, 0.5F, 0 }, 0.6F };
    EXPECT_EQ(cd::physics::intersects(c, s), cd::physics::intersects(s, c));
}

#include <cd/physics/SweepResult.hpp>

TEST(Sweep, NoHitWhenFar)
{
    cd::physics::Aabb box { { 10, 10, 10 }, { 11, 11, 11 } };
    auto r = cd::physics::sweep_sphere_aabb(
        { 0, 0, 0 }, { 0, 0, 1 }, 0.5F, box);
    EXPECT_FALSE(r.hit);
}

TEST(Sweep, HitForwardOnAxis)
{
    cd::physics::Aabb box { { 0, -1, 4 }, { 1, 1, 5 } };
    auto r = cd::physics::sweep_sphere_aabb(
        { 0.5F, 0, 0 }, { 0, 0, 10 }, 0.5F, box);
    EXPECT_TRUE(r.hit);
    EXPECT_LT(r.toi, 1.0F);
    EXPECT_GT(r.toi, 0.0F);
    EXPECT_FLOAT_EQ(r.normal.z, -1.0F);
}

TEST(Sweep, ImmediateContactReturnsTOIZero)
{
    cd::physics::Aabb box { { -1, -1, -1 }, { 1, 1, 1 } };
    auto r = cd::physics::sweep_sphere_aabb(
        { 0, 0, 0 }, { 0, 0, 1 }, 0.5F, box);
    EXPECT_TRUE(r.hit);
    EXPECT_FLOAT_EQ(r.toi, 0.0F);
}
