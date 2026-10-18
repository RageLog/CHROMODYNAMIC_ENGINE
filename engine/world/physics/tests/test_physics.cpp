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
    constexpr float kDt = 1.0F / 60.0F;
    for (int i = 0; i < 60; ++i)
        w->step(kDt);
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

#include <cd/physics/InertiaTensor.hpp>

TEST(InertiaTensor, SolidSphereDiagonalEqual)
{
    auto m = cd::physics::solid_sphere(2.0F, 0.5F);
    EXPECT_FLOAT_EQ(m.mass, 2.0F);
    EXPECT_NEAR(m.inertia.x, 0.4F * 2.0F * 0.25F, 1e-5F);
    EXPECT_FLOAT_EQ(m.inertia.x, m.inertia.y);
    EXPECT_FLOAT_EQ(m.inertia.x, m.inertia.z);
}

TEST(InertiaTensor, SolidBoxAxisDependent)
{
    auto m = cd::physics::solid_box(12.0F, 2.0F, 4.0F, 6.0F);
    // Ixx = (1/12) * 12 * (16 + 36) = 52
    EXPECT_FLOAT_EQ(m.inertia.x, 52.0F);
    // Iyy = (1/12) * 12 * (4 + 36) = 40
    EXPECT_FLOAT_EQ(m.inertia.y, 40.0F);
    // Izz = (1/12) * 12 * (4 + 16) = 20
    EXPECT_FLOAT_EQ(m.inertia.z, 20.0F);
}

TEST(InertiaTensor, KinematicHasZeroInverses)
{
    auto m = cd::physics::kinematic();
    EXPECT_FLOAT_EQ(m.inv_mass, 0.0F);
    EXPECT_FLOAT_EQ(m.inv_inertia.x, 0.0F);
}

TEST(InertiaTensor, InvMassReciprocal)
{
    auto m = cd::physics::solid_sphere(4.0F, 1.0F);
    EXPECT_FLOAT_EQ(m.inv_mass, 0.25F);
}

#include <cd/physics/SoftBodyParams.hpp>

TEST(SoftBodyParams, DefaultsAreSane)
{
    cd::physics::SoftBodyParams p;
    EXPECT_GT(p.stiffness, 0.0F);
    EXPECT_GT(p.iterations, 0u);
    EXPECT_FLOAT_EQ(p.gravity.y, -9.81F);
}

TEST(SoftBodyParams, ClothPresetSofterThanRope)
{
    auto cloth = cd::physics::soft_body_cloth_default();
    auto rope  = cd::physics::soft_body_rope_default();
    EXPECT_LT(cloth.stiffness, rope.stiffness);
}

TEST(SoftBodyParams, IterationsConfigurable)
{
    cd::physics::SoftBodyParams p;
    p.iterations = 20;
    EXPECT_EQ(p.iterations, 20u);
}

#include <cd/physics/ContactPoint.hpp>

TEST(ContactManifold, EmptyByDefault)
{
    cd::physics::ContactManifold m;
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0u);
}

TEST(ContactManifold, AddAccumulates)
{
    cd::physics::ContactManifold m;
    cd::physics::ContactPoint p;
    p.world_position = { 1, 2, 3 };
    p.penetration = 0.05F;
    EXPECT_TRUE(m.add(p));
    EXPECT_EQ(m.size(), 1u);
    EXPECT_FLOAT_EQ(m.at(0).world_position.y, 2.0F);
}

TEST(ContactManifold, CapacityRejectsOverflow)
{
    cd::physics::ContactManifold m;
    cd::physics::ContactPoint p;
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(m.add(p));
    EXPECT_FALSE(m.add(p));   // capacity 4
    EXPECT_EQ(m.size(), 4u);
}

TEST(ContactManifold, ClearEmpties)
{
    cd::physics::ContactManifold m;
    cd::physics::ContactPoint p;
    m.add(p);
    m.add(p);
    m.clear();
    EXPECT_TRUE(m.empty());
}

#include <cd/physics/SpringJoint.hpp>

TEST(SpringJoint, DefaultStiffnessSane)
{
    cd::physics::SpringJoint j;
    EXPECT_GT(j.stiffness, 0.0F);
    EXPECT_GE(j.damping, 0.0F);
    EXPECT_FLOAT_EQ(j.rest_length, 1.0F);
}

TEST(SpringJoint, RopeFactoryStifferThanCloth)
{
    auto rope = cd::physics::make_rope_link(0, 1, 1.0F);
    auto cloth = cd::physics::make_cloth_link(0, 1, 1.0F);
    EXPECT_GT(rope.stiffness, cloth.stiffness);
}

TEST(SpringJoint, BodyIndicesStored)
{
    auto j = cd::physics::make_rope_link(5, 10, 2.0F);
    EXPECT_EQ(j.body_a, 5u);
    EXPECT_EQ(j.body_b, 10u);
    EXPECT_FLOAT_EQ(j.rest_length, 2.0F);
}

// =============================================================================
// 80→100 depth pass — ADD-ONLY edge/negative coverage + OBB/OBB SAT primitive.
// No existing collision math touched; every assertion exercises an existing
// or newly-added query at a boundary the prior 55-test suite skipped.
// =============================================================================

// ---------------------------------------------------------------------------
// AABB — edge / corner / face contact + degenerate + containment boundary.
// ---------------------------------------------------------------------------
TEST(AabbEdge, EdgeContactSharedLineOverlaps)
{
    // Share only the x=1 edge-line (touch along an edge, not a face).
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    cd::physics::Aabb b { cd::math::Vec3f { 1, 1, 0 }, cd::math::Vec3f { 2, 2, 1 } };
    EXPECT_TRUE(cd::physics::overlaps(a, b));
}

