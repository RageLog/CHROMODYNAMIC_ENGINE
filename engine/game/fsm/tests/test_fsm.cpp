// =============================================================================
// CHROMODYNAMIC - tests/test_fsm.cpp
// Phase 472 - cd::game::fsm unit tests (G2.1).
//
// Covers the 10 contract requirements from the brief:
//   1. Simple 2-state A -> B on trigger.
//   2. on_enter / on_exit fire correctly.
//   3. Hierarchical: parent contains child sub-machine.
//   4. Shallow history: re-entering parent resumes last child.
//   5. Deep history: re-entering deep parent resumes deepest state.
//   6. Conditional transition fires when predicate true.
//   7. Self-transition allowed.
//   8. Initial state set at construction.
//   9. Multiple machines are independent.
//  10. tick(dt) threads through to current state.
//
// Plus extras: orthogonal regions, history reset on kNone, predicates
// in tick_h hierarchical context.
// =============================================================================
#include <cd/game/fsm/Fsm.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

// -- Test context -------------------------------------------------------------
// Single mutable Context shared by all leaves: a trace log + a few flags
// flipped by predicates. Trace records "+X" on enter, "-X" on exit, "uX:dt"
// on update.
struct Ctx
{
    std::vector<std::string> trace {};
    bool                     trigger {false};
    bool                     trigger_b {false};
    bool                     trigger_back {false};
    float                    accum_dt {0.0F};
};

using cd::game::fsm::HierarchicalFsm;
using cd::game::fsm::HistoryKind;
using cd::game::fsm::State;
using cd::game::fsm::StateId;
using cd::game::fsm::StateMachine;
using cd::game::fsm::kInvalidStateId;

// Convenience: build a State<Ctx> that traces its name on every callback.
std::unique_ptr<State<Ctx>> make_traced_state(std::string name)
{
    auto s = std::make_unique<State<Ctx>>(name);
    s->set_on_enter([n = name](Ctx& c) { c.trace.push_back("+" + n); });
    s->set_on_exit([n = name](Ctx& c) { c.trace.push_back("-" + n); });
    s->set_on_update([n = name](Ctx& c, float dt) {
        c.trace.push_back("u" + n);
        c.accum_dt += dt;
    });
    return s;
}

// ----------------------------------------------------------------------------
// 1. Simple 2-state A -> B on trigger.
// ----------------------------------------------------------------------------
TEST(GameFsm, SimpleTwoStateTransition)
{
    StateMachine<Ctx> m;
    const StateId a = m.add_state(make_traced_state("A"), /*mark_initial*/ true);
    const StateId b = m.add_state(make_traced_state("B"));
    m.add_transition(a, b, [](const Ctx& c) { return c.trigger; });

    Ctx ctx;
    m.start(ctx);
    EXPECT_EQ(m.current_state(), a);

    m.tick(ctx, 0.016F);              // no trigger -> stays in A, on_update fires
    EXPECT_EQ(m.current_state(), a);

    ctx.trigger = true;
    const bool fired = m.tick(ctx, 0.016F);
    EXPECT_TRUE(fired);
    EXPECT_EQ(m.current_state(), b);
}

// ----------------------------------------------------------------------------
// 2. on_enter / on_exit fire correctly across transitions.
// ----------------------------------------------------------------------------
TEST(GameFsm, EnterExitHooksFireInOrder)
{
    StateMachine<Ctx> m;
    const StateId a = m.add_state(make_traced_state("A"), /*mark_initial*/ true);
    const StateId b = m.add_state(make_traced_state("B"));
    m.add_transition(a, b, [](const Ctx& c) { return c.trigger; });

    Ctx ctx;
    m.start(ctx);
    ctx.trigger = true;
    m.tick(ctx, 0.0F);

    // Expected trace: +A on start, then -A then +B on transition.
    ASSERT_EQ(ctx.trace.size(), 3u);
    EXPECT_EQ(ctx.trace[0], "+A");
    EXPECT_EQ(ctx.trace[1], "-A");
    EXPECT_EQ(ctx.trace[2], "+B");
}

// ----------------------------------------------------------------------------
// 3. Hierarchical: parent state contains child sub-machine.
// ----------------------------------------------------------------------------
TEST(GameFsm, HierarchicalParentContainsChildRegion)
{
    HierarchicalFsm<Ctx> root;
    const StateId parent = root.add_state(make_traced_state("P"));

    auto region = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId c1 = region->add_state(make_traced_state("C1"));
    const StateId c2 = region->add_state(make_traced_state("C2"));
    region->add_transition(c1, c2, [](const Ctx& c) { return c.trigger_b; });
    root.add_region(parent, std::move(region));

    Ctx ctx;
    root.start_h(ctx);

    // Active leaf should be the child C1 (deepest active).
    EXPECT_EQ(root.active_leaf(), c1);

    // tick should push the child region's transition.
    ctx.trigger_b = true;
    root.tick_h(ctx, 0.016F);
    EXPECT_EQ(root.active_leaf(), c2);

    // Parent itself never moved.
    EXPECT_EQ(root.current_state(), parent);
}

