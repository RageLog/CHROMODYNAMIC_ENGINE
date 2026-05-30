// =============================================================================
// CHROMODYNAMIC - tests/test_particles_event.cpp
// Phase 498 (G3.4) - cd::game::particles_event unit tests.
//
// Brief-mandated coverage (6 cases):
//   1. Register recipe + fire spawns burst with N particles.
//   2. Multiple fires at different positions = independent bursts.
//   3. Lifetime expires removes burst.
//   4. Unknown recipe = no-op + log warning.
//   5. Multiple recipes coexist.
//   6. Emitter shape kBox vs kSphere yields different bounds.
//
// Extras (regression / boundary):
//   7. on_emit callback fires once per fire() with correct burst data.
//   8. unregister_recipe blocks future fires but does not kill live bursts.
//   9. alive_count decays monotonically with age.
//  10. clear_active + reset semantics.
// =============================================================================
#include <cd/game/particles_event/ParticlesEvent.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace
{

using cd::game::particles_event::ActiveBurst;
using cd::game::particles_event::EmitterShape;
using cd::game::particles_event::ParticleEventDispatcher;
using cd::game::particles_event::ParticleRecipe;
using cd::math::Quatf;
using cd::math::Vec3f;
using cd::math::Vec4f;

ParticleRecipe make_recipe(std::uint32_t count,
                           float          lifetime_s,
                           EmitterShape   shape,
                           const Vec3f&   extent)
{
    ParticleRecipe r {};
    r.count                    = count;
    r.lifetime_s               = lifetime_s;
    r.gravity                  = Vec3f {0.0F, -9.81F, 0.0F};
    r.velocity_min             = Vec3f {-1.0F, 0.0F, -1.0F};
    r.velocity_max             = Vec3f { 1.0F, 4.0F,  1.0F};
    r.color_start              = Vec4f {1.0F, 1.0F, 1.0F, 1.0F};
    r.color_end                = Vec4f {1.0F, 1.0F, 1.0F, 0.0F};
    r.emitter_shape            = shape;
    r.emitter_radius_or_extent = extent;
    return r;
}

}  // namespace

// 1 - Register recipe + fire spawns burst with N particles.
TEST(ParticlesEvent, RegisterAndFireSpawnsBurstWithExpectedCount)
{
    ParticleEventDispatcher d;
    d.register_recipe("muzzle_flash",
                      make_recipe(48U, 1.0F, EmitterShape::kSphere,
                                  Vec3f {0.5F, 0.0F, 0.0F}));

    EXPECT_EQ(d.recipe_count(), 1U);

    const std::uint32_t idx = d.fire("muzzle_flash",
                                     Vec3f {1.0F, 2.0F, 3.0F},
                                     Quatf::identity());

    ASSERT_NE(idx, ParticleEventDispatcher::kInvalidBurst);
    ASSERT_EQ(d.active_count(), 1U);
    const ActiveBurst& b = d.active_bursts()[idx];
    EXPECT_EQ(b.alive_count, 48U);
    EXPECT_EQ(b.recipe_idx, 0U);
    EXPECT_FLOAT_EQ(b.origin.x, 1.0F);
    EXPECT_FLOAT_EQ(b.origin.y, 2.0F);
    EXPECT_FLOAT_EQ(b.origin.z, 3.0F);
    EXPECT_FLOAT_EQ(b.age_s, 0.0F);
}

// 2 - Multiple fires at different positions = independent bursts.
TEST(ParticlesEvent, MultipleFiresProduceIndependentBursts)
{
    ParticleEventDispatcher d;
    d.register_recipe("spark",
                      make_recipe(16U, 2.0F, EmitterShape::kSphere,
                                  Vec3f {0.25F, 0.0F, 0.0F}));

    const std::uint32_t a = d.fire("spark", Vec3f {0.0F, 0.0F, 0.0F},
                                    Quatf::identity());
    const std::uint32_t b = d.fire("spark", Vec3f {10.0F, 0.0F, 0.0F},
                                    Quatf::identity());
    const std::uint32_t c = d.fire("spark", Vec3f {0.0F, 0.0F, 20.0F},
                                    Quatf::identity());

    ASSERT_EQ(d.active_count(), 3U);
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);

    const auto bursts = d.active_bursts();
    EXPECT_FLOAT_EQ(bursts[a].origin.x, 0.0F);
    EXPECT_FLOAT_EQ(bursts[b].origin.x, 10.0F);
    EXPECT_FLOAT_EQ(bursts[c].origin.z, 20.0F);
    EXPECT_EQ(bursts[a].alive_count, 16U);
    EXPECT_EQ(bursts[b].alive_count, 16U);
    EXPECT_EQ(bursts[c].alive_count, 16U);
}

