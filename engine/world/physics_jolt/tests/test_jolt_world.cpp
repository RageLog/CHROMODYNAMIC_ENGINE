// =============================================================================
// CHROMODYNAMIC — cd::physics_jolt tests (Phase 525 / T0.3)
//
// Exercises the JoltWorld stub backend against the `IPhysicsWorld` contract
// plus the Jolt-side adapter / mapper surface (JoltJobAdapter +
// body_desc_to_component + hash_shape_desc). The same test fixture will
// re-run against the real Jolt backend once `ADR-2026xxxx-physics-jolt-bringup`
// lands — no rewriting required.
//
// Pattern: Arrange / Act / Assert per CLAUDE.md §5. No sleep_for; the
// solver is deterministic so 60 fixed steps stand in for "1 second of
// simulation".
// =============================================================================
#include <cd/physics/IPhysicsWorld.hpp>
#include <cd/physics_jolt/JoltWorld.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace
{

[[nodiscard]] bool approx_eq(float a, float b, float eps = 1e-3F) noexcept
{
    return std::fabs(a - b) < eps;
}

// ---- 1. World construction + factory ---------------------------------------

TEST(JoltWorld, FactoryReturnsLiveWorld)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(world->body_count(), 0U);
    // is_stub_backend() == false  → real Jolt (JPH::PhysicsSystem) is active.
    // is_stub_backend() == true   → Euler stub (no Jolt vendor link).
    // Both are valid; the test just records which path is live.
    (void)cd::physics_jolt::is_stub_backend();
}

// ---- 1b. Backend-selection invariant (build-flag gating, ADR §2.2) ---------
//
// The real Jolt backend (CD_PHYSICS_JOLT_REAL) is the DEFAULT configuration:
// when Jolt resolves via vcpkg/find_package or FetchContent the .cpp compiles
// JoltWorldReal and is_stub_backend() returns false. When the vendor is absent
// (network-isolated CI or CD_PHYSICS_JOLT_FORCE_STUB=ON) the Euler stub
// compiles and is_stub_backend() returns true. We pin whichever path the build
// selected against its compile definition so a silent mis-gating (e.g. the real
// path falling back to the stub without anyone noticing) fails on revert.
TEST(JoltWorld, BackendSelectionMatchesBuildDefinition)
{
#if defined(CD_PHYSICS_JOLT_REAL)
    EXPECT_FALSE(cd::physics_jolt::is_stub_backend())
        << "CD_PHYSICS_JOLT_REAL is defined → the real Jolt backend MUST be live";
#elif defined(CD_PHYSICS_JOLT_STUB)
    EXPECT_TRUE(cd::physics_jolt::is_stub_backend())
        << "CD_PHYSICS_JOLT_STUB is defined → the Euler stub MUST be live";
#else
    // Neither define present: the .cpp falls through to the stub branch.
    EXPECT_TRUE(cd::physics_jolt::is_stub_backend())
        << "No backend define → the Euler stub fallback MUST be live";
#endif
}

// ---- 2. Static box + dynamic falling ball ----------------------------------

TEST(JoltWorld, StaticBoxAndDynamicBallFallsUnderGravity)
{
    // Arrange: world with default Earth gravity, one static "floor" box, one
    // dynamic ball hovering above it.
    auto world = cd::physics_jolt::make_jolt_physics_world();
    auto* stub = dynamic_cast<cd::physics::IPhysicsWorld*>(world.get());
    ASSERT_NE(stub, nullptr);

    cd::physics::BodyDesc floor_desc {};
    floor_desc.type = cd::physics::BodyType::kStatic;
    floor_desc.position = { 0.0F, -1.0F, 0.0F };
    floor_desc.mass = 0.0F;  // mass ignored for static
    const auto floor = world->create_body(floor_desc);
    ASSERT_TRUE(floor.has_value());

    cd::physics::BodyDesc ball_desc {};
    ball_desc.position = { 0.0F, 10.0F, 0.0F };
    ball_desc.mass = 1.0F;
    const auto ball = world->create_body(ball_desc);
    ASSERT_TRUE(ball.has_value());

    // Act: simulate 1 second at 60 Hz.
    constexpr float dt = 1.0F / 60.0F;
    for (int i = 0; i < 60; ++i)
    {
        world->step(dt);
    }

    // Assert: ball moved downward, floor stayed put.
    EXPECT_LT(world->position(*ball).y, 10.0F);
    EXPECT_TRUE(approx_eq(world->position(*floor).y, -1.0F));
    EXPECT_EQ(world->body_count(), 2U);
}

// ---- 3. Two-body restitution-like collision (impulse exchange) -------------

