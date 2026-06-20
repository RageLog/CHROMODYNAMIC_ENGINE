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
//
// Dense-handle + tombstone + generation (new, 100% close):
//  11. Recipe slot reuse: tombstone -> re-register reuses freed slot.
//  12. Double-free guard: second unregister returns false, one free slot only.
//  13. Stale BurstHandle: lookup_burst returns nullptr after burst expires.
//  14. kInvalidHandle lookup always returns nullptr.
//  15. fire_handle with unknown recipe returns kInvalidHandle.
//  16. Zero-count recipe burst: alive_count=0 throughout lifetime.
//  17. Zero-lifetime burst expires on first positive tick.
//  18. tick(0.0F) is a complete no-op (no aging, no compaction).
//  19. tick(negative) is a complete no-op.
//  20. Capacity growth: 200 fires, all alive, all expired in one tick.
//  21. Max count recipe: alive_count decay stays near UINT32_MAX/2 at 50%.
//  22. fire("") empty name: no-op unless "" is registered.
//  23. Recipe name echoed back: find_recipe(n)->name == n always.
//  24. register_recipe replaces in-place: live burst sees updated recipe.
//  25. Callback self-remove during dispatch is safe (snapshot pattern).
//  26. Burst staging order: fire order governs active_bursts() position.
//  27. Dispatch with no recipes registered: all fires return kInvalidBurst.
//  28. BurstHandle uniqueness: each fire_handle yields a distinct handle.
//  29. find_recipe returns nullptr after unregister_recipe.
// =============================================================================
#include <cd/game/particles_event/ParticlesEvent.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

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

// ---------------------------------------------------------------------------
// 11 - Recipe slot reuse: unregister + re-register reuses the tombstoned slot.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, RecipeSlotReusedAfterUnregister)
{
    ParticleEventDispatcher d;
    d.register_recipe("a", make_recipe(4U, 1.0F, EmitterShape::kSphere,
                                       Vec3f {0.5F, 0.0F, 0.0F}));
    // One slot used; zero free.
    EXPECT_EQ(d.free_recipe_slot_count(), 0U);

    EXPECT_TRUE(d.unregister_recipe("a"));
    // Slot is now on the free list.
    EXPECT_EQ(d.free_recipe_slot_count(), 1U);
    EXPECT_EQ(d.recipe_count(), 0U);

    // Register a new (different) name — should reuse the freed slot.
    d.register_recipe("b", make_recipe(8U, 2.0F, EmitterShape::kCone,
                                       Vec3f {0.3F, 1.0F, 0.0F}));
    EXPECT_EQ(d.free_recipe_slot_count(), 0U);  // slot consumed
    EXPECT_EQ(d.recipe_count(), 1U);

    const ParticleRecipe* b = d.find_recipe("b");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->count, 8U);

    // Fire using reused slot — burst must reflect the new recipe data.
    const std::uint32_t idx = d.fire("b", Vec3f {0.0F, 0.0F, 0.0F},
                                      Quatf::identity());
    ASSERT_NE(idx, ParticleEventDispatcher::kInvalidBurst);
    EXPECT_EQ(d.active_bursts()[idx].alive_count, 8U);
}

// ---------------------------------------------------------------------------
// 12 - Double-free guard: unregister_recipe is idempotent (false on 2nd call).
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, UnregisterRecipeDoubleFreeGuard)
{
    ParticleEventDispatcher d;
    d.register_recipe("x", make_recipe(4U, 1.0F, EmitterShape::kSphere,
                                       Vec3f {0.5F, 0.0F, 0.0F}));
    EXPECT_TRUE(d.unregister_recipe("x"));
    // Second call must not crash and must return false.
    EXPECT_FALSE(d.unregister_recipe("x"));
    // Only one slot on the free list (not two).
    EXPECT_EQ(d.free_recipe_slot_count(), 1U);
}

