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

// ----------------------------------------------------------------------------
// 13. Guard-rejected transition: false predicate keeps machine in same state.
//     Tests both tick return value and state invariant.
// ----------------------------------------------------------------------------
TEST(GameFsm, GuardRejectedTransitionStaysInCurrentState)
{
    StateMachine<Ctx> m;
    const StateId a = m.add_state(make_traced_state("A"));
    const StateId b = m.add_state(make_traced_state("B"));
    m.add_transition(a, b, [](const Ctx& /*c*/) { return false; });  // never fires

    Ctx ctx;
    m.start(ctx);

    const bool fired = m.tick(ctx, 0.016F);
    EXPECT_FALSE(fired);
    EXPECT_EQ(m.current_state(), a);

    // Trace must contain +A (start) and uA (on_update) but no -A or +B.
    EXPECT_EQ(ctx.trace[0], "+A");
    EXPECT_EQ(ctx.trace[1], "uA");
    EXPECT_EQ(ctx.trace.size(), 2u);
}

// ----------------------------------------------------------------------------
// 14. Unhandled event (no transition for current state): tick is a no-op.
//     Machine stays in A; no crash, no spurious on_exit/on_enter.
// ----------------------------------------------------------------------------
TEST(GameFsm, UnhandledEventIsNoOp)
{
    StateMachine<Ctx> m;
    const StateId a = m.add_state(make_traced_state("A"));
    const StateId b = m.add_state(make_traced_state("B"));
    // Transition only from B, never from A.
    m.add_transition(b, a, [](const Ctx& c) { return c.trigger; });

    Ctx ctx;
    ctx.trigger = true;
    m.start(ctx);  // starts in A

    const bool fired = m.tick(ctx, 0.0F);
    EXPECT_FALSE(fired);
    EXPECT_EQ(m.current_state(), a);  // still A

    // No exit from A, no enter of B.
    for (const auto& entry : ctx.trace)
    {
        EXPECT_NE(entry, "-A");
        EXPECT_NE(entry, "+B");
    }
}

// ----------------------------------------------------------------------------
// 15. Outer transitions checked before inner region ticks.
//     When outer P->Q fires, the inner region's transition must NOT fire in
//     the same tick (tick_h returns true and skips region tick).
// ----------------------------------------------------------------------------
TEST(GameFsm, OuterTransitionPrecedesInnerRegionTick)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));
    const StateId Q = root.add_state(make_traced_state("Q"));

    auto region = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId C1 = region->add_state(make_traced_state("C1"));
    const StateId C2 = region->add_state(make_traced_state("C2"));
    // Inner transition fires when trigger_b.
    region->add_transition(C1, C2, [](const Ctx& c) { return c.trigger_b; });
    root.add_region(P, std::move(region));

    // Outer transition fires when trigger.
    root.add_transition(P, Q, [](const Ctx& c) { return c.trigger; });

    Ctx ctx;
    root.start_h(ctx);
    EXPECT_EQ(root.active_leaf(), C1);

    // Fire BOTH triggers simultaneously — outer must win.
    ctx.trigger   = true;
    ctx.trigger_b = true;
    const bool fired = root.tick_h(ctx, 0.0F);

    EXPECT_TRUE(fired);
    EXPECT_EQ(root.current_state(), Q);
    // Inner region was abandoned at C1, not at C2.
    // (C1 was current when snapshot was taken; C2 was never entered.)
    // After outer transition the inner region no longer exists under Q.
    EXPECT_EQ(root.active_leaf(), Q);
}

// ----------------------------------------------------------------------------
// 16. start() is idempotent: calling it twice must not re-fire on_enter.
// ----------------------------------------------------------------------------
TEST(GameFsm, StartIsIdempotent)
{
    StateMachine<Ctx> m;
    m.add_state(make_traced_state("A"));

    Ctx ctx;
    m.start(ctx);
    m.start(ctx);  // second call must be a no-op

    // Only one +A, not two.
    std::size_t enter_count = 0;
    for (const auto& e : ctx.trace)
    {
        if (e == "+A") { ++enter_count; }
    }
    EXPECT_EQ(enter_count, 1u);
    EXPECT_TRUE(m.started());
}

