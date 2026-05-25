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

TEST(GpuParticles, GlslSimulateKernelNonEmpty)
{
    EXPECT_FALSE(cd::gpu_particles::kSimulateCS.empty());
}

}  // namespace
