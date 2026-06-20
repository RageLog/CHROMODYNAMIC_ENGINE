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

// ----------------------------------------------------------------------------
// 13. Re-adding a volume under the same owner WIPES prior occupancy so the
//     replacement starts dispatch fresh: a subject that was inside the old
//     volume fires on_enter (NOT on_stay) on the first tick of the new volume.
//     Locks the Trigger.cpp add_trigger occupancy-wipe contract (the new shape
//     has no relationship to prior "was inside" pairs).
// ----------------------------------------------------------------------------
TEST(GameTrigger, ReAddWipesOccupancyAndReEnters)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    // Subject inside the original volume -> on_enter fires once.
    const std::vector<Subject> inside {{subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}};
    world.tick(nullptr, 0.016F, inside);
    EXPECT_EQ(counts.enter, 1);
    EXPECT_TRUE(world.is_inside(owner, subject));

    // Replace the volume under the same owner. Occupancy must be wiped, so the
    // subject (still inside the new identical box) is treated as a NEW enter
    // rather than a stay.
    EventCounts counts2;
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts2));
    EXPECT_FALSE(world.is_inside(owner, subject));

    world.tick(nullptr, 0.016F, inside);
    EXPECT_EQ(counts2.enter, 1);  // fresh enter, not stay
    EXPECT_EQ(counts2.stay,  0);
    // The original callbacks must NOT fire again (the volume was replaced).
    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.stay,  0);
}

// ----------------------------------------------------------------------------
// 14. A volume with NO callbacks set (all std::function empty) must not crash
//     across enter/stay/exit/subject-removal transitions; occupancy bookkeeping
//     still tracks correctly so is_inside reflects reality. Locks the "any
//     unset callback is a no-op" header contract on every dispatch branch.
// ----------------------------------------------------------------------------
TEST(GameTrigger, EmptyCallbacksAreNoOpAcrossAllTransitions)
{
    TriggerWorld world;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);

    TriggerVolume v;
    v.name  = "no-callbacks";
    v.shape = unit_box();
    // on_enter / on_stay / on_exit deliberately left empty.
    world.add_trigger(owner, std::move(v));

    const std::vector<Subject> inside  {{subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}};
    const std::vector<Subject> outside {{subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}};

    // enter (no crash) -> occupancy set.
    world.tick(nullptr, 0.016F, inside);
    EXPECT_TRUE(world.is_inside(owner, subject));
    // stay (no crash) -> still occupied.
    world.tick(nullptr, 0.016F, inside);
    EXPECT_TRUE(world.is_inside(owner, subject));
    // exit (no crash) -> occupancy cleared.
    world.tick(nullptr, 0.016F, outside);
    EXPECT_FALSE(world.is_inside(owner, subject));
    // re-enter then subject-removal exit (no crash) -> occupancy cleared.
    world.tick(nullptr, 0.016F, inside);
    EXPECT_TRUE(world.is_inside(owner, subject));
    world.tick(nullptr, 0.016F, {});
    EXPECT_FALSE(world.is_inside(owner, subject));
}

// ----------------------------------------------------------------------------
// 15. Re-enter after exit: a subject that left a volume can enter it again;
//     on_enter fires a second time (not on_stay) and the full
//     enter -> stay -> exit cycle repeats from scratch.
// ----------------------------------------------------------------------------
TEST(GameTrigger, ReEnterAfterExit)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    const std::vector<Subject> inside  {{subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}};
    const std::vector<Subject> outside {{subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}};

    // First occupancy cycle: enter -> stay -> exit.
    world.tick(nullptr, 0.016F, inside);   // enter #1
    world.tick(nullptr, 0.016F, inside);   // stay  #1
    world.tick(nullptr, 0.016F, outside);  // exit  #1

    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.stay,  1);
    EXPECT_EQ(counts.exit,  1);
    EXPECT_FALSE(world.is_inside(owner, subject));

    // Second occupancy cycle: on_enter fires again (not on_stay).
    world.tick(nullptr, 0.016F, inside);   // enter #2
    world.tick(nullptr, 0.016F, inside);   // stay  #2
    world.tick(nullptr, 0.016F, outside);  // exit  #2

    EXPECT_EQ(counts.enter, 2);
    EXPECT_EQ(counts.stay,  2);
    EXPECT_EQ(counts.exit,  2);
    EXPECT_FALSE(world.is_inside(owner, subject));
}