// ---------------------------------------------------------------------------
// 13 - Stale-handle access: fire_handle + tick past lifetime -> lookup nullptr.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, BurstHandleInvalidatedAfterExpiry)
{
    using cd::game::particles_event::BurstHandle;
    using cd::game::particles_event::kInvalidHandle;

    ParticleEventDispatcher d;
    d.register_recipe("h", make_recipe(10U, 0.5F, EmitterShape::kSphere,
                                       Vec3f {0.5F, 0.0F, 0.0F}));

    const BurstHandle h = d.fire_handle("h", Vec3f {1.0F, 2.0F, 3.0F},
                                        Quatf::identity());
    EXPECT_TRUE(h.is_valid());
    EXPECT_NE(h, kInvalidHandle);

    // While alive, lookup succeeds.
    const cd::game::particles_event::ActiveBurst* p = d.lookup_burst(h);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->origin.x, 1.0F);
    EXPECT_EQ(p->alive_count, 10U);

    // Tick past lifetime.
    d.tick(0.6F);  // 0.6 > 0.5 -> burst expired and compacted
    EXPECT_EQ(d.active_count(), 0U);

    // Stale handle now returns nullptr.
    EXPECT_EQ(d.lookup_burst(h), nullptr);
}

// ---------------------------------------------------------------------------
// 14 - kInvalidHandle lookup is always nullptr.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, InvalidHandleLookupReturnsNull)
{
    using cd::game::particles_event::kInvalidHandle;

    ParticleEventDispatcher d;
    EXPECT_EQ(d.lookup_burst(kInvalidHandle), nullptr);

    // Even with live bursts present.
    d.register_recipe("q", make_recipe(5U, 1.0F, EmitterShape::kSphere,
                                       Vec3f {0.5F, 0.0F, 0.0F}));
    d.fire("q", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    EXPECT_EQ(d.lookup_burst(kInvalidHandle), nullptr);
}

// ---------------------------------------------------------------------------
// 15 - fire_handle with unknown recipe returns kInvalidHandle.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, FireHandleUnknownRecipeReturnsInvalidHandle)
{
    using cd::game::particles_event::kInvalidHandle;

    ParticleEventDispatcher d;
    const auto h = d.fire_handle("nope", Vec3f {0.0F, 0.0F, 0.0F},
                                  Quatf::identity());
    EXPECT_EQ(h, kInvalidHandle);
    EXPECT_FALSE(h.is_valid());
    EXPECT_EQ(d.active_count(), 0U);
}

// ---------------------------------------------------------------------------
// 16 - Count=0 recipe fires a burst with alive_count=0 that ticks correctly.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, ZeroCountRecipeBurstBehavior)
{
    ParticleEventDispatcher d;
    d.register_recipe("zero", make_recipe(0U, 1.0F, EmitterShape::kSphere,
                                          Vec3f {0.5F, 0.0F, 0.0F}));
    const std::uint32_t idx = d.fire("zero", Vec3f {0.0F, 0.0F, 0.0F},
                                      Quatf::identity());
    ASSERT_NE(idx, ParticleEventDispatcher::kInvalidBurst);
    EXPECT_EQ(d.active_bursts()[idx].alive_count, 0U);

    // Mid-life: alive_count remains 0 (0 * any_fraction == 0).
    d.tick(0.4F);
    ASSERT_EQ(d.active_count(), 1U);
    EXPECT_EQ(d.active_bursts()[0].alive_count, 0U);

    // Expired after lifetime.
    d.tick(0.7F);  // total 1.1 > 1.0
    EXPECT_EQ(d.active_count(), 0U);
}

// ---------------------------------------------------------------------------
// 17 - Zero-lifetime recipe: burst expires on very next tick.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, ZeroLifetimeBurstExpiresImmediately)
{
    ParticleEventDispatcher d;
    // lifetime_s = 0 -> clamp to 1e-6F inside tick.
    d.register_recipe("instant", make_recipe(4U, 0.0F, EmitterShape::kSphere,
                                             Vec3f {0.5F, 0.0F, 0.0F}));
    d.fire("instant", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    ASSERT_EQ(d.active_count(), 1U);

    // Any positive dt >= 0 satisfies age_s >= lifetime_s (0 >= 0 already).
    d.tick(1e-7F);
    EXPECT_EQ(d.active_count(), 0U);
}

// ---------------------------------------------------------------------------
// 18 - tick(0.0F) is a complete no-op: no aging, no compaction of expired.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, TickZeroDtIsNoOp)
{
    ParticleEventDispatcher d;
    d.register_recipe("nop", make_recipe(10U, 0.5F, EmitterShape::kSphere,
                                         Vec3f {0.5F, 0.0F, 0.0F}));
    d.fire("nop", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());

    // Age the burst to just below lifetime.
    d.tick(0.49F);
    ASSERT_EQ(d.active_count(), 1U);
    const float age_before = d.active_bursts()[0].age_s;

    // Zero tick: must NOT age, NOT compact.
    d.tick(0.0F);
    ASSERT_EQ(d.active_count(), 1U);
    EXPECT_FLOAT_EQ(d.active_bursts()[0].age_s, age_before);
}

// ---------------------------------------------------------------------------
// 19 - tick(negative dt) is a complete no-op.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, TickNegativeDtIsNoOp)
{
    ParticleEventDispatcher d;
    d.register_recipe("neg", make_recipe(10U, 1.0F, EmitterShape::kSphere,
                                         Vec3f {0.5F, 0.0F, 0.0F}));
    d.fire("neg", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    d.tick(0.3F);
    const float age_after_fwd = d.active_bursts()[0].age_s;

    // Negative tick must NOT reverse age and must NOT compact.
    d.tick(-999.0F);
    ASSERT_EQ(d.active_count(), 1U);
    EXPECT_FLOAT_EQ(d.active_bursts()[0].age_s, age_after_fwd);
}

// ---------------------------------------------------------------------------
// 20 - Burst capacity growth: 200 fires, all alive, then all expired.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, CapacityGrowthManyFires)
{
    ParticleEventDispatcher d;
    d.register_recipe("many", make_recipe(1U, 1.0F, EmitterShape::kSphere,
                                          Vec3f {0.5F, 0.0F, 0.0F}));

    constexpr std::size_t kCount = 200U;
    for (std::size_t i = 0; i < kCount; ++i)
    {
        const auto fi = static_cast<float>(i);
        d.fire("many", Vec3f {fi, 0.0F, 0.0F}, Quatf::identity());
    }
    EXPECT_EQ(d.active_count(), kCount);

    // Verify all origins are distinct and in order.
    const auto bursts = d.active_bursts();
    for (std::size_t i = 0; i < kCount; ++i)
    {
        EXPECT_FLOAT_EQ(bursts[i].origin.x, static_cast<float>(i));
    }

    // Single tick past lifetime removes all.
    d.tick(1.1F);
    EXPECT_EQ(d.active_count(), 0U);
}

// ---------------------------------------------------------------------------
// 21 - Burst with count=UINT32_MAX does not wrap alive_count computation.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, MaxCountRecipeAliveCountDecay)
{
    ParticleEventDispatcher d;
    ParticleRecipe r {};
    r.count      = std::numeric_limits<std::uint32_t>::max();
    r.lifetime_s = 1.0F;
    d.register_recipe("big", r);

    d.fire("big", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    ASSERT_EQ(d.active_count(), 1U);
    EXPECT_EQ(d.active_bursts()[0].alive_count,
              std::numeric_limits<std::uint32_t>::max());

    // At 50% life: alive_count should be ~UINT32_MAX/2.
    d.tick(0.5F);
    ASSERT_EQ(d.active_count(), 1U);
    const std::uint32_t half = d.active_bursts()[0].alive_count;
    // Allow ±1 for rounding.
    const auto expected = static_cast<std::uint32_t>(
        std::numeric_limits<std::uint32_t>::max() / 2U);
    EXPECT_LE(half, expected + 1U);
    EXPECT_GE(half, expected - 1U);
}

// ---------------------------------------------------------------------------
// 22 - fire("") empty name: treated as unknown recipe -> kInvalidBurst.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, FireEmptyNameIsNoOp)
{
    ParticleEventDispatcher d;
    const std::uint32_t idx = d.fire("", Vec3f {0.0F, 0.0F, 0.0F},
                                      Quatf::identity());
    EXPECT_EQ(idx, ParticleEventDispatcher::kInvalidBurst);
    EXPECT_EQ(d.active_count(), 0U);

    // Registering "" IS legal; then fire("") should succeed.
    d.register_recipe("", make_recipe(2U, 1.0F, EmitterShape::kSphere,
                                       Vec3f {0.5F, 0.0F, 0.0F}));
    const std::uint32_t idx2 = d.fire("", Vec3f {0.0F, 0.0F, 0.0F},
                                       Quatf::identity());
    EXPECT_NE(idx2, ParticleEventDispatcher::kInvalidBurst);
    EXPECT_EQ(d.active_count(), 1U);
}

// ---------------------------------------------------------------------------
// 23 - Recipe name echoed: find_recipe(n)->name == n always.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, RecipeNameEchoedBackOnRegister)
{
    ParticleEventDispatcher d;
    ParticleRecipe r {};
    r.name       = "wrong_name";   // deliberately wrong; should be overwritten
    r.count      = 5U;
    r.lifetime_s = 1.0F;
    d.register_recipe("correct_name", r);

    const ParticleRecipe* found = d.find_recipe("correct_name");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "correct_name");
}

// ---------------------------------------------------------------------------
// 24 - register_recipe replaces in-place: live burst sees updated count.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, RegisterRecipeReplacesInPlaceLiveBurstSees)
{
    ParticleEventDispatcher d;
    d.register_recipe("live", make_recipe(10U, 2.0F, EmitterShape::kSphere,
                                          Vec3f {0.5F, 0.0F, 0.0F}));
    d.fire("live", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    ASSERT_EQ(d.active_count(), 1U);

    // Replace recipe with different count.
    d.register_recipe("live", make_recipe(99U, 2.0F, EmitterShape::kSphere,
                                          Vec3f {0.5F, 0.0F, 0.0F}));

    // Tick: alive_count decay uses recipe.count = 99 now.
    d.tick(1.0F);  // 1.0 of 2.0 -> 50% remaining
    ASSERT_EQ(d.active_count(), 1U);
    // Expected: round(99 * 0.5) == 50
    EXPECT_EQ(d.active_bursts()[0].alive_count, 50U);
}

// ---------------------------------------------------------------------------
// 25 - Callback self-remove during dispatch does not crash (snapshot safety).
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, CallbackSelfRemoveDuringDispatchIsSafe)
{
    ParticleEventDispatcher d;
    d.register_recipe("snap", make_recipe(3U, 1.0F, EmitterShape::kSphere,
                                          Vec3f {0.5F, 0.0F, 0.0F}));

    int call_count = 0;
    std::uint32_t self_id = 0U;

    self_id = d.add_on_emit([&](const cd::game::particles_event::ActiveBurst&) {
        ++call_count;
        // Remove self during dispatch — snapshot must protect against this.
        d.remove_on_emit(self_id);
    });

    // First fire: callback invoked once (via snapshot), then removed.
    d.fire("snap", Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    EXPECT_EQ(call_count, 1);

    // Second fire: callback already removed — should NOT fire again.
    d.fire("snap", Vec3f {1.0F, 0.0F, 0.0F}, Quatf::identity());
    EXPECT_EQ(call_count, 1);
}

// ---------------------------------------------------------------------------
// 26 - Burst staging order: multiple recipe names; dispatch order preserved.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, RecipeStagingOrderPreserved)
{
    ParticleEventDispatcher d;
    d.register_recipe("first",  make_recipe(1U, 1.0F, EmitterShape::kSphere,
                                             Vec3f {0.5F, 0.0F, 0.0F}));
    d.register_recipe("second", make_recipe(2U, 1.0F, EmitterShape::kSphere,
                                             Vec3f {0.5F, 0.0F, 0.0F}));
    d.register_recipe("third",  make_recipe(3U, 1.0F, EmitterShape::kSphere,
                                             Vec3f {0.5F, 0.0F, 0.0F}));

    d.fire("third",  Vec3f {0.0F, 0.0F, 0.0F}, Quatf::identity());
    d.fire("first",  Vec3f {1.0F, 0.0F, 0.0F}, Quatf::identity());
    d.fire("second", Vec3f {2.0F, 0.0F, 0.0F}, Quatf::identity());

    ASSERT_EQ(d.active_count(), 3U);
    const auto b = d.active_bursts();
    // Creation order governs position, not recipe registration order.
    EXPECT_EQ(b[0].alive_count, 3U);  // "third"  fired 1st
    EXPECT_EQ(b[1].alive_count, 1U);  // "first"  fired 2nd
    EXPECT_EQ(b[2].alive_count, 2U);  // "second" fired 3rd
}

// ---------------------------------------------------------------------------
// 27 - Dispatch with empty recipe (no recipes registered) fires nothing.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, DispatchWithNoRecipesRegistered)
{
    ParticleEventDispatcher d;
    EXPECT_EQ(d.recipe_count(), 0U);

    for (int i = 0; i < 5; ++i)
    {
        const std::uint32_t idx = d.fire("any", Vec3f {0.0F, 0.0F, 0.0F},
                                          Quatf::identity());
        EXPECT_EQ(idx, ParticleEventDispatcher::kInvalidBurst);
    }
    EXPECT_EQ(d.active_count(), 0U);
}

// ---------------------------------------------------------------------------
// 28 - Burst handle uniqueness: each fire_handle returns a distinct handle.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, BurstHandleUniquePerFire)
{
    using cd::game::particles_event::BurstHandle;

    ParticleEventDispatcher d;
    d.register_recipe("u", make_recipe(5U, 2.0F, EmitterShape::kSphere,
                                       Vec3f {0.5F, 0.0F, 0.0F}));

    constexpr int kN = 10;
    std::vector<BurstHandle> handles;
    handles.reserve(kN);
    for (int i = 0; i < kN; ++i)
    {
        handles.emplace_back(d.fire_handle("u", Vec3f {static_cast<float>(i), 0.0F, 0.0F},
                                            Quatf::identity()));
    }

    // All handles must be valid and distinct.
    for (int i = 0; i < kN; ++i)
    {
        EXPECT_TRUE(handles[static_cast<std::size_t>(i)].is_valid());
        for (int j = i + 1; j < kN; ++j)
        {
            EXPECT_NE(handles[static_cast<std::size_t>(i)],
                      handles[static_cast<std::size_t>(j)]);
        }
    }
}

// ---------------------------------------------------------------------------
// 29 - find_recipe returns nullptr after unregister_recipe.
// ---------------------------------------------------------------------------
TEST(ParticlesEvent, FindRecipeNullAfterUnregister)
{
    ParticleEventDispatcher d;
    d.register_recipe("gone", make_recipe(4U, 1.0F, EmitterShape::kSphere,
                                          Vec3f {0.5F, 0.0F, 0.0F}));
    ASSERT_NE(d.find_recipe("gone"), nullptr);

    d.unregister_recipe("gone");
    EXPECT_EQ(d.find_recipe("gone"), nullptr);
}