// ----------------------------------------------------------------------------
// 17. state_count() and state() accessor return consistent view.
// ----------------------------------------------------------------------------
TEST(GameFsm, StateAccessorsConsistent)
{
    StateMachine<Ctx> m;
    const StateId a = m.add_state(make_traced_state("Alpha"));
    const StateId b = m.add_state(make_traced_state("Beta"));

    EXPECT_EQ(m.state_count(), 2u);
    EXPECT_EQ(m.state(a).name(), "Alpha");
    EXPECT_EQ(m.state(b).name(), "Beta");
}

// ----------------------------------------------------------------------------
// 18. Shallow vs deep: 3-level hierarchy.
//     P (shallow) -> C1 -> L1/L2.
//     After driving L1->L2 and leaving+returning P, shallow must resume C1
//     but NOT L2 — C1 restarts at its initial (L1).
// ----------------------------------------------------------------------------
TEST(GameFsm, ShallowVsDeepThreeLevel)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));
    const StateId Q = root.add_state(make_traced_state("Q"));

    auto region_p = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId C1 = region_p->add_state(make_traced_state("C1"));
    const StateId C2 = region_p->add_state(make_traced_state("C2"));

    auto region_c1 = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId L1 = region_c1->add_state(make_traced_state("L1"));
    const StateId L2 = region_c1->add_state(make_traced_state("L2"));
    region_c1->add_transition(L1, L2, [](const Ctx& c) { return c.trigger; });
    region_p->add_region(C1, std::move(region_c1));

    region_p->add_transition(C1, C2, [](const Ctx& c) { return c.trigger_b; });
    root.add_region(P, std::move(region_p));

    // P uses SHALLOW — resumes last direct child of P (either C1 or C2),
    // but the child's own sub-region restarts from its initial.
    root.set_history(P, HistoryKind::kShallow);
    root.add_transition(P, Q, [](const Ctx& c) { return c.trigger_back; });
    root.add_transition(Q, P, [](const Ctx& c) { return c.trigger; });

    Ctx ctx;
    root.start_h(ctx);
    EXPECT_EQ(root.active_leaf(), L1);

    // Drive L1 -> L2 (inner-most).
    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger = false;
    EXPECT_EQ(root.active_leaf(), L2);

    // Leave P -> Q.
    ctx.trigger_back = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger_back = false;
    EXPECT_EQ(root.current_state(), Q);

    // Return Q -> P with SHALLOW history.
    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger = false;

    EXPECT_EQ(root.current_state(), P);
    // Shallow: P resumes C1 (last direct child) but C1 itself restarts at L1.
    EXPECT_EQ(root.active_leaf(), L1);
}

// ----------------------------------------------------------------------------
// 19. History of never-visited region falls back to initial state.
//     Set kShallow on P, but immediately leave before any inner transition.
//     On re-entry the region must start at its declared initial (C1), not
//     produce kInvalidStateId or crash.
// ----------------------------------------------------------------------------
TEST(GameFsm, HistoryOfNeverVisitedRegionFallsBackToInitial)
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
    // Do NOT drive C1->C2; leave immediately.
    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger = false;
    EXPECT_EQ(root.current_state(), Q);

    // Return — shallow history has a snapshot but last_direct == C1 (initial).
    ctx.trigger_back = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger_back = false;

    EXPECT_EQ(root.current_state(), P);
    EXPECT_EQ(root.active_leaf(), C1);  // fallback to initial, no crash
    EXPECT_NE(root.active_leaf(), cd::game::fsm::kInvalidStateId);
}

// ----------------------------------------------------------------------------
// 20. Self-transition in HierarchicalFsm: exit+re-enter the child region.
//     P has a region with C1; P self-transitions -> C1's on_exit + on_enter
//     must fire (region tears down and re-enters).
// ----------------------------------------------------------------------------
TEST(GameFsm, SelfTransitionInHierarchicalFsmReentersRegion)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));
    root.add_transition(P, P, [](const Ctx& c) { return c.trigger; });

    auto region = std::make_unique<HierarchicalFsm<Ctx>>();
    region->add_state(make_traced_state("C1"));
    root.add_region(P, std::move(region));

    Ctx ctx;
    root.start_h(ctx);
    ctx.trace.clear();  // clear boot trace

    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);

    // Expected: -C1 (region exit), -P (parent exit), +P (parent enter), +C1 (region enter).
    ASSERT_GE(ctx.trace.size(), 4u);
    EXPECT_EQ(ctx.trace[0], "-C1");
    EXPECT_EQ(ctx.trace[1], "-P");
    EXPECT_EQ(ctx.trace[2], "+P");
    EXPECT_EQ(ctx.trace[3], "+C1");
}

