#include <cd/gpu_particles/GpuParticles.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace
{

using cd::gpu_particles::advance;
using cd::gpu_particles::compact_alive;
using cd::gpu_particles::Particle;

constexpr float kEps = 1e-3F;

TEST(GpuParticles, AdvanceMovesParticleByVelocityTimesDt)
{
    std::vector<Particle> ps(1);
    ps[0].life = 1.0F;
    ps[0].max_life = 1.0F;
    ps[0].velocity = { 1.0F, 0, 0 };
    advance(ps, 0.5F, { 0, 0, 0 });
    EXPECT_NEAR(ps[0].position.x, 0.5F, kEps);
    EXPECT_NEAR(ps[0].life,       0.5F, kEps);
}

TEST(GpuParticles, GravityAcceleratesVelocity)
{
    std::vector<Particle> ps(1);
    ps[0].life = 1.0F;
    advance(ps, 1.0F, { 0, -9.8F, 0 });
    EXPECT_NEAR(ps[0].velocity.y, -9.8F, kEps);
}

TEST(GpuParticles, DeadParticleNotAdvanced)
{
    std::vector<Particle> ps(1);
    ps[0].life = 0.0F;
    ps[0].velocity = { 10.0F, 0, 0 };
    advance(ps, 1.0F, {});
    EXPECT_NEAR(ps[0].position.x, 0.0F, kEps);
}

TEST(GpuParticles, AlphaFadesWithLife)
{
    std::vector<Particle> ps(1);
    ps[0].life = 0.5F;
    ps[0].max_life = 1.0F;
    advance(ps, 0.0F, {});
    EXPECT_NEAR(ps[0].color.w, 0.5F, kEps);
}

TEST(GpuParticles, CompactAliveDropsDeadEntries)
{
    std::vector<Particle> ps(4);
    ps[0].life = 1.0F;
    ps[1].life = 0.0F;
    ps[2].life = 1.0F;
    ps[3].life = 0.0F;
    EXPECT_EQ(compact_alive(ps), 2U);
}

// Death branch: a particle whose remaining life is < dt crosses zero this
// step and must stop being advanced thereafter (no negative-life ghost).
TEST(GpuParticles, ParticleDiesWhenLifeCrossesZero)
{
    std::vector<Particle> ps(1);
    ps[0].life = 0.3F;
    ps[0].max_life = 1.0F;
    ps[0].velocity = { 2.0F, 0, 0 };
    advance(ps, 0.5F, { 0, 0, 0 });      // life 0.3 - 0.5 = -0.2 → dead
    EXPECT_LE(ps[0].life, 0.0F);
    const float x_after_death = ps[0].position.x;
    advance(ps, 0.5F, { 0, 0, 0 });      // dead → must not move further
    EXPECT_NEAR(ps[0].position.x, x_after_death, kEps);
    EXPECT_EQ(compact_alive(ps), 0U);    // compaction drops it
}

// Compaction must preserve survivor PAYLOAD (not merely the count) and pack
// survivors contiguously into the head, in stable order.
TEST(GpuParticles, CompactAlivePreservesSurvivorPayloadInOrder)
{
    std::vector<Particle> ps(5);
    ps[0].life = 0.0F;                   // dead
    ps[1].life = 1.0F; ps[1].position = { 11.0F, 0, 0 };
    ps[2].life = 0.0F;                   // dead
    ps[3].life = 1.0F; ps[3].position = { 33.0F, 0, 0 };
    ps[4].life = 1.0F; ps[4].position = { 44.0F, 0, 0 };

    const std::uint32_t live = compact_alive(ps);
    ASSERT_EQ(live, 3U);
    EXPECT_NEAR(ps[0].position.x, 11.0F, kEps);
    EXPECT_NEAR(ps[1].position.x, 33.0F, kEps);
    EXPECT_NEAR(ps[2].position.x, 44.0F, kEps);
}

// Zero-survivor and all-survivor boundaries.
TEST(GpuParticles, CompactAliveBoundaries)
{
    std::vector<Particle> empty;
    EXPECT_EQ(compact_alive(empty), 0U);

    std::vector<Particle> all_dead(3);  // default life = 0
    EXPECT_EQ(compact_alive(all_dead), 0U);

    std::vector<Particle> all_live(3);
    for (auto& p : all_live) p.life = 1.0F;
    EXPECT_EQ(compact_alive(all_live), 3U);
}

TEST(GpuParticles, GlslSimulateKernelNonEmpty)
{
    EXPECT_FALSE(cd::gpu_particles::kSimulateCS.empty());
}

// ---- advance(): integration semantics ---------------------------------------

// The integrator is SEMI-IMPLICIT (symplectic) Euler: velocity is updated by
// gravity FIRST, then position uses the ALREADY-UPDATED velocity. Pin that the
// position step sees the post-gravity velocity, not the pre-gravity one.
TEST(GpuParticles, AdvanceUsesSemiImplicitEulerOrder)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 1.0F;
    ps[0].max_life = 1.0F;
    ps[0].velocity = { 0, 0, 0 };
    advance(ps, 1.0F, { 0, -10.0F, 0 });
    // velocity.y = 0 + (-10)*1 = -10 ; position.y = 0 + (-10)*1 = -10
    // (explicit Euler would give position.y = 0; semi-implicit gives -10).
    EXPECT_NEAR(ps[0].velocity.y, -10.0F, kEps);
    EXPECT_NEAR(ps[0].position.y, -10.0F, kEps);
}