TEST(JoltWorld, TwoBodiesWithRestitutionExchangeImpulse)
{
    // The stub backend does not run a contact solver, but the impulse-
    // exchange API is what real Jolt uses internally. We simulate the
    // "after-collision" velocity exchange that a perfectly elastic 1-D
    // collision between equal masses produces: ball_a stops, ball_b
    // continues. This validates the IPhysicsWorld impulse path used by
    // the eventual Jolt restitution code.
    auto world = cd::physics_jolt::make_jolt_physics_world();
    world->set_gravity({ 0.0F, 0.0F, 0.0F });

    cd::physics::BodyDesc a_desc {};
    a_desc.mass = 1.0F;
    a_desc.linear_velocity = { 5.0F, 0.0F, 0.0F };
    a_desc.position = { 0.0F, 0.0F, 0.0F };
    const auto a = world->create_body(a_desc);
    ASSERT_TRUE(a.has_value());

    cd::physics::BodyDesc b_desc {};
    b_desc.mass = 1.0F;
    b_desc.linear_velocity = { 0.0F, 0.0F, 0.0F };
    b_desc.position = { 2.0F, 0.0F, 0.0F };
    const auto b = world->create_body(b_desc);
    ASSERT_TRUE(b.has_value());

    // Act: model perfectly-elastic 1-D collision via impulse exchange
    // (restitution = 1, equal masses): -m*v on `a`, +m*v on `b`.
    world->apply_impulse(*a, { -5.0F, 0.0F, 0.0F });
    world->apply_impulse(*b, { +5.0F, 0.0F, 0.0F });

    EXPECT_TRUE(approx_eq(world->linear_velocity(*a).x, 0.0F));
    EXPECT_TRUE(approx_eq(world->linear_velocity(*b).x, 5.0F));
}

// ---- 4. Joint registration constrains body bookkeeping ---------------------

TEST(JoltWorld, HingeJointRegistrationTracksConstraintMetadata)
{
    // The stub keeps a joint table off to the side so the
    // `JointComponent -> JPH::Constraint` mapper can be exercised by
    // tests. The real backend wires the same table into Jolt's
    // `ConstraintManager::Add`. Here we verify that:
    //   * Joints can be registered through the public mapper API.
    //   * Joints reference two valid bodies.
    //   * Destroying one body removes joints anchored to it.
    auto world = cd::physics_jolt::make_jolt_physics_world();

    cd::physics::BodyDesc anchor {};
    anchor.type = cd::physics::BodyType::kStatic;
    const auto root = world->create_body(anchor);
    ASSERT_TRUE(root.has_value());

    cd::physics::BodyDesc arm {};
    arm.mass = 2.0F;
    arm.position = { 1.0F, 0.0F, 0.0F };
    const auto pendulum = world->create_body(arm);
    ASSERT_TRUE(pendulum.has_value());

    cd::physics_jolt::components::JointComponent hinge {};
    hinge.kind = cd::physics_jolt::components::JointKind::kHinge;
    hinge.body_a = *root;
    hinge.body_b = *pendulum;
    hinge.axis = { 0.0F, 0.0F, 1.0F };

    // Register the joint through the public Jolt-side surface. Real
    // backend forwards this to `JPH::ConstraintManager::Add` via
    // `PhysicsSyncSystem` (ADR §C); the stub stores it in a side-band
    // table so the lifecycle hook can be exercised end-to-end.
    EXPECT_TRUE(cd::physics_jolt::attach_joint(*world, hinge));
    EXPECT_EQ(cd::physics_jolt::joint_count(*world), 1U);

    // Anchor must not move.
    ASSERT_TRUE(approx_eq(world->position(*root).x, 0.0F));
    ASSERT_TRUE(approx_eq(world->position(*pendulum).x, 1.0F));

    // Destroying the pendulum body must also remove the joint that
    // referenced it (no dangling endpoints).
    world->destroy_body(*pendulum);
    EXPECT_EQ(world->body_count(), 1U);
    EXPECT_EQ(cd::physics_jolt::joint_count(*world), 0U);

    // Destroying root must succeed cleanly with no joints left to detach.
    world->destroy_body(*root);
    EXPECT_EQ(world->body_count(), 0U);
}

// ---- 5. Body remove + recreate cleans up resources -------------------------

TEST(JoltWorld, BodyDestroyReleasesResources)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();
    constexpr std::size_t kCount = 64;
    std::vector<cd::physics::BodyHandle> handles;
    handles.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i)
    {
        cd::physics::BodyDesc d {};
        d.mass = 1.0F;
        auto r = world->create_body(d);
        ASSERT_TRUE(r.has_value());
        handles.push_back(*r);
    }
    EXPECT_EQ(world->body_count(), kCount);

    for (const auto h : handles)
    {
        world->destroy_body(h);
    }
    EXPECT_EQ(world->body_count(), 0U);

    // Recreate after teardown — handles must still be issuable.
    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    auto r = world->create_body(d);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(world->body_count(), 1U);
}

// ---- 6. Multiple worlds are independent ------------------------------------

TEST(JoltWorld, TwoWorldsAreIndependent)
{
    // Equivalent of "editor preview world + game world" use case
    // (ADR-20260530 Açık Sorular Q5).
    auto a = cd::physics_jolt::make_jolt_physics_world();
    auto b = cd::physics_jolt::make_jolt_physics_world();

    a->set_gravity({ 0.0F, -1.0F, 0.0F });
    b->set_gravity({ 0.0F, -100.0F, 0.0F });

    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    const auto ba = a->create_body(d);
    const auto bb = b->create_body(d);
    ASSERT_TRUE(ba.has_value());
    ASSERT_TRUE(bb.has_value());

    constexpr float dt = 1.0F / 60.0F;
    for (int i = 0; i < 60; ++i)
    {
        a->step(dt);
        b->step(dt);
    }

    // Both bodies fell, but `b` fell much further (stronger gravity).
    EXPECT_LT(a->position(*ba).y, 0.0F);
    EXPECT_LT(b->position(*bb).y, a->position(*ba).y - 10.0F);
    EXPECT_EQ(a->body_count(), 1U);
    EXPECT_EQ(b->body_count(), 1U);
}