// ----------------------------------------------------------------------------
// 21. Null predicate transition is never fired (treated as always-false guard).
// ----------------------------------------------------------------------------
TEST(GameFsm, NullPredicateTransitionNeverFires)
{
    StateMachine<Ctx> m;
    const StateId a = m.add_state(make_traced_state("A"));
    const StateId b = m.add_state(make_traced_state("B"));
    // Explicitly add a transition with an empty (null) predicate.
    m.add_transition(a, b, StateMachine<Ctx>::Predicate{});

    Ctx ctx;
    m.start(ctx);
    const bool fired = m.tick(ctx, 0.0F);
    EXPECT_FALSE(fired);
    EXPECT_EQ(m.current_state(), a);
}

// ----------------------------------------------------------------------------
// 22. Hierarchical set_state: jumping from a parent exits regions correctly.
//     Root -> P (with region C1) -> Q.  set_state(Q) from P must exit C1.
// ----------------------------------------------------------------------------
TEST(GameFsm, HierarchicalSetStateExitsRegions)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));
    const StateId Q = root.add_state(make_traced_state("Q"));

    auto region = std::make_unique<HierarchicalFsm<Ctx>>();
    region->add_state(make_traced_state("C1"));
    root.add_region(P, std::move(region));

    Ctx ctx;
    root.start_h(ctx);
    // Verify we are in P/C1 before the jump.
    EXPECT_EQ(root.current_state(), P);
    ctx.trace.clear();

    // Direct jump P -> Q via set_state (bypasses predicates).
    root.set_state(ctx, Q);

    EXPECT_EQ(root.current_state(), Q);
    // C1 exit and P exit must have been fired.
    bool saw_exit_c1 = false;
    bool saw_exit_p  = false;
    for (const auto& e : ctx.trace)
    {
        if (e == "-C1") { saw_exit_c1 = true; }
        if (e == "-P")  { saw_exit_p  = true; }
    }
    EXPECT_TRUE(saw_exit_c1);
    EXPECT_TRUE(saw_exit_p);
    EXPECT_EQ(ctx.trace.back(), "+Q");
}

// ----------------------------------------------------------------------------
// 23. Orthogonal regions: both regions are ticked independently per tick_h.
//     P owns two orthogonal regions R1 (A1->A2) and R2 (B1->B2).
//     Firing trigger_b drives R1's transition; trigger drives R2's.
// ----------------------------------------------------------------------------
TEST(GameFsm, OrthogonalRegionsBothTicked)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));

    auto r1 = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId A1 = r1->add_state(make_traced_state("A1"));
    const StateId A2 = r1->add_state(make_traced_state("A2"));
    r1->add_transition(A1, A2, [](const Ctx& c) { return c.trigger_b; });

    auto r2 = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId B1 = r2->add_state(make_traced_state("B1"));
    const StateId B2 = r2->add_state(make_traced_state("B2"));
    r2->add_transition(B1, B2, [](const Ctx& c) { return c.trigger; });

    root.add_region(P, std::move(r1));
    root.add_region(P, std::move(r2));

    Ctx ctx;
    root.start_h(ctx);

    // Fire R1's predicate.
    ctx.trigger_b = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger_b = false;

    // Fire R2's predicate.
    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger = false;

    // Both regions must have advanced independently.
    EXPECT_EQ(root.has_regions(P), true);
    // active_leaf() follows first region; verify via trace.
    bool saw_a2 = false;
    bool saw_b2 = false;
    for (const auto& e : ctx.trace)
    {
        if (e == "+A2") { saw_a2 = true; }
        if (e == "+B2") { saw_b2 = true; }
    }
    EXPECT_TRUE(saw_a2);
    EXPECT_TRUE(saw_b2);
}