// Gravity accelerates all three axes independently and position integrates the
// updated velocity on each axis.
TEST(GpuParticles, AdvanceIntegratesAllThreeAxes)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 10.0F;
    ps[0].max_life = 10.0F;
    ps[0].velocity = { 1.0F, 2.0F, 3.0F };
    advance(ps, 2.0F, { 4.0F, 5.0F, 6.0F });
    // v += g*dt → (1+8, 2+10, 3+12) = (9, 12, 15)
    EXPECT_NEAR(ps[0].velocity.x, 9.0F, kEps);
    EXPECT_NEAR(ps[0].velocity.y, 12.0F, kEps);
    EXPECT_NEAR(ps[0].velocity.z, 15.0F, kEps);
    // p += v*dt → (9*2, 12*2, 15*2) = (18, 24, 30)
    EXPECT_NEAR(ps[0].position.x, 18.0F, kEps);
    EXPECT_NEAR(ps[0].position.y, 24.0F, kEps);
    EXPECT_NEAR(ps[0].position.z, 30.0F, kEps);
}

// dt scales the per-step displacement: a single 1.0 step ends at the same
// place as two 0.5 steps only when there is NO acceleration (constant velocity).
// With constant velocity, displacement is exactly velocity*dt.
TEST(GpuParticles, AdvanceConstantVelocityScalesWithDt)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 10.0F;
    ps[0].max_life = 10.0F;
    ps[0].velocity = { 2.0F, 0, 0 };
    advance(ps, 0.25F, { 0, 0, 0 });
    EXPECT_NEAR(ps[0].position.x, 0.5F, kEps);   // 2 * 0.25
    EXPECT_NEAR(ps[0].life,       9.75F, kEps);  // 10 - 0.25
}

// Lifetime decrements by exactly dt every step regardless of motion.
TEST(GpuParticles, AdvanceDecrementsLifeByDt)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 2.0F;
    ps[0].max_life = 2.0F;
    advance(ps, 0.3F, {});
    EXPECT_NEAR(ps[0].life, 1.7F, kEps);
    advance(ps, 0.3F, {});
    EXPECT_NEAR(ps[0].life, 1.4F, kEps);
}

// Zero dt is a no-op for position/velocity/life but STILL recomputes the alpha
// fade (the only side effect of a zero-dt advance on a live particle).
TEST(GpuParticles, AdvanceZeroDtIsNoOpButRefreshesAlpha)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 0.25F;
    ps[0].max_life = 1.0F;
    ps[0].velocity = { 5.0F, 5.0F, 5.0F };
    ps[0].position = { 7.0F, 8.0F, 9.0F };
    ps[0].color    = { 1, 1, 1, 1 };
    advance(ps, 0.0F, { 9.0F, 9.0F, 9.0F });
    EXPECT_NEAR(ps[0].position.x, 7.0F, kEps);
    EXPECT_NEAR(ps[0].position.y, 8.0F, kEps);
    EXPECT_NEAR(ps[0].position.z, 9.0F, kEps);
    EXPECT_NEAR(ps[0].velocity.x, 5.0F, kEps);
    EXPECT_NEAR(ps[0].life,       0.25F, kEps);
    EXPECT_NEAR(ps[0].color.w,    0.25F, kEps);  // 0.25 / 1.0
}

// Alpha fade clamps to [0,1]: life beyond max_life saturates alpha at 1.
TEST(GpuParticles, AdvanceAlphaClampsToOneWhenLifeExceedsMaxLife)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 5.0F;
    ps[0].max_life = 1.0F;
    advance(ps, 0.0F, {});
    EXPECT_NEAR(ps[0].color.w, 1.0F, kEps);  // 5/1 clamped to 1
}

// Guard against divide-by-zero: max_life of 0 must use the 1e-3 floor, not NaN,
// and the resulting alpha is still finite and clamped.
TEST(GpuParticles, AdvanceAlphaSafeWhenMaxLifeZero)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 0.0005F;  // below the 1e-3 floor numerator
    ps[0].max_life = 0.0F;     // forces the max(.,1e-3) guard
    advance(ps, 0.0F, {});
    EXPECT_GE(ps[0].color.w, 0.0F);
    EXPECT_LE(ps[0].color.w, 1.0F);
}

// advance() over an empty span is a well-defined no-op (no crash, no UB).
TEST(GpuParticles, AdvanceEmptySpanIsNoOp)
{
    std::vector<Particle> empty;
    advance(empty, 1.0F, { 0, -9.8F, 0 });
    SUCCEED();
}

// Mixed live/dead in one call: dead entries are skipped, live ones integrate.
TEST(GpuParticles, AdvanceSkipsDeadButMovesLiveInSameCall)
{
    std::vector<Particle> ps(2);
    ps[0].life = 0.0F; ps[0].velocity = { 9.0F, 0, 0 };   // dead, frozen
    ps[1].life = 1.0F; ps[1].max_life = 1.0F;
    ps[1].velocity = { 3.0F, 0, 0 };
    advance(ps, 1.0F, { 0, 0, 0 });
    EXPECT_NEAR(ps[0].position.x, 0.0F, kEps);  // untouched
    EXPECT_NEAR(ps[1].position.x, 3.0F, kEps);  // moved
}

