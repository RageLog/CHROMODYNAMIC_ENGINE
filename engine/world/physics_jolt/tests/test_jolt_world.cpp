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
    // The stub identifies itself so feature gates / sample skips can pivot
    // on it cleanly. When the real backend lands this becomes `false`.
    EXPECT_TRUE(cd::physics_jolt::is_stub_backend());
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

}  // namespace