// ----------------------------------------------------------------------------
// 24. Deep history with never-visited nested region falls back to initial.
//     P (deep history) -> C1 -> (L1 initial, L2).
//     Leave P immediately without driving L1->L2.
//     Return: must land on L1, not crash or return kInvalidStateId.
// ----------------------------------------------------------------------------
TEST(GameFsm, DeepHistoryNeverVisitedFallsBackToInitial)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));
    const StateId Q = root.add_state(make_traced_state("Q"));

    auto region_p = std::make_unique<HierarchicalFsm<Ctx>>();
    const StateId C1 = region_p->add_state(make_traced_state("C1"));

    auto region_c1 = std::make_unique<HierarchicalFsm<Ctx>>();
    region_c1->add_state(make_traced_state("L1"));
    const StateId L2 = region_c1->add_state(make_traced_state("L2"));
    (void)L2;
    region_p->add_region(C1, std::move(region_c1));

    root.add_region(P, std::move(region_p));
    root.set_history(P, HistoryKind::kDeep);

    root.add_transition(P, Q, [](const Ctx& c) { return c.trigger_back; });
    root.add_transition(Q, P, [](const Ctx& c) { return c.trigger; });

    Ctx ctx;
    root.start_h(ctx);
    // Do not drive any inner transition — leave immediately.
    ctx.trigger_back = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger_back = false;
    EXPECT_EQ(root.current_state(), Q);

    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);
    ctx.trigger = false;

    EXPECT_EQ(root.current_state(), P);
    EXPECT_NE(root.active_leaf(), cd::game::fsm::kInvalidStateId);
    // Should have fallen back to L1 (initial of C1's region).
    bool saw_l1 = false;
    for (const auto& e : ctx.trace)
    {
        if (e == "+L1") { saw_l1 = true; }
    }
    EXPECT_TRUE(saw_l1);
}

// ----------------------------------------------------------------------------
// 25. Transition entry ordering: parent on_enter fires BEFORE child on_enter.
//     On initial start_h the trace must show +P before +C1.
// ----------------------------------------------------------------------------
TEST(GameFsm, HierarchicalEntryOrderParentBeforeChild)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));

    auto region = std::make_unique<HierarchicalFsm<Ctx>>();
    region->add_state(make_traced_state("C1"));
    root.add_region(P, std::move(region));

    Ctx ctx;
    root.start_h(ctx);

    ASSERT_GE(ctx.trace.size(), 2u);
    EXPECT_EQ(ctx.trace[0], "+P");
    EXPECT_EQ(ctx.trace[1], "+C1");
}

// ----------------------------------------------------------------------------
// 26. Transition exit ordering: child on_exit fires BEFORE parent on_exit.
//     Leaving P->Q must show -C1 then -P in trace.
// ----------------------------------------------------------------------------
TEST(GameFsm, HierarchicalExitOrderChildBeforeParent)
{
    HierarchicalFsm<Ctx> root;
    const StateId P = root.add_state(make_traced_state("P"));
    const StateId Q = root.add_state(make_traced_state("Q"));
    root.add_transition(P, Q, [](const Ctx& c) { return c.trigger; });

    auto region = std::make_unique<HierarchicalFsm<Ctx>>();
    region->add_state(make_traced_state("C1"));
    root.add_region(P, std::move(region));

    Ctx ctx;
    root.start_h(ctx);
    ctx.trace.clear();  // clear boot trace

    ctx.trigger = true;
    root.tick_h(ctx, 0.0F);

    // Must see -C1 before -P.
    std::size_t idx_exit_c1 = ctx.trace.size();
    std::size_t idx_exit_p  = ctx.trace.size();
    for (std::size_t i = 0; i < ctx.trace.size(); ++i)
    {
        if (ctx.trace[i] == "-C1" && idx_exit_c1 == ctx.trace.size()) { idx_exit_c1 = i; }
        if (ctx.trace[i] == "-P"  && idx_exit_p  == ctx.trace.size()) { idx_exit_p  = i; }
    }
    EXPECT_LT(idx_exit_c1, idx_exit_p);
    EXPECT_NE(idx_exit_c1, ctx.trace.size());  // both found
    EXPECT_NE(idx_exit_p,  ctx.trace.size());
}

}  // namespace