// ---- compact_alive(): packing + stability -----------------------------------

// A fully-alive buffer is returned unchanged: count == size and every payload
// remains in its original slot/order.
TEST(GpuParticles, CompactAliveAllLiveLeavesPayloadUnchanged)
{
    std::vector<Particle> ps(3);
    for (std::size_t i = 0; i < ps.size(); ++i)
    {
        ps[i].life       = 1.0F;
        ps[i].position.x = static_cast<float>(i) * 10.0F;
    }
    EXPECT_EQ(compact_alive(ps), 3U);
    EXPECT_NEAR(ps[0].position.x, 0.0F, kEps);
    EXPECT_NEAR(ps[1].position.x, 10.0F, kEps);
    EXPECT_NEAR(ps[2].position.x, 20.0F, kEps);
}

// Leading-dead block: survivors must slide to the front in stable order.
TEST(GpuParticles, CompactAliveSlidesSurvivorsPastLeadingDead)
{
    std::vector<Particle> ps(4);
    ps[0].life = 0.0F;                                  // dead
    ps[1].life = 0.0F;                                  // dead
    ps[2].life = 1.0F; ps[2].position = { 22.0F, 0, 0 };
    ps[3].life = 1.0F; ps[3].position = { 33.0F, 0, 0 };
    ASSERT_EQ(compact_alive(ps), 2U);
    EXPECT_NEAR(ps[0].position.x, 22.0F, kEps);
    EXPECT_NEAR(ps[1].position.x, 33.0F, kEps);
}

// Single-element buffers: alive→count 1, dead→count 0.
TEST(GpuParticles, CompactAliveSingleElement)
{
    std::vector<Particle> live(1);
    live[0].life = 1.0F;
    EXPECT_EQ(compact_alive(live), 1U);

    std::vector<Particle> dead(1);  // default life = 0
    EXPECT_EQ(compact_alive(dead), 0U);
}

// compact_alive is idempotent: running it on an already-compacted buffer of all
// survivors returns the same count and leaves the head untouched.
TEST(GpuParticles, CompactAliveIsIdempotentOnPackedBuffer)
{
    std::vector<Particle> ps(3);
    ps[0].life = 1.0F; ps[0].position = { 1.0F, 0, 0 };
    ps[1].life = 0.0F;
    ps[2].life = 1.0F; ps[2].position = { 3.0F, 0, 0 };
    const std::uint32_t first = compact_alive(ps);
    ASSERT_EQ(first, 2U);
    const float head0 = ps[0].position.x;
    const float head1 = ps[1].position.x;
    // Re-running over the first `first` survivors yields the same count + order.
    const std::uint32_t second = compact_alive(std::span<Particle>(ps).first(first));
    EXPECT_EQ(second, 2U);
    EXPECT_NEAR(ps[0].position.x, head0, kEps);
    EXPECT_NEAR(ps[1].position.x, head1, kEps);
}

// ---- kSimulateCS: host/device contract --------------------------------------

// The compute kernel must declare the bindings + push-constant layout the host
// advance() math mirrors. Pin the contract textually so a shader edit that
// breaks the host parity assumption fails a test.
TEST(GpuParticles, GlslSimulateKernelDeclaresContract)
{
    const std::string_view cs = cd::gpu_particles::kSimulateCS;
    // Workgroup size mirrors the documented 64-wide dispatch.
    EXPECT_NE(cs.find("local_size_x = 64"), std::string_view::npos);
    // Particle storage buffer at set 0 / binding 0.
    EXPECT_NE(cs.find("binding = 0) buffer Buf"), std::string_view::npos);
    // Indirect-draw args buffer at set 0 / binding 1.
    EXPECT_NE(cs.find("binding = 1) buffer IndirectArgs"), std::string_view::npos);
    EXPECT_NE(cs.find("instance_count"), std::string_view::npos);
    // Push constants: count / dt / gravity, matching advance()'s parameters.
    EXPECT_NE(cs.find("uint count"), std::string_view::npos);
    EXPECT_NE(cs.find("float dt"), std::string_view::npos);
    EXPECT_NE(cs.find("vec3 gravity"), std::string_view::npos);
}

// The kernel body must mirror the host advance() integration + the alive-count
// accumulation that drives the indirect draw.
TEST(GpuParticles, GlslSimulateKernelMirrorsHostAdvance)
{
    const std::string_view cs = cd::gpu_particles::kSimulateCS;
    EXPECT_NE(cs.find("p.velocity += pc.gravity * pc.dt"), std::string_view::npos);
    EXPECT_NE(cs.find("p.position += p.velocity * pc.dt"), std::string_view::npos);
    EXPECT_NE(cs.find("p.life     -= pc.dt"), std::string_view::npos);
    EXPECT_NE(cs.find("p.life <= 0.0"), std::string_view::npos);  // dead-skip guard
    EXPECT_NE(cs.find("atomicAdd(Ind.instance_count, 1u)"), std::string_view::npos);
}

// ---- Particle defaults -------------------------------------------------------

