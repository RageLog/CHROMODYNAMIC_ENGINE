#include <cd/gpu_particles/GpuParticles.hpp>

#include <gtest/gtest.h>

#include <cstddef>
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

}  // namespace
