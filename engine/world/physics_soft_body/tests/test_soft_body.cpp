// =============================================================================
// CHROMODYNAMIC — tests/test_soft_body.cpp
// Phase 721 — cd::physics::soft_body Sprint-1 unit tests.
//
// Coverage (7 cases):
//   1. Rope of 10 particles falls under gravity (free particles descend).
//   2. Pinned particle stays pinned under gravity.
//   3. Distance constraint is preserved within tolerance after many iterations.
//   4. More solver iterations improve constraint satisfaction (stability test).
//   5. apply_force() perturbs a single particle position detectably.
//   6. Zero dt tick leaves particles unchanged.
//   7. Both particles pinned: constraint solver is a no-op (no NaN, no crash).
//
// Test methodology:
//   * Arrange / Act / Assert pattern.
//   * No sleep_for — purely deterministic tick(dt) calls.
//   * All assertions use EXPECT_NEAR / EXPECT_LT with physically motivated
//     tolerances (not magic numbers pulled from air).
// =============================================================================

#include <cd/physics/soft_body/SoftBody.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace
{

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

    SoftBody sb_low, sb_high;
    auto cfg_low  = make_rope(5, 1.0F, /*iterations=*/1);
    auto cfg_high = make_rope(5, 1.0F, /*iterations=*/20);
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
            if (v > worst) { worst = v; }
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

}  // namespace