// ---- 7. Determinism: same seed + steps -> same state -----------------------

TEST(JoltWorld, SameInputsProduceSameStateAcrossRuns)
{
    // Tier-1 determinism (ADR-008 §E) requires bit-exact reproduction on
    // the same platform with the same compile flags. The stub backend
    // uses pure FP math identical to the reference Euler integrator and
    // therefore is deterministic by construction.
    auto run = []() {
        auto world = cd::physics_jolt::make_jolt_physics_world();
        world->set_gravity({ 0.0F, -9.81F, 0.0F });
        cd::physics::BodyDesc d {};
        d.mass = 1.5F;
        d.position = { 0.5F, 5.0F, -0.25F };
        d.linear_velocity = { 1.0F, 0.0F, 0.0F };
        auto h = world->create_body(d);
        for (int i = 0; i < 120; ++i)
        {
            world->apply_force(*h, { 0.0F, 2.0F, 0.0F });
            world->step(1.0F / 60.0F);
        }
        return world->position(*h);
    };
    const auto a = run();
    const auto b = run();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
    EXPECT_FLOAT_EQ(a.z, b.z);
}

// ---- 8. JoltJobAdapter routes jobs without deadlock ------------------------

TEST(JoltJobAdapter, RoutesDispatchedJobsThroughSubmitHook)
{
    std::atomic<int> ran { 0 };
    cd::physics_jolt::JoltJobAdapter adapter {
        [&](cd::physics_jolt::JoltJobAdapter::JobFn job) {
            // Stand-in for `cd::concurrency::IJobSystem::submit`. The real
            // adapter routes here; we run inline to keep the test single-
            // threaded and assert that no second thread pool is spawned by
            // the adapter itself.
            if (job)
                job();
        }
    };
    ASSERT_TRUE(adapter.has_submit_hook());

    constexpr int kJobs = 256;
    for (int i = 0; i < kJobs; ++i)
    {
        adapter.dispatch([&] { ran.fetch_add(1, std::memory_order_relaxed); });
    }

    EXPECT_EQ(ran.load(), kJobs);
    EXPECT_EQ(adapter.dispatched(), static_cast<std::uint64_t>(kJobs));
}

TEST(JoltJobAdapter, InlineFallbackRunsJobsWithoutPool)
{
    // No submit hook installed -> adapter runs jobs on the calling thread.
    // Validates that the adapter never spawns its own `std::thread` (ADR-15
    // single-pool rule).
    cd::physics_jolt::JoltJobAdapter adapter {};
    EXPECT_FALSE(adapter.has_submit_hook());

    int counter = 0;
    adapter.dispatch([&] { counter = 42; });
    EXPECT_EQ(counter, 42);
    EXPECT_EQ(adapter.dispatched(), 1U);
}

// ---- 9. Component mappers: BodyDesc -> RigidBodyComponent ------------------

TEST(JoltMappers, BodyDescDefaultsApplyConfigDamping)
{
    cd::physics_jolt::JoltBackendConfig cfg {};
    cfg.default_linear_damping = 0.25F;
    cfg.default_angular_damping = 0.10F;
    cd::physics::BodyDesc d {};
    d.type = cd::physics::BodyType::kDynamic;
    d.mass = 4.0F;
    d.linear_damping = 0.0F;  // request defaults

    const auto rb = cd::physics_jolt::body_desc_to_component(d, cfg);
    EXPECT_EQ(rb.type, cd::physics::BodyType::kDynamic);
    EXPECT_FLOAT_EQ(rb.mass, 4.0F);
    EXPECT_FLOAT_EQ(rb.linear_damping, 0.25F);
    EXPECT_FLOAT_EQ(rb.angular_damping, 0.10F);
}

TEST(JoltMappers, ShapeHashesDistinguishVariants)
{
    using cd::physics_jolt::components::BoxShape;
    using cd::physics_jolt::components::CapsuleShape;
    using cd::physics_jolt::components::ShapeDesc;
    using cd::physics_jolt::components::SphereShape;
    using cd::physics_jolt::hash_shape_desc;

    const ShapeDesc box { BoxShape { { 1.0F, 2.0F, 3.0F } } };
    const ShapeDesc sphere { SphereShape { 1.0F } };
    const ShapeDesc capsule { CapsuleShape { 1.0F, 0.5F } };
    const ShapeDesc box_same { BoxShape { { 1.0F, 2.0F, 3.0F } } };
    const ShapeDesc box_diff { BoxShape { { 1.0F, 2.0F, 3.5F } } };

    EXPECT_EQ(hash_shape_desc(box), hash_shape_desc(box_same));
    EXPECT_NE(hash_shape_desc(box), hash_shape_desc(box_diff));
    EXPECT_NE(hash_shape_desc(box), hash_shape_desc(sphere));
    EXPECT_NE(hash_shape_desc(sphere), hash_shape_desc(capsule));
}

// ---- 9b. Collider attach + backend config introspection -------------------

