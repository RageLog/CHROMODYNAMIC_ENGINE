// =============================================================================
// CHROMODYNAMIC - engine/game/dialog_tree/tests/test_dialog_tree.cpp
// Phase 645 - cd::game::dialog_tree test suite
//
// 5-node BG3-style branching dialog: designer-authored graph, player choices
// drive different endings, condition variables gate branches at runtime.
//
// Tree layout used across tests:
//
//   [greet]  kSay   -> [question]
//   [question] kChoice -> [path_a | path_b]
//   [cond_check] kCondition(var="has_key") -> [path_a | path_b]
//   [path_a] kSay   -> (end)
//   [path_b] kSay   -> (end)
//   [end_node] kEnd
// =============================================================================
#include <cd/game/dialog_tree/DialogTree.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

using namespace cd::game::dialog_tree;

// ---------------------------------------------------------------------------
// Helper: build the canonical 5-node test tree
//
//   greet (kSay)       -> question
//   question (kChoice) -> path_a [0] / path_b [1]
//   path_a (kSay)      -> (no next = end-of-branch)
//   path_b (kSay)      -> (no next = end-of-branch)
//   cond_check (kCondition, var="has_key") -> path_a / path_b
// ---------------------------------------------------------------------------
DialogTree make_test_tree()
{
    DialogTree t;
    t.tree_id = "test_tree";
    t.root_id = "greet";

    DialogNode greet;
    greet.node_id  = "greet";
    greet.kind     = NodeKind::kSay;
    greet.text     = "Greetings, traveller. What brings you here?";
    greet.next_ids = { "question" };

    DialogNode question;
    question.node_id  = "question";
    question.kind     = NodeKind::kChoice;
    question.text     = "What do you seek?";
    question.next_ids = { "path_a", "path_b" };

    DialogNode path_a;
    path_a.node_id  = "path_a";
    path_a.kind     = NodeKind::kSay;
    path_a.text     = "Ah, you chose the noble path. Farewell.";
    // next_ids empty -> end-of-branch

    DialogNode path_b;
    path_b.node_id  = "path_b";
    path_b.kind     = NodeKind::kSay;
    path_b.text     = "A darker road awaits you. Goodbye.";
    // next_ids empty -> end-of-branch

    DialogNode cond_check;
    cond_check.node_id      = "cond_check";
    cond_check.kind         = NodeKind::kCondition;
    cond_check.condition_var = "has_key";
    cond_check.next_ids     = { "path_a", "path_b" };  // [0]=true, [1]=false

    t.nodes = { greet, question, path_a, path_b, cond_check };
    return t;
}

// ---------------------------------------------------------------------------
// T1: Linear walk through kSay -> kChoice -> kSay (choice index 0)
// ---------------------------------------------------------------------------
TEST(DialogTree, LinearWalkChoiceZero)
{
    DialogTreeRuntime rt;
    rt.load(make_test_tree());
    rt.start();

    ASSERT_FALSE(rt.is_done());
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "greet");
    EXPECT_EQ(rt.current()->kind,    NodeKind::kSay);

    // advance past kSay -> question
    EXPECT_TRUE(rt.advance(0));
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "question");
    EXPECT_EQ(rt.current()->kind,    NodeKind::kChoice);

    // player picks choice 0 -> path_a
    EXPECT_TRUE(rt.advance(0));
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "path_a");

    // advance past final kSay (no next_ids -> done)
    EXPECT_TRUE(rt.advance(0));
    EXPECT_TRUE(rt.is_done());
    EXPECT_EQ(rt.current(), nullptr);
}

// ---------------------------------------------------------------------------
// T2: Player picks choice index 1 -> ends at path_b
// ---------------------------------------------------------------------------
TEST(DialogTree, ChoiceOneLeadsToPathB)
{
    DialogTreeRuntime rt;
    rt.load(make_test_tree());
    rt.start();

    rt.advance(0);  // greet -> question
    EXPECT_TRUE(rt.advance(1));  // question -> path_b
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "path_b");
    EXPECT_EQ(rt.current()->kind,    NodeKind::kSay);

    rt.advance(0);
    EXPECT_TRUE(rt.is_done());
}

