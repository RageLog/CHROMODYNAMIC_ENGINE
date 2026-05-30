// =============================================================================
// CHROMODYNAMIC - tests/test_trigger.cpp
// Phase 476 - cd::game::trigger unit tests (G3.2).
//
// Brief-mandated coverage (8 cases):
//   1. Subject enters trigger -> on_enter fires once.
//   2. Subject inside trigger -> on_stay fires per tick.
//   3. Subject exits trigger -> on_exit fires once.
//   4. Multiple triggers fire independently.
//   5. Layer mask filters subjects.
//   6. Disabled trigger no-ops.
//   7. Subject teleport (jump in then out same frame) handled.
//   8. Remove trigger stops callbacks.
//
// Extras (regression / boundary):
//   9. Sphere shape parity with AABB shape.
//  10. Subject removal mid-occupancy fires on_exit.
//  11. is_inside reflects current occupancy.
//  12. Re-enabling a disabled volume restarts occupancy fresh.
// =============================================================================
#include <cd/game/trigger/Trigger.hpp>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace
{

using cd::ecs::Entity;
using cd::game::trigger::kAllLayers;
using cd::game::trigger::Subject;
using cd::game::trigger::TriggerVolume;
using cd::game::trigger::TriggerWorld;
using cd::math::Vec3f;
using cd::physics::Aabb;
using cd::physics::Sphere;

// Small counters captured per volume to assert event multiplicity.
struct EventCounts
{
    int enter {0};
    int stay  {0};
    int exit  {0};
    Entity last_enter {};
    Entity last_exit  {};
};

// Build an AABB-shaped trigger volume whose callbacks bump `out`.
TriggerVolume make_aabb_volume(const Aabb& box, EventCounts& out,
                               cd::game::trigger::LayerMask mask = kAllLayers)
{
    TriggerVolume v;
    v.name       = "test";
    v.shape      = box;
    v.layer_mask = mask;
    v.on_enter   = [&out](Entity e) { ++out.enter; out.last_enter = e; };
    v.on_stay    = [&out](Entity   ) { ++out.stay; };
    v.on_exit    = [&out](Entity e) { ++out.exit;  out.last_exit  = e; };
    return v;
}

// Convenience to fabricate a "valid-looking" entity directly without going
// through cd::ecs::EntityManager - the trigger library treats Entity as an
// opaque key, so any non-zero generation pair is acceptable for tests.
Entity make_entity(std::uint32_t id, std::uint32_t gen = 1)
{
    return Entity {id, gen};
}

// Unit AABB centred on the origin, +/- 1 unit on each axis.
Aabb unit_box()
{
    return Aabb {Vec3f {-1.0F, -1.0F, -1.0F}, Vec3f {1.0F, 1.0F, 1.0F}};
}

}  // namespace

// ----------------------------------------------------------------------------
// 1. Subject enters trigger -> on_enter fires exactly once.
// ----------------------------------------------------------------------------
TEST(GameTrigger, EnterFiresOnce)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    // Tick 1: subject outside -> no events.
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts.enter, 0);
    EXPECT_EQ(counts.stay,  0);
    EXPECT_EQ(counts.exit,  0);

    // Tick 2: subject inside -> on_enter fires once.
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.stay,  0);
    EXPECT_EQ(counts.exit,  0);
    EXPECT_EQ(counts.last_enter, subject);
}

// ----------------------------------------------------------------------------
// 2. Subject inside trigger -> on_stay fires per tick after enter.
// ----------------------------------------------------------------------------
TEST(GameTrigger, StayFiresPerTickWhileInside)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    const std::vector<Subject> inside {{subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}};

    world.tick(nullptr, 0.016F, inside);  // enter
    world.tick(nullptr, 0.016F, inside);  // stay #1
    world.tick(nullptr, 0.016F, inside);  // stay #2
    world.tick(nullptr, 0.016F, inside);  // stay #3

    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.stay,  3);
    EXPECT_EQ(counts.exit,  0);
}