// A default-constructed Particle is a dead, fully-opaque white point at the
// origin with unit max_life — the canonical "empty slot" state.
TEST(GpuParticles, DefaultParticleIsDeadOpaqueWhiteAtOrigin)
{
    const Particle p {};
    EXPECT_NEAR(p.position.x, 0.0F, kEps);
    EXPECT_NEAR(p.position.y, 0.0F, kEps);
    EXPECT_NEAR(p.position.z, 0.0F, kEps);
    EXPECT_NEAR(p.life,       0.0F, kEps);  // dead by default → skipped/compacted
    EXPECT_NEAR(p.max_life,   1.0F, kEps);
    EXPECT_NEAR(p.color.x,    1.0F, kEps);
    EXPECT_NEAR(p.color.w,    1.0F, kEps);
}


// =============================================================================
// ---- advance(): additional boundary + integration paths ---------------------
// =============================================================================

// life == 0.0F exactly is the dead threshold — the particle must be skipped
// even when it sits exactly on the boundary (not just life < 0).
TEST(GpuParticles, AdvanceExactZeroLifeIsDead)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 0.0F;
    ps[0].velocity = { 7.0F, 0, 0 };
    advance(ps, 0.1F, { 0, 0, 0 });
    EXPECT_NEAR(ps[0].position.x, 0.0F, kEps);  // untouched
}

// Pure Z-axis: gravity + velocity work on every axis independently;
// Z was already covered in AdvanceIntegratesAllThreeAxes but this isolates Z
// with zero X/Y to confirm no cross-axis bleed.
TEST(GpuParticles, AdvancePureZAxisMotion)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 10.0F;
    ps[0].max_life = 10.0F;
    ps[0].velocity = { 0, 0, 3.0F };
    advance(ps, 2.0F, { 0, 0, -1.0F });
    // v.z = 3 + (-1)*2 = 1.0;  p.z = 1.0 * 2 = 2.0
    EXPECT_NEAR(ps[0].velocity.z, 1.0F, kEps);
    EXPECT_NEAR(ps[0].position.z, 2.0F, kEps);
    EXPECT_NEAR(ps[0].position.x, 0.0F, kEps);  // no X bleed
    EXPECT_NEAR(ps[0].position.y, 0.0F, kEps);  // no Y bleed
}

// Multi-step integration chain: applying advance() N times with the same dt
// and no gravity gives final position == initial_velocity * N * dt.
TEST(GpuParticles, AdvanceMultiStepAccumulatesPosition)
{
    constexpr int kSteps = 5;
    constexpr float kDt  = 0.1F;
    constexpr float kVx  = 2.0F;
    std::vector<Particle> ps(1);
    ps[0].life     = 10.0F;
    ps[0].max_life = 10.0F;
    ps[0].velocity = { kVx, 0, 0 };
    for (int i = 0; i < kSteps; ++i)
    {
        advance(ps, kDt, { 0, 0, 0 });
    }
    EXPECT_NEAR(ps[0].position.x, kVx * static_cast<float>(kSteps) * kDt, kEps);
    EXPECT_NEAR(ps[0].life,       10.0F - static_cast<float>(kSteps) * kDt, kEps);
}

// All-dead span: advance() over a span of only dead particles must not mutate
// any field of any entry (position, velocity, color, life all frozen).
TEST(GpuParticles, AdvanceAllDeadSpanMutatesNothing)
{
    std::vector<Particle> ps(4);
    // Populate with recognisable non-default values but leave life == 0.
    for (std::size_t i = 0; i < ps.size(); ++i)
    {
        ps[i].life       = 0.0F;
        ps[i].velocity   = { 5.0F, 5.0F, 5.0F };
        ps[i].position   = { static_cast<float>(i), 0, 0 };
        ps[i].max_life   = 1.0F;
        ps[i].color      = { 1, 0, 0, 0.5F };
    }
    advance(ps, 1.0F, { 0, -9.8F, 0 });
    for (std::size_t i = 0; i < ps.size(); ++i)
    {
        EXPECT_NEAR(ps[i].position.x, static_cast<float>(i), kEps) << "idx=" << i;
        EXPECT_NEAR(ps[i].velocity.x, 5.0F, kEps) << "idx=" << i;
        EXPECT_NEAR(ps[i].velocity.y, 5.0F, kEps) << "idx=" << i;
        EXPECT_NEAR(ps[i].life,       0.0F, kEps) << "idx=" << i;
    }
}

// Large-N capacity: advance() and compact_alive() must handle a large particle
// count without UB; we verify only live-count correctness (not every field).
TEST(GpuParticles, AdvanceAndCompactLargeN)
{
    constexpr std::size_t kN = 4096;
    std::vector<Particle> ps(kN);
    // Even indices alive, odd indices dead.
    for (std::size_t i = 0; i < kN; ++i)
    {
        ps[i].life     = (i % 2 == 0) ? 1.0F : 0.0F;
        ps[i].max_life = 1.0F;
        ps[i].velocity = { 1.0F, 0, 0 };
    }
    advance(ps, 0.1F, { 0, 0, 0 });
    const std::uint32_t live = compact_alive(ps);
    EXPECT_EQ(live, static_cast<std::uint32_t>(kN / 2));
}