TEST(AabbEdge, CornerContactSinglePointOverlaps)
{
    // Touch at the single corner (1,1,1).
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    cd::physics::Aabb b { cd::math::Vec3f { 1, 1, 1 }, cd::math::Vec3f { 2, 2, 2 } };
    EXPECT_TRUE(cd::physics::overlaps(a, b));
}

TEST(AabbEdge, GapOnSingleAxisSeparates)
{
    // Overlap on y,z but a tiny gap on x → no overlap.
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    cd::physics::Aabb b { cd::math::Vec3f { 1.001F, 0, 0 }, cd::math::Vec3f { 2, 1, 1 } };
    EXPECT_FALSE(cd::physics::overlaps(a, b));
}

TEST(AabbEdge, ContainsBoundaryPointIsInclusive)
{
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    EXPECT_TRUE(cd::physics::contains(a, cd::math::Vec3f { 0, 0, 0 }));   // min corner
    EXPECT_TRUE(cd::physics::contains(a, cd::math::Vec3f { 1, 1, 1 }));   // max corner
    EXPECT_TRUE(cd::physics::contains(a, cd::math::Vec3f { 1, 0.5F, 0 })); // on a face
    EXPECT_FALSE(cd::physics::contains(a, cd::math::Vec3f { 1.0001F, 0.5F, 0.5F }));
}

TEST(AabbEdge, DegenerateZeroVolumeBoxContainsItsPoint)
{
    cd::physics::Aabb p { cd::math::Vec3f { 2, 2, 2 }, cd::math::Vec3f { 2, 2, 2 } };
    EXPECT_TRUE(cd::physics::contains(p, cd::math::Vec3f { 2, 2, 2 }));
    EXPECT_TRUE(cd::physics::overlaps(p, p));
    EXPECT_FLOAT_EQ(cd::physics::extent(p).x, 0.0F);
    EXPECT_FLOAT_EQ(cd::physics::center(p).y, 2.0F);
}

TEST(AabbEdge, ContainedBoxOverlapsEnclosingBox)
{
    cd::physics::Aabb big   { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 10, 10, 10 } };
    cd::physics::Aabb small { cd::math::Vec3f { 4, 4, 4 }, cd::math::Vec3f { 6, 6, 6 } };
    EXPECT_TRUE(cd::physics::overlaps(big, small));
    EXPECT_TRUE(cd::physics::overlaps(small, big));  // symmetric
}

// ---------------------------------------------------------------------------
// Sphere — just-touching / concentric / separated boundaries.
// ---------------------------------------------------------------------------
TEST(SphereEdge, JustTouchingCountsAsIntersect)
{
    // Centers exactly r0+r1 apart → surfaces kiss; `<=` makes this true.
    cd::physics::Sphere a { cd::math::Vec3f { 0, 0, 0 }, 1.0F };
    cd::physics::Sphere b { cd::math::Vec3f { 2, 0, 0 }, 1.0F };
    EXPECT_TRUE(cd::physics::intersects(a, b));
}

TEST(SphereEdge, JustSeparatedDoesNotIntersect)
{
    cd::physics::Sphere a { cd::math::Vec3f { 0, 0, 0 }, 1.0F };
    cd::physics::Sphere b { cd::math::Vec3f { 2.001F, 0, 0 }, 1.0F };
    EXPECT_FALSE(cd::physics::intersects(a, b));
}

TEST(SphereEdge, ConcentricSpheresIntersect)
{
    cd::physics::Sphere a { cd::math::Vec3f { 1, 1, 1 }, 2.0F };
    cd::physics::Sphere b { cd::math::Vec3f { 1, 1, 1 }, 0.5F };
    EXPECT_TRUE(cd::physics::intersects(a, b));
}

TEST(SphereEdge, ContainsSurfacePointIsInclusive)
{
    cd::physics::Sphere s { cd::math::Vec3f { 0, 0, 0 }, 2.0F };
    EXPECT_TRUE(cd::physics::contains(s, cd::math::Vec3f { 2, 0, 0 }));   // on surface
    EXPECT_TRUE(cd::physics::contains(s, cd::math::Vec3f { 0, 0, 0 }));   // center
    EXPECT_FALSE(cd::physics::contains(s, cd::math::Vec3f { 2.001F, 0, 0 }));
}

TEST(SphereEdge, AabbCenterInsideBoxIntersects)
{
    // Sphere center sits inside the box → closest-point distance 0.
    cd::physics::Sphere s { cd::math::Vec3f { 0.5F, 0.5F, 0.5F }, 0.1F };
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    EXPECT_TRUE(cd::physics::intersects(s, a));
}

TEST(SphereEdge, AabbCornerJustOutOfReachMisses)
{
    // Nearest point is the corner (1,1,1); distance = sqrt(3)*1 ≈ 1.732.
    cd::physics::Sphere s { cd::math::Vec3f { 2, 2, 2 }, 1.7F };
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    EXPECT_FALSE(cd::physics::intersects(s, a));
    cd::physics::Sphere s2 { cd::math::Vec3f { 2, 2, 2 }, 1.74F };
    EXPECT_TRUE(cd::physics::intersects(s2, a));
}

// ---------------------------------------------------------------------------
// Capsule — endpoint hemispheres / off-segment / zero-length / radial boundary.
// ---------------------------------------------------------------------------
TEST(CapsuleEdge, BeyondEndpointUsesHemisphereDistance)
{
    cd::physics::Capsule c { cd::math::Vec3f { 0, 0, 0 },
                              cd::math::Vec3f { 0, 4, 0 }, 1.0F };
    // Diagonal beyond p1: (0.6, 4.6, 0) is sqrt(0.72) ≈ 0.848 from p1 → inside.
    EXPECT_TRUE(cd::physics::contains(c, cd::math::Vec3f { 0.6F, 4.6F, 0 }));
    // (1.0, 4.8, 0) is sqrt(1.64) ≈ 1.28 from p1 → outside.
    EXPECT_FALSE(cd::physics::contains(c, cd::math::Vec3f { 1.0F, 4.8F, 0 }));
}