// ---------------------------------------------------------------------------
// T3: kCondition node routes to path_a when condition var is true
// ---------------------------------------------------------------------------
TEST(DialogTree, ConditionNodeTrueBranch)
{
    // Build a tree whose root is cond_check directly
    DialogTree t = make_test_tree();
    t.root_id    = "cond_check";

    DialogTreeRuntime rt;
    rt.load(t);
    rt.set_condition_var("has_key", true);
    rt.start();

    // kCondition is transparent — current() should skip to path_a
    ASSERT_FALSE(rt.is_done());
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "path_a");
}

// ---------------------------------------------------------------------------
// T4: kCondition node routes to path_b when condition var is false
// ---------------------------------------------------------------------------
TEST(DialogTree, ConditionNodeFalseBranch)
{
    DialogTree t = make_test_tree();
    t.root_id    = "cond_check";

    DialogTreeRuntime rt;
    rt.load(t);
    rt.set_condition_var("has_key", false);
    rt.start();

    ASSERT_FALSE(rt.is_done());
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "path_b");
}

// ---------------------------------------------------------------------------
// T5: Out-of-range choice index returns false, does not advance
// ---------------------------------------------------------------------------
TEST(DialogTree, OutOfRangeChoiceReturnsFalse)
{
    DialogTreeRuntime rt;
    rt.load(make_test_tree());
    rt.start();

    rt.advance(0);  // greet -> question (kChoice with 2 options)
    EXPECT_EQ(rt.current()->node_id, "question");

    // choice index 5 is out of range
    EXPECT_FALSE(rt.advance(5));

    // runtime stays on question
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "question");
    EXPECT_FALSE(rt.is_done());
}

// ---------------------------------------------------------------------------
// T6: advance() before start() returns false and keeps is_done() true
// ---------------------------------------------------------------------------
TEST(DialogTree, AdvanceBeforeStartIsNoOp)
{
    DialogTreeRuntime rt;
    rt.load(make_test_tree());
    // start() NOT called

    EXPECT_TRUE(rt.is_done());
    EXPECT_EQ(rt.current(), nullptr);
    EXPECT_FALSE(rt.advance(0));
    EXPECT_TRUE(rt.is_done());
}

// ---------------------------------------------------------------------------
// T7: Condition var undefined -> false branch taken (defensive default)
// ---------------------------------------------------------------------------
TEST(DialogTree, UndefinedConditionVarDefaultsFalse)
{
    DialogTree t = make_test_tree();
    t.root_id    = "cond_check";

    DialogTreeRuntime rt;
    rt.load(t);
    // "has_key" not set -> default false -> path_b
    rt.start();

    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "path_b");
}

// ---------------------------------------------------------------------------
// T8: kEnd node reached via explicit end_node id
// ---------------------------------------------------------------------------
TEST(DialogTree, ExplicitEndNodeTerminates)
{
    DialogTree t;
    t.tree_id = "end_test";
    t.root_id = "start";

    DialogNode start_node;
    start_node.node_id  = "start";
    start_node.kind     = NodeKind::kSay;
    start_node.text     = "Begin.";
    start_node.next_ids = { "fin" };

    DialogNode fin;
    fin.node_id = "fin";
    fin.kind    = NodeKind::kEnd;

    t.nodes = { start_node, fin };

    DialogTreeRuntime rt;
    rt.load(t);
    rt.start();

    ASSERT_FALSE(rt.is_done());
    EXPECT_EQ(rt.current()->node_id, "start");

    rt.advance(0);  // follow next_ids[0] = fin (kEnd) -> done
    EXPECT_TRUE(rt.is_done());
}

}  // namespace