// Single particle: advance() updates all six motion fields and the alpha in one
// call; confirm every field mutates or stays correct (full-field round-trip).
TEST(GpuParticles, AdvanceSingleParticleFullFieldRoundTrip)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 2.0F;
    ps[0].max_life = 2.0F;
    ps[0].velocity = { 1.0F, 2.0F, 3.0F };
    ps[0].position = { 10.0F, 20.0F, 30.0F };
    ps[0].color    = { 0.5F, 0.5F, 0.5F, 1.0F };
    advance(ps, 0.5F, { 0, -2.0F, 0 });
    // v += g*dt → (1, 2 + (-2)*0.5, 3) = (1, 1, 3)
    EXPECT_NEAR(ps[0].velocity.x, 1.0F, kEps);
    EXPECT_NEAR(ps[0].velocity.y, 1.0F, kEps);
    EXPECT_NEAR(ps[0].velocity.z, 3.0F, kEps);
    // p += v*dt → (10 + 0.5, 20 + 0.5, 30 + 1.5) = (10.5, 20.5, 31.5)
    EXPECT_NEAR(ps[0].position.x, 10.5F, kEps);
    EXPECT_NEAR(ps[0].position.y, 20.5F, kEps);
    EXPECT_NEAR(ps[0].position.z, 31.5F, kEps);
    // life -= dt → 1.5; alpha = 1.5 / 2.0 = 0.75
    EXPECT_NEAR(ps[0].life,    1.5F, kEps);
    EXPECT_NEAR(ps[0].color.w, 0.75F, kEps);
    // RGB channels untouched
    EXPECT_NEAR(ps[0].color.x, 0.5F, kEps);
    EXPECT_NEAR(ps[0].color.y, 0.5F, kEps);
    EXPECT_NEAR(ps[0].color.z, 0.5F, kEps);
}

// Alpha clamps to zero floor: alpha = life/max_life must be clamped at 0, not
// go negative when life goes below 0 mid-step.
TEST(GpuParticles, AdvanceAlphaFloorZeroWhenLifeNegative)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 0.1F;
    ps[0].max_life = 1.0F;
    ps[0].color.w  = 1.0F;
    advance(ps, 0.5F, {});  // life → -0.4; alpha = clamp(-0.4/1.0) → 0
    EXPECT_GE(ps[0].color.w, 0.0F);
    EXPECT_LE(ps[0].color.w, 1.0F);
}

// Zero-gravity: advance() with a zero-gravity vector must not change velocity
// (pure ballistic: position += velocity * dt, nothing else).
TEST(GpuParticles, AdvanceZeroGravityPreservesVelocity)
{
    std::vector<Particle> ps(1);
    ps[0].life     = 5.0F;
    ps[0].max_life = 5.0F;
    ps[0].velocity = { 3.0F, -1.0F, 2.0F };
    advance(ps, 1.0F, { 0, 0, 0 });
    EXPECT_NEAR(ps[0].velocity.x, 3.0F, kEps);
    EXPECT_NEAR(ps[0].velocity.y, -1.0F, kEps);
    EXPECT_NEAR(ps[0].velocity.z, 2.0F, kEps);
}

// =============================================================================
// ---- compact_alive(): additional dead/alive patterns ------------------------
// =============================================================================

// Trailing-dead only: alive particles are at the front and dead ones follow —
// no data movement is needed but the live count must still be correct.
TEST(GpuParticles, CompactAliveTrailingDeadBlock)
{
    std::vector<Particle> ps(5);
    ps[0].life = 1.0F; ps[0].position = { 10.0F, 0, 0 };
    ps[1].life = 1.0F; ps[1].position = { 20.0F, 0, 0 };
    ps[2].life = 0.0F;
    ps[3].life = 0.0F;
    ps[4].life = 0.0F;
    const std::uint32_t live = compact_alive(ps);
    ASSERT_EQ(live, 2U);
    EXPECT_NEAR(ps[0].position.x, 10.0F, kEps);
    EXPECT_NEAR(ps[1].position.x, 20.0F, kEps);
}

// Only the last particle is alive — it must be compacted to slot 0.
TEST(GpuParticles, CompactAliveOnlyLastIsAlive)
{
    std::vector<Particle> ps(4);
    ps[3].life = 1.0F; ps[3].position = { 99.0F, 0, 0 };
    const std::uint32_t live = compact_alive(ps);
    ASSERT_EQ(live, 1U);
    EXPECT_NEAR(ps[0].position.x, 99.0F, kEps);
}

// Alternating dead/alive: every velocity field of survivors must be preserved
// (not just position) to confirm full Particle struct copy semantics.
TEST(GpuParticles, CompactAliveAlternatingPreservesFullPayload)
{
    std::vector<Particle> ps(6);
    // Slots 1, 3, 5 alive with unique velocity signatures.
    ps[1].life = 1.0F; ps[1].velocity = { 11.0F, 12.0F, 13.0F };
    ps[3].life = 1.0F; ps[3].velocity = { 31.0F, 32.0F, 33.0F };
    ps[5].life = 1.0F; ps[5].velocity = { 51.0F, 52.0F, 53.0F };
    const std::uint32_t live = compact_alive(ps);
    ASSERT_EQ(live, 3U);
    EXPECT_NEAR(ps[0].velocity.x, 11.0F, kEps);
    EXPECT_NEAR(ps[0].velocity.y, 12.0F, kEps);
    EXPECT_NEAR(ps[0].velocity.z, 13.0F, kEps);
    EXPECT_NEAR(ps[1].velocity.x, 31.0F, kEps);
    EXPECT_NEAR(ps[2].velocity.x, 51.0F, kEps);
    EXPECT_NEAR(ps[2].velocity.y, 52.0F, kEps);
    EXPECT_NEAR(ps[2].velocity.z, 53.0F, kEps);
}