// 3 - Lifetime expires removes burst.
TEST(ParticlesEvent, LifetimeExpiresDropsBurst)
{
    ParticleEventDispatcher d;
    d.register_recipe("puff",
                      make_recipe(8U, 0.5F, EmitterShape::kSphere,
                                  Vec3f {0.5F, 0.0F, 0.0F}));

    d.fire("puff", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    ASSERT_EQ(d.active_count(), 1U);

    // Half-life: burst still alive but alive_count has decayed.
    d.tick(0.25F);
    ASSERT_EQ(d.active_count(), 1U);
    EXPECT_LT(d.active_bursts()[0].alive_count, 8U);
    EXPECT_GT(d.active_bursts()[0].alive_count, 0U);

    // Past lifetime: burst removed.
    d.tick(0.30F);  // total 0.55 > 0.5
    EXPECT_EQ(d.active_count(), 0U);
}

// 4 - Unknown recipe = no-op + warning.
TEST(ParticlesEvent, FireUnknownRecipeIsNoOpAndReturnsInvalid)
{
    ParticleEventDispatcher d;
    // No recipe registered.
    const std::uint32_t idx = d.fire("does_not_exist",
                                     Vec3f {0.0F, 0.0F, 0.0F},
                                     Quatf::identity());
    EXPECT_EQ(idx, ParticleEventDispatcher::kInvalidBurst);
    EXPECT_EQ(d.active_count(), 0U);

    // Even with another recipe present, the unknown name still bails.
    d.register_recipe("known",
                      make_recipe(4U, 1.0F, EmitterShape::kSphere,
                                  Vec3f {0.5F, 0.0F, 0.0F}));
    const std::uint32_t idx2 = d.fire("other",
                                       Vec3f {1.0F, 1.0F, 1.0F},
                                       Quatf::identity());
    EXPECT_EQ(idx2, ParticleEventDispatcher::kInvalidBurst);
    EXPECT_EQ(d.active_count(), 0U);
}

// 5 - Multiple recipes coexist.
TEST(ParticlesEvent, MultipleRecipesCoexist)
{
    ParticleEventDispatcher d;
    d.register_recipe("a",
                      make_recipe(4U, 1.0F, EmitterShape::kSphere,
                                  Vec3f {0.5F, 0.0F, 0.0F}));
    d.register_recipe("b",
                      make_recipe(8U, 2.0F, EmitterShape::kCone,
                                  Vec3f {0.4F, 1.0F, 0.0F}));
    d.register_recipe("c",
                      make_recipe(12U, 3.0F, EmitterShape::kBox,
                                  Vec3f {0.5F, 0.5F, 0.5F}));

    EXPECT_EQ(d.recipe_count(), 3U);

    const std::uint32_t ia = d.fire("a", Vec3f {0.0F, 0.0F, 0.0F},
                                     Quatf::identity());
    const std::uint32_t ib = d.fire("b", Vec3f {1.0F, 0.0F, 0.0F},
                                     Quatf::identity());
    const std::uint32_t ic = d.fire("c", Vec3f {2.0F, 0.0F, 0.0F},
                                     Quatf::identity());

    ASSERT_EQ(d.active_count(), 3U);
    const auto bursts = d.active_bursts();
    EXPECT_EQ(bursts[ia].alive_count, 4U);
    EXPECT_EQ(bursts[ib].alive_count, 8U);
    EXPECT_EQ(bursts[ic].alive_count, 12U);
    // Each burst references a distinct recipe index.
    EXPECT_NE(bursts[ia].recipe_idx, bursts[ib].recipe_idx);
    EXPECT_NE(bursts[ib].recipe_idx, bursts[ic].recipe_idx);
    EXPECT_NE(bursts[ia].recipe_idx, bursts[ic].recipe_idx);
}

// 6 - Emitter shape kBox vs kSphere yields different recipe-side bounds.
TEST(ParticlesEvent, EmitterShapeBoxVsSphereStagesDistinctBounds)
{
    ParticleEventDispatcher d;
    d.register_recipe("sph",
                      make_recipe(4U, 1.0F, EmitterShape::kSphere,
                                  Vec3f {0.75F, 0.0F, 0.0F}));
    d.register_recipe("box",
                      make_recipe(4U, 1.0F, EmitterShape::kBox,
                                  Vec3f {1.0F, 2.0F, 3.0F}));

    const ParticleRecipe* sph = d.find_recipe("sph");
    const ParticleRecipe* box = d.find_recipe("box");
    ASSERT_NE(sph, nullptr);
    ASSERT_NE(box, nullptr);

    // Shape tag differs.
    EXPECT_EQ(sph->emitter_shape, EmitterShape::kSphere);
    EXPECT_EQ(box->emitter_shape, EmitterShape::kBox);

    // Sphere radius stored in .x; .y/.z unused (zero).
    EXPECT_FLOAT_EQ(sph->emitter_radius_or_extent.x, 0.75F);
    EXPECT_FLOAT_EQ(sph->emitter_radius_or_extent.y, 0.0F);
    EXPECT_FLOAT_EQ(sph->emitter_radius_or_extent.z, 0.0F);

    // Box full Vec3 extents.
    EXPECT_FLOAT_EQ(box->emitter_radius_or_extent.x, 1.0F);
    EXPECT_FLOAT_EQ(box->emitter_radius_or_extent.y, 2.0F);
    EXPECT_FLOAT_EQ(box->emitter_radius_or_extent.z, 3.0F);

    // Spheres and boxes do NOT produce the same bound vector.
    const Vec3f s = sph->emitter_radius_or_extent;
    const Vec3f b = box->emitter_radius_or_extent;
    const bool  same = (s.x == b.x) && (s.y == b.y) && (s.z == b.z);
    EXPECT_FALSE(same);
}

// 7 - on_emit callback fires once per fire() with correct burst data.
TEST(ParticlesEvent, OnEmitCallbackFiresWithBurstSnapshot)
{
    ParticleEventDispatcher d;
    d.register_recipe("cb",
                      make_recipe(7U, 1.0F, EmitterShape::kSphere,
                                  Vec3f {0.5F, 0.0F, 0.0F}));

    int   call_count   = 0;
    Vec3f last_origin {0.0F, 0.0F, 0.0F};
    std::uint32_t last_count = 0U;

    const std::uint32_t cb_id =
        d.add_on_emit([&](const ActiveBurst& b) {
            ++call_count;
            last_origin = b.origin;
            last_count  = b.alive_count;
        });

    d.fire("cb", Vec3f {5.0F, 6.0F, 7.0F}, Quatf::identity());
    EXPECT_EQ(call_count, 1);
    EXPECT_FLOAT_EQ(last_origin.x, 5.0F);
    EXPECT_FLOAT_EQ(last_origin.y, 6.0F);
    EXPECT_FLOAT_EQ(last_origin.z, 7.0F);
    EXPECT_EQ(last_count, 7U);

    d.fire("cb", Vec3f {-1.0F, 0.0F, 0.0F}, Quatf::identity());
    EXPECT_EQ(call_count, 2);
    EXPECT_FLOAT_EQ(last_origin.x, -1.0F);

    // tick() must NOT invoke on_emit callbacks.
    d.tick(0.1F);
    EXPECT_EQ(call_count, 2);

    // remove_on_emit detaches.
    d.remove_on_emit(cb_id);
    d.fire("cb", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    EXPECT_EQ(call_count, 2);
}

// 8 - unregister_recipe blocks future fires but keeps live bursts alive.
TEST(ParticlesEvent, UnregisterRecipeBlocksFutureFiresKeepsLiveBursts)
{
    ParticleEventDispatcher d;
    d.register_recipe("tmp",
                      make_recipe(3U, 1.0F, EmitterShape::kSphere,
                                  Vec3f {0.5F, 0.0F, 0.0F}));

    d.fire("tmp", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    ASSERT_EQ(d.active_count(), 1U);

    EXPECT_TRUE(d.unregister_recipe("tmp"));
    EXPECT_EQ(d.recipe_count(), 0U);
    EXPECT_FALSE(d.unregister_recipe("tmp"));

    // Future fires fail.
    const std::uint32_t idx = d.fire("tmp",
                                     Vec3f {0.0F, 0.0F, 0.0F},
                                     Quatf::identity());
    EXPECT_EQ(idx, ParticleEventDispatcher::kInvalidBurst);

    // Live burst still ages.
    ASSERT_EQ(d.active_count(), 1U);
    d.tick(0.5F);
    EXPECT_EQ(d.active_count(), 1U);
    d.tick(0.6F);  // total 1.1 > 1.0
    EXPECT_EQ(d.active_count(), 0U);
}

// 9 - alive_count decays monotonically with age.
TEST(ParticlesEvent, AliveCountDecaysMonotonically)
{
    ParticleEventDispatcher d;
    d.register_recipe("decay",
                      make_recipe(100U, 1.0F, EmitterShape::kSphere,
                                  Vec3f {0.5F, 0.0F, 0.0F}));

    d.fire("decay", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());

    std::uint32_t prev = d.active_bursts()[0].alive_count;
    EXPECT_EQ(prev, 100U);
    for (int step = 0; step < 5; ++step)
    {
        d.tick(0.1F);
        if (d.active_count() == 0U)
        {
            break;
        }
        const std::uint32_t now = d.active_bursts()[0].alive_count;
        EXPECT_LE(now, prev);
        prev = now;
    }
}

// 10 - clear_active + reset semantics.
TEST(ParticlesEvent, ClearActiveAndResetClearScopeOnly)
{
    ParticleEventDispatcher d;
    d.register_recipe("r",
                      make_recipe(4U, 1.0F, EmitterShape::kSphere,
                                  Vec3f {0.5F, 0.0F, 0.0F}));
    d.fire("r", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    d.fire("r", Vec3f {1.0F, 0.0F, 0.0F}, Quatf::identity());
    ASSERT_EQ(d.active_count(), 2U);
    ASSERT_EQ(d.recipe_count(), 1U);

    d.clear_active();
    EXPECT_EQ(d.active_count(), 0U);
    EXPECT_EQ(d.recipe_count(), 1U);  // recipe preserved.

    d.fire("r", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    EXPECT_EQ(d.active_count(), 1U);

    d.reset();
    EXPECT_EQ(d.active_count(), 0U);
    EXPECT_EQ(d.recipe_count(), 0U);
    EXPECT_EQ(d.find_recipe("r"), nullptr);
}