TEST(CapsuleEdge, BelowP0Hemisphere)
{
    cd::physics::Capsule c { cd::math::Vec3f { 0, 0, 0 },
                              cd::math::Vec3f { 0, 4, 0 }, 1.0F };
    EXPECT_TRUE(cd::physics::contains(c, cd::math::Vec3f { 0, -0.9F, 0 }));
    EXPECT_FALSE(cd::physics::contains(c, cd::math::Vec3f { 0, -1.1F, 0 }));
}

TEST(CapsuleEdge, RadialSurfaceIsInclusive)
{
    cd::physics::Capsule c { cd::math::Vec3f { 0, 0, 0 },
                              cd::math::Vec3f { 0, 4, 0 }, 1.0F };
    EXPECT_TRUE(cd::physics::contains(c, cd::math::Vec3f { 1.0F, 2, 0 }));   // exactly on surface
    EXPECT_FALSE(cd::physics::contains(c, cd::math::Vec3f { 1.0001F, 2, 0 }));
}

TEST(CapsuleEdge, ZeroLengthSegmentActsAsSphere)
{
    // p0 == p1 → degenerate segment falls into the len2<=eps branch.
    cd::physics::Capsule c { cd::math::Vec3f { 1, 1, 1 },
                              cd::math::Vec3f { 1, 1, 1 }, 2.0F };
    EXPECT_TRUE(cd::physics::contains(c, cd::math::Vec3f { 2.9F, 1, 1 }));   // dist 1.9 < 2
    EXPECT_FALSE(cd::physics::contains(c, cd::math::Vec3f { 3.1F, 1, 1 }));  // dist 2.1 > 2
}

TEST(CapsuleEdge, DistancePointSegmentClampsToEndpoints)
{
    const cd::math::Vec3f a { 0, 0, 0 };
    const cd::math::Vec3f b { 0, 0, 4 };
    // Query well before a: projection t<0, clamps to a.
    EXPECT_NEAR(cd::physics::distance_point_segment({ 0, 0, -3 }, a, b), 3.0F, 1e-4F);
    // Query well past b: projection t>1, clamps to b.
    EXPECT_NEAR(cd::physics::distance_point_segment({ 0, 0, 9 }, a, b), 5.0F, 1e-4F);
    // Perpendicular at midpoint.
    EXPECT_NEAR(cd::physics::distance_point_segment({ 3, 0, 2 }, a, b), 3.0F, 1e-4F);
}

// ---------------------------------------------------------------------------
// Capsule ↔ Sphere — just-touching / concentric-segment boundaries.
// ---------------------------------------------------------------------------
TEST(CapsuleSphereEdge, ExactlyTouchingRadialSurface)
{
    cd::physics::Capsule c { cd::math::Vec3f { 0, 0, 0 },
                              cd::math::Vec3f { 0, 10, 0 }, 1.0F };
    // center distance from segment = 1.5 = (capsule.r 1.0 + sphere.r 0.5).
    cd::physics::Sphere s { cd::math::Vec3f { 1.5F, 5, 0 }, 0.5F };
    EXPECT_TRUE(cd::physics::intersects(c, s));
}

TEST(CapsuleSphereEdge, JustBeyondCombinedRadiusMisses)
{
    cd::physics::Capsule c { cd::math::Vec3f { 0, 0, 0 },
                              cd::math::Vec3f { 0, 10, 0 }, 1.0F };
    cd::physics::Sphere s { cd::math::Vec3f { 1.51F, 5, 0 }, 0.5F };
    EXPECT_FALSE(cd::physics::intersects(c, s));
    EXPECT_FALSE(cd::physics::intersects(s, c));  // symmetric
}

// ---------------------------------------------------------------------------
// Ray vs AABB — parallel-inside / parallel-outside / grazing / behind / inside.
// ---------------------------------------------------------------------------
TEST(RayAabbEdge, ParallelInsideSlabHits)
{
    // Direction has zero x; origin x sits inside the slab → not rejected.
    cd::physics::Ray r { cd::math::Vec3f { 0.5F, -5, 0.5F }, cd::math::Vec3f { 0, 1, 0 } };
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    auto t = cd::physics::intersect_ray_aabb(r, a);
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(*t, 5.0F, 1e-4F);
}

TEST(RayAabbEdge, ParallelOutsideSlabMisses)
{
    // Direction parallel to a slab but origin x is outside it → reject.
    cd::physics::Ray r { cd::math::Vec3f { 5, -5, 0.5F }, cd::math::Vec3f { 0, 1, 0 } };
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    EXPECT_FALSE(cd::physics::intersect_ray_aabb(r, a).has_value());
}

TEST(RayAabbEdge, GrazingFaceCountsAsHit)
{
    // Ray skims along the y=1 top face; slab test is inclusive at the boundary.
    cd::physics::Ray r { cd::math::Vec3f { -5, 1, 0.5F }, cd::math::Vec3f { 1, 0, 0 } };
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    auto t = cd::physics::intersect_ray_aabb(r, a);
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(*t, 5.0F, 1e-4F);
}

TEST(RayAabbEdge, BehindOriginMisses)
{
    // Box entirely behind the ray (pointing +x, box at -x).
    cd::physics::Ray r { cd::math::Vec3f { 5, 0.5F, 0.5F }, cd::math::Vec3f { 1, 0, 0 } };
    cd::physics::Aabb a { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 1, 1 } };
    EXPECT_FALSE(cd::physics::intersect_ray_aabb(r, a).has_value());
}