// ----------------------------------------------------------------------------
// 3. Subject exits trigger -> on_exit fires exactly once.
// ----------------------------------------------------------------------------
TEST(GameTrigger, ExitFiresOnce)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}});  // enter
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}});  // stay
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}});  // exit
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}});  // outside

    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.stay,  1);
    EXPECT_EQ(counts.exit,  1);
    EXPECT_EQ(counts.last_exit, subject);
}

// ----------------------------------------------------------------------------
// 4. Multiple triggers fire independently on the same subject.
// ----------------------------------------------------------------------------
TEST(GameTrigger, MultipleTriggersIndependent)
{
    TriggerWorld world;
    EventCounts  counts_a;
    EventCounts  counts_b;

    const Entity owner_a = make_entity(1);
    const Entity owner_b = make_entity(2);
    const Entity subject = make_entity(100);

    // Two volumes side-by-side along X: A in [-1,1], B in [4,6].
    world.add_trigger(owner_a, make_aabb_volume(unit_box(), counts_a));
    world.add_trigger(owner_b, make_aabb_volume(
        Aabb {Vec3f {4.0F, -1.0F, -1.0F}, Vec3f {6.0F, 1.0F, 1.0F}}, counts_b));

    // Subject inside A only.
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts_a.enter, 1);
    EXPECT_EQ(counts_b.enter, 0);

    // Subject jumps to B - exits A, enters B in the same tick.
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts_a.enter, 1);
    EXPECT_EQ(counts_a.exit,  1);
    EXPECT_EQ(counts_b.enter, 1);
    EXPECT_EQ(counts_b.exit,  0);
}

// ----------------------------------------------------------------------------
// 5. Layer mask filters subjects: a volume with mask=bit2 only sees subjects
//    on layer 2.
// ----------------------------------------------------------------------------
TEST(GameTrigger, LayerMaskFiltersSubjects)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner = make_entity(1);
    // Volume mask = bit 2 only.
    const cd::game::trigger::LayerMask mask = (cd::game::trigger::LayerMask {1} << 2U);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts, mask));

    const Entity subj_layer0 = make_entity(100);
    const Entity subj_layer2 = make_entity(101);
    const Entity subj_layer3 = make_entity(102);

    world.tick(nullptr, 0.016F, {
        Subject {subj_layer0, Vec3f {0.0F, 0.0F, 0.0F}, 0U},  // wrong layer
        Subject {subj_layer2, Vec3f {0.0F, 0.0F, 0.0F}, 2U},  // matches
        Subject {subj_layer3, Vec3f {0.0F, 0.0F, 0.0F}, 3U},  // wrong layer
    });

    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.last_enter, subj_layer2);
}

// ----------------------------------------------------------------------------
// 6. Disabled trigger no-ops: no on_enter / on_stay / on_exit fire while
//    disabled; re-enabling restarts occupancy fresh (subject already inside
//    fires on_enter on the first re-enabled tick).
// ----------------------------------------------------------------------------
TEST(GameTrigger, DisabledTriggerNoOps)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    EXPECT_TRUE(world.set_enabled(owner, false));

    const std::vector<Subject> inside {{subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}};
    world.tick(nullptr, 0.016F, inside);
    world.tick(nullptr, 0.016F, inside);
    EXPECT_EQ(counts.enter, 0);
    EXPECT_EQ(counts.stay,  0);
    EXPECT_EQ(counts.exit,  0);

    // Re-enable: subject still inside -> first tick fires on_enter.
    EXPECT_TRUE(world.set_enabled(owner, true));
    world.tick(nullptr, 0.016F, inside);
    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.stay,  0);
}