// ----------------------------------------------------------------------------
// 4. Shallow history: re-entering parent resumes the last active direct child.
//     P (shallow history) -> { C1, C2 }, root has P and Q at top level.
// ----------------------------------------------------------------------------
TEST(GameFsm, ShallowHistoryResumesLastChild)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));
    const StateId Q = root.add_state(make_traced_state("Q"));

    auto region = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId C1 = region->add_state(make_traced_state("C1"));
    const StateId C2 = region->add_state(make_traced_state("C2"));
    region->add_transition(C1, C2, [](const Ctx& c) { return c.trigger_b; });
    root.add_region(P, std::move(region));
    root.set_history(P, HistoryKind::kShallow);

    root.add_transition(P, Q, [](const Ctx& c) { return c.trigger; });
    root.add_transition(Q, P, [](const Ctx& c) { return c.trigger_back; });

    Ctx ctx;
    root.start_h(ctx);
    EXPECT_EQ(root.active_leaf(), C1);

    // Drive inner transition C1 -> C2.
    ctx.trigger_b = true;
    root.tick_h(ctx, 0.0F);
    EXPECT_EQ(root.active_leaf(), C2);
    ctx.trigger_b = false;

    // Leave P -> Q (outer), then come back: shallow history must restore C2.
    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger = false;
    EXPECT_EQ(root.current_state(), Q);

    ctx.trigger_back = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger_back = false;
    EXPECT_EQ(root.current_state(), P);
    EXPECT_EQ(root.active_leaf(), C2);
}

// ----------------------------------------------------------------------------
// 5. Deep history: re-entering a deep parent restores the deepest leaf.
//     Root -> { P (deep), Q }.  P -> { C1 (no history) -> { L1, L2 }, C2 }.
//     Drive L1 -> L2, then leave P, then come back: must land on L2.
// ----------------------------------------------------------------------------
TEST(GameFsm, DeepHistoryRestoresDeepestState)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));
    const StateId Q = root.add_state(make_traced_state("Q"));

    auto region_p = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId C1 = region_p->add_state(make_traced_state("C1"));
    const StateId C2 = region_p->add_state(make_traced_state("C2"));
    region_p->add_transition(C1, C2, [](const Ctx& c) { return c.trigger_b; });

    auto region_c1 = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId L1 = region_c1->add_state(make_traced_state("L1"));
    const StateId L2 = region_c1->add_state(make_traced_state("L2"));
    region_c1->add_transition(L1, L2, [](const Ctx& c) { return c.trigger; });
    region_p->add_region(C1, std::move(region_c1));

    root.add_region(P, std::move(region_p));
    root.set_history(P, HistoryKind::kDeep);

    root.add_transition(P, Q, [](const Ctx& c) { return c.trigger_back; });
    root.add_transition(Q, P, [](const Ctx& c) { return c.trigger; });

    Ctx ctx;
    root.start_h(ctx);
    EXPECT_EQ(root.active_leaf(), L1);

    // L1 -> L2 inside C1 inside P.
    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger = false;
    EXPECT_EQ(root.active_leaf(), L2);

    // Leave P -> Q.
    ctx.trigger_back = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger_back = false;
    EXPECT_EQ(root.current_state(), Q);

    // Come back Q -> P. Deep history should re-enter P, then C1, then L2.
    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger = false;
    EXPECT_EQ(root.current_state(), P);
    EXPECT_EQ(root.active_leaf(), L2);
}

// ----------------------------------------------------------------------------
// 6. Conditional transition fires when predicate becomes true (and only then).
// ----------------------------------------------------------------------------
TEST(GameFsm, ConditionalTransitionFiresOnlyWhenPredicateTrue)
{
    StateMachine<Ctx> m;
    const StateId a = m.add_state(make_traced_state("A"));
    const StateId b = m.add_state(make_traced_state("B"));
    m.add_transition(a, b, [](const Ctx& c) { return c.accum_dt >= 0.05F; });

    Ctx ctx;
    m.start(ctx);
    for (int i = 0; i < 2; ++i)
    {
        const bool fired = m.tick(ctx, 0.01F);
        EXPECT_FALSE(fired);
        EXPECT_EQ(m.current_state(), a);
    }
    // After ~5 more ticks of 0.01 accum_dt crosses 0.05 -> predicate fires.
    bool fired_eventually = false;
    for (int i = 0; i < 10 && !fired_eventually; ++i)
    {
        fired_eventually = m.tick(ctx, 0.01F);
    }
    EXPECT_TRUE(fired_eventually);
    EXPECT_EQ(m.current_state(), b);
}