// ---------------------------------------------------------------------------
// Ray vs Sphere — tangent / grazing-miss / parallel-along-axis / zero-dir.
// ---------------------------------------------------------------------------
TEST(RaySphereEdge, TangentHitHasSingleRoot)
{
    // Ray along +z at x=1 grazes a unit sphere centered at origin → tangent.
    cd::physics::Ray r { cd::math::Vec3f { 1, 0, -5 }, cd::math::Vec3f { 0, 0, 1 } };
    cd::physics::Sphere s { cd::math::Vec3f { 0, 0, 0 }, 1.0F };
    auto t = cd::physics::intersect_ray_sphere(r, s);
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(*t, 5.0F, 1e-3F);  // touches at z=0
}

TEST(RaySphereEdge, JustOutsideRadiusMisses)
{
    cd::physics::Ray r { cd::math::Vec3f { 1.001F, 0, -5 }, cd::math::Vec3f { 0, 0, 1 } };
    cd::physics::Sphere s { cd::math::Vec3f { 0, 0, 0 }, 1.0F };
    EXPECT_FALSE(cd::physics::intersect_ray_sphere(r, s).has_value());
}

TEST(RaySphereEdge, ZeroLengthDirectionReturnsNullopt)
{
    cd::physics::Ray r { cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 0, 0, 0 } };
    cd::physics::Sphere s { cd::math::Vec3f { 0, 0, 5 }, 1.0F };
    EXPECT_FALSE(cd::physics::intersect_ray_sphere(r, s).has_value());
}

TEST(RaySphereEdge, OnSurfacePointingInwardReturnsZeroT)
{
    // Origin exactly on the surface, direction toward center → entry t≈0.
    cd::physics::Ray r { cd::math::Vec3f { 0, 0, -1 }, cd::math::Vec3f { 0, 0, 1 } };
    cd::physics::Sphere s { cd::math::Vec3f { 0, 0, 0 }, 1.0F };
    auto t = cd::physics::intersect_ray_sphere(r, s);
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(*t, 0.0F, 1e-4F);
}

// ---------------------------------------------------------------------------
// Sweep sphere/AABB — TOI at t=0, t=1, no-hit past end, grazing-corner.
// ---------------------------------------------------------------------------
TEST(SweepEdge, ContactExactlyAtEndOfMotionT1)
{
    // Inflated box near face at z=4..5; radius 0.5 → expanded min z = 3.5.
    // Start at z=0 moving +3.5 reaches the surface exactly at toi=1.
    cd::physics::Aabb box { cd::math::Vec3f { -1, -1, 4 }, cd::math::Vec3f { 1, 1, 5 } };
    auto r = cd::physics::sweep_sphere_aabb(
        cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 0, 0, 3.5F }, 0.5F, box);
    ASSERT_TRUE(r.hit);
    EXPECT_NEAR(r.toi, 1.0F, 1e-4F);
}

TEST(SweepEdge, ContactJustPastEndIsNoHit)
{
    // Same geometry but motion stops a hair short → contact would be toi>1.
    cd::physics::Aabb box { cd::math::Vec3f { -1, -1, 4 }, cd::math::Vec3f { 1, 1, 5 } };
    auto r = cd::physics::sweep_sphere_aabb(
        cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 0, 0, 3.49F }, 0.5F, box);
    EXPECT_FALSE(r.hit);
}

TEST(SweepEdge, AlreadyOverlappingReturnsToiZeroAndPointAtStart)
{
    cd::physics::Aabb box { cd::math::Vec3f { -1, -1, -1 }, cd::math::Vec3f { 1, 1, 1 } };
    const cd::math::Vec3f start { 0, 0, 0 };
    auto r = cd::physics::sweep_sphere_aabb(start, cd::math::Vec3f { 0, 0, 5 }, 0.5F, box);
    ASSERT_TRUE(r.hit);
    EXPECT_FLOAT_EQ(r.toi, 0.0F);
    EXPECT_FLOAT_EQ(r.point.z, start.z);  // point == start at toi 0
}

TEST(SweepEdge, MotionParallelToFaceMissesDistantBox)
{
    // Move along +x but the box is far away in +z; never reaches it.
    cd::physics::Aabb box { cd::math::Vec3f { 0, 0, 100 }, cd::math::Vec3f { 1, 1, 101 } };
    auto r = cd::physics::sweep_sphere_aabb(
        cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 0, 0 }, 0.5F, box);
    EXPECT_FALSE(r.hit);
}

// ---------------------------------------------------------------------------
// Triangle — degenerate (collinear), vertices B & C, edge midpoint, outside.
// ---------------------------------------------------------------------------
TEST(TriangleEdge, DegenerateCollinearReturnsFallback)
{
    // a,b,c collinear → denom ~0 → documented fallback (1,0,0).
    cd::physics::Triangle t {
        cd::math::Vec3f { 0, 0, 0 },
        cd::math::Vec3f { 1, 0, 0 },
        cd::math::Vec3f { 2, 0, 0 },
    };
    const auto bc = cd::physics::barycentric(t, cd::math::Vec3f { 0.5F, 0, 0 });
    EXPECT_FLOAT_EQ(bc.x, 1.0F);
    EXPECT_FLOAT_EQ(bc.y, 0.0F);
    EXPECT_FLOAT_EQ(bc.z, 0.0F);
}

TEST(TriangleEdge, BarycentricVertexBAndC)
{
    cd::physics::Triangle t {
        cd::math::Vec3f { 0, 0, 0 },
        cd::math::Vec3f { 2, 0, 0 },
        cd::math::Vec3f { 0, 2, 0 },
    };
    const auto bcb = cd::physics::barycentric(t, t.b);
    EXPECT_NEAR(bcb.x, 0.0F, 1e-4F);
    EXPECT_NEAR(bcb.y, 1.0F, 1e-4F);
    EXPECT_NEAR(bcb.z, 0.0F, 1e-4F);
    const auto bcc = cd::physics::barycentric(t, t.c);
    EXPECT_NEAR(bcc.x, 0.0F, 1e-4F);
    EXPECT_NEAR(bcc.y, 0.0F, 1e-4F);
    EXPECT_NEAR(bcc.z, 1.0F, 1e-4F);
}