TEST(JoltWorld, AttachColliderAndReadBackendConfig)
{
    cd::physics_jolt::JoltBackendConfig cfg {};
    cfg.max_bodies = 8192U;
    cfg.default_linear_damping = 0.33F;
    auto world = cd::physics_jolt::make_jolt_physics_world(cfg);

    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    const auto body = world->create_body(d);
    ASSERT_TRUE(body.has_value());

    cd::physics_jolt::components::ColliderComponent box_collider {};
    box_collider.shape = cd::physics_jolt::components::BoxShape { { 0.5F, 0.5F, 0.5F } };
    EXPECT_TRUE(cd::physics_jolt::attach_collider(*world, *body, box_collider));

    cd::physics_jolt::components::ColliderComponent sphere_collider {};
    sphere_collider.shape = cd::physics_jolt::components::SphereShape { 0.75F };
    sphere_collider.is_trigger = true;
    EXPECT_TRUE(cd::physics_jolt::attach_collider(*world, *body, sphere_collider));

    EXPECT_EQ(cd::physics_jolt::collider_count(*world, *body), 2U);

    // Config round-trips: caller-supplied knobs survive.
    const auto resolved = cd::physics_jolt::backend_config(*world);
    EXPECT_EQ(resolved.max_bodies, 8192U);
    EXPECT_FLOAT_EQ(resolved.default_linear_damping, 0.33F);
}

// ---- 10. Character-controller-style grounded sweep (kinematic capsule) -----

TEST(JoltWorld, KinematicCapsuleStaysGroundedOnFlatStep)
{
    // ADR-20260530 §C lists `CharacterControllerComponent` -> `JPH::CharacterVirtual`.
    // The stub doesn't carry a sweep solver yet, so we verify the
    // kinematic-mover contract that the real character controller leans on:
    //   * Kinematic body ignores impulses (ADR-008 motion-type rule).
    //   * `set_position` teleports cleanly.
    //   * Velocity is reported back without being integrated.
    auto world = cd::physics_jolt::make_jolt_physics_world();
    world->set_gravity({ 0.0F, -9.81F, 0.0F });

    cd::physics::BodyDesc d {};
    d.type = cd::physics::BodyType::kKinematic;
    d.mass = 1.0F;
    d.position = { 0.0F, 0.0F, 0.0F };
    const auto ch = world->create_body(d);
    ASSERT_TRUE(ch.has_value());

    // Impulses must NOT toss a kinematic body around.
    world->apply_impulse(*ch, { 0.0F, 1000.0F, 0.0F });
    world->step(1.0F / 60.0F);
    EXPECT_TRUE(approx_eq(world->position(*ch).y, 0.0F));

    // Mover writes velocity for downstream queries.
    world->set_linear_velocity(*ch, { 1.0F, 0.0F, 0.0F });
    EXPECT_TRUE(approx_eq(world->linear_velocity(*ch).x, 1.0F));

    // Mover teleports the body — the controller integrates position itself
    // (Guerrilla GDC 2022 character controller pattern).
    world->set_position(*ch, { 5.0F, 0.0F, 0.0F });
    EXPECT_TRUE(approx_eq(world->position(*ch).x, 5.0F));
    EXPECT_TRUE(approx_eq(world->position(*ch).y, 0.0F));
}

// ===========================================================================
// DEPTH PASS (75->100) — deepen the DEFAULT Euler-stub path coverage.
//
// Everything below targets the stub fallback (the path that compiles when the
// vendored Jolt library is ABSENT, i.e. CD_PHYSICS_JOLT_STUB / no define).
// The stub delegates dynamics to cd::physics::make_builtin_physics_world(),
// so these tests pin the IPhysicsWorld contract AND the Jolt-side side-band
// (colliders / joints / config / vehicle no-ops). Assertions that can only
// hold for the real JPH::PhysicsSystem backend are SEALED behind
// CD_PHYSICS_JOLT_REAL with a rationale, because they cannot be verified
// here without the vendor link. AAA + edge + negative per CLAUDE.md §5.
// ===========================================================================

// ---- 11. Body create — negative: dynamic body with non-positive mass -------

TEST(JoltWorldDepth, NonPositiveDynamicMassIsClampedToUnit)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();

    // The backend CLAMPS a non-positive dynamic mass to a safe unit default
    // (`desc.mass > 0 ? desc.mass : 1.0`) rather than rejecting it — the body
    // is still created with a valid handle. This pins that documented choice.
    cd::physics::BodyDesc zero_mass {};
    zero_mass.type = cd::physics::BodyType::kDynamic;
    zero_mass.mass = 0.0F;
    const auto r_zero = world->create_body(zero_mass);
    ASSERT_TRUE(r_zero.has_value());
    EXPECT_TRUE(r_zero->is_valid());

    cd::physics::BodyDesc neg_mass {};
    neg_mass.type = cd::physics::BodyType::kDynamic;
    neg_mass.mass = -2.0F;
    EXPECT_TRUE(world->create_body(neg_mass).has_value());

    EXPECT_EQ(world->body_count(), 2U);

    // Static body with zero mass is also legal (mass ignored for non-dynamic).
    cd::physics::BodyDesc static_desc {};
    static_desc.type = cd::physics::BodyType::kStatic;
    static_desc.mass = 0.0F;
    EXPECT_TRUE(world->create_body(static_desc).has_value());
    EXPECT_EQ(world->body_count(), 3U);
}