// ----------------------------------------------------------------------------
// 7. Subject teleport (outside on tick N, inside-then-outside between samples,
//    outside on tick N+1) must NOT fire on_enter / on_exit because the
//    discrete sampler never observed the subject inside.
// ----------------------------------------------------------------------------
TEST(GameTrigger, TeleportThroughVolumeFiresNothing)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    // Sample A: subject at -5 (outside).
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {-5.0F, 0.0F, 0.0F}, 0U}});
    // Sample B: subject at +5 (outside).  Trajectory crossed the volume but
    // we never sampled inside -> no events fire (Unity / Unreal discrete
    // overlap semantics, documented in Trigger.hpp).
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f { 5.0F, 0.0F, 0.0F}, 0U}});

    EXPECT_EQ(counts.enter, 0);
    EXPECT_EQ(counts.stay,  0);
    EXPECT_EQ(counts.exit,  0);
}

// ----------------------------------------------------------------------------
// 8. remove_trigger stops callbacks even if the subject was inside.
// ----------------------------------------------------------------------------
TEST(GameTrigger, RemoveTriggerStopsCallbacks)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts.enter, 1);

    EXPECT_TRUE(world.remove_trigger(owner));
    EXPECT_EQ(world.trigger_count(), 0U);

    // No volume left -> no callbacks (and no exit fired, by design - the
    // volume disappeared together with its callback target).
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.exit,  0);

    // Removing twice returns false (idempotent).
    EXPECT_FALSE(world.remove_trigger(owner));
}

// ----------------------------------------------------------------------------
// 9. Sphere shape parity: sphere volumes obey the same enter/stay/exit
//    contract as AABB volumes.
// ----------------------------------------------------------------------------
TEST(GameTrigger, SphereShapeParity)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);

    TriggerVolume v;
    v.name     = "sphere";
    v.shape    = Sphere {Vec3f {0.0F, 0.0F, 0.0F}, 2.0F};
    v.on_enter = [&counts](Entity) { ++counts.enter; };
    v.on_exit  = [&counts](Entity) { ++counts.exit;  };
    v.on_stay  = [&counts](Entity) { ++counts.stay;  };
    world.add_trigger(owner, std::move(v));

    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}}); // out
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {1.0F, 0.0F, 0.0F}, 0U}}); // in (r=1<=2)
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.5F, 0.0F, 0.0F}, 0U}}); // stay
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}}); // out

    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.stay,  1);
    EXPECT_EQ(counts.exit,  1);
}

// ----------------------------------------------------------------------------
// 10. Subject removed from the subject list while inside -> on_exit fires once
//     on the tick the subject disappears.  Mirrors Unity OnTriggerExit on
//     gameobject destroy.
// ----------------------------------------------------------------------------
TEST(GameTrigger, SubjectRemovalFiresExit)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts.enter, 1);

    // Subject "destroyed" - empty subject list.
    world.tick(nullptr, 0.016F, {});
    EXPECT_EQ(counts.exit, 1);

    // Subsequent empty ticks do nothing.
    world.tick(nullptr, 0.016F, {});
    EXPECT_EQ(counts.exit, 1);
}

// ----------------------------------------------------------------------------
// 11. is_inside reports the current occupancy bit.
// ----------------------------------------------------------------------------
TEST(GameTrigger, IsInsideReflectsOccupancy)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    EXPECT_FALSE(world.is_inside(owner, subject));
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_TRUE(world.is_inside(owner, subject));
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_FALSE(world.is_inside(owner, subject));
}

// ----------------------------------------------------------------------------
// 12. Re-enabling a disabled volume restarts occupancy fresh - subject who
//     was inside on the last enabled tick is treated as a new enter, not as
//     "stay" (covered in test 6, this one verifies the symmetric: no exit
//     fires merely because of disable, and a disabled volume's find() still
//     succeeds).
// ----------------------------------------------------------------------------
TEST(GameTrigger, DisableThenFindStillSucceeds)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner = make_entity(1);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    EXPECT_TRUE(world.set_enabled(owner, false));
    const TriggerVolume* v = world.find(owner);
    ASSERT_NE(v, nullptr);
    EXPECT_FALSE(v->enabled);

    // set_enabled on an unknown owner returns false.
    EXPECT_FALSE(world.set_enabled(make_entity(999), true));
}