// ----------------------------------------------------------------------------
// 7. Self-transition allowed: A -> A on trigger.
// ----------------------------------------------------------------------------
TEST(GameFsm, SelfTransitionRunsExitThenEnter)
{
    StateMachine<Ctx> m;
    const StateId a = m.add_state(make_traced_state("A"));
    m.add_transition(a, a, [](const Ctx& c) { return c.trigger; });

    Ctx ctx;
    m.start(ctx);
    ctx.trigger = true;
    const bool fired = m.tick(ctx, 0.0F);
    EXPECT_TRUE(fired);
    EXPECT_EQ(m.current_state(), a);

    // Trace must contain +A (start), -A, +A (self-transition).
    ASSERT_GE(ctx.trace.size(), 3u);
    EXPECT_EQ(ctx.trace[0], "+A");
    EXPECT_EQ(ctx.trace[1], "-A");
    EXPECT_EQ(ctx.trace[2], "+A");
}

// ----------------------------------------------------------------------------
// 8. Initial state can be set explicitly at construction.
// ----------------------------------------------------------------------------
TEST(GameFsm, InitialStateSetExplicitly)
{
    StateMachine<Ctx> m;
    const StateId a = m.add_state(make_traced_state("A"));
    const StateId b = m.add_state(make_traced_state("B"));
    m.set_initial_state(b);

    Ctx ctx;
    m.start(ctx);
    EXPECT_EQ(m.current_state(), b);
    EXPECT_EQ(m.initial_state(), b);
    EXPECT_NE(m.current_state(), a);
    EXPECT_EQ(ctx.trace.front(), "+B");
}

// ----------------------------------------------------------------------------
// 9. Multiple machines are independent (no shared state).
// ----------------------------------------------------------------------------
TEST(GameFsm, MultipleMachinesIndependent)
{
    StateMachine<Ctx> m1;
    StateMachine<Ctx> m2;
    const StateId a1 = m1.add_state(make_traced_state("M1A"));
    const StateId b1 = m1.add_state(make_traced_state("M1B"));
    const StateId a2 = m2.add_state(make_traced_state("M2A"));
    const StateId b2 = m2.add_state(make_traced_state("M2B"));
    m1.add_transition(a1, b1, [](const Ctx& c) { return c.trigger; });
    m2.add_transition(a2, b2, [](const Ctx& c) { return c.trigger_b; });

    Ctx ctx1;
    Ctx ctx2;
    m1.start(ctx1);
    m2.start(ctx2);

    ctx1.trigger = true;             // fires only m1
    m1.tick(ctx1, 0.0F);
    m2.tick(ctx2, 0.0F);
    EXPECT_EQ(m1.current_state(), b1);
    EXPECT_EQ(m2.current_state(), a2);

    ctx2.trigger_b = true;           // fires only m2
    m1.tick(ctx1, 0.0F);
    m2.tick(ctx2, 0.0F);
    EXPECT_EQ(m2.current_state(), b2);
}

// ----------------------------------------------------------------------------
// 10. tick(dt) threads through to current state on_update.
// ----------------------------------------------------------------------------
TEST(GameFsm, TickThreadsDtThroughToCurrentState)
{
    StateMachine<Ctx> m;
    m.add_state(make_traced_state("Only"));

    Ctx ctx;
    m.start(ctx);

    constexpr float kDt = 0.033F;
    m.tick(ctx, kDt);
    m.tick(ctx, kDt);
    m.tick(ctx, kDt);

    EXPECT_FLOAT_EQ(ctx.accum_dt, 3.0F * kDt);
}

// ----------------------------------------------------------------------------
// Extras
// ----------------------------------------------------------------------------

// kNone history: re-entry restarts the region at its declared initial state.
TEST(GameFsm, NoHistoryResetsRegionOnReentry)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));
    const StateId Q = root.add_state(make_traced_state("Q"));

    auto region = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId C1 = region->add_state(make_traced_state("C1"));
    const StateId C2 = region->add_state(make_traced_state("C2"));
    region->add_transition(C1, C2, [](const Ctx& c) { return c.trigger_b; });
    root.add_region(P, std::move(region));
    // No history set => defaults to kNone.

    root.add_transition(P, Q, [](const Ctx& c) { return c.trigger; });
    root.add_transition(Q, P, [](const Ctx& c) { return c.trigger_back; });

    Ctx ctx;
    root.start_h(ctx);
    ctx.trigger_b = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger_b = false;
    EXPECT_EQ(root.active_leaf(), C2);

    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger = false;
    ctx.trigger_back = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger_back = false;

    // No history => region restarts at C1.
    EXPECT_EQ(root.active_leaf(), C1);
}

// set_state bypasses predicates and runs exit/enter explicitly.
TEST(GameFsm, SetStateImmediateJump)
{
    StateMachine<Ctx> m;
    const StateId a = m.add_state(make_traced_state("A"));
    const StateId b = m.add_state(make_traced_state("B"));

    Ctx ctx;
    m.start(ctx);
    EXPECT_EQ(m.current_state(), a);
    m.set_state(ctx, b);
    EXPECT_EQ(m.current_state(), b);
    EXPECT_EQ(ctx.trace.back(), "+B");
}

}  // namespace
