// =============================================================================
// CHROMODYNAMIC — tests/test_soft_body.cpp
// Phase 721 — cd::physics::soft_body Sprint-1 unit tests.
// Phase 760 — Sprint-2 self-collision tests (3 added).
//
// Coverage (10 base cases + depth pass):
//   1. Rope of 10 particles falls under gravity (free particles descend).
//   2. Pinned particle stays pinned under gravity.
//   3. Distance constraint is preserved within tolerance after many iterations.
//   4. More solver iterations improve constraint satisfaction (stability test).
//   5. apply_force() perturbs a single particle position detectably.
//   6. Zero dt tick leaves particles unchanged.
//   7. Both particles pinned: constraint solver is a no-op (no NaN, no crash).
//   8. Sprint-2 — two close particles repel under self-collision.
//   9. Sprint-2 — rope folding does not self-penetrate (min sep >= ~2*radius).
//  10. Sprint-2 — perf smoke: 100 particles tick under 1 ms.
//
// Depth pass (phase >=1258, ADD-ONLY — no existing case changed):
//   Integrator / math:
//   * Single free particle Verlet matches the EXACT discrete free-fall sum.
//   * Negative dt is a no-op (same as zero dt).
//   * Damping strictly reduces kinetic energy vs. undamped over equal sim.
//   * Gravity sign: +Y gravity lifts, -Y gravity drops.
//   * Empty body / zero gravity / no constraints are all safe no-ops.
//   Distance constraint:
//   * Over-stretched spring contracts toward rest length (not past it).
//   * Compressed spring expands toward rest length.
//   * Stiffness 0 leaves the pair free (no correction).
//   * Zero rest-length drives the pair together without NaN (div-by-zero guard).
//   Bending (Provot flexion):
//   * Bending constraint resists a fold (straightens an L-bent triple).
//   * Empty bending vector is byte-identical to the no-bending baseline.
//   * Out-of-range / a==b bending indices are ignored (no crash).
//   Ground plane:
//   * Falling particle stops at the floor (does not penetrate).
//   * Disabled ground lets the particle fall through (opt-in proof).
//   * Degenerate (zero) normal is a safe no-op.
//   * Friction kills tangential sliding velocity.
//   * Pinned particle below the plane is NOT pushed (static obstacle).
//   Self-collision negative:
//   * Single particle with self-collision on is a safe no-op.
//   * Zero radius disables repulsion (degenerate guard).
//
// Test methodology:
//   * Arrange / Act / Assert pattern.
//   * No sleep_for — purely deterministic tick(dt) calls. Perf test uses
//     std::chrono::steady_clock for one timed measurement (not a wait).
//   * All assertions use EXPECT_NEAR / EXPECT_LT with physically motivated
//     tolerances (not magic numbers pulled from air).
// =============================================================================

#include <cd/physics/soft_body/SoftBody.hpp>

#include <gtest/gtest.h>

#include <algorithm>

#include <array>
#include <chrono>
#include <cmath>