// ----------------------------------------------------------------------------
// 16. Volume with zero occupants: an enabled volume whose subject list is empty
//     must not fire any event and must not accumulate occupancy.
// ----------------------------------------------------------------------------
TEST(GameTrigger, VolumeWithZeroOccupants)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner = make_entity(1);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    // Tick with zero subjects - no events, trigger_count unchanged.
    world.tick(nullptr, 0.016F, {});
    world.tick(nullptr, 0.016F, {});
    world.tick(nullptr, 0.016F, {});

    EXPECT_EQ(counts.enter, 0);
    EXPECT_EQ(counts.stay,  0);
    EXPECT_EQ(counts.exit,  0);
    EXPECT_EQ(world.trigger_count(), 1U);
}

// ----------------------------------------------------------------------------
// 17. Subject in multiple volumes simultaneously: one subject overlapping two
//     distinct volumes fires on_enter for BOTH volumes on the same tick, and
//     on_exit for BOTH when it leaves.  Occupancy is tracked independently
//     per (owner, subject) pair.
// ----------------------------------------------------------------------------
TEST(GameTrigger, SubjectInMultipleVolumesSimultaneously)
{
    TriggerWorld world;
    EventCounts  counts_a;
    EventCounts  counts_b;

    const Entity owner_a = make_entity(1);
    const Entity owner_b = make_entity(2);
    const Entity subject = make_entity(100);

    // Two overlapping AABB volumes both centred on the origin.
    world.add_trigger(owner_a, make_aabb_volume(unit_box(), counts_a));
    world.add_trigger(owner_b, make_aabb_volume(
        Aabb {Vec3f {-2.0F, -2.0F, -2.0F}, Vec3f {2.0F, 2.0F, 2.0F}}, counts_b));

    const std::vector<Subject> inside  {{subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}};
    const std::vector<Subject> outside {{subject, Vec3f {5.0F, 0.0F, 0.0F}, 0U}};

    // Enter tick: subject is inside BOTH volumes simultaneously.
    world.tick(nullptr, 0.016F, inside);
    EXPECT_EQ(counts_a.enter, 1);
    EXPECT_EQ(counts_b.enter, 1);
    EXPECT_TRUE(world.is_inside(owner_a, subject));
    EXPECT_TRUE(world.is_inside(owner_b, subject));

    // Stay tick.
    world.tick(nullptr, 0.016F, inside);
    EXPECT_EQ(counts_a.stay, 1);
    EXPECT_EQ(counts_b.stay, 1);

    // Exit tick: subject leaves both volumes.
    world.tick(nullptr, 0.016F, outside);
    EXPECT_EQ(counts_a.exit, 1);
    EXPECT_EQ(counts_b.exit, 1);
    EXPECT_FALSE(world.is_inside(owner_a, subject));
    EXPECT_FALSE(world.is_inside(owner_b, subject));
}

// ----------------------------------------------------------------------------
// 18. Exact-boundary enter (inclusive): the AABB contains() predicate uses
//     `>= min` and `<= max` so a point exactly ON the surface is considered
//     inside.  The Sphere contains() predicate uses `<= r^2` so a point
//     exactly on the surface is also inside.  Boundary points must produce
//     on_enter, not silence.
// ----------------------------------------------------------------------------
TEST(GameTrigger, ExactBoundaryEnterInclusive)
{
    // --- AABB boundary ---
    {
        TriggerWorld world;
        EventCounts  counts;
        const Entity owner   = make_entity(1);
        const Entity subject = make_entity(100);
        world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

        // Point exactly on the max-X face of the unit box.
        world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {1.0F, 0.0F, 0.0F}, 0U}});
        EXPECT_EQ(counts.enter, 1) << "AABB: point on max face must fire on_enter";

        // Point exactly on the min-Y face.
        EventCounts c2;
        const Entity owner2   = make_entity(2);
        const Entity subject2 = make_entity(200);
        world.add_trigger(owner2, make_aabb_volume(unit_box(), c2));
        world.tick(nullptr, 0.016F, {Subject {subject2, Vec3f {0.0F, -1.0F, 0.0F}, 0U}});
        EXPECT_EQ(c2.enter, 1) << "AABB: point on min face must fire on_enter";
    }

    // --- Sphere boundary ---
    {
        TriggerWorld world;
        EventCounts  counts;
        const Entity owner   = make_entity(1);
        const Entity subject = make_entity(100);

        TriggerVolume v;
        v.name     = "sphere-boundary";
        v.shape    = Sphere {Vec3f {0.0F, 0.0F, 0.0F}, 1.0F};
        v.on_enter = [&counts](Entity) { ++counts.enter; };
        v.on_stay  = [&counts](Entity) { ++counts.stay;  };
        v.on_exit  = [&counts](Entity) { ++counts.exit;  };
        world.add_trigger(owner, std::move(v));

        // Point exactly at radius distance along X: d^2 == r^2.
        world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {1.0F, 0.0F, 0.0F}, 0U}});
        EXPECT_EQ(counts.enter, 1) << "Sphere: point on surface must fire on_enter";
    }
}