// ---- 12. Handle reuse — destroyed handle does NOT alias a fresh body -------

TEST(JoltWorldDepth, DestroyedHandleDoesNotAliasNewBody)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();

    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    d.position = { 1.0F, 2.0F, 3.0F };
    const auto first = world->create_body(d);
    ASSERT_TRUE(first.has_value());
    EXPECT_TRUE(first->is_valid());

    world->destroy_body(*first);
    EXPECT_EQ(world->body_count(), 0U);

    // Re-create: the stub's monotonic id allocator never recycles an index,
    // so the new handle is distinct from the destroyed one. A stale handle
    // must not silently address the newcomer.
    cd::physics::BodyDesc d2 {};
    d2.mass = 1.0F;
    d2.position = { 9.0F, 9.0F, 9.0F };
    const auto second = world->create_body(d2);
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first->value(), second->value());

    // The fresh body carries its own position; the stale handle reads zero
    // (no record) rather than the newcomer's state.
    EXPECT_TRUE(approx_eq(world->position(*second).x, 9.0F));
    EXPECT_TRUE(approx_eq(world->position(*first).x, 0.0F));
}

// ---- 13. Transform + velocity round-trip via set/get -----------------------

TEST(JoltWorldDepth, SetGetTransformAndVelocityRoundTrip)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();
    world->set_gravity({ 0.0F, 0.0F, 0.0F });  // freeze integration

    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    const auto h = world->create_body(d);
    ASSERT_TRUE(h.has_value());

    world->set_position(*h, { 4.0F, -5.0F, 6.0F });
    const auto p = world->position(*h);
    EXPECT_TRUE(approx_eq(p.x, 4.0F));
    EXPECT_TRUE(approx_eq(p.y, -5.0F));
    EXPECT_TRUE(approx_eq(p.z, 6.0F));

    world->set_linear_velocity(*h, { -1.0F, 2.0F, -3.0F });
    const auto v = world->linear_velocity(*h);
    EXPECT_TRUE(approx_eq(v.x, -1.0F));
    EXPECT_TRUE(approx_eq(v.y, 2.0F));
    EXPECT_TRUE(approx_eq(v.z, -3.0F));

    // With zero gravity and no force, a step must not perturb a set transform
    // beyond integrating the velocity we just wrote (semi-implicit Euler).
    constexpr float dt = 1.0F / 60.0F;
    world->set_linear_velocity(*h, { 0.0F, 0.0F, 0.0F });
    world->step(dt);
    const auto p_after = world->position(*h);
    EXPECT_TRUE(approx_eq(p_after.x, 4.0F));
    EXPECT_TRUE(approx_eq(p_after.y, -5.0F));
    EXPECT_TRUE(approx_eq(p_after.z, 6.0F));
}

// ---- 14. Gravity set/get + negative/zero dt no-op --------------------------

TEST(JoltWorldDepth, GravityRoundTripAndNonPositiveDtIsNoOp)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();

    world->set_gravity({ 1.0F, -3.0F, 2.0F });
    const auto g = world->gravity();
    EXPECT_TRUE(approx_eq(g.x, 1.0F));
    EXPECT_TRUE(approx_eq(g.y, -3.0F));
    EXPECT_TRUE(approx_eq(g.z, 2.0F));

    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    d.position = { 0.0F, 0.0F, 0.0F };
    const auto h = world->create_body(d);
    ASSERT_TRUE(h.has_value());

    // dt == 0 and dt < 0 must leave state untouched (guard in the integrator).
    world->step(0.0F);
    world->step(-1.0F / 60.0F);
    const auto p = world->position(*h);
    EXPECT_TRUE(approx_eq(p.x, 0.0F));
    EXPECT_TRUE(approx_eq(p.y, 0.0F));
    EXPECT_TRUE(approx_eq(p.z, 0.0F));
    EXPECT_TRUE(approx_eq(world->linear_velocity(*h).y, 0.0F));
}

// ---- 15. Body-type gating — static / kinematic ignore dynamics -------------

TEST(JoltWorldDepth, StaticAndKinematicBodiesIgnoreForcesAndGravity)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();
    world->set_gravity({ 0.0F, -9.81F, 0.0F });

    cd::physics::BodyDesc static_desc {};
    static_desc.type = cd::physics::BodyType::kStatic;
    static_desc.position = { 0.0F, 0.0F, 0.0F };
    const auto s = world->create_body(static_desc);
    ASSERT_TRUE(s.has_value());

    cd::physics::BodyDesc kin_desc {};
    kin_desc.type = cd::physics::BodyType::kKinematic;
    kin_desc.mass = 1.0F;
    kin_desc.position = { 0.0F, 0.0F, 0.0F };
    const auto k = world->create_body(kin_desc);
    ASSERT_TRUE(k.has_value());

    EXPECT_EQ(world->body_type(*s), cd::physics::BodyType::kStatic);
    EXPECT_EQ(world->body_type(*k), cd::physics::BodyType::kKinematic);

    // Forces + impulses + gravity must not move either body.
    world->apply_force(*s, { 0.0F, 1000.0F, 0.0F });
    world->apply_impulse(*s, { 0.0F, 1000.0F, 0.0F });
    world->apply_force(*k, { 0.0F, 1000.0F, 0.0F });
    world->apply_impulse(*k, { 0.0F, 1000.0F, 0.0F });

    constexpr float dt = 1.0F / 60.0F;
    for (int i = 0; i < 60; ++i)
    {
        world->step(dt);
    }
    EXPECT_TRUE(approx_eq(world->position(*s).y, 0.0F));
    EXPECT_TRUE(approx_eq(world->position(*k).y, 0.0F));
    EXPECT_TRUE(approx_eq(world->linear_velocity(*s).y, 0.0F));
    EXPECT_TRUE(approx_eq(world->linear_velocity(*k).y, 0.0F));
}

