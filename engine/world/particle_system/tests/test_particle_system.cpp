// =============================================================================
// CHROMODYNAMIC - tests/test_particle_system.cpp
// Phase 564 (M3 W5C) - cd::particle::system unit tests.
// Phase 583 (M5 W1) - Updated for renamed namespace (was system_v2).
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
#include <cd/particle/system/ParticleSystem.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace
{

using cd::particle::system::EmitterId;
using cd::particle::system::EmitterSpec;
using cd::particle::system::ParticleSnapshot;
using cd::particle::system::System;
using cd::particle::system::kInvalidEmitter;

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
TEST(ParticleSystem, EmptySystemHasZeroParticles)
{
    System sys;
    EXPECT_EQ(sys.particle_count(), 0U);
}

// ---------------------------------------------------------------------------
// 2. Emitter spawns at expected rate over 1 second.
//    Rate = 10 p/s, step = 0.1s x 10 steps => expect 10 particles.
// ---------------------------------------------------------------------------
TEST(ParticleSystem, EmitterSpawnsAtExpectedRateOverOneSecond)
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
TEST(ParticleSystem, ParticlesDieAfterLifeSeconds)
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
TEST(ParticleSystem, SnapshotWritesCorrectCountAndInterpolatedColor)
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
TEST(ParticleSystem, MultipleEmittersCompose)
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
TEST(ParticleSystem, RemoveEmitterStopsSpawningExistingParticlesAgeOut)
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
TEST(ParticleSystem, SnapshotClampedToParticleCount)
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

// ===========================================================================
// Band4 singletons topup — real untested CPU-sim branches.
// ===========================================================================

// ---------------------------------------------------------------------------
// 8. tick() with dt <= 0 is a no-op (the `if (dt <= 0.0F) return;` guard).
//    Neither spawning nor integration nor ageing happens.
// ---------------------------------------------------------------------------
TEST(ParticleSystem, ZeroAndNegativeDtAreNoOps)
{
    System sys;
    sys.add_emitter(make_spec(/*rate=*/100.0F, /*life_s=*/5.0F));

    sys.tick(0.0F);
    EXPECT_EQ(sys.particle_count(), 0U) << "dt=0 must not spawn";

    sys.tick(-1.0F);
    EXPECT_EQ(sys.particle_count(), 0U) << "dt<0 must not spawn";

    // Spawn one particle, then confirm a zero-dt tick neither ages it out nor
    // moves it.
    sys.tick(0.01F);  // 100 p/s * 0.01 = 1.0 -> exactly one particle
    ASSERT_EQ(sys.particle_count(), 1U);

    std::vector<ParticleSnapshot> before(1);
    ASSERT_EQ(sys.snapshot(before), 1U);
    sys.tick(0.0F);  // no-op: position + age unchanged
    std::vector<ParticleSnapshot> after(1);
    ASSERT_EQ(sys.snapshot(after), 1U);
    EXPECT_FLOAT_EQ(after[0].pos[1], before[0].pos[1]);
}

// ---------------------------------------------------------------------------
// 9. Spawn accumulator carries the fractional remainder forward across ticks.
//    Rate=10 p/s, dt=0.05s -> 0.5 particle/tick. After one tick: 0 spawned,
//    accum=0.5. After the second tick: accum=1.0 -> exactly one spawn.
// ---------------------------------------------------------------------------
TEST(ParticleSystem, SpawnAccumulatorCarriesFractionForward)
{
    System sys;
    sys.add_emitter(make_spec(/*rate=*/10.0F, /*life_s=*/100.0F));

    sys.tick(0.05F);  // accum = 0.5 -> below threshold, no spawn yet
    EXPECT_EQ(sys.particle_count(), 0U);

    sys.tick(0.05F);  // accum = 1.0 -> exactly one spawn, remainder 0
    EXPECT_EQ(sys.particle_count(), 1U);

    sys.tick(0.05F);  // accum = 0.5 again -> still one particle
    EXPECT_EQ(sys.particle_count(), 1U);
}

// ---------------------------------------------------------------------------
// 10. A single large dt can consume multiple whole spawns from the
//     accumulator in one tick (the `while (accum >= 1.0F)` loop).
// ---------------------------------------------------------------------------
TEST(ParticleSystem, LargeDtConsumesMultipleWholeSpawns)
{
    System sys;
    sys.add_emitter(make_spec(/*rate=*/10.0F, /*life_s=*/100.0F));

    sys.tick(0.35F);  // 10 * 0.35 = 3.5 -> 3 spawns, remainder 0.5
    EXPECT_EQ(sys.particle_count(), 3U);
}

// ---------------------------------------------------------------------------
// 11. Dead-particle compaction: the middle particle of a batch expires while
//     younger neighbours survive. swap-and-pop moves the last live particle
//     into the freed slot and must NOT skip processing it. Verifies the
//     "do NOT increment i" branch in tick().
// ---------------------------------------------------------------------------
TEST(ParticleSystem, DeadParticleCompactionKeepsSurvivors)
{
    System sys;
    // Emitter A: short life — its particle dies first.
    EmitterSpec short_lived = make_spec(/*rate=*/1.0F, /*life_s=*/0.3F);
    short_lived.position = {5.0F, 0.0F, 0.0F};
    short_lived.velocity_min = {0.0F, 0.0F, 0.0F};
    short_lived.velocity_max = {0.0F, 0.0F, 0.0F};

    // Emitter B: long life — its particle survives the compaction.
    EmitterSpec long_lived = make_spec(/*rate=*/1.0F, /*life_s=*/10.0F);
    long_lived.position = {-7.0F, 0.0F, 0.0F};
    long_lived.velocity_min = {0.0F, 0.0F, 0.0F};
    long_lived.velocity_max = {0.0F, 0.0F, 0.0F};

    const EmitterId a = sys.add_emitter(short_lived);
    const EmitterId b = sys.add_emitter(long_lived);

    sys.tick(1.0F);  // both spawn at least one (1 p/s * 1s); short one (life 0.3)
                     // ages to 1.0 > 0.3 and is compacted out this same tick.

    sys.remove_emitter(a);
    sys.remove_emitter(b);

    // Exactly the long-lived survivor remains, at its emitter origin (x=-7),
    // proving the swapped-in particle wasn't dropped or corrupted.
    ASSERT_EQ(sys.particle_count(), 1U);
    std::vector<ParticleSnapshot> snaps(1);
    ASSERT_EQ(sys.snapshot(snaps), 1U);
    EXPECT_NEAR(snaps[0].pos[0], -7.0F, 0.01F);
}

// ---------------------------------------------------------------------------
// 12. Velocity spread: with a non-degenerate [min,max] envelope every sampled
//     component stays within the authored bounds (random_range happy path).
// ---------------------------------------------------------------------------
TEST(ParticleSystem, VelocitySpreadStaysWithinBounds)
{
    System sys;
    EmitterSpec spec = make_spec(/*rate=*/50.0F, /*life_s=*/100.0F);
    spec.velocity_min = {-2.0F, 1.0F, -3.0F};
    spec.velocity_max = { 2.0F, 5.0F,  3.0F};
    sys.add_emitter(spec);

    sys.tick(1.0F);  // ~50 particles
    const std::size_t n = sys.particle_count();
    ASSERT_GE(n, 1U);

    // After one second of constant-velocity integration the X displacement
    // equals vx (dt=1). Bounds: vx in [-2,2] -> |x| <= 2 + small float slack.
    std::vector<ParticleSnapshot> snaps(n);
    ASSERT_EQ(sys.snapshot(snaps), n);
    for (const auto& s : snaps)
    {
        EXPECT_GE(s.pos[0], -2.0001F);
        EXPECT_LE(s.pos[0],  2.0001F);
    }
}

// ---------------------------------------------------------------------------
// 13. remove_emitter() with an unknown / invalid id is a silent no-op and
//     does not disturb existing emitters.
// ---------------------------------------------------------------------------
TEST(ParticleSystem, RemoveUnknownEmitterIsNoOp)
{
    System sys;
    const EmitterId valid = sys.add_emitter(make_spec(/*rate=*/10.0F, /*life_s=*/5.0F));

    sys.remove_emitter(kInvalidEmitter);          // unknown sentinel — no-op
    sys.remove_emitter(valid + 12345U);           // out-of-range id — no-op

    sys.tick(1.0F);  // the still-registered valid emitter keeps spawning
    EXPECT_EQ(sys.particle_count(), 10U);
}