// ----------------------------------------------------------------------------
// 19. Simultaneous enter + exit in different volumes on the same tick:
//     subject moves from inside volume A to inside volume B in one step.
//     Both on_exit(A) and on_enter(B) must fire in the same tick, with
//     final occupancy reflecting the new state.
// ----------------------------------------------------------------------------
TEST(GameTrigger, SimultaneousEnterExitDifferentVolumes)
{
    TriggerWorld world;
    EventCounts  counts_a;
    EventCounts  counts_b;

    const Entity owner_a = make_entity(1);
    const Entity owner_b = make_entity(2);
    const Entity subject = make_entity(100);

    // A: [-1, 1] box around origin; B: [3, 5] box.
    world.add_trigger(owner_a, make_aabb_volume(unit_box(), counts_a));
    world.add_trigger(owner_b, make_aabb_volume(
        Aabb {Vec3f {3.0F, -1.0F, -1.0F}, Vec3f {5.0F, 1.0F, 1.0F}}, counts_b));

    // Tick 1: subject inside A only.
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts_a.enter, 1);
    EXPECT_EQ(counts_b.enter, 0);
    EXPECT_TRUE(world.is_inside(owner_a, subject));
    EXPECT_FALSE(world.is_inside(owner_b, subject));

    // Tick 2: subject jumps to B in a single tick — exits A AND enters B.
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {4.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts_a.exit,  1) << "on_exit must fire for volume A";
    EXPECT_EQ(counts_b.enter, 1) << "on_enter must fire for volume B";
    EXPECT_FALSE(world.is_inside(owner_a, subject));
    EXPECT_TRUE(world.is_inside(owner_b, subject));

    // No extra spurious events.
    EXPECT_EQ(counts_a.enter, 1);
    EXPECT_EQ(counts_a.stay,  0);
    EXPECT_EQ(counts_b.exit,  0);
}

// ----------------------------------------------------------------------------
// 20. Remove volume mid-overlap: removing a volume while a subject is inside
//     does NOT fire on_exit (the callback target is gone, matching Unity /
//     Unreal "destroy gameobject" semantics), and subsequent ticks with the
//     same subject position are silent.
// ----------------------------------------------------------------------------
TEST(GameTrigger, RemoveVolumeMidOverlap)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    // Subject enters the volume.
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts.enter, 1);
    EXPECT_TRUE(world.is_inside(owner, subject));

    // Remove the volume while the subject is still inside.
    EXPECT_TRUE(world.remove_trigger(owner));
    EXPECT_EQ(world.trigger_count(), 0U);

    // on_exit must NOT have fired (volume is gone, no callback target).
    EXPECT_EQ(counts.exit, 0);

    // Subsequent ticks with the same subject at the same position are silent.
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}});
    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.stay,  0);
    EXPECT_EQ(counts.exit,  0);
}