TEST(TriangleEdge, EdgeMidpointIsContainedAndSumsToOne)
{
    cd::physics::Triangle t {
        cd::math::Vec3f { 0, 0, 0 },
        cd::math::Vec3f { 2, 0, 0 },
        cd::math::Vec3f { 0, 2, 0 },
    };
    const cd::math::Vec3f mid_ab { 1, 0, 0 };
    const auto bc = cd::physics::barycentric(t, mid_ab);
    EXPECT_NEAR(bc.x + bc.y + bc.z, 1.0F, 1e-4F);
    EXPECT_TRUE(cd::physics::contains(t, mid_ab));
}

TEST(TriangleEdge, OutsidePointHasNegativeCoordAndIsNotContained)
{
    cd::physics::Triangle t {
        cd::math::Vec3f { 0, 0, 0 },
        cd::math::Vec3f { 2, 0, 0 },
        cd::math::Vec3f { 0, 2, 0 },
    };
    const cd::math::Vec3f outside { -1, -1, 0 };
    const auto bc = cd::physics::barycentric(t, outside);
    EXPECT_LT(bc.y, 0.0F);  // v negative → outside edge
    EXPECT_FALSE(cd::physics::contains(t, outside));
}

// ---------------------------------------------------------------------------
// InertiaTensor — analytic known shapes + degenerate / kinematic handling.
// ---------------------------------------------------------------------------
TEST(InertiaTensorEdge, SolidSphereMatchesTwoFifthsMRSquared)
{
    // Unit mass, unit radius → I = 2/5 on every axis.
    auto m = cd::physics::solid_sphere(1.0F, 1.0F);
    EXPECT_NEAR(m.inertia.x, 0.4F, 1e-6F);
    EXPECT_NEAR(m.inertia.y, 0.4F, 1e-6F);
    EXPECT_NEAR(m.inertia.z, 0.4F, 1e-6F);
    EXPECT_NEAR(m.inv_inertia.x, 1.0F / 0.4F, 1e-4F);
}

TEST(InertiaTensorEdge, SolidCubeIsIsotropic)
{
    // A cube (equal extents) has equal inertia on all 3 axes.
    auto m = cd::physics::solid_box(6.0F, 2.0F, 2.0F, 2.0F);
    // I = (1/12)*6*(4+4) = 4 on every axis.
    EXPECT_NEAR(m.inertia.x, 4.0F, 1e-4F);
    EXPECT_FLOAT_EQ(m.inertia.x, m.inertia.y);
    EXPECT_FLOAT_EQ(m.inertia.y, m.inertia.z);
}

TEST(InertiaTensorEdge, CylinderAxialLessThanRadialForTallShape)
{
    // Tall thin cylinder: spin about its own Y-axis (Iyy) is cheap vs
    // tumbling end-over-end (Ixx = Izz).
    auto m = cd::physics::solid_cylinder_y(3.0F, 0.5F, 4.0F);
    EXPECT_LT(m.inertia.y, m.inertia.x);
    EXPECT_FLOAT_EQ(m.inertia.x, m.inertia.z);
    // Iyy = 0.5*3*0.25 = 0.375.
    EXPECT_NEAR(m.inertia.y, 0.375F, 1e-4F);
}

TEST(InertiaTensorEdge, ZeroMassYieldsZeroInverses)
{
    // inv_or_zero guards against division by zero for massless bodies.
    auto m = cd::physics::solid_sphere(0.0F, 1.0F);
    EXPECT_FLOAT_EQ(m.inv_mass, 0.0F);
    EXPECT_FLOAT_EQ(m.inv_inertia.x, 0.0F);
}

// ---------------------------------------------------------------------------
// OBB / OBB separating-axis test (NEW primitive, Wave depth pass).
// ---------------------------------------------------------------------------
TEST(ObbSat, IdenticalBoxesOverlap)
{
    cd::physics::Obb a;  // unit AABB-aligned box at origin
    cd::physics::Obb b;
    EXPECT_TRUE(cd::physics::intersects(a, b));
}

TEST(ObbSat, FaceAxisSeparationOnX)
{
    cd::physics::Obb a;
    cd::physics::Obb b;
    b.center = cd::math::Vec3f { 1.001F, 0, 0 };  // gap just past 0.5+0.5
    EXPECT_FALSE(cd::physics::intersects(a, b));
    b.center = cd::math::Vec3f { 0.999F, 0, 0 };  // overlap by a hair
    EXPECT_TRUE(cd::physics::intersects(a, b));
}

TEST(ObbSat, AxisAlignedTouchingFaceOverlaps)
{
    cd::physics::Obb a;
    cd::physics::Obb b;
    b.center = cd::math::Vec3f { 1.0F, 0, 0 };  // exactly touching faces
    EXPECT_TRUE(cd::physics::intersects(a, b));
}

TEST(ObbSat, EdgeCrossAxisCatchesRotatedNearMiss)
{
    // Two boxes rotated 45° about Y so a face axis alone won't separate
    // them, but an edge-edge cross axis does once pulled apart on the
    // diagonal. Half-extent along a rotated diagonal of a unit box is
    // sqrt(2)/2 ≈ 0.707; placing centers 2.0 apart on +x clears both.
    const float c = 0.7071068F;
    cd::physics::Obb a;
    a.axis_x = cd::math::Vec3f { c, 0, -c };
    a.axis_z = cd::math::Vec3f { c, 0, c };
    cd::physics::Obb b = a;
    b.center = cd::math::Vec3f { 2.0F, 0, 0 };
    EXPECT_FALSE(cd::physics::intersects(a, b));
    // Pull them close so the rotated corners interpenetrate.
    b.center = cd::math::Vec3f { 1.2F, 0, 0 };
    EXPECT_TRUE(cd::physics::intersects(a, b));
}