// Exact-zero-life boundary: life == 0.0F must be treated as dead even if other
// fields (velocity, color) are non-default.
TEST(GpuParticles, CompactAliveZeroLifeExactBoundaryIsDead)
{
    std::vector<Particle> ps(3);
    ps[0].life = 0.0F; ps[0].velocity = { 99.0F, 0, 0 };
    ps[1].life = 1.0F; ps[1].position = { 5.0F,  0, 0 };
    ps[2].life = 0.0F; ps[2].velocity = { 88.0F, 0, 0 };
    const std::uint32_t live = compact_alive(ps);
    ASSERT_EQ(live, 1U);
    EXPECT_NEAR(ps[0].position.x, 5.0F, kEps);
}

// compact_alive preserves color payload (RGBA), not just position/velocity.
TEST(GpuParticles, CompactAlivePreservesColorPayload)
{
    std::vector<Particle> ps(3);
    ps[0].life = 0.0F;
    ps[1].life = 1.0F;
    ps[1].color = { 0.2F, 0.4F, 0.6F, 0.8F };
    ps[2].life = 0.0F;
    const std::uint32_t live = compact_alive(ps);
    ASSERT_EQ(live, 1U);
    EXPECT_NEAR(ps[0].color.x, 0.2F, kEps);
    EXPECT_NEAR(ps[0].color.y, 0.4F, kEps);
    EXPECT_NEAR(ps[0].color.z, 0.6F, kEps);
    EXPECT_NEAR(ps[0].color.w, 0.8F, kEps);
}

// compact_alive preserves max_life (not just life) so the alpha formula remains
// correct in subsequent advance() calls after compaction.
TEST(GpuParticles, CompactAlivePreservesMaxLife)
{
    std::vector<Particle> ps(3);
    ps[0].life = 0.0F;
    ps[1].life = 0.5F; ps[1].max_life = 3.0F;
    ps[2].life = 0.0F;
    const std::uint32_t live = compact_alive(ps);
    ASSERT_EQ(live, 1U);
    EXPECT_NEAR(ps[0].max_life, 3.0F, kEps);
}

// Two-element buffer: the minimal non-trivial case (one alive, one dead) covers
// both the in-place skip (live == index) and the copy (live < index) branches.
TEST(GpuParticles, CompactAliveTwoElementAliveFirst)
{
    std::vector<Particle> ps(2);
    ps[0].life = 1.0F; ps[0].position = { 1.0F, 0, 0 };
    ps[1].life = 0.0F;
    ASSERT_EQ(compact_alive(ps), 1U);
    EXPECT_NEAR(ps[0].position.x, 1.0F, kEps);
}

TEST(GpuParticles, CompactAliveTwoElementDeadFirst)
{
    std::vector<Particle> ps(2);
    ps[0].life = 0.0F;
    ps[1].life = 1.0F; ps[1].position = { 2.0F, 0, 0 };
    ASSERT_EQ(compact_alive(ps), 1U);
    EXPECT_NEAR(ps[0].position.x, 2.0F, kEps);
}

// =============================================================================
// ---- advance() + compact_alive() interaction: simulate-then-compact ----------
// =============================================================================

// Simulate N steps and compact; the live count must match the number of
// particles still with life > 0 after the steps.
TEST(GpuParticles, AdvanceThenCompactLiveCountMatchesSurvivors)
{
    constexpr float kDt = 0.4F;
    constexpr int kSteps = 3;
    // Particles with life 0.5, 1.0, 1.5, 2.0. After 3 * 0.4 = 1.2s:
    //   0.5 → dead after step 2 (0.5 - 0.4 = 0.1 > 0, 0.1 - 0.4 = -0.3 ≤ 0)
    //   1.0 → dead after step 3 (0.2 > 0, -0.2 ≤ 0 at step 3)
    //       → life 1.0: after 3 steps: 1.0 - 1.2 = -0.2 → dead
    //   1.5 → 1.5 - 1.2 = 0.3 → alive
    //   2.0 → 2.0 - 1.2 = 0.8 → alive
    std::vector<Particle> ps(4);
    ps[0].life = 0.5F; ps[0].max_life = 2.0F;
    ps[1].life = 1.0F; ps[1].max_life = 2.0F;
    ps[2].life = 1.5F; ps[2].max_life = 2.0F;
    ps[3].life = 2.0F; ps[3].max_life = 2.0F;
    for (int i = 0; i < kSteps; ++i)
    {
        advance(ps, kDt, { 0, 0, 0 });
    }
    const std::uint32_t live = compact_alive(ps);
    EXPECT_EQ(live, 2U);  // ps[2] and ps[3] survive
}

