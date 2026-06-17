#include <cd/gpu_particles/GpuParticles.hpp>

#include <gtest/gtest.h>

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

}  // namespace