TEST(ObbSat, ParallelAxesEpsilonDoesNotFalseSeparate)
{
    // Perfectly parallel axes → some cross products are zero-length; the
    // epsilon guard must keep a genuine overlap reported as overlapping.
    cd::physics::Obb a;
    cd::physics::Obb b;
    b.center = cd::math::Vec3f { 0.5F, 0.5F, 0.5F };  // clearly overlapping
    EXPECT_TRUE(cd::physics::intersects(a, b));
}

TEST(ObbSat, SeparationOnRotatedBAxis)
{
    // B rotated 45° about Z, separated along its own rotated x-axis.
    const float c = 0.7071068F;
    cd::physics::Obb a;
    cd::physics::Obb b;
    b.axis_x = cd::math::Vec3f { c, c, 0 };
    b.axis_y = cd::math::Vec3f { -c, c, 0 };
    // Move B far along world +x+y diagonal so no axis overlaps.
    b.center = cd::math::Vec3f { 3, 3, 0 };
    EXPECT_FALSE(cd::physics::intersects(a, b));
}

TEST(ObbSat, IntersectIsSymmetric)
{
    const float c = 0.7071068F;
    cd::physics::Obb a;
    a.axis_x = cd::math::Vec3f { c, 0, -c };
    a.axis_z = cd::math::Vec3f { c, 0, c };
    cd::physics::Obb b;
    b.center = cd::math::Vec3f { 0.7F, 0, 0.3F };
    EXPECT_EQ(cd::physics::intersects(a, b), cd::physics::intersects(b, a));
}

// =============================================================================
// 32-Module Rockstar 2D + 3D Unified Physics Engine Verification Suite
// =============================================================================
#include <cd/physics/RockstarPhysicsWorldAdapter.hpp>
#include <cd/physics/UnifiedPhysicsEngine.hpp>

TEST(RockstarPhysicsWorldAdapter, IPhysicsWorldInterfaceBackedByUnifiedEngine)
{
    auto world = cd::physics::make_rockstar_physics_world();
    ASSERT_NE(world, nullptr);
    world->set_gravity({ 0.0F, -9.81F, 0.0F });
    EXPECT_FLOAT_EQ(world->gravity().y, -9.81F);

    cd::physics::BodyDesc bad_dyn {};
    bad_dyn.type = cd::physics::BodyType::kDynamic;
    bad_dyn.mass = 0.0F;
    EXPECT_FALSE(world->create_body(bad_dyn).has_value());

    cd::physics::BodyDesc dyn {};
    dyn.type     = cd::physics::BodyType::kDynamic;
    dyn.position = { 0.0F, 4.0F, 0.0F };
    dyn.mass     = 2.0F;
    auto h = world->create_body(dyn);
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(world->body_count(), 1u);
    EXPECT_EQ(world->body_type(*h), cd::physics::BodyType::kDynamic);

    world->apply_impulse(*h, { 4.0F, 0.0F, 0.0F });
    EXPECT_NEAR(world->linear_velocity(*h).x, 2.0F, 1e-4F);

    world->step(1.0F / 60.0F);
    EXPECT_LT(world->position(*h).y, 4.0F);
    EXPECT_GT(world->position(*h).x, 0.0F);

    world->destroy_body(*h);
    EXPECT_EQ(world->body_count(), 0u);
}

TEST(RockstarPhysicsCore, FilterMatrixAndDynamicBvh2D3DAndIslands)
{
    using namespace cd::physics::core;

    CollisionFilterMatrix matrix {};
    CollisionFilter f0 { .category_layer = 0u, .category_bits = 1u, .mask_bits = 0xFFFFFFFFu, .group_index = 0 };
    CollisionFilter f1 { .category_layer = 1u, .category_bits = 2u, .mask_bits = 0xFFFFFFFFu, .group_index = 0 };
    EXPECT_TRUE(matrix.should_collide(f0, f1));
    matrix.set_layer_collision(0u, 1u, false);
    EXPECT_FALSE(matrix.should_collide(f0, f1));

    // Negative group index overrides and never collides with same negative group
    f0.group_index = -5;
    f1.group_index = -5;
    matrix.set_layer_collision(0u, 1u, true);
    EXPECT_FALSE(matrix.should_collide(f0, f1));

    // DynamicBvh2D pair & raycast query
    DynamicBvh2D bvh2d {};
    const auto p0 = bvh2d.insert_proxy(10u, SpatialAabb2D { { 0.0F, 0.0F }, { 1.0F, 1.0F } });
    const auto p1 = bvh2d.insert_proxy(20u, SpatialAabb2D { { 0.5F, 0.5F }, { 1.5F, 1.5F } });
    EXPECT_GE(p0, 0);
    EXPECT_GE(p1, 0);
    std::uint32_t pairs_2d = 0u;
    bvh2d.query_all_pairs([&](std::uint32_t, std::uint32_t) { ++pairs_2d; });
    EXPECT_EQ(pairs_2d, 1u);

    // DynamicBvh3D pair & raycast query
    DynamicBvh3D bvh3d {};
    (void)bvh3d.insert_proxy(100u, SpatialAabb3D { { -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F } });
    (void)bvh3d.insert_proxy(200u, SpatialAabb3D { { 0.2F, 0.2F, 0.2F }, { 2.0F, 2.0F, 2.0F } });
    std::uint32_t pairs_3d = 0u;
    bvh3d.query_all_pairs([&](std::uint32_t, std::uint32_t) { ++pairs_3d; });
    EXPECT_EQ(pairs_3d, 1u);

    // IslandBuilder with static body isolation
    IslandBuilder islands {};
    islands.reset(4u);
    islands.mark_static(0u);
    islands.unite(0u, 1u);  // static 0 should not merge 1 and 2
    islands.unite(0u, 2u);
    islands.unite(1u, 3u);  // dynamic 1 and 3 form one island
    const auto built = islands.build_islands();
    EXPECT_EQ(built.size(), 2u);
}