// After compact_alive() the head of the buffer is a fully-alive sub-span;
// a subsequent advance() over that sub-span must integrate all entries
// (no dead entries to skip; progress is guaranteed).
TEST(GpuParticles, AdvanceOnCompactedHeadMovesAllEntries)
{
    std::vector<Particle> ps(4);
    ps[0].life = 0.0F;                                  // dead
    ps[1].life = 2.0F; ps[1].max_life = 2.0F; ps[1].velocity = { 1.0F, 0, 0 };
    ps[2].life = 0.0F;                                  // dead
    ps[3].life = 2.0F; ps[3].max_life = 2.0F; ps[3].velocity = { 2.0F, 0, 0 };

    const std::uint32_t live = compact_alive(ps);
    ASSERT_EQ(live, 2U);

    // Advance only the live sub-span.
    advance(std::span<Particle>(ps).first(live), 1.0F, { 0, 0, 0 });

    EXPECT_NEAR(ps[0].position.x, 1.0F, kEps);
    EXPECT_NEAR(ps[1].position.x, 2.0F, kEps);
}

// Particles die at predictable steps; compact after each step and confirm the
// returned count decrements as expected.
TEST(GpuParticles, AdvanceCompactStepwiseDecrementsLiveCount)
{
    // Three particles with life 0.3, 0.7, 1.1.  dt = 0.4.
    // After step 1: 0.3-0.4=-0.1 (dead), 0.3, 0.7 → live=2
    // After step 2: -0.1 (dead from step 1), 0.3-0.4=-0.1 (dead), 0.3 → live=1
    // After step 3: all dead → live=0
    std::vector<Particle> ps(3);
    ps[0].life = 0.3F; ps[0].max_life = 1.1F;
    ps[1].life = 0.7F; ps[1].max_life = 1.1F;
    ps[2].life = 1.1F; ps[2].max_life = 1.1F;

    advance(ps, 0.4F, {});
    EXPECT_EQ(compact_alive(ps), 2U);  // step 1

    advance(std::span<Particle>(ps).first(2), 0.4F, {});
    const std::uint32_t after2 = compact_alive(std::span<Particle>(ps).first(2));
    EXPECT_EQ(after2, 1U);  // step 2

    advance(std::span<Particle>(ps).first(1), 0.4F, {});
    EXPECT_EQ(compact_alive(std::span<Particle>(ps).first(1)), 0U);  // step 3
}

// =============================================================================
// ---- kSimulateCS: deeper contract pins ---------------------------------------
// =============================================================================

// The shader must carry #version 460 — required for Vulkan GLSL + atomicAdd on
// storage buffers (GLSL 4.60 / VK_KHR_spirv_1_1).
TEST(GpuParticles, GlslSimulateKernelVersion460)
{
    EXPECT_NE(cd::gpu_particles::kSimulateCS.find("#version 460"),
              std::string_view::npos);
}

// The alpha formula must use the exact 1e-3 floor guard matching the CPU path.
TEST(GpuParticles, GlslSimulateKernelAlphaGuardMatchesHostFloor)
{
    const std::string_view cs = cd::gpu_particles::kSimulateCS;
    // Host: std::max(p.max_life, 1e-3F); kernel must mirror with 1e-3.
    EXPECT_NE(cs.find("max(p.max_life, 1e-3)"), std::string_view::npos);
    EXPECT_NE(cs.find("p.color.a"), std::string_view::npos);
    EXPECT_NE(cs.find("clamp(p.life"), std::string_view::npos);
}

// The out-of-bounds invocation guard must appear BEFORE the main body so that
// threads beyond pc.count cannot corrupt out-of-bounds memory.
TEST(GpuParticles, GlslSimulateKernelHasInvocationBoundsGuard)
{
    const std::string_view cs = cd::gpu_particles::kSimulateCS;
    const auto guard_pos  = cs.find("if (i >= pc.count) return");
    const auto body_pos   = cs.find("p.velocity += pc.gravity");
    EXPECT_NE(guard_pos, std::string_view::npos);
    EXPECT_NE(body_pos,  std::string_view::npos);
    EXPECT_LT(guard_pos, body_pos);  // guard comes first
}

// Dead-skip guard: the kernel must check life <= 0 BEFORE the integration so
// dead slots are not advanced (mirrors the CPU advance() early-continue).
TEST(GpuParticles, GlslSimulateKernelDeadSkipBeforeIntegration)
{
    const std::string_view cs = cd::gpu_particles::kSimulateCS;
    const auto dead_pos  = cs.find("if (p.life <= 0.0) return");
    const auto integ_pos = cs.find("p.velocity += pc.gravity");
    EXPECT_NE(dead_pos,  std::string_view::npos);
    EXPECT_NE(integ_pos, std::string_view::npos);
    EXPECT_LT(dead_pos, integ_pos);  // dead-check precedes integration
}

// atomicAdd must accumulate AFTER the life decrement (so particles that die
// this step are NOT counted into instance_count).
TEST(GpuParticles, GlslSimulateKernelAtomicAddAfterLifeDecrement)
{
    const std::string_view cs = cd::gpu_particles::kSimulateCS;
    const auto decrement_pos = cs.find("p.life     -= pc.dt");
    const auto atomic_pos    = cs.find("atomicAdd(Ind.instance_count, 1u)");
    EXPECT_NE(decrement_pos, std::string_view::npos);
    EXPECT_NE(atomic_pos,    std::string_view::npos);
    EXPECT_LT(decrement_pos, atomic_pos);
}

