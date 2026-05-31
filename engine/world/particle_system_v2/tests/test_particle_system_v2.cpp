// =============================================================================
// CHROMODYNAMIC - tests/test_particle_system_v2.cpp
// Phase 564 (M3 W5C) - cd::particle::system_v2 unit tests.
//
// Brief-mandated coverage (5+ cases):
//   1. Empty system has 0 particles.
//   2. Emitter spawns at expected rate over 1 second.
//   3. Particles die after life_seconds.
//   4. snapshot() writes correct count + interpolated color.
//   5. Multiple emitters compose.
//
// Extras:
//   6. remove_emitter() stops further spawning; existing particles age out.
//   7. snapshot() size clamped to particle_count().
// =============================================================================
#include <cd/particle/system_v2/ParticleSystemV2.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace
{

using cd::particle::system_v2::EmitterId;
using cd::particle::system_v2::EmitterSpec;
using cd::particle::system_v2::ParticleSnapshot;
using cd::particle::system_v2::ParticleSpec;
using cd::particle::system_v2::System;
using cd::particle::system_v2::kInvalidEmitter;

/// Build a deterministic EmitterSpec with fixed velocity (zero spread) so
/// spawn-rate assertions are not confounded by jitter.
EmitterSpec make_spec(float rate,
                      float life_s,
                      std::array<float, 4> color_start = {1.0F, 0.0F, 0.0F, 1.0F},
                      std::array<float, 4> color_end   = {0.0F, 0.0F, 1.0F, 0.0F})
{
    EmitterSpec s {};
    s.emit_rate_per_sec = rate;
    s.position          = {0.0F, 0.0F, 0.0F};
    // Zero velocity spread so every spawn goes straight up.
    s.velocity_min = {0.0F, 1.0F, 0.0F};
    s.velocity_max = {0.0F, 1.0F, 0.0F};
    s.particle.life_seconds = life_s;
    s.particle.size_start   = 2.0F;
    s.particle.size_end     = 0.0F;
    s.particle.color_start  = color_start;
    s.particle.color_end    = color_end;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Empty system has 0 particles.
// ---------------------------------------------------------------------------
TEST(ParticleSystemV2, EmptySystemHasZeroParticles)
{
    System sys;
    EXPECT_EQ(sys.particle_count(), 0U);
}

// ---------------------------------------------------------------------------
// 2. Emitter spawns at expected rate over 1 second.
//    Rate = 10 p/s, step = 0.1s x 10 steps => expect 10 particles.
// ---------------------------------------------------------------------------
TEST(ParticleSystemV2, EmitterSpawnsAtExpectedRateOverOneSecond)
{
    System sys;
    sys.add_emitter(make_spec(10.0F, /*life_s=*/5.0F));

    // 10 ticks of 0.1s = 1 second wall-clock.
    for (int i = 0; i < 10; ++i)
    {
        sys.tick(0.1F);
    }

    EXPECT_EQ(sys.particle_count(), 10U);
}

// ---------------------------------------------------------------------------
// 3. Particles die after life_seconds.
//    Spawn 1 particle then remove the emitter so no new particles are
//    created.  After the particle's life_seconds have elapsed it must be gone.
// ---------------------------------------------------------------------------
TEST(ParticleSystemV2, ParticlesDieAfterLifeSeconds)
{
    System sys;
    const EmitterId eid = sys.add_emitter(make_spec(/*rate=*/5.0F, /*life_s=*/0.5F));

    // Advance 0.2s -> spawn at least one particle.
    sys.tick(0.2F);
    const std::size_t alive_mid = sys.particle_count();
    EXPECT_GT(alive_mid, 0U);

    // Remove emitter so no new particles are spawned in the next tick.
    sys.remove_emitter(eid);

    // Advance well past life_s=0.5 from the spawn time.
    // The particles were spawned at age=0; after the next tick their
    // age will be 0.2 + 0.4 = 0.6 > 0.5 = life.
    sys.tick(0.4F);
    EXPECT_EQ(sys.particle_count(), 0U);
}

// ---------------------------------------------------------------------------
// 4. snapshot() writes correct count + interpolated color.
//    color_start = {1,0,0,1}, color_end = {0,0,1,0}.
//    Spawn 1 particle with life=2.0s.  After tick(1.0) the particle is at
//    normalized age = 0.5, so red ~ 0.5, blue ~ 0.5, size ~ 1.0.
// ---------------------------------------------------------------------------
TEST(ParticleSystemV2, SnapshotWritesCorrectCountAndInterpolatedColor)
{
    // Use rate=1 p/s, life=2.0s.  After tick(1.0s) exactly 1 particle lives
    // at normalized age 0.5 (1.0 / 2.0).
    System sys;
    sys.add_emitter(make_spec(/*rate=*/1.0F, /*life_s=*/2.0F,
                              {1.0F, 0.0F, 0.0F, 1.0F},    // color_start: red
                              {0.0F, 0.0F, 1.0F, 0.0F}));  // color_end:   blue

    sys.tick(1.0F);  // age=1.0, life=2.0 => t_norm=0.5
    ASSERT_EQ(sys.particle_count(), 1U);

    std::vector<ParticleSnapshot> snaps(1);
    const std::size_t written = sys.snapshot(snaps);
    EXPECT_EQ(written, 1U);

    // At normalized age = 0.5: red ~ 0.5, blue ~ 0.5.
    EXPECT_NEAR(snaps[0].color[0], 0.5F, 0.01F);  // red lerp
    EXPECT_NEAR(snaps[0].color[2], 0.5F, 0.01F);  // blue lerp
    // Size lerp: size_start=2, size_end=0 -> size at midpoint = 1.0.
    EXPECT_NEAR(snaps[0].size, 1.0F, 0.01F);
}

// ---------------------------------------------------------------------------
// 5. Multiple emitters compose independently.
//    Two emitters at different positions; particles stay near their origin.
// ---------------------------------------------------------------------------
TEST(ParticleSystemV2, MultipleEmittersCompose)
{
    System sys;
    EmitterSpec specA = make_spec(5.0F, /*life_s=*/5.0F);
    specA.position   = {10.0F, 0.0F, 0.0F};
    specA.velocity_min = {0.0F, 0.0F, 0.0F};
    specA.velocity_max = {0.0F, 0.0F, 0.0F};

    EmitterSpec specB = make_spec(5.0F, /*life_s=*/5.0F);
    specB.position   = {-10.0F, 0.0F, 0.0F};
    specB.velocity_min = {0.0F, 0.0F, 0.0F};
    specB.velocity_max = {0.0F, 0.0F, 0.0F};

    sys.add_emitter(specA);
    sys.add_emitter(specB);

    sys.tick(0.2F);  // should spawn 1 particle each (5 p/s * 0.2 = 1.0)

    // Expect 2 particles total (one from each emitter).
    ASSERT_EQ(sys.particle_count(), 2U);

    std::vector<ParticleSnapshot> snaps(2);
    const std::size_t written_multi = sys.snapshot(snaps);
    ASSERT_EQ(written_multi, 2U);

    // One particle near x=10, one near x=-10.
    const bool found_pos = (std::fabs(snaps[0].pos[0] - 10.0F) < 0.01F)
                        || (std::fabs(snaps[1].pos[0] - 10.0F) < 0.01F);
    const bool found_neg = (std::fabs(snaps[0].pos[0] + 10.0F) < 0.01F)
                        || (std::fabs(snaps[1].pos[0] + 10.0F) < 0.01F);
    EXPECT_TRUE(found_pos);
    EXPECT_TRUE(found_neg);
}

// ---------------------------------------------------------------------------
// 6. remove_emitter() stops further spawning; existing particles age out.
// ---------------------------------------------------------------------------
TEST(ParticleSystemV2, RemoveEmitterStopsSpawningExistingParticlesAgeOut)
{
    System sys;
    const EmitterId id = sys.add_emitter(make_spec(/*rate=*/10.0F, /*life_s=*/1.0F));

    sys.tick(0.1F);  // spawn ~1 particle
    const std::size_t count_before = sys.particle_count();
    EXPECT_GT(count_before, 0U);

    sys.remove_emitter(id);

    // Another tick should NOT increase the count (no new spawns).
    sys.tick(0.1F);
    // Count may have stayed same or dropped (ageing), but never more.
    EXPECT_LE(sys.particle_count(), count_before);

    // Advance past life_s=1.0 from spawn time.
    sys.tick(1.0F);
    EXPECT_EQ(sys.particle_count(), 0U);
}

// ---------------------------------------------------------------------------
// 7. snapshot() is clamped to particle_count() even if span is larger.
// ---------------------------------------------------------------------------
TEST(ParticleSystemV2, SnapshotClampedToParticleCount)
{
    System sys;
    sys.add_emitter(make_spec(/*rate=*/3.0F, /*life_s=*/5.0F));
    sys.tick(1.0F);  // 3 particles

    const std::size_t n = sys.particle_count();
    ASSERT_GT(n, 0U);

    // Allocate more slots than there are particles.
    std::vector<ParticleSnapshot> snaps(n + 10U);
    const std::size_t written = sys.snapshot(snaps);
    EXPECT_EQ(written, n);
}