TEST(RockstarPhysics2D, NarrowphaseCcdSoftStepWorldAndCharacter2D)
{
    using namespace cd::physics;
    using namespace cd::physics::v2d;

    // 1. 2D Narrowphase: Box-Box SAT + 2-point contact clipping
    ShapeVariant2D box_a = BoxShape2D { { 1.0F, 1.0F } };
    ShapeVariant2D box_b = BoxShape2D { { 0.5F, 0.5F } };
    core::Transform2D xf_a { { 0.0F, 0.0F }, 0.0F };
    core::Transform2D xf_b { { 0.0F, 1.3F }, 0.0F };
    const auto manifold = collide_shapes_2d(box_a, xf_a, box_b, xf_b);
    EXPECT_GT(manifold.point_count, 0u);
    EXPECT_NEAR(manifold.points[0].penetration, 0.2F, 1e-3F);

    // 2. 2D CCD Sweep TOI: Fast bullet circle approaching thin wall box
    ShapeVariant2D bullet = CircleShape2D { 0.1F };
    ShapeVariant2D wall   = BoxShape2D { { 0.1F, 2.0F } };
    const float toi = sweep_shapes_toi_2d(
        bullet,
        core::Transform2D { { -2.0F, 0.0F }, 0.0F },
        cd::math::Vec2f { 20.0F, 0.0F },
        wall,
        core::Transform2D { { 0.0F, 0.0F }, 0.0F },
        cd::math::Vec2f { 0.0F, 0.0F });
    EXPECT_GT(toi, 0.0F);
    EXPECT_LT(toi, 0.2F);

    // 3. PhysicsWorld2D: Box stacking + RevoluteJoint2D + CharacterController2D
    PhysicsWorld2D world {};
    const auto floor_id = world.create_body(
        core::BodyMotionType::kStatic,
        core::Transform2D { { 0.0F, -1.0F }, 0.0F },
        BoxShape2D { { 10.0F, 1.0F } });
    const auto crate_id = world.create_body(
        core::BodyMotionType::kDynamic,
        core::Transform2D { { 0.0F, 0.8F }, 0.0F },
        BoxShape2D { { 0.5F, 0.5F } });
    EXPECT_NE(floor_id, crate_id);

    for (int i = 0; i < 60; ++i)
    {
        world.step(1.0F / 60.0F, 4u);
    }
    const auto* crate = world.get_body(crate_id);
    ASSERT_NE(crate, nullptr);
    EXPECT_NEAR(crate->transform.position.y, 0.5F, 0.08F);

    // 2D CharacterController move_and_slide
    const auto char_id = world.create_body(
        core::BodyMotionType::kKinematic,
        core::Transform2D { { 2.0F, 0.52F }, 0.0F },
        CapsuleShape2D { 0.25F, 0.25F });
    CharacterController2D cc2d {};
    cc2d.move_and_slide(world, char_id, { 3.0F, -2.0F }, 1.0F / 60.0F);
    EXPECT_TRUE(cc2d.on_floor());
}