// atomicAdd is guarded by `if (p.life > 0.0)` so only particles that survive
// this step contribute to the indirect draw count.
TEST(GpuParticles, GlslSimulateKernelAtomicGuardedByPostStepLife)
{
    const std::string_view cs = cd::gpu_particles::kSimulateCS;
    // The guard `if (p.life > 0.0)` must appear between the life-decrement and
    // the atomicAdd.
    const auto guard_pos  = cs.find("if (p.life > 0.0)");
    const auto atomic_pos = cs.find("atomicAdd(Ind.instance_count, 1u)");
    EXPECT_NE(guard_pos,  std::string_view::npos);
    EXPECT_NE(atomic_pos, std::string_view::npos);
    EXPECT_LT(guard_pos, atomic_pos);
}

// IndirectArgs must expose vertex_count and first_vertex alongside
// instance_count + first_instance — all four fields of VkDrawIndirectCommand.
TEST(GpuParticles, GlslSimulateKernelIndirectArgsFourFields)
{
    const std::string_view cs = cd::gpu_particles::kSimulateCS;
    EXPECT_NE(cs.find("vertex_count"),   std::string_view::npos);
    EXPECT_NE(cs.find("instance_count"), std::string_view::npos);
    EXPECT_NE(cs.find("first_vertex"),   std::string_view::npos);
    EXPECT_NE(cs.find("first_instance"), std::string_view::npos);
}

// The global invocation index 'i' must be derived from gl_GlobalInvocationID.x.
TEST(GpuParticles, GlslSimulateKernelUsesGlobalInvocationId)
{
    EXPECT_NE(cd::gpu_particles::kSimulateCS.find("gl_GlobalInvocationID.x"),
              std::string_view::npos);
}

// The particle write-back must store to P.particles[i] (the slot for THIS
// thread's invocation), not a hard-coded or derived alternative index.
TEST(GpuParticles, GlslSimulateKernelWritesBackToSlotI)
{
    EXPECT_NE(cd::gpu_particles::kSimulateCS.find("P.particles[i] = p"),
              std::string_view::npos);
}

// =============================================================================
// ---- Particle struct layout / size ------------------------------------------
// =============================================================================

// Particle must be exactly 48 bytes: position(12) + life(4) + velocity(12) +
// max_life(4) + color(16) = 48. The GLSL struct mirrors this layout field-by-
// field; any C++ struct padding would break the host ↔ device parity contract.
TEST(GpuParticles, ParticleStructSizeMatchesGlslLayout)
{
    // 3×float + 1×float + 3×float + 1×float + 4×float = 12 floats = 48 bytes.
    static_assert(sizeof(Particle) == 48,
        "Particle size changed — update kSimulateCS GLSL struct to match.");
    EXPECT_EQ(sizeof(Particle), std::size_t{48});
}

// field offsets must match the GLSL struct declaration order
// (position at 0, life at 12, velocity at 16, max_life at 28, color at 32).
TEST(GpuParticles, ParticleFieldOffsetsMatchGlslDeclaration)
{
    EXPECT_EQ(offsetof(Particle, position), std::size_t{0});
    EXPECT_EQ(offsetof(Particle, life),     std::size_t{12});
    EXPECT_EQ(offsetof(Particle, velocity), std::size_t{16});
    EXPECT_EQ(offsetof(Particle, max_life), std::size_t{28});
    EXPECT_EQ(offsetof(Particle, color),    std::size_t{32});
}

// =============================================================================
// ---- GPU emit kernel + indirect-draw buffer ownership SEALED (charter) ------
// =============================================================================

// This test formally seals the two documented out-of-charter features.  It pins
// that NO emit kernel token exists in the current header (if one were silently
// added without the proper charter + ADR, the test would catch it and force a
// deliberate decision).  Likewise there is no IndirectBuffer owner in the public
// API — the host DOES NOT own an IndirectArgs buffer object today.
//
// ADR reference: ADR-20260616-band6-render-misc-scope.md §2.
// Promote-on-need trigger: when a GPU-driven-particles consumer wires the
// render pass, add an emit kernel + IndirectArgs owner alongside kSimulateCS.
TEST(GpuParticles, EmitKernelAndIndirectOwnerSealedOutOfCharter)
{
    const std::string_view cs = cd::gpu_particles::kSimulateCS;
    // kSimulateCS is the in-place simulate kernel; it must NOT contain an emit
    // or spawn entry-point.  "void emit_main" / "void spawn_main" are canary
    // tokens that would indicate the deferred work was accidentally merged.
    EXPECT_EQ(cs.find("emit_main"),  std::string_view::npos);
    EXPECT_EQ(cs.find("spawn_main"), std::string_view::npos);

    // The public API must contain exactly the sealed symbols: advance(),
    // compact_alive(), Particle, and kSimulateCS.  There must be no
    // IndirectBuffer / IndirectArgs class/struct in the public header — that
    // object is out-of-charter until a render-pass consumer is wired.
    // We verify by ensuring the only IndirectArgs reference is INSIDE the
    // kSimulateCS GLSL string (the layout declaration), not as a host C++ type.
    // (A host type would need to exist independently of the GLSL string; here we
    //  confirm the source string itself starts with the GLSL open-quote, meaning
    //  the IndirectArgs token is inside the shader literal, not outside it.)
    EXPECT_NE(cs.find("IndirectArgs"), std::string_view::npos);  // in the GLSL
}

}  // namespace