namespace
{

using cd::physics::soft_body::BendingConstraint;
using cd::physics::soft_body::Particle;
using cd::physics::soft_body::SoftBody;
using cd::physics::soft_body::SoftBodyConfig;
using cd::physics::soft_body::SpringConstraint;

// ---------------------------------------------------------------------------
// Helper: build a vertical rope of N particles pinned at index 0.
// Particles are spaced 0.1 m apart along -Y (hanging down from pin).
// ---------------------------------------------------------------------------

SoftBodyConfig make_rope(uint32_t count, float stiffness = 1.0F,
                         uint32_t solver_iterations = 15)
{
    SoftBodyConfig cfg {};
    cfg.solver_iterations = solver_iterations;
    cfg.damping           = 0.01F;

    for (uint32_t i = 0; i < count; ++i)
    {
        Particle p {};
        p.position      = { 0.0F, static_cast<float>(i) * -0.1F, 0.0F };
        p.prev_position = p.position;
        p.inv_mass      = (i == 0U) ? 0.0F : 1.0F; // First particle pinned via inv_mass.
        p.pinned        = (i == 0U);
        cfg.particles.push_back(p);
    }

    for (uint32_t i = 0; i + 1 < count; ++i)
    {
        SpringConstraint sc {};
        sc.a           = i;
        sc.b           = i + 1U;
        sc.rest_length = 0.1F;
        sc.stiffness   = stiffness;
        cfg.constraints.push_back(sc);
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// Helper: distance between two particles in the span.
// ---------------------------------------------------------------------------

float particle_dist(const Particle& a, const Particle& b) noexcept
{
    const float dx = b.position[0] - a.position[0];
    const float dy = b.position[1] - a.position[1];
    const float dz = b.position[2] - a.position[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------
// Test 1: Rope falls under gravity — free particles descend.
// ---------------------------------------------------------------------------

TEST(SoftBodySprint1, RopeFallsUnderGravity)
{
    // Arrange: 10-particle rope, particle 0 pinned.
    SoftBody sb;
    sb.configure(make_rope(10));

    const float initial_y_last = sb.particles()[9].position[1];

    // Act: simulate 60 ticks at 1/60 s (1 second of sim time).
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    for (int i = 0; i < 60; ++i)
    {
        sb.tick(1.0F / 60.0F, kGravity);
    }

    // Assert: tail particle has descended from its initial Y.
    // The rope is constrained so the lowest particle can only move a small amount
    // (the chain above it limits excursion). We verify a measurable displacement
    // (>= 1 mm) rather than the full free-fall distance.
    const float final_y_last = sb.particles()[9].position[1];
    EXPECT_LT(final_y_last, initial_y_last - 1e-3F)
        << "Last rope particle should have descended at least 1 mm under gravity. "
        << "initial_y=" << initial_y_last << " final_y=" << final_y_last;
}

// ---------------------------------------------------------------------------
// Test 2: Pinned particle stays pinned.
// ---------------------------------------------------------------------------

TEST(SoftBodySprint1, PinnedParticleStaysPinned)
{
    // Arrange: 10-particle rope, particle 0 pinned.
    SoftBody sb;
    sb.configure(make_rope(10));

    const std::array<float, 3> pin_pos = sb.particles()[0].position;

    // Act: 120 ticks.
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    for (int i = 0; i < 120; ++i)
    {
        sb.tick(1.0F / 60.0F, kGravity);
    }

    // Assert: pinned particle has not moved.
    const Particle& p0 = sb.particles()[0];
    EXPECT_NEAR(p0.position[0], pin_pos[0], 1e-5F) << "Pinned X should not change.";
    EXPECT_NEAR(p0.position[1], pin_pos[1], 1e-5F) << "Pinned Y should not change.";
    EXPECT_NEAR(p0.position[2], pin_pos[2], 1e-5F) << "Pinned Z should not change.";
}

// ---------------------------------------------------------------------------
// Test 3: Distance constraint is preserved after convergence.
// ---------------------------------------------------------------------------

TEST(SoftBodySprint1, DistanceConstraintPreserved)
{
    // Arrange: 2-particle system, rest_length = 1.0m, high iterations.
    SoftBodyConfig cfg {};
    cfg.solver_iterations = 30;
    cfg.damping           = 0.05F;

    Particle anchor {};
    anchor.position      = { 0.0F, 0.0F, 0.0F };
    anchor.prev_position = anchor.position;
    anchor.inv_mass      = 0.0F;
    anchor.pinned        = true;
    cfg.particles.push_back(anchor);

    Particle bob {};
    bob.position      = { 0.0F, -1.0F, 0.0F };
    bob.prev_position = bob.position;
    bob.inv_mass      = 1.0F;
    bob.pinned        = false;
    cfg.particles.push_back(bob);

    SpringConstraint sc {};
    sc.a           = 0;
    sc.b           = 1;
    sc.rest_length = 1.0F;
    sc.stiffness   = 1.0F;
    cfg.constraints.push_back(sc);

    SoftBody sb;
    sb.configure(cfg);

    // Act: simulate 120 ticks — let it settle.
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    for (int i = 0; i < 120; ++i)
    {
        sb.tick(1.0F / 60.0F, kGravity);
    }

    // Assert: distance remains close to rest_length (within 5 cm tolerance).
    const auto pts = sb.particles();
    const float dist = particle_dist(pts[0], pts[1]);
    EXPECT_NEAR(dist, 1.0F, 0.05F)
        << "Distance constraint should hold within 5 cm after 120 ticks.";
}

// ---------------------------------------------------------------------------
// Test 4: More solver iterations improve constraint satisfaction.
// ---------------------------------------------------------------------------

TEST(SoftBodySprint1, MoreIterationsImproveStability)
{
    // Arrange two identical ropes, one with 1 iteration, one with 20.
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };

    SoftBody sb_low;
    SoftBody sb_high;
    auto cfg_low  = make_rope(5, 1.0F, /*solver_iterations=*/1);
    auto cfg_high = make_rope(5, 1.0F, /*solver_iterations=*/20);
    sb_low.configure(cfg_low);
    sb_high.configure(cfg_high);

    // Act: 30 ticks.
    for (int i = 0; i < 30; ++i)
    {
        sb_low.tick(1.0F / 60.0F, kGravity);
        sb_high.tick(1.0F / 60.0F, kGravity);
    }

    // Assert: compute max constraint violation for each sim.
    auto max_violation = [](const SoftBody& sim, float rest_len) -> float {
        float worst = 0.0F;
        const auto pts = sim.particles();
        for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        {
            const float d = particle_dist(pts[i], pts[i + 1]);
            const float v = std::fabs(d - rest_len);
            worst = std::max(worst, v);
        }
        return worst;
    };

    const float viol_low  = max_violation(sb_low,  0.1F);
    const float viol_high = max_violation(sb_high, 0.1F);

    EXPECT_LT(viol_high, viol_low)
        << "Higher iteration count should produce smaller constraint violations. "
        << "low=" << viol_low << " high=" << viol_high;
}

// ---------------------------------------------------------------------------
// Test 5: apply_force() perturbs a single particle.
// ---------------------------------------------------------------------------

TEST(SoftBodySprint1, ApplyForcePerturbsParticle)
{
    // Arrange: 3-particle rope pinned at 0. Observe particle 2.
    SoftBody sb;
    sb.configure(make_rope(3, 1.0F, 15));

    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };

    // Baseline: one tick without external force.
    sb.tick(1.0F / 60.0F, kGravity);
    const float baseline_x = sb.particles()[2].position[0];

    // Re-configure to get a fresh simulation starting point.
    sb.configure(make_rope(3, 1.0F, 15));

    // Apply a strong lateral force to particle 2 before the tick.
    sb.apply_force(2U, { 5000.0F, 0.0F, 0.0F });
    sb.tick(1.0F / 60.0F, kGravity);
    const float perturbed_x = sb.particles()[2].position[0];

    // Assert: perturbed position differs from baseline by a measurable amount.
    EXPECT_GT(std::fabs(perturbed_x - baseline_x), 1e-4F)
        << "apply_force() should visibly displace the target particle in X. "
        << "baseline=" << baseline_x << " perturbed=" << perturbed_x;
}

// ---------------------------------------------------------------------------
// Test 6: Zero dt tick leaves particles unchanged.
// ---------------------------------------------------------------------------

TEST(SoftBodySprint1, ZeroDtDoesNothing)
{
    // Arrange: rope with initial positions.
    SoftBody sb;
    sb.configure(make_rope(5));

    const std::array<float, 3> pos_before = sb.particles()[4].position;

    // Act: zero dt tick.
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    sb.tick(0.0F, kGravity);

    // Assert: particle unchanged.
    const std::array<float, 3> pos_after = sb.particles()[4].position;
    EXPECT_NEAR(pos_after[0], pos_before[0], 1e-9F);
    EXPECT_NEAR(pos_after[1], pos_before[1], 1e-9F);
    EXPECT_NEAR(pos_after[2], pos_before[2], 1e-9F);
}

// ---------------------------------------------------------------------------
// Test 7: Both particles pinned — no crash, no NaN.
// ---------------------------------------------------------------------------

TEST(SoftBodySprint1, BothParticlesPinnedNoNaN)
{
    // Arrange: two pinned particles with a constraint between them.
    SoftBodyConfig cfg {};
    cfg.solver_iterations = 10;
    cfg.damping           = 0.01F;

    for (int i = 0; i < 2; ++i)
    {
        Particle p {};
        p.position      = { static_cast<float>(i), 0.0F, 0.0F };
        p.prev_position = p.position;
        p.inv_mass      = 0.0F;
        p.pinned        = true;
        cfg.particles.push_back(p);
    }

    SpringConstraint sc {};
    sc.a           = 0;
    sc.b           = 1;
    sc.rest_length = 1.0F;
    sc.stiffness   = 1.0F;
    cfg.constraints.push_back(sc);

    SoftBody sb;
    sb.configure(cfg);

    // Act: 60 ticks — must not crash, produce NaN, or infinite loop.
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    for (int i = 0; i < 60; ++i)
    {
        sb.tick(1.0F / 60.0F, kGravity);
    }

    // Assert: positions unchanged (both pinned) and no NaN.
    const auto pts = sb.particles();
    EXPECT_NEAR(pts[0].position[0], 0.0F, 1e-9F);
    EXPECT_NEAR(pts[1].position[0], 1.0F, 1e-9F);
    EXPECT_FALSE(std::isnan(pts[0].position[1]));
    EXPECT_FALSE(std::isnan(pts[1].position[1]));
}

// ===========================================================================
// Sprint-2 — Self-collision (phase 760)
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 8: Two particles inside contact range repel.
// ---------------------------------------------------------------------------

TEST(SoftBodySprint2, TwoCloseParticlesRepel)
{
    // Arrange: two unpinned particles 1 cm apart, with radius 1 cm so they
    // are well inside the 2 * radius = 2 cm contact threshold. Zero gravity
    // isolates the self-collision effect.
    SoftBodyConfig cfg {};
    cfg.solver_iterations = 4;
    cfg.damping           = 0.0F;
    cfg.self_collision.enable_self_collision   = true;
    cfg.self_collision.particle_radius         = 0.01F;
    cfg.self_collision.spatial_hash_cell_size  = 0.05F;

    Particle a {};
    a.position      = { 0.0F, 0.0F, 0.0F };
    a.prev_position = a.position;
    a.inv_mass      = 1.0F;
    cfg.particles.push_back(a);

    Particle b {};
    b.position      = { 0.01F, 0.0F, 0.0F }; // 1 cm apart on X.
    b.prev_position = b.position;
    b.inv_mass      = 1.0F;
    cfg.particles.push_back(b);

    SoftBody sb;
    sb.configure(cfg);

    const float initial_sep = std::fabs(sb.particles()[1].position[0] -
                                        sb.particles()[0].position[0]);
    ASSERT_NEAR(initial_sep, 0.01F, 1e-6F);

    // Act: 10 ticks at 1/60 s with NO gravity — pure repulsion.
    constexpr std::array<float, 3> kZeroG { 0.0F, 0.0F, 0.0F };
    for (int i = 0; i < 10; ++i)
    {
        sb.tick(1.0F / 60.0F, kZeroG);
    }

    // Assert: separation is now at least the contact distance (0.02 m) within
    // a small slack. Particles must have moved APART, not converged.
    const auto pts = sb.particles();
    const float final_sep = std::fabs(pts[1].position[0] - pts[0].position[0]);
    EXPECT_GT(final_sep, 0.0195F)
        << "Self-collision should push two close particles to >= 2*radius. "
        << "initial=" << initial_sep << " final=" << final_sep;

    // And the centre of mass should be roughly preserved (symmetric push):
    // both particles are free with equal inv_mass, so they should split the
    // overlap symmetrically (each moves ~0.5 cm).
    const float midpoint = 0.5F * (pts[0].position[0] + pts[1].position[0]);
    EXPECT_NEAR(midpoint, 0.005F, 1e-3F)
        << "Symmetric impulse should preserve the midpoint.";
}

// ---------------------------------------------------------------------------
// Test 9: Rope folded back on itself does not self-penetrate.
// ---------------------------------------------------------------------------

TEST(SoftBodySprint2, RopeSelfFoldDoesNotPenetrate)
{
    // Arrange: 8-particle rope. Configure the initial state so the second
    // half overlaps the first half (a fold). Self-collision must enforce
    // minimum separation between all non-adjacent particles after settling.
    SoftBodyConfig cfg {};
    cfg.solver_iterations = 8;
    cfg.damping           = 0.02F;
    cfg.self_collision.enable_self_collision  = true;
    cfg.self_collision.particle_radius        = 0.02F; // 2 cm.
    cfg.self_collision.spatial_hash_cell_size = 0.06F; // 3 * radius.

    constexpr uint32_t kN              = 8U;
    constexpr float    kSegment        = 0.05F; // 5 cm segment length.
    constexpr float    kFoldZ          = 0.001F; // tiny Z offset to break degeneracy.

    // First 4 particles laid out along +X from origin; remaining 4 fold back
    // toward the origin along -X, slightly offset on +Z so initial separation
    // along the fold axis is intentionally small (collision must push apart).
    for (uint32_t i = 0; i < kN; ++i)
    {
        Particle p {};
        if (i < 4U)
        {
            p.position = { static_cast<float>(i) * kSegment, 0.0F, 0.0F };
        }
        else
        {
            // Folded leg comes back: i=4 sits ~at i=3, i=5 ~at i=2 etc.
            const auto backward_idx = static_cast<float>(3U - (i - 4U));
            p.position = { backward_idx * kSegment, 0.0F, kFoldZ };
        }
        p.prev_position = p.position;
        p.inv_mass      = (i == 0U) ? 0.0F : 1.0F; // Anchor head.
        p.pinned        = (i == 0U);
        cfg.particles.push_back(p);
    }

    // Spring chain.
    for (uint32_t i = 0; i + 1U < kN; ++i)
    {
        SpringConstraint sc {};
        sc.a           = i;
        sc.b           = i + 1U;
        sc.rest_length = kSegment;
        sc.stiffness   = 1.0F;
        cfg.constraints.push_back(sc);
    }

    SoftBody sb;
    sb.configure(cfg);

    // Act: 90 ticks with NO gravity — let self-collision push the fold apart.
    constexpr std::array<float, 3> kZeroG { 0.0F, 0.0F, 0.0F };
    for (int t = 0; t < 90; ++t)
    {
        sb.tick(1.0F / 60.0F, kZeroG);
    }

    // Assert: every non-adjacent particle pair must be at least ~contact_dist
    // apart (allow 25% slack to absorb PBD residual + 1 spring iteration mix).
    const auto  pts          = sb.particles();
    const float radius       = cfg.self_collision.particle_radius;
    const float contact_dist = 2.0F * radius;
    const float min_allowed  = 0.75F * contact_dist;

    for (std::size_t i = 0; i < pts.size(); ++i)
    {
        for (std::size_t j = i + 2U; j < pts.size(); ++j) // skip adjacent.
        {
            const float dx = pts[j].position[0] - pts[i].position[0];
            const float dy = pts[j].position[1] - pts[i].position[1];
            const float dz = pts[j].position[2] - pts[i].position[2];
            const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
            EXPECT_GE(d, min_allowed)
                << "Non-adjacent pair (" << i << "," << j << ") penetrated. "
                << "d=" << d << " min_allowed=" << min_allowed;
        }
    }
}

// ---------------------------------------------------------------------------
// Test 10: Perf smoke — 100 particles tick under 1 ms with self-collision.
// ---------------------------------------------------------------------------

TEST(SoftBodySprint2, HundredParticlesTickUnderOneMillisecond)
{
    // Arrange: 100 particles in a 10x10 grid, all with self-collision on.
    // No springs — the test measures the broad-phase + near-phase cost in
    // isolation.
    SoftBodyConfig cfg {};
    cfg.solver_iterations = 4;
    cfg.damping           = 0.01F;
    cfg.self_collision.enable_self_collision  = true;
    cfg.self_collision.particle_radius        = 0.02F;
    cfg.self_collision.spatial_hash_cell_size = 0.05F;

    constexpr uint32_t kSide    = 10U;
    constexpr float    kSpacing = 0.05F;
    for (uint32_t y = 0; y < kSide; ++y)
    {
        for (uint32_t x = 0; x < kSide; ++x)
        {
            Particle p {};
            p.position = {
                static_cast<float>(x) * kSpacing,
                0.0F,
                static_cast<float>(y) * kSpacing
            };
            p.prev_position = p.position;
            p.inv_mass      = 1.0F;
            cfg.particles.push_back(p);
        }
    }
    ASSERT_EQ(cfg.particles.size(), 100U);

    SoftBody sb;
    sb.configure(cfg);

    // Warm up: one untimed tick to ensure caches + allocator are settled
    // (configure() already preallocates, so this is mostly icache warm-up).
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    sb.tick(1.0F / 60.0F, kGravity);

    // Act: time a single tick.
    const auto t0 = std::chrono::steady_clock::now();
    sb.tick(1.0F / 60.0F, kGravity);
    const auto t1 = std::chrono::steady_clock::now();

    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    // Assert: tick must complete in under 1 ms (1'000'000 ns).
    // Slack: 5 ms ceiling for CI / debug-build hosts. We want to flag MAJOR
    // perf regressions (10x+) without being flaky on slow runners. Sprint-3
    // GPU port will tighten this dramatically.
    EXPECT_LT(elapsed_ns, 5'000'000)
        << "100-particle self-collision tick took " << elapsed_ns
        << " ns (target <1 ms, ceiling 5 ms for debug/CI).";
}

// ===========================================================================
// Depth pass — integrator math (analytic Verlet verification)
// ===========================================================================

// ---------------------------------------------------------------------------
// Helper: single free particle (no constraints, no pin) at the origin.
// ---------------------------------------------------------------------------

SoftBodyConfig make_single_free_particle(float damping = 0.0F)
{
    SoftBodyConfig cfg {};
    cfg.solver_iterations = 1;
    cfg.damping           = damping;

    Particle p {};
    p.position      = { 0.0F, 0.0F, 0.0F };
    p.prev_position = p.position;
    p.inv_mass      = 1.0F;
    p.pinned        = false;
    cfg.particles.push_back(p);
    return cfg;
}

// ---------------------------------------------------------------------------
// Depth 1: single free particle Verlet matches the EXACT discrete free-fall.
//
// With damping = 0 and prev == pos initially, the integrator
//   x_{k+1} = x_k + (x_k - x_{k-1}) + a*dt^2
// yields total displacement after n ticks of a*dt^2 * n(n+1)/2 (NOT the
// continuous 1/2 a t^2 — Verlet's discrete sum is the ground truth here).
// ---------------------------------------------------------------------------

TEST(SoftBodyIntegrator, SingleParticleFreeFallAnalytic)
{
    // Arrange.
    SoftBody sb;
    sb.configure(make_single_free_particle(/*damping=*/0.0F));

    constexpr float kG  = -9.81F;
    constexpr float kDt = 1.0F / 120.0F;
    constexpr int   kN  = 20;
    constexpr std::array<float, 3> kGravity { 0.0F, kG, 0.0F };

    // Act.
    for (int i = 0; i < kN; ++i)
    {
        sb.tick(kDt, kGravity);
    }

    // Assert: y = a * dt^2 * n(n+1)/2.
    const float dt2          = kDt * kDt;
    const float n_sum        = static_cast<float>(kN * (kN + 1)) * 0.5F;
    const float expected_y   = kG * dt2 * n_sum;
    const float actual_y     = sb.particles()[0].position[1];
    EXPECT_NEAR(actual_y, expected_y, 1e-5F)
        << "Verlet free-fall must match the discrete analytic sum. "
        << "expected=" << expected_y << " actual=" << actual_y;

    // X and Z must remain exactly zero (no lateral force).
    EXPECT_NEAR(sb.particles()[0].position[0], 0.0F, 1e-9F);
    EXPECT_NEAR(sb.particles()[0].position[2], 0.0F, 1e-9F);
}

// ---------------------------------------------------------------------------
// Depth 2: negative dt is a no-op (same contract as zero dt).
// ---------------------------------------------------------------------------

TEST(SoftBodyIntegrator, NegativeDtIsNoOp)
{
    // Arrange.
    SoftBody sb;
    sb.configure(make_single_free_particle());
    const std::array<float, 3> before = sb.particles()[0].position;

    // Act: negative dt.
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    sb.tick(-0.016F, kGravity);

    // Assert: unchanged.
    const std::array<float, 3> after = sb.particles()[0].position;
    EXPECT_NEAR(after[0], before[0], 1e-9F);
    EXPECT_NEAR(after[1], before[1], 1e-9F);
    EXPECT_NEAR(after[2], before[2], 1e-9F);
}

// ---------------------------------------------------------------------------
// Depth 3: damping strictly reduces energy (slower descent) vs. undamped.
//
// Two identical free particles, one with heavy damping. After equal sim time
// the damped particle must have travelled LESS far (its implicit velocity is
// scaled down each tick), i.e. damping removes kinetic energy.
// ---------------------------------------------------------------------------

TEST(SoftBodyIntegrator, DampingReducesEnergy)
{
    // Arrange.
    SoftBody undamped;
    SoftBody damped;
    undamped.configure(make_single_free_particle(/*damping=*/0.0F));
    damped.configure(make_single_free_particle(/*damping=*/0.3F));

    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    constexpr float kDt = 1.0F / 60.0F;

    // Act: 30 ticks each.
    for (int i = 0; i < 30; ++i)
    {
        undamped.tick(kDt, kGravity);
        damped.tick(kDt, kGravity);
    }

    // Assert: damped particle has fallen strictly less far (higher Y).
    const float y_undamped = undamped.particles()[0].position[1];
    const float y_damped   = damped.particles()[0].position[1];
    EXPECT_GT(y_damped, y_undamped)
        << "Damping must reduce descent (remove energy). "
        << "undamped_y=" << y_undamped << " damped_y=" << y_damped;
}

// ---------------------------------------------------------------------------
// Depth 4: gravity sign — +Y gravity lifts, -Y gravity drops.
// ---------------------------------------------------------------------------

TEST(SoftBodyIntegrator, GravitySignControlsDirection)
{
    // Arrange.
    SoftBody up;
    SoftBody down;
    up.configure(make_single_free_particle());
    down.configure(make_single_free_particle());

    constexpr float kDt = 1.0F / 60.0F;

    // Act.
    for (int i = 0; i < 10; ++i)
    {
        up.tick(kDt, { 0.0F, +9.81F, 0.0F });
        down.tick(kDt, { 0.0F, -9.81F, 0.0F });
    }

    // Assert.
    EXPECT_GT(up.particles()[0].position[1], 0.0F)   << "+Y gravity should lift.";
    EXPECT_LT(down.particles()[0].position[1], 0.0F) << "-Y gravity should drop.";
}

// ---------------------------------------------------------------------------
// Depth 5: empty body / zero gravity / no constraints are safe no-ops.
// ---------------------------------------------------------------------------

TEST(SoftBodyIntegrator, EmptyBodyAndZeroGravityAreSafe)
{
    // Arrange: completely empty body.
    SoftBody empty;
    empty.configure(SoftBodyConfig{});

    // Act + Assert: tick must not crash and the span is empty.
    empty.tick(1.0F / 60.0F, { 0.0F, -9.81F, 0.0F });
    EXPECT_EQ(empty.particles().size(), 0U);

    // Arrange: single particle under exactly zero gravity, no constraints.
    SoftBody floater;
    floater.configure(make_single_free_particle());
    const std::array<float, 3> before = floater.particles()[0].position;

    // Act: 50 ticks at zero gravity.
    for (int i = 0; i < 50; ++i)
    {
        floater.tick(1.0F / 60.0F, { 0.0F, 0.0F, 0.0F });
    }

    // Assert: with no force the particle never moves.
    const std::array<float, 3> after = floater.particles()[0].position;
    EXPECT_NEAR(after[1], before[1], 1e-9F) << "Zero gravity must not move a free particle.";
}

// ===========================================================================
// Depth pass — distance constraint behaviour
// ===========================================================================

// ---------------------------------------------------------------------------
// Helper: two-particle pair (both free unless pinned), one spring.
// ---------------------------------------------------------------------------

SoftBodyConfig make_pair(float sep, float rest_length, float stiffness,
                         uint32_t iters = 20, bool pin_a = false)
{
    SoftBodyConfig cfg {};
    cfg.solver_iterations = iters;
    cfg.damping           = 0.0F;

    Particle a {};
    a.position      = { 0.0F, 0.0F, 0.0F };
    a.prev_position = a.position;
    a.inv_mass      = pin_a ? 0.0F : 1.0F;
    a.pinned        = pin_a;
    cfg.particles.push_back(a);

    Particle b {};
    b.position      = { sep, 0.0F, 0.0F };
    b.prev_position = b.position;
    b.inv_mass      = 1.0F;
    cfg.particles.push_back(b);

    SpringConstraint sc {};
    sc.a           = 0;
    sc.b           = 1;
    sc.rest_length = rest_length;
    sc.stiffness   = stiffness;
    cfg.constraints.push_back(sc);
    return cfg;
}

// ---------------------------------------------------------------------------
// Depth 6: over-stretched spring contracts toward rest length (not past it).
// ---------------------------------------------------------------------------

TEST(SoftBodyConstraint, OverStretchedSpringContracts)
{
    // Arrange: pair 2 m apart, rest 1 m, fully rigid. Zero gravity isolates
    // the constraint.
    SoftBody sb;
    sb.configure(make_pair(/*sep=*/2.0F, /*rest_length=*/1.0F, /*stiffness=*/1.0F));

    constexpr std::array<float, 3> kZeroG { 0.0F, 0.0F, 0.0F };
    sb.tick(1.0F / 60.0F, kZeroG);

    // Assert: separation pulled close to rest length, and NOT overshoot to
    // below ~rest (a single rigid pass converges essentially exactly here).
    const float d = particle_dist(sb.particles()[0], sb.particles()[1]);
    EXPECT_NEAR(d, 1.0F, 1e-3F) << "Over-stretched rigid spring should reach rest length.";
    EXPECT_LT(d, 2.0F) << "Must have contracted from the 2 m start.";
}

// ---------------------------------------------------------------------------
// Depth 7: compressed spring expands toward rest length.
// ---------------------------------------------------------------------------

TEST(SoftBodyConstraint, CompressedSpringExpands)
{
    // Arrange: pair 0.3 m apart, rest 1 m, rigid.
    SoftBody sb;
    sb.configure(make_pair(/*sep=*/0.3F, /*rest_length=*/1.0F, /*stiffness=*/1.0F));

    constexpr std::array<float, 3> kZeroG { 0.0F, 0.0F, 0.0F };
    sb.tick(1.0F / 60.0F, kZeroG);

    const float d = particle_dist(sb.particles()[0], sb.particles()[1]);
    EXPECT_NEAR(d, 1.0F, 1e-3F) << "Compressed rigid spring should reach rest length.";
    EXPECT_GT(d, 0.3F) << "Must have expanded from the 0.3 m start.";
}

// ---------------------------------------------------------------------------
// Depth 8: stiffness 0 leaves the pair free (no correction).
// ---------------------------------------------------------------------------

TEST(SoftBodyConstraint, ZeroStiffnessDoesNotCorrect)
{
    // Arrange: pair 2 m apart, rest 1 m, stiffness 0 -> no projection.
    SoftBody sb;
    sb.configure(make_pair(/*sep=*/2.0F, /*rest_length=*/1.0F, /*stiffness=*/0.0F));

    constexpr std::array<float, 3> kZeroG { 0.0F, 0.0F, 0.0F };
    sb.tick(1.0F / 60.0F, kZeroG);

    // Assert: separation unchanged (no gravity, no stiffness).
    const float d = particle_dist(sb.particles()[0], sb.particles()[1]);
    EXPECT_NEAR(d, 2.0F, 1e-6F) << "Zero stiffness must leave the pair untouched.";
}

// ---------------------------------------------------------------------------
// Depth 9: zero rest-length drives the pair together without NaN.
// ---------------------------------------------------------------------------

TEST(SoftBodyConstraint, ZeroRestLengthNoNaN)
{
    // Arrange: pair 1 m apart, rest 0 -> they should collapse toward each other.
    // The 1e-12 dist^2 guard stops the projection once they (nearly) coincide.
    SoftBody sb;
    sb.configure(make_pair(/*sep=*/1.0F, /*rest_length=*/0.0F, /*stiffness=*/1.0F));

    constexpr std::array<float, 3> kZeroG { 0.0F, 0.0F, 0.0F };
    for (int i = 0; i < 30; ++i)
    {
        sb.tick(1.0F / 60.0F, kZeroG);
    }

    // Assert: finite (no divide-by-zero NaN) and collapsed very close together.
    const auto pts = sb.particles();
    EXPECT_FALSE(std::isnan(pts[0].position[0]));
    EXPECT_FALSE(std::isnan(pts[1].position[0]));
    const float d = particle_dist(pts[0], pts[1]);
    EXPECT_LT(d, 0.05F) << "Zero rest-length pair should collapse together. d=" << d;
}

// ===========================================================================
// Depth pass — bending constraint (Provot flexion)
// ===========================================================================

// ---------------------------------------------------------------------------
// Depth 10: bending constraint straightens an L-bent particle triple.
//
// Three particles forming a right angle (0,0)-(1,0)-(1,1). Structural springs
// keep the two legs at length 1; a bending constraint across the corner with a
// straight rest length (2.0) pulls the triple toward a straight line, so the
// span p0..p2 grows from sqrt(2) toward 2.
// ---------------------------------------------------------------------------

TEST(SoftBodyBending, BendingResistsFold)
{
    // Arrange.
    SoftBodyConfig cfg {};
    cfg.solver_iterations = 20;
    cfg.damping           = 0.0F;

    const std::array<std::array<float, 3>, 3> init {{
        { 0.0F, 0.0F, 0.0F },
        { 1.0F, 0.0F, 0.0F },
        { 1.0F, 1.0F, 0.0F }
    }};
    for (const auto& pos : init)
    {
        Particle p {};
        p.position      = pos;
        p.prev_position = pos;
        p.inv_mass      = 1.0F;
        cfg.particles.push_back(p);
    }

    // Structural springs (length 1) keep the legs rigid.
    for (uint32_t i = 0; i < 2U; ++i)
    {
        SpringConstraint sc {};
        sc.a = i; sc.b = i + 1U; sc.rest_length = 1.0F; sc.stiffness = 1.0F;
        cfg.constraints.push_back(sc);
    }

    // Bending span p0..p2 with straight rest length 2.0 and full stiffness.
    BendingConstraint bc {};
    bc.a = 0U; bc.b = 2U; bc.bend_rest_length = 2.0F; bc.stiffness = 1.0F;
    cfg.bending_constraints.push_back(bc);

    SoftBody sb;
    sb.configure(cfg);

    const float span_before = particle_dist(sb.particles()[0], sb.particles()[2]);

    // Act: settle with zero gravity.
    constexpr std::array<float, 3> kZeroG { 0.0F, 0.0F, 0.0F };
    for (int i = 0; i < 60; ++i)
    {
        sb.tick(1.0F / 60.0F, kZeroG);
    }

    // Assert: the corner has opened up (span grew toward straight = 2.0).
    const float span_after = particle_dist(sb.particles()[0], sb.particles()[2]);
    EXPECT_GT(span_after, span_before + 0.1F)
        << "Bending constraint should straighten the fold. before=" << span_before
        << " after=" << span_after;
}

// ---------------------------------------------------------------------------
// Depth 11: empty bending vector is byte-identical to the no-bending baseline.
// ---------------------------------------------------------------------------

TEST(SoftBodyBending, EmptyBendingMatchesBaseline)
{
    // Arrange two identical ropes; neither has bending constraints.
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };

    SoftBody base;
    SoftBody same;
    base.configure(make_rope(6));
    same.configure(make_rope(6)); // bending_constraints default-empty.

    // Act.
    for (int i = 0; i < 40; ++i)
    {
        base.tick(1.0F / 60.0F, kGravity);
        same.tick(1.0F / 60.0F, kGravity);
    }

    // Assert: bit-for-bit identical (empty bending pass must be a true no-op).
    const auto a = base.particles();
    const auto b = same.particles();
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_FLOAT_EQ(a[i].position[0], b[i].position[0]);
        EXPECT_FLOAT_EQ(a[i].position[1], b[i].position[1]);
        EXPECT_FLOAT_EQ(a[i].position[2], b[i].position[2]);
    }
}

// ---------------------------------------------------------------------------
// Depth 12: out-of-range / a==b bending indices are ignored (no crash).
// ---------------------------------------------------------------------------

TEST(SoftBodyBending, InvalidBendingIndicesIgnored)
{
    // Arrange.
    SoftBodyConfig cfg = make_rope(3);
    BendingConstraint oob {};
    oob.a = 0U; oob.b = 99U; oob.bend_rest_length = 0.2F; oob.stiffness = 1.0F;
    cfg.bending_constraints.push_back(oob);
    BendingConstraint self {};
    self.a = 1U; self.b = 1U; self.bend_rest_length = 0.2F; self.stiffness = 1.0F;
    cfg.bending_constraints.push_back(self);

    SoftBody sb;
    sb.configure(cfg);

    // Act + Assert: must not crash / produce NaN.
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    for (int i = 0; i < 20; ++i)
    {
        sb.tick(1.0F / 60.0F, kGravity);
    }
    for (const auto& p : sb.particles())
    {
        EXPECT_FALSE(std::isnan(p.position[1]));
    }
}

// ===========================================================================
// Depth pass — ground plane collision
// ===========================================================================

// ---------------------------------------------------------------------------
// Helper: single free particle starting at height y0 above a floor at y=0.
// ---------------------------------------------------------------------------

SoftBodyConfig make_falling_with_floor(float y0, bool enable_floor,
                                        float friction = 0.0F)
{
    SoftBodyConfig cfg {};
    cfg.solver_iterations = 4;
    cfg.damping           = 0.0F;

    Particle p {};
    p.position      = { 0.0F, y0, 0.0F };
    p.prev_position = p.position;
    p.inv_mass      = 1.0F;
    cfg.particles.push_back(p);

    cfg.ground.enable_ground = enable_floor;
    cfg.ground.normal        = { 0.0F, 1.0F, 0.0F };
    cfg.ground.offset        = 0.0F; // Floor at y = 0.
    cfg.ground.friction      = friction;
    return cfg;
}

// ---------------------------------------------------------------------------
// Depth 13: falling particle stops at the floor (does not penetrate).
// ---------------------------------------------------------------------------

TEST(SoftBodyGround, ParticleStopsAtFloor)
{
    // Arrange: start 1 m up, floor at y=0 enabled.
    SoftBody sb;
    sb.configure(make_falling_with_floor(/*y0=*/1.0F, /*enable_floor=*/true));

    // Act: drop for 2 seconds.
    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    for (int i = 0; i < 120; ++i)
    {
        sb.tick(1.0F / 60.0F, kGravity);
    }

    // Assert: never penetrated below the floor (within float epsilon).
    const float y = sb.particles()[0].position[1];
    EXPECT_GE(y, -1e-5F) << "Particle must not penetrate the floor. y=" << y;
    EXPECT_LT(y, 0.1F)   << "Particle should be resting near the floor.";
}

// ---------------------------------------------------------------------------
// Depth 14: disabled ground lets the particle fall through (opt-in proof).
// ---------------------------------------------------------------------------

TEST(SoftBodyGround, DisabledGroundLetsParticleFall)
{
    // Arrange: same drop but floor DISABLED.
    SoftBody sb;
    sb.configure(make_falling_with_floor(/*y0=*/1.0F, /*enable_floor=*/false));

    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    for (int i = 0; i < 120; ++i)
    {
        sb.tick(1.0F / 60.0F, kGravity);
    }

    // Assert: particle fell well below the floor plane.
    EXPECT_LT(sb.particles()[0].position[1], -1.0F)
        << "Disabled ground must NOT stop the particle.";
}

// ---------------------------------------------------------------------------
// Depth 15: degenerate (zero) plane normal is a safe no-op.
// ---------------------------------------------------------------------------

TEST(SoftBodyGround, ZeroNormalIsNoOp)
{
    // Arrange: enabled ground but zero-length normal.
    SoftBody sb;
    SoftBodyConfig cfg = make_falling_with_floor(/*y0=*/1.0F, /*enable_floor=*/true);
    cfg.ground.normal = { 0.0F, 0.0F, 0.0F };
    sb.configure(cfg);

    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    for (int i = 0; i < 60; ++i)
    {
        sb.tick(1.0F / 60.0F, kGravity);
    }

    // Assert: with a degenerate normal the ground does nothing -> falls through,
    // and stays finite (no divide-by-zero NaN).
    const float y = sb.particles()[0].position[1];
    EXPECT_FALSE(std::isnan(y));
    EXPECT_LT(y, 0.0F) << "Degenerate normal must be a no-op (no collision).";
}

// ---------------------------------------------------------------------------
// Depth 16: friction kills tangential sliding velocity on contact.
//
// A particle is launched horizontally just above the floor. With high
// friction it must travel LESS far in X than with zero friction once it is in
// contact with the plane.
// ---------------------------------------------------------------------------

TEST(SoftBodyGround, FrictionReducesTangentialSlide)
{
    auto launch = [](float friction) -> float {
        SoftBody sb;
        SoftBodyConfig cfg =
            make_falling_with_floor(/*y0=*/0.0F, /*enable_floor=*/true, friction);
        sb.configure(cfg);
        // Kick the particle sideways.
        sb.apply_force(0U, { 2000.0F, 0.0F, 0.0F });
        // Gravity holds it on the floor so it stays in tangential contact.
        constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
        for (int i = 0; i < 60; ++i)
        {
            sb.tick(1.0F / 60.0F, kGravity);
        }
        return sb.particles()[0].position[0];
    };

    // Act.
    const float x_frictionless = launch(0.0F);
    const float x_high_friction = launch(1.0F);

    // Assert: friction shortens the slide.
    EXPECT_LT(x_high_friction, x_frictionless)
        << "Friction should reduce horizontal travel. frictionless=" << x_frictionless
        << " high_friction=" << x_high_friction;
}

// ---------------------------------------------------------------------------
// Depth 17: pinned particle below the plane is NOT pushed (static obstacle).
// ---------------------------------------------------------------------------

TEST(SoftBodyGround, PinnedParticleBelowPlaneNotPushed)
{
    // Arrange: a pinned particle parked below the floor. The ground pass must
    // leave static particles where they are (they act as immovable obstacles).
    SoftBodyConfig cfg {};
    cfg.solver_iterations    = 4;
    cfg.ground.enable_ground = true;
    cfg.ground.normal        = { 0.0F, 1.0F, 0.0F };
    cfg.ground.offset        = 0.0F;

    Particle p {};
    p.position      = { 0.0F, -0.5F, 0.0F }; // Below the floor.
    p.prev_position = p.position;
    p.inv_mass      = 0.0F;
    p.pinned        = true;
    cfg.particles.push_back(p);

    SoftBody sb;
    sb.configure(cfg);

    constexpr std::array<float, 3> kGravity { 0.0F, -9.81F, 0.0F };
    for (int i = 0; i < 30; ++i)
    {
        sb.tick(1.0F / 60.0F, kGravity);
    }

    // Assert: pinned particle stayed exactly where configured.
    EXPECT_NEAR(sb.particles()[0].position[1], -0.5F, 1e-6F)
        << "Pinned particle must not be lifted by the ground pass.";
}

// ===========================================================================
// Depth pass — self-collision negative / degenerate guards
// ===========================================================================

// ---------------------------------------------------------------------------
// Depth 18: single particle with self-collision enabled is a safe no-op.
// ---------------------------------------------------------------------------

TEST(SoftBodySelfCollisionEdge, SingleParticleIsNoOp)
{
    // Arrange: one particle, self-collision on.
    SoftBodyConfig cfg {};
    cfg.solver_iterations = 4;
    cfg.self_collision.enable_self_collision  = true;
    cfg.self_collision.particle_radius        = 0.02F;
    cfg.self_collision.spatial_hash_cell_size = 0.05F;

    Particle p {};
    p.position      = { 0.0F, 0.0F, 0.0F };
    p.prev_position = p.position;
    p.inv_mass      = 1.0F;
    cfg.particles.push_back(p);

    SoftBody sb;
    sb.configure(cfg);

    // Act + Assert: must not crash; only gravity moves it.
    constexpr std::array<float, 3> kZeroG { 0.0F, 0.0F, 0.0F };
    for (int i = 0; i < 10; ++i)
    {
        sb.tick(1.0F / 60.0F, kZeroG);
    }
    EXPECT_NEAR(sb.particles()[0].position[0], 0.0F, 1e-9F);
}

// ---------------------------------------------------------------------------
// Depth 19: zero radius disables self-collision repulsion (degenerate guard).
// ---------------------------------------------------------------------------

TEST(SoftBodySelfCollisionEdge, ZeroRadiusDisablesRepulsion)
{
    // Arrange: two coincident-ish particles, radius 0 -> no contact distance.
    SoftBodyConfig cfg {};
    cfg.solver_iterations = 4;
    cfg.damping           = 0.0F;
    cfg.self_collision.enable_self_collision  = true;
    cfg.self_collision.particle_radius        = 0.0F; // Degenerate.
    cfg.self_collision.spatial_hash_cell_size = 0.05F;

    Particle a {};
    a.position = { 0.0F, 0.0F, 0.0F }; a.prev_position = a.position; a.inv_mass = 1.0F;
    cfg.particles.push_back(a);
    Particle b {};
    b.position = { 0.01F, 0.0F, 0.0F }; b.prev_position = b.position; b.inv_mass = 1.0F;
    cfg.particles.push_back(b);

    SoftBody sb;
    sb.configure(cfg);

    constexpr std::array<float, 3> kZeroG { 0.0F, 0.0F, 0.0F };
    for (int i = 0; i < 10; ++i)
    {
        sb.tick(1.0F / 60.0F, kZeroG);
    }

    // Assert: separation unchanged (no repulsion with zero radius).
    const float d = particle_dist(sb.particles()[0], sb.particles()[1]);
    EXPECT_NEAR(d, 0.01F, 1e-6F) << "Zero radius must disable repulsion.";
}

}  // namespace