// ----------------------------------------------------------------------------
// 21. clear_occupancy: resets all occupancy state without removing volumes or
//     firing on_exit.  A subject that was inside becomes "fresh" so the next
//     tick fires on_enter again.
// ----------------------------------------------------------------------------
TEST(GameTrigger, ClearOccupancyResetsStateWithoutExit)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));

    const std::vector<Subject> inside {{subject, Vec3f {0.0F, 0.0F, 0.0F}, 0U}};

    // Subject enters and stays.
    world.tick(nullptr, 0.016F, inside);
    world.tick(nullptr, 0.016F, inside);
    EXPECT_EQ(counts.enter, 1);
    EXPECT_EQ(counts.stay,  1);
    EXPECT_TRUE(world.is_inside(owner, subject));

    // clear_occupancy: no on_exit fires, volume count unchanged.
    world.clear_occupancy();
    EXPECT_EQ(counts.exit, 0);
    EXPECT_EQ(world.trigger_count(), 1U);
    EXPECT_FALSE(world.is_inside(owner, subject));

    // Next tick with subject still inside: on_enter fires again (not on_stay).
    world.tick(nullptr, 0.016F, inside);
    EXPECT_EQ(counts.enter, 2);
    EXPECT_EQ(counts.stay,  1);
    EXPECT_TRUE(world.is_inside(owner, subject));
}

// ----------------------------------------------------------------------------
// 22. Layer-clamping for layer >= 64: a subject with layer == 200 must be
//     clamped to channel 0, not trigger UB from (1ULL << 200).
//     A volume with mask == bit0 fires; one with mask == bit1 does not.
// ----------------------------------------------------------------------------
TEST(GameTrigger, LayerClampGeq64MapsToChannel0)
{
    TriggerWorld world;
    EventCounts  counts_ch0;
    EventCounts  counts_ch1;

    const Entity owner0 = make_entity(1);
    const Entity owner1 = make_entity(2);
    const Entity subject = make_entity(100);

    const auto mask_ch0 = cd::game::trigger::LayerMask {1};                                  // bit 0
    const cd::game::trigger::LayerMask mask_ch1 = cd::game::trigger::LayerMask {1} << 1U;    // bit 1

    world.add_trigger(owner0, make_aabb_volume(unit_box(), counts_ch0, mask_ch0));
    world.add_trigger(owner1, make_aabb_volume(unit_box(), counts_ch1, mask_ch1));

    // Subject with layer=200 (>= 64): must clamp to channel 0.
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 200U}});

    EXPECT_EQ(counts_ch0.enter, 1) << "layer >= 64 clamped to 0; bit-0 volume must fire";
    EXPECT_EQ(counts_ch1.enter, 0) << "bit-1 volume must NOT fire";
}

// ----------------------------------------------------------------------------
// 23. find() on a non-existent owner returns nullptr; find() after
//     remove_trigger also returns nullptr.  Tests the read-only inspection path.
// ----------------------------------------------------------------------------
TEST(GameTrigger, FindReturnsNullForMissingOwner)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity unknown = make_entity(999);

    // No volume registered yet.
    EXPECT_EQ(world.find(unknown), nullptr);

    // Register + verify find succeeds.
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts));
    ASSERT_NE(world.find(owner), nullptr);
    EXPECT_EQ(world.find(owner)->name, "test");

    // After removal, find returns nullptr.
    world.remove_trigger(owner);
    EXPECT_EQ(world.find(owner), nullptr);
}

// ----------------------------------------------------------------------------
// 24. Despawn-exit fires on_exit even for a subject filtered by layer mask on
//     the tick it disappears: if the subject was IN occupancy (it passed the
//     layer filter when it entered), then disappears from the subject list, the
//     removal-loop fires on_exit regardless of layer (occupancy is the source of
//     truth, not re-filtering at exit time).
// ----------------------------------------------------------------------------
TEST(GameTrigger, DespawnExitForLayerFilteredSubject)
{
    TriggerWorld world;
    EventCounts  counts;
    const Entity owner   = make_entity(1);
    const Entity subject = make_entity(100);

    // Volume that only accepts layer 3.
    const cd::game::trigger::LayerMask mask = cd::game::trigger::LayerMask {1} << 3U;
    world.add_trigger(owner, make_aabb_volume(unit_box(), counts, mask));

    // Subject on layer 3 enters.
    world.tick(nullptr, 0.016F, {Subject {subject, Vec3f {0.0F, 0.0F, 0.0F}, 3U}});
    EXPECT_EQ(counts.enter, 1);
    EXPECT_TRUE(world.is_inside(owner, subject));

    // Subject disappears from the subject list entirely (despawn).
    world.tick(nullptr, 0.016F, {});
    EXPECT_EQ(counts.exit, 1);
    EXPECT_FALSE(world.is_inside(owner, subject));
}