// ---- 16. Step subdivision determinism (free-fall closed form) --------------

TEST(JoltWorldDepth, FreeFallVelocityTracksDampedClosedForm)
{
    // Constant gravity over N steps gives the undamped closed form v = g*N*dt.
    // A body created WITHOUT explicit damping (desc.linear_damping == 0) inherits
    // the world's default linear damping — `mLinearDamping = damping > 0 ? damping
    // : config.default` — so the real velocity is slightly SMALLER in magnitude
    // than the undamped bound. Assert the body falls, stays under that bound, and
    // tracks it within the damping band (rather than an unachievable exact match).
    auto world = cd::physics_jolt::make_jolt_physics_world();
    constexpr float kG = -9.81F;
    world->set_gravity({ 0.0F, kG, 0.0F });

    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    const auto h = world->create_body(d);
    ASSERT_TRUE(h.has_value());

    constexpr int kSteps = 120;
    constexpr float dt = 1.0F / 60.0F;
    for (int i = 0; i < kSteps; ++i)
    {
        world->step(dt);
    }
    const float undamped = kG * (static_cast<float>(kSteps) * dt);  // ≈ -19.62
    const float vy       = world->linear_velocity(*h).y;
    EXPECT_LT(vy, 0.0F);          // accelerating downward
    EXPECT_GT(vy, undamped);      // damped → less negative than the undamped bound
    EXPECT_NEAR(vy, undamped, 2.0F);  // but still within the damping band
}

// ---- 17. Invalid / null handle guards — every accessor stays safe ----------

TEST(JoltWorldDepth, InvalidHandleAccessorsAreSafeAndReturnDefaults)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();

    const cd::physics::BodyHandle null_handle {};  // value == 0
    // A packed-but-never-issued handle (index 9999, generation 7).
    const cd::physics::BodyHandle stale_handle {
        cd::physics::BodyHandle::pack(9999U, std::uint16_t { 7U })
    };

    for (const auto bogus : { null_handle, stale_handle })
    {
        // Reads return defaults, never UB / crash.
        const auto p = world->position(bogus);
        EXPECT_TRUE(approx_eq(p.x, 0.0F));
        EXPECT_TRUE(approx_eq(p.y, 0.0F));
        EXPECT_TRUE(approx_eq(p.z, 0.0F));
        const auto v = world->linear_velocity(bogus);
        EXPECT_TRUE(approx_eq(v.x, 0.0F));
        EXPECT_TRUE(approx_eq(v.y, 0.0F));
        EXPECT_EQ(world->body_type(bogus), cd::physics::BodyType::kStatic);

        // Mutators on an unknown handle are silent no-ops.
        world->set_position(bogus, { 1.0F, 1.0F, 1.0F });
        world->set_linear_velocity(bogus, { 1.0F, 1.0F, 1.0F });
        world->apply_force(bogus, { 1.0F, 1.0F, 1.0F });
        world->apply_impulse(bogus, { 1.0F, 1.0F, 1.0F });
        world->destroy_body(bogus);  // double / unknown destroy is harmless
    }

    // None of the above created or destroyed any state.
    EXPECT_EQ(world->body_count(), 0U);

    // Collider / joint side-band tolerates an invalid owner gracefully.
    EXPECT_EQ(cd::physics_jolt::collider_count(*world, null_handle), 0U);
    EXPECT_EQ(cd::physics_jolt::joint_count(*world), 0U);
}

// ---- 18. Collider registration — multiple shapes per owner + isolation -----

TEST(JoltWorldDepth, ColliderCountsArePerOwnerAndIsolated)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();

    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    const auto a = world->create_body(d);
    const auto b = world->create_body(d);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());

    cd::physics_jolt::components::ColliderComponent box {};
    box.shape = cd::physics_jolt::components::BoxShape { { 1.0F, 1.0F, 1.0F } };
    cd::physics_jolt::components::ColliderComponent capsule {};
    capsule.shape = cd::physics_jolt::components::CapsuleShape { 0.5F, 0.25F };

    EXPECT_TRUE(cd::physics_jolt::attach_collider(*world, *a, box));
    EXPECT_TRUE(cd::physics_jolt::attach_collider(*world, *a, capsule));
    EXPECT_TRUE(cd::physics_jolt::attach_collider(*world, *b, box));

    EXPECT_EQ(cd::physics_jolt::collider_count(*world, *a), 2U);
    EXPECT_EQ(cd::physics_jolt::collider_count(*world, *b), 1U);

    // An owner with no colliders reports zero, not a fabricated count.
    cd::physics::BodyDesc d2 {};
    d2.mass = 1.0F;
    const auto c = world->create_body(d2);
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(cd::physics_jolt::collider_count(*world, *c), 0U);
}

