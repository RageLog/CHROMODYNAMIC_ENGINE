// =============================================================================
// CHROMODYNAMIC — tests/test_ai_bt.cpp
// Phase 473 — cd::game::ai_bt unit tests.
//
// Covers the 10 contract requirements from the G2.2 brief plus a handful of
// extra edge cases (running propagation, decorator composition, blackboard
// type-mismatch defaults).
// =============================================================================
#include <cd/game/ai_bt/BehaviorTree.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace
{

using cd::game::ai_bt::BehaviorTree;
using cd::game::ai_bt::Blackboard;
using cd::game::ai_bt::InverterNode;
using cd::game::ai_bt::Node;
using cd::game::ai_bt::ParallelNode;
using cd::game::ai_bt::RepeaterNode;
using cd::game::ai_bt::SelectorNode;
using cd::game::ai_bt::SequenceNode;
using cd::game::ai_bt::Status;
using cd::game::ai_bt::UntilSuccessNode;
using cd::game::ai_bt::make_leaf;

// Tiny helpers — single-shot leaves with hard-coded outcomes.
[[nodiscard]] std::unique_ptr<Node> ok_leaf()    { return make_leaf([](Blackboard&) { return Status::kSuccess; }); }
[[nodiscard]] std::unique_ptr<Node> fail_leaf()  { return make_leaf([](Blackboard&) { return Status::kFailure; }); }
[[nodiscard]] std::unique_ptr<Node> run_leaf()   { return make_leaf([](Blackboard&) { return Status::kRunning; }); }

// Increment a named blackboard counter and return kSuccess.
[[nodiscard]] std::unique_ptr<Node> counter_leaf(std::string key)
{
    return make_leaf([k = std::move(key)](Blackboard& bb) {
        bb.set_int(k, bb.get_int(k, 0) + 1);
        return Status::kSuccess;
    });
}

// -----------------------------------------------------------------------------
// 1) Sequence: all children succeed -> Success.
// -----------------------------------------------------------------------------
TEST(BehaviorTree, SequenceAllSuccess)
{
    auto seq = std::make_unique<SequenceNode>();
    seq->add_child(ok_leaf());
    seq->add_child(ok_leaf());
    seq->add_child(ok_leaf());

    Blackboard bb;
    EXPECT_EQ(seq->tick(bb), Status::kSuccess);
}

// -----------------------------------------------------------------------------
// 2) Sequence: first failure -> Failure (later children must not run).
// -----------------------------------------------------------------------------
TEST(BehaviorTree, SequenceFirstFailure)
{
    auto seq = std::make_unique<SequenceNode>();
    seq->add_child(ok_leaf());
    seq->add_child(fail_leaf());
    seq->add_child(counter_leaf("after_fail"));  // must NEVER run

    Blackboard bb;
    EXPECT_EQ(seq->tick(bb), Status::kFailure);
    EXPECT_FALSE(bb.has("after_fail"));
}

// -----------------------------------------------------------------------------
// 3) Selector: first success -> Success (later children must not run).
// -----------------------------------------------------------------------------
TEST(BehaviorTree, SelectorFirstSuccess)
{
    auto sel = std::make_unique<SelectorNode>();
    sel->add_child(fail_leaf());
    sel->add_child(ok_leaf());
    sel->add_child(counter_leaf("after_success"));  // must NEVER run

    Blackboard bb;
    EXPECT_EQ(sel->tick(bb), Status::kSuccess);
    EXPECT_FALSE(bb.has("after_success"));
}

// -----------------------------------------------------------------------------
// 4) Selector: all fail -> Failure.
// -----------------------------------------------------------------------------
TEST(BehaviorTree, SelectorAllFail)
{
    auto sel = std::make_unique<SelectorNode>();
    sel->add_child(fail_leaf());
    sel->add_child(fail_leaf());
    sel->add_child(fail_leaf());

    Blackboard bb;
    EXPECT_EQ(sel->tick(bb), Status::kFailure);
}

// -----------------------------------------------------------------------------
// 5) Parallel: tracks ALL children (default thresholds: all-success / any-fail).
// -----------------------------------------------------------------------------
TEST(BehaviorTree, ParallelTracksAllChildren)
{
    auto par = std::make_unique<ParallelNode>();
    par->add_child(counter_leaf("a"));
    par->add_child(counter_leaf("b"));
    par->add_child(counter_leaf("c"));

    Blackboard bb;
    EXPECT_EQ(par->tick(bb), Status::kSuccess);  // every child kSuccess
    EXPECT_EQ(bb.get_int("a"), 1);
    EXPECT_EQ(bb.get_int("b"), 1);
    EXPECT_EQ(bb.get_int("c"), 1);

    // Failure threshold: first failure aborts.
    auto par2 = std::make_unique<ParallelNode>();
    par2->add_child(counter_leaf("x"));
    par2->add_child(fail_leaf());
    par2->add_child(counter_leaf("z"));

    Blackboard bb2;
    EXPECT_EQ(par2->tick(bb2), Status::kFailure);
    EXPECT_EQ(bb2.get_int("x"), 1);  // ticked
    EXPECT_EQ(bb2.get_int("z"), 1);  // ticked (parallel runs all children)

    // Running until thresholds met.
    auto par3 = std::make_unique<ParallelNode>(/*success_threshold=*/2, /*failure_threshold=*/3);
    par3->add_child(ok_leaf());
    par3->add_child(run_leaf());
    par3->add_child(run_leaf());
    Blackboard bb3;
    EXPECT_EQ(par3->tick(bb3), Status::kRunning);  // 1 success, 0 failures
}

// -----------------------------------------------------------------------------
// 6) Inverter: flips success/failure; running passes through.
// -----------------------------------------------------------------------------
TEST(BehaviorTree, InverterFlipsTerminals)
{
    Blackboard bb;

    InverterNode inv_ok(ok_leaf());
    EXPECT_EQ(inv_ok.tick(bb), Status::kFailure);

    InverterNode inv_fail(fail_leaf());
    EXPECT_EQ(inv_fail.tick(bb), Status::kSuccess);

    InverterNode inv_run(run_leaf());
    EXPECT_EQ(inv_run.tick(bb), Status::kRunning);
}

// -----------------------------------------------------------------------------
// 7) Repeater: runs the child N times then reports the final status.
// -----------------------------------------------------------------------------
TEST(BehaviorTree, RepeaterRunsNTimes)
{
    constexpr std::size_t kIterations = 5;
    RepeaterNode rep(counter_leaf("hits"), kIterations);

    Blackboard bb;
    const Status final_status = rep.tick(bb);
    EXPECT_EQ(final_status, Status::kSuccess);
    EXPECT_EQ(bb.get_int("hits"), static_cast<int>(kIterations));

    // After completion, target_count is unchanged, current_iter is reset.
    EXPECT_EQ(rep.target_count(), kIterations);
    EXPECT_EQ(rep.current_iter(), 0U);
}

// -----------------------------------------------------------------------------
// 8) UntilSuccess: loops until child succeeds.
//
// The child fails its first 2 ticks then succeeds on the 3rd. The decorator
// reports kRunning twice then kSuccess.
// -----------------------------------------------------------------------------
TEST(BehaviorTree, UntilSuccessLoopsUntilWin)
{
    int call_count = 0;
    auto flaky = make_leaf([&call_count](Blackboard&) {
        ++call_count;
        return call_count >= 3 ? Status::kSuccess : Status::kFailure;
    });

    UntilSuccessNode until(std::move(flaky));
    Blackboard bb;

    EXPECT_EQ(until.tick(bb), Status::kRunning);  // call 1 -> fail -> running
    EXPECT_EQ(until.tick(bb), Status::kRunning);  // call 2 -> fail -> running
    EXPECT_EQ(until.tick(bb), Status::kSuccess);  // call 3 -> succeed -> success
    EXPECT_EQ(call_count, 3);
}

// -----------------------------------------------------------------------------
// 9) Blackboard read / write — covers every variant alternative + defaults.
// -----------------------------------------------------------------------------
TEST(BehaviorTree, BlackboardReadWrite)
{
    Blackboard bb;

    bb.set_bool ("alive",       true);
    bb.set_int  ("hp",          75);
    bb.set_float("stamina",     0.5F);
    bb.set_string("target_id",  "enemy_007");

    EXPECT_TRUE (bb.has("alive"));
    EXPECT_TRUE (bb.get_bool("alive"));
    EXPECT_EQ   (bb.get_int("hp"), 75);
    EXPECT_FLOAT_EQ(bb.get_float("stamina"), 0.5F);
    EXPECT_EQ   (bb.get_string("target_id"), "enemy_007");

    // Defaults on missing key.
    EXPECT_FALSE(bb.has("missing"));
    EXPECT_FALSE(bb.get_bool("missing", false));
    EXPECT_EQ   (bb.get_int("missing", -1), -1);
    EXPECT_FLOAT_EQ(bb.get_float("missing", 3.14F), 3.14F);
    EXPECT_EQ   (bb.get_string("missing", "n/a"), "n/a");

    // Type-mismatch falls back to default rather than throwing.
    EXPECT_EQ(bb.get_int("alive", -1), -1);          // alive is bool
    EXPECT_FLOAT_EQ(bb.get_float("target_id", 9.0F), 9.0F);

    // Erase + size + clear.
    EXPECT_EQ(bb.size(), 4U);
    EXPECT_TRUE(bb.erase("alive"));
    EXPECT_FALSE(bb.has("alive"));
    EXPECT_EQ(bb.size(), 3U);
    bb.clear();
    EXPECT_EQ(bb.size(), 0U);
}

// -----------------------------------------------------------------------------
// 10) Composite-of-all: Sequence containing Selector containing Leaf.
//
// Mirrors the brief explicitly. Also threads a BehaviorTree wrapper so the
// public dt-publishing + root-owning surface is exercised at least once.
// -----------------------------------------------------------------------------
TEST(BehaviorTree, CompositeSequenceSelectorLeaf)
{
    auto sel = std::make_unique<SelectorNode>();
    sel->add_child(fail_leaf());                    // first option fails
    sel->add_child(counter_leaf("chosen"));         // second option succeeds

    auto seq = std::make_unique<SequenceNode>();
    seq->add_child(counter_leaf("step1"));
    seq->add_child(std::move(sel));                 // Sequence > Selector > Leaf
    seq->add_child(counter_leaf("step3"));

    BehaviorTree tree(std::move(seq));
    Blackboard bb;

    constexpr float kDt = 0.016F;
    EXPECT_EQ(tree.tick(bb, kDt), Status::kSuccess);
    EXPECT_EQ(bb.get_int("step1"), 1);
    EXPECT_EQ(bb.get_int("chosen"), 1);
    EXPECT_EQ(bb.get_int("step3"), 1);
    EXPECT_FLOAT_EQ(bb.get_float("dt"), kDt);  // dt was published

    // Reset clears in-flight composite state.
    tree.reset();

    // Second tick repeats deterministically.
    EXPECT_EQ(tree.tick(bb, kDt), Status::kSuccess);
    EXPECT_EQ(bb.get_int("step1"), 2);
    EXPECT_EQ(bb.get_int("chosen"), 2);
    EXPECT_EQ(bb.get_int("step3"), 2);
}

// -----------------------------------------------------------------------------
// 11) Extra: Sequence with a running child resumes from the same cursor.
// -----------------------------------------------------------------------------
TEST(BehaviorTree, SequenceResumesFromRunningChild)
{
    int gate = 0;  // 0 -> kRunning, 1 -> kSuccess
    auto runner = make_leaf([&gate](Blackboard&) {
        return gate == 0 ? Status::kRunning : Status::kSuccess;
    });

    auto seq = std::make_unique<SequenceNode>();
    seq->add_child(counter_leaf("before"));   // succeeds once
    seq->add_child(std::move(runner));        // pauses the sequence
    seq->add_child(counter_leaf("after"));    // should fire only after gate flips

    Blackboard bb;
    EXPECT_EQ(seq->tick(bb), Status::kRunning);
    EXPECT_EQ(bb.get_int("before"), 1);
    EXPECT_FALSE(bb.has("after"));

    // Flip the gate; sequence advances and finishes.
    gate = 1;
    EXPECT_EQ(seq->tick(bb), Status::kSuccess);
    EXPECT_EQ(bb.get_int("before"), 1);   // NOT re-ticked (memory-sequence)
    EXPECT_EQ(bb.get_int("after"),  1);
}

// -----------------------------------------------------------------------------
// 12) Extra: Repeater(forever) yields kRunning every iteration so siblings
//      in a Parallel still get airtime.
// -----------------------------------------------------------------------------
TEST(BehaviorTree, RepeaterForeverYields)
{
    RepeaterNode forever(counter_leaf("ticks"), /*count=*/0);
    Blackboard bb;

    EXPECT_EQ(forever.tick(bb), Status::kRunning);
    EXPECT_EQ(forever.tick(bb), Status::kRunning);
    EXPECT_EQ(forever.tick(bb), Status::kRunning);
    EXPECT_EQ(bb.get_int("ticks"), 3);
}

}  // namespace