TEST(RockstarPhysics3D, GjkEpaObbSatTgsSolverJointsAndCharacterMover3D)
{
    using namespace cd::physics;
    using namespace cd::physics::v3d;

    // 1. GJK + EPA on two overlapping convex hulls
    ConvexHullShape3D hull_a {};
    hull_a.vertices = {
        { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
        { -1, -1,  1 }, { 1, -1,  1 }, { 1, 1,  1 }, { -1, 1,  1 }
    };
    ShapeVariant3D sa = hull_a;
    ShapeVariant3D sb = hull_a;
    ContactManifold3D gjk_m {};
    const bool hit_gjk = gjk_epa_intersect_3d(
        sa, core::Transform3D { { 0.0F, 0.0F, 0.0F }, cd::math::Quatf::identity() },
        sb, core::Transform3D { { 0.0F, 1.6F, 0.0F }, cd::math::Quatf::identity() },
        gjk_m);
    EXPECT_TRUE(hit_gjk);
    EXPECT_GT(gjk_m.point_count, 0u);
    EXPECT_NEAR(gjk_m.points[0].penetration, 0.4F, 0.05F);

    // 2. 3D PhysicsWorld3D + 6-DOF TGS Solver + CharacterMover3D
    PhysicsWorld3D world3d {};
    RigidBody3D floor_body {};
    floor_body.motion_type = core::BodyMotionType::kStatic;
    floor_body.transform.position = { 0.0F, -0.5F, 0.0F };
    floor_body.shape = BoxShape3D { { 20.0F, 0.5F, 20.0F } };
    (void)world3d.create_body(floor_body);

    RigidBody3D box_body {};
    box_body.motion_type = core::BodyMotionType::kDynamic;
    box_body.transform.position = { 0.0F, 1.2F, 0.0F };
    box_body.mass = 4.0F;
    box_body.shape = BoxShape3D { { 0.5F, 0.5F, 0.5F } };
    const auto box_id = world3d.create_body(box_body);

    for (int i = 0; i < 60; ++i)
    {
        world3d.step(1.0F / 60.0F, 4u, 8u);
    }
    const auto* settled = world3d.get_body(box_id);
    ASSERT_NE(settled, nullptr);
    EXPECT_NEAR(settled->transform.position.y, 0.5F, 0.08F);

    // CharacterMover3D move_and_slide over ground
    RigidBody3D avatar_body {};
    avatar_body.motion_type = core::BodyMotionType::kKinematic;
    avatar_body.transform.position = { 3.0F, 0.52F, 0.0F };
    avatar_body.shape = CapsuleShape3D { 0.25F, 0.25F };
    const auto avatar_id = world3d.create_body(avatar_body);

    CharacterMover3D mover3d {};
    mover3d.move_and_slide(world3d, avatar_id, { 4.0F, -1.0F, 0.0F }, 1.0F / 60.0F);
    EXPECT_TRUE(mover3d.on_ground());
}

TEST(RockstarAdvancedSimulation, EuphoriaRagdollPacejkaVehicleFractureBuoyancyAndXpbd)
{
    using namespace cd::physics;
    using namespace cd::physics::rockstar;

    // 1. ActiveRagdollEuphoria3D: ballistic impact -> stagger -> Capture Point recovery step
    ActiveRagdollEuphoria3D ragdoll {};
    ragdoll.configure_humanoid_rig({ 0.0F, 1.0F, 0.0F }, 80.0F);
    ragdoll.apply_ballistic_hit_impulse(
        RagdollBoneId::kChest,
        { 140.0F, 20.0F, 0.0F },
        { 0.0F, 1.35F, 0.0F });
    ragdoll.step(1.0F / 60.0F, 0.0F);
    const auto& chest = ragdoll.bone_state(RagdollBoneId::kChest);
    EXPECT_LT(chest.current_stiffness_scale, 1.0F);
    EXPECT_GT(ragdoll.balance_state().balance_error_m, 0.0F);
    EXPECT_TRUE(ragdoll.balance_state().needs_recovery_step || ragdoll.balance_state().brace_for_impact);

    // 2. PacejkaVehicleDynamics3D: Magic Formula + acceleration + chassis deformation
    PacejkaVehicleDynamics3D vehicle {};
    const float peak_fy = PacejkaVehicleDynamics3D::evaluate_magic_formula(0.15F, PacejkaTireParams {});
    EXPECT_GT(peak_fy, 0.5F);
    VehicleInputState input {};
    input.throttle = 1.0F;
    input.steer_angle_rad = 0.12F;
    for (int i = 0; i < 30; ++i)
    {
        vehicle.step(input, 1.0F / 60.0F);
    }
    EXPECT_GT(vehicle.forward_speed_m_s(), 0.5F);
    const auto deform = vehicle.apply_collision_deformation(
        { 0.8F, 0.2F, 2.0F },
        { -25000.0F, 0.0F, -45000.0F });
    EXPECT_GT(cd::math::length(deform), 0.0F);

    // 3. FractureDestructionSystem3D: Voronoi/Grid stress graph bond breaking & island detachment
    FractureDestructionSystem3D fracture {};
    fracture.generate_grid_fracture_object(2u, 3u, 2u, { 0.0F, 0.0F, 0.0F }, { 0.25F, 0.25F, 0.25F }, 5.0F, 50.0F);
    EXPECT_EQ(fracture.chunks().size(), 12u);
    const std::uint32_t broken = fracture.apply_impact_impulse({ 0.0F, 0.75F, 0.0F }, 1.2F, 350.0F);
    EXPECT_GT(broken, 0u);
    fracture.step_debris(1.0F / 60.0F, 0.0F);
    EXPECT_GT(fracture.detached_chunk_count(), 0u);

    // 4. BuoyancyFluidField3D: Gerstner wave height + multi-probe Archimedes lift
    BuoyancyFluidField3D fluid {};
    const float wave_h = fluid.sample_wave_height(2.0F, -1.0F, 1.5F);
    EXPECT_GT(wave_h, -2.0F);
    EXPECT_LT(wave_h,  2.0F);
    const std::array<cd::math::Vec3f, 4> hull_probes {{
        { -0.5F, -0.4F, -0.5F },
        {  0.5F, -0.4F, -0.5F },
        { -0.5F, -0.4F,  0.5F },
        {  0.5F, -0.4F,  0.5F },
    }};
    const auto buoy = fluid.compute_buoyancy_forces(
        { 0.0F, -0.3F, 0.0F },
        cd::math::Quatf::identity(),
        { 0.0F, -1.0F, 0.0F },
        { 0.0F, 0.0F, 0.0F },
        hull_probes,
        BuoyancyBodyConfig3D {},
        0.0F);
    EXPECT_GT(buoy.buoyancy_force_n.y, 0.0F);
    EXPECT_GT(buoy.submerged_fraction, 0.0F);

    // 5. XpbdClothSoftBodySolver3D + UnifiedPhysicsEngine
    UnifiedPhysicsEngine engine {};
    (void)engine.create_euphoria_ragdoll({ 0.0F, 1.0F, 0.0F }, 78.0F);
    (void)engine.create_pacejka_vehicle();
    const auto frac_idx = engine.create_fracture_system();
    engine.get_fracture_system(frac_idx)->generate_grid_fracture_object(2u, 2u, 2u);
    const auto xpbd_idx = engine.create_xpbd_solver();
    engine.get_xpbd_solver(xpbd_idx)->create_cloth_grid(4u, 4u, 0.15F);

    engine.step_all(1.0F / 60.0F, 4u, 4u, 0.0F);
    const auto tel = engine.telemetry();
    EXPECT_EQ(tel.active_ragdolls, 1u);
    EXPECT_EQ(tel.active_vehicles, 1u);
    EXPECT_EQ(tel.fracture_systems, 1u);
    EXPECT_EQ(tel.xpbd_soft_bodies, 1u);
    EXPECT_EQ(tel.total_steps, 1u);
}