// ---- 19. Joint table survives body removal (dangling-endpoint prune) -------

TEST(JoltWorldDepth, JointTablePrunesOnEitherEndpointDestroy)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();

    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    const auto a = world->create_body(d);
    const auto b = world->create_body(d);
    const auto cc = world->create_body(d);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(cc.has_value());

    cd::physics_jolt::components::JointComponent ab {};
    ab.kind = cd::physics_jolt::components::JointKind::kFixed;
    ab.body_a = *a;
    ab.body_b = *b;

    cd::physics_jolt::components::JointComponent bc {};
    bc.kind = cd::physics_jolt::components::JointKind::kDistance;
    bc.body_a = *b;
    bc.body_b = *cc;

    EXPECT_TRUE(cd::physics_jolt::attach_joint(*world, ab));
    EXPECT_TRUE(cd::physics_jolt::attach_joint(*world, bc));
    EXPECT_EQ(cd::physics_jolt::joint_count(*world), 2U);

    // Destroying `b` references BOTH joints -> both pruned.
    world->destroy_body(*b);
    EXPECT_EQ(cd::physics_jolt::joint_count(*world), 0U);
    EXPECT_EQ(world->body_count(), 2U);
}

// ---- 20. Multiple bodies in one world are independent -----------------------

TEST(JoltWorldDepth, ManyBodiesIntegrateIndependently)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();
    world->set_gravity({ 0.0F, -9.81F, 0.0F });

    constexpr std::size_t kCount = 32;
    std::vector<cd::physics::BodyHandle> handles;
    handles.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i)
    {
        cd::physics::BodyDesc d {};
        d.mass = 1.0F;
        // Distinct starting heights so we can detect any cross-talk.
        d.position = { 0.0F, static_cast<float>(i), 0.0F };
        auto r = world->create_body(d);
        ASSERT_TRUE(r.has_value());
        handles.push_back(*r);
    }
    EXPECT_EQ(world->body_count(), kCount);

    constexpr float dt = 1.0F / 60.0F;
    for (int i = 0; i < 30; ++i)
    {
        world->step(dt);
    }

    // Each body fell by the SAME amount (same gravity, no force) yet kept its
    // unique offset — proof there is no shared-state aliasing across records.
    const float drop = world->position(handles.front()).y - 0.0F;
    for (std::size_t i = 0; i < kCount; ++i)
    {
        const float expected = static_cast<float>(i) + drop;
        EXPECT_NEAR(world->position(handles[i]).y, expected, 1e-3F);
    }
}

// ---- 21. Shutdown / reset — destroy-all then rebuild a clean world ----------

TEST(JoltWorldDepth, DestroyAllThenRebuildLeavesNoResidue)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();

    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    const auto owner = world->create_body(d);
    ASSERT_TRUE(owner.has_value());

    cd::physics_jolt::components::ColliderComponent col {};
    col.shape = cd::physics_jolt::components::SphereShape { 1.0F };
    EXPECT_TRUE(cd::physics_jolt::attach_collider(*world, *owner, col));

    cd::physics_jolt::components::JointComponent joint {};
    joint.body_a = *owner;
    joint.body_b = *owner;
    EXPECT_TRUE(cd::physics_jolt::attach_joint(*world, joint));
    EXPECT_EQ(cd::physics_jolt::joint_count(*world), 1U);

    // Destroy the only body: joints referencing it are pruned.
    world->destroy_body(*owner);
    EXPECT_EQ(world->body_count(), 0U);
    EXPECT_EQ(cd::physics_jolt::joint_count(*world), 0U);

    // Rebuild — the world is still usable and starts from a clean slate.
    const auto fresh = world->create_body(d);
    ASSERT_TRUE(fresh.has_value());
    EXPECT_EQ(world->body_count(), 1U);
    EXPECT_EQ(cd::physics_jolt::joint_count(*world), 0U);
}

// ---- 22. Backend config defaults round-trip when no override supplied -------

TEST(JoltWorldDepth, DefaultBackendConfigMatchesHeaderDefaults)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();
    const auto cfg = cd::physics_jolt::backend_config(*world);
    // Defaults declared in JoltBackendConfig — pin them so a silent drift fails.
    EXPECT_EQ(cfg.max_bodies, 65536U);
    EXPECT_EQ(cfg.max_body_pairs, 65536U);
    EXPECT_EQ(cfg.max_contact_constraints, 10240U);
    EXPECT_EQ(cfg.max_barriers, 8U);
    EXPECT_FLOAT_EQ(cfg.default_linear_damping, 0.05F);
    EXPECT_FLOAT_EQ(cfg.default_angular_damping, 0.05F);
}

// ---- 23. Mapper: explicit damping is preserved, mesh hash is order-stable ---

TEST(JoltMappersDepth, ExplicitDampingKeptAndMeshHashStable)
{
    cd::physics_jolt::JoltBackendConfig cfg {};
    cfg.default_linear_damping = 0.5F;

    // A caller-supplied positive damping must win over the config default.
    cd::physics::BodyDesc d {};
    d.type = cd::physics::BodyType::kDynamic;
    d.mass = 2.0F;
    d.linear_damping = 0.125F;
    const auto rb = cd::physics_jolt::body_desc_to_component(d, cfg);
    EXPECT_FLOAT_EQ(rb.linear_damping, 0.125F);
    EXPECT_FLOAT_EQ(rb.angular_damping, cfg.default_angular_damping);

    // Mesh-shape hashing is deterministic and sensitive to counts.
    using cd::physics_jolt::components::MeshShape;
    using cd::physics_jolt::components::ShapeDesc;
    using cd::physics_jolt::hash_shape_desc;

    const std::array<float, 9> verts { 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F };
    const std::array<std::uint32_t, 3> idx { 0U, 1U, 2U };

    MeshShape m {};
    m.vertices = verts.data();
    m.vertex_count = verts.size();
    m.indices = idx.data();
    m.index_count = idx.size();
    const ShapeDesc mesh_a { m };
    const ShapeDesc mesh_a_again { m };
    EXPECT_EQ(hash_shape_desc(mesh_a), hash_shape_desc(mesh_a_again));

    MeshShape m_diff = m;
    m_diff.index_count = 6U;  // different topology
    const ShapeDesc mesh_b { m_diff };
    EXPECT_NE(hash_shape_desc(mesh_a), hash_shape_desc(mesh_b));

    // A mesh hash never collides with a box hash sharing the byte pattern.
    const ShapeDesc box { cd::physics_jolt::components::BoxShape { { 0.5F, 0.5F, 0.5F } } };
    EXPECT_NE(hash_shape_desc(mesh_a), hash_shape_desc(box));
}

// ---- 24. Vehicle constraint surface — SEALED behind CD_PHYSICS_JOLT_REAL ---
//
// The vehicle path is a thin wrapper over JPH::VehicleConstraint +
// WheeledVehicleController, which only exist when the vendored Jolt library is
// linked. Without the vendor we CANNOT verify the real lifecycle here, so the
// real-only assertions are sealed behind CD_PHYSICS_JOLT_REAL. The DEFAULT
// stub path is fully exercised: every entry point is a documented no-op
// (kInvalid handle / false contact / silent destroy) and we pin exactly that.

TEST(JoltVehicleDepth, StubVehicleSurfaceIsDocumentedNoOp)
{
    auto world = cd::physics_jolt::make_jolt_physics_world();

    cd::physics::BodyDesc chassis_desc {};
    chassis_desc.mass = 1400.0F;
    const auto chassis = world->create_body(chassis_desc);
    ASSERT_TRUE(chassis.has_value());

    cd::physics_jolt::VehicleConstraintDesc desc {};
    desc.total_mass_kg = 1400.0F;
    desc.wheels[0].is_driven = true;
    const auto handle =
        cd::physics_jolt::create_vehicle_constraint(*world, *chassis, desc);

#if defined(CD_PHYSICS_JOLT_REAL)
    // Real backend: a valid chassis must yield a live constraint handle.
    // SEALED — cannot run here without the vendored Jolt library.
    EXPECT_TRUE(handle.is_valid())
        << "real Jolt backend must build a VehicleConstraint for a valid chassis";
#else
    // Stub fallback: the surface is a documented no-op. Handle is kInvalid,
    // wheel contact is always false, and the lifecycle calls are harmless.
    EXPECT_FALSE(handle.is_valid());
    EXPECT_EQ(handle.index, cd::physics_jolt::VehicleConstraintHandle::kInvalid.index);

    // Driver input + destroy on an invalid handle must not throw or crash.
    cd::physics_jolt::set_vehicle_driver_input(*world, handle, 1.0F, 0.0F, 0.0F);
    for (int wheel = 0; wheel < 4; ++wheel)
    {
        EXPECT_FALSE(
            cd::physics_jolt::get_vehicle_wheel_contact(*world, handle, wheel));
    }
    cd::physics_jolt::destroy_vehicle_constraint(*world, handle);

    // The vehicle no-ops never perturbed body bookkeeping.
    EXPECT_EQ(world->body_count(), 1U);
#endif
}

// ---- 25. Side-band rejects a foreign (non-Jolt) IPhysicsWorld --------------

TEST(JoltWorldDepth, SidebandRejectsForeignWorld)
{
    // The built-in Euler world is NOT a JoltWorldStub; attach_* must decline
    // it (return false / 0) so caller code stays portable across backends.
    auto foreign = cd::physics::make_builtin_physics_world();
    ASSERT_NE(foreign, nullptr);

    cd::physics::BodyDesc d {};
    d.mass = 1.0F;
    const auto body = foreign->create_body(d);
    ASSERT_TRUE(body.has_value());

    cd::physics_jolt::components::ColliderComponent col {};
    col.shape = cd::physics_jolt::components::SphereShape { 1.0F };
    EXPECT_FALSE(cd::physics_jolt::attach_collider(*foreign, *body, col));
    EXPECT_EQ(cd::physics_jolt::collider_count(*foreign, *body), 0U);

    cd::physics_jolt::components::JointComponent joint {};
    joint.body_a = *body;
    joint.body_b = *body;
    EXPECT_FALSE(cd::physics_jolt::attach_joint(*foreign, joint));
    EXPECT_EQ(cd::physics_jolt::joint_count(*foreign), 0U);

    // backend_config falls back to header defaults for a foreign world.
    const auto cfg = cd::physics_jolt::backend_config(*foreign);
    EXPECT_EQ(cfg.max_bodies, 65536U);
}

}  // namespace
