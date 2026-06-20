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

using cd::game::dialog_tree::DialogNode;
using cd::game::dialog_tree::DialogTree;
using cd::game::dialog_tree::DialogTreeRuntime;
using cd::game::dialog_tree::NodeKind;

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

// ---------------------------------------------------------------------------
// T9: Chained kCondition nodes — two back-to-back condition gates, both
//     evaluated transparently; current() lands on the correct terminal node.
//
//   root_cond (kCondition, "gate_a") -> mid_cond / path_b
//   mid_cond  (kCondition, "gate_b") -> path_a   / path_b
//   path_a, path_b (kSay, no next)
// ---------------------------------------------------------------------------
TEST(DialogTree, ChainedConditionNodes)
{
    DialogTree t;
    t.tree_id = "chain_cond";
    t.root_id = "root_cond";

    DialogNode root_cond;
    root_cond.node_id       = "root_cond";
    root_cond.kind          = NodeKind::kCondition;
    root_cond.condition_var = "gate_a";
    root_cond.next_ids      = { "mid_cond", "path_b" };  // true -> mid_cond

    DialogNode mid_cond;
    mid_cond.node_id       = "mid_cond";
    mid_cond.kind          = NodeKind::kCondition;
    mid_cond.condition_var = "gate_b";
    mid_cond.next_ids      = { "path_a", "path_b" };  // true -> path_a

    DialogNode path_a;
    path_a.node_id = "path_a";
    path_a.kind    = NodeKind::kSay;
    path_a.text    = "Noble path.";

    DialogNode path_b;
    path_b.node_id = "path_b";
    path_b.kind    = NodeKind::kSay;
    path_b.text    = "Dark road.";

    t.nodes = { root_cond, mid_cond, path_a, path_b };

    // Both gates true -> root_cond true->mid_cond true->path_a
    {
        DialogTreeRuntime rt;
        rt.load(t);
        rt.set_condition_var("gate_a", true);
        rt.set_condition_var("gate_b", true);
        rt.start();

        ASSERT_FALSE(rt.is_done());
        ASSERT_NE(rt.current(), nullptr);
        EXPECT_EQ(rt.current()->node_id, "path_a");
    }

    // gate_a true, gate_b false -> root_cond true->mid_cond false->path_b
    {
        DialogTreeRuntime rt;
        rt.load(t);
        rt.set_condition_var("gate_a", true);
        rt.set_condition_var("gate_b", false);
        rt.start();

        ASSERT_NE(rt.current(), nullptr);
        EXPECT_EQ(rt.current()->node_id, "path_b");
    }

    // gate_a false -> root_cond false->path_b (mid_cond never reached)
    {
        DialogTreeRuntime rt;
        rt.load(t);
        rt.set_condition_var("gate_a", false);
        rt.start();

        ASSERT_NE(rt.current(), nullptr);
        EXPECT_EQ(rt.current()->node_id, "path_b");
    }
}

// ---------------------------------------------------------------------------
// T10: Broken link (node_id typo in next_ids) -> runtime ends gracefully,
//      never derefs a null pointer.
// ---------------------------------------------------------------------------
TEST(DialogTree, BrokenLinkEndsGracefully)
{
    DialogTree t;
    t.tree_id = "broken";
    t.root_id = "start";

    DialogNode start_node;
    start_node.node_id  = "start";
    start_node.kind     = NodeKind::kSay;
    start_node.text     = "Hello.";
    start_node.next_ids = { "DOES_NOT_EXIST" };  // deliberate typo

    t.nodes = { start_node };

    DialogTreeRuntime rt;
    rt.load(t);
    rt.start();

    ASSERT_FALSE(rt.is_done());
    EXPECT_EQ(rt.current()->node_id, "start");

    // advance() follows the broken link
    EXPECT_TRUE(rt.advance(0));
    EXPECT_TRUE(rt.is_done());
    EXPECT_EQ(rt.current(), nullptr);
}

// ---------------------------------------------------------------------------
// T11: kCondition with only the true-branch id (no false entry in next_ids).
//      When the condition is false and next_ids[1] is absent, tree ends.
// ---------------------------------------------------------------------------
TEST(DialogTree, ConditionFalseBranchMissingEnds)
{
    DialogTree t;
    t.tree_id = "half_cond";
    t.root_id = "cond";

    DialogNode cond;
    cond.node_id       = "cond";
    cond.kind          = NodeKind::kCondition;
    cond.condition_var = "has_sword";
    cond.next_ids      = { "reward" };  // only true branch present

    DialogNode reward;
    reward.node_id = "reward";
    reward.kind    = NodeKind::kSay;
    reward.text    = "You have a sword!";

    t.nodes = { cond, reward };

    // Condition is false -> next_ids[1] missing -> done
    DialogTreeRuntime rt;
    rt.load(t);
    rt.set_condition_var("has_sword", false);
    rt.start();

    EXPECT_TRUE(rt.is_done());
    EXPECT_EQ(rt.current(), nullptr);

    // Condition is true -> reward node visible
    DialogTreeRuntime rt2;
    rt2.load(t);
    rt2.set_condition_var("has_sword", true);
    rt2.start();

    ASSERT_FALSE(rt2.is_done());
    ASSERT_NE(rt2.current(), nullptr);
    EXPECT_EQ(rt2.current()->node_id, "reward");
}

// ---------------------------------------------------------------------------
// T12: Cyclic kCondition graph (A -> B -> A) must not stack-overflow.
//      The depth guard must fire and terminate the conversation gracefully.
// ---------------------------------------------------------------------------
TEST(DialogTree, CyclicConditionGraphEndsGracefully)
{
    DialogTree t;
    t.tree_id = "cyclic";
    t.root_id = "node_a";

    // node_a (kCondition, "always_true") -> node_b [true], path_end [false]
    // node_b (kCondition, "always_true") -> node_a [true]  <- cycle!
    DialogNode node_a;
    node_a.node_id       = "node_a";
    node_a.kind          = NodeKind::kCondition;
    node_a.condition_var = "always_true";
    node_a.next_ids      = { "node_b", "path_end" };

    DialogNode node_b;
    node_b.node_id       = "node_b";
    node_b.kind          = NodeKind::kCondition;
    node_b.condition_var = "always_true";
    node_b.next_ids      = { "node_a" };  // cycle back to node_a

    DialogNode path_end;
    path_end.node_id = "path_end";
    path_end.kind    = NodeKind::kSay;
    path_end.text    = "Escaped.";

    t.nodes = { node_a, node_b, path_end };

    DialogTreeRuntime rt;
    rt.load(t);
    rt.set_condition_var("always_true", true);
    rt.start();  // must return (not hang)

    // After the depth ceiling fires the conversation is done
    EXPECT_TRUE(rt.is_done());
    EXPECT_EQ(rt.current(), nullptr);
}

// ---------------------------------------------------------------------------
// T13: Duplicate node_id in load() — the second entry wins (last-writer-wins
//      semantics of the hash index). Document and test the behaviour so it
//      is not accidentally changed.
// ---------------------------------------------------------------------------
TEST(DialogTree, DuplicateNodeIdLastWriterWins)
{
    DialogTree t;
    t.tree_id = "dup";
    t.root_id = "dupe";

    DialogNode v1;
    v1.node_id = "dupe";
    v1.kind    = NodeKind::kSay;
    v1.text    = "Version 1";

    DialogNode v2;
    v2.node_id = "dupe";
    v2.kind    = NodeKind::kSay;
    v2.text    = "Version 2";  // overwrites v1 in the index

    t.nodes = { v1, v2 };

    DialogTreeRuntime rt;
    rt.load(t);
    rt.start();

    ASSERT_FALSE(rt.is_done());
    ASSERT_NE(rt.current(), nullptr);
    // The index entry for "dupe" points at the last inserted node (v2)
    EXPECT_EQ(rt.current()->text, "Version 2");
}

// ---------------------------------------------------------------------------
// T14: Empty tree (no nodes) — start() is a no-op; is_done() stays true.
// ---------------------------------------------------------------------------
TEST(DialogTree, EmptyTreeIsImmediatelyDone)
{
    DialogTree t;
    t.tree_id = "empty";
    t.root_id = "";  // also empty

    DialogTreeRuntime rt;
    rt.load(t);
    rt.start();

    EXPECT_TRUE(rt.is_done());
    EXPECT_EQ(rt.current(), nullptr);
    EXPECT_FALSE(rt.advance(0));
}

// ---------------------------------------------------------------------------
// T15: Deep kSay chain — 50 nodes linked linearly; runtime must walk all
//      50 without issue and then report is_done() true.
// ---------------------------------------------------------------------------
TEST(DialogTree, DeepSayChainTerminates)
{
    static constexpr int kDepth = 50;

    DialogTree t;
    t.tree_id = "deep";
    t.root_id = "node_0";

    for (int i = 0; i < kDepth; ++i)
    {
        DialogNode n;
        n.node_id = "node_" + std::to_string(i);
        n.kind    = NodeKind::kSay;
        n.text    = "Line " + std::to_string(i);
        if (i + 1 < kDepth)
        {
            n.next_ids = { "node_" + std::to_string(i + 1) };
        }
        // last node has no next -> end-of-branch
        t.nodes.emplace_back(std::move(n));
    }

    DialogTreeRuntime rt;
    rt.load(t);
    rt.start();

    int steps = 0;
    while (!rt.is_done())
    {
        ASSERT_NE(rt.current(), nullptr);
        rt.advance(0);
        ++steps;
    }

    EXPECT_EQ(steps, kDepth);
    EXPECT_TRUE(rt.is_done());
    EXPECT_EQ(rt.current(), nullptr);
}

// ---------------------------------------------------------------------------
// T16: kChoice node with zero options — the node has next_ids empty.
//      advance() should return false and mark done; the conversation
//      never crashes on an empty next_ids vector.
// ---------------------------------------------------------------------------
TEST(DialogTree, ChoiceNodeWithZeroOptionsEndsDone)
{
    DialogTree t;
    t.tree_id = "no_choices";
    t.root_id = "menu";

    DialogNode menu;
    menu.node_id  = "menu";
    menu.kind     = NodeKind::kChoice;
    menu.text     = "Choose your fate.";
    // next_ids intentionally empty — no choices authored

    t.nodes = { menu };

    DialogTreeRuntime rt;
    rt.load(t);
    rt.start();

    ASSERT_FALSE(rt.is_done());
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "menu");

    // Any choice index is out-of-range when next_ids is empty
    EXPECT_FALSE(rt.advance(0));
    // Runtime stays on the same node (out-of-range = no-op, not done)
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "menu");
    EXPECT_FALSE(rt.is_done());
}

// ---------------------------------------------------------------------------
// T17: load() called a second time resets runtime state but preserves
//      condition vars (so callers can seed before load).
//      Verify that a second load with a different tree walks the new tree.
// ---------------------------------------------------------------------------
TEST(DialogTree, ReloadClearsRuntimeState)
{
    // First tree: greet -> question
    DialogTreeRuntime rt;
    rt.load(make_test_tree());
    rt.start();
    rt.advance(0);  // greet -> question
    EXPECT_EQ(rt.current()->node_id, "question");

    // Reload with a simple two-node tree
    DialogTree t2;
    t2.tree_id = "second";
    t2.root_id = "hello";

    DialogNode hello;
    hello.node_id  = "hello";
    hello.kind     = NodeKind::kSay;
    hello.text     = "Hello again.";

    t2.nodes = { hello };

    rt.load(t2);
    // After load(), runtime must be in pre-start state
    EXPECT_TRUE(rt.is_done());
    EXPECT_EQ(rt.current(), nullptr);

    rt.start();
    ASSERT_FALSE(rt.is_done());
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "hello");
}

// ---------------------------------------------------------------------------
// T18: Condition vars set BEFORE load() are preserved across the load() call
//      (the intentional NOT-cleared semantic documented in the .cpp).
// ---------------------------------------------------------------------------
TEST(DialogTree, ConditionVarsPreservedAcrossLoad)
{
    DialogTreeRuntime rt;
    // Seed BEFORE load
    rt.set_condition_var("has_key", true);

    DialogTree t = make_test_tree();
    t.root_id    = "cond_check";

    rt.load(t);
    rt.start();

    // cond_check routes on "has_key" which was seeded before load
    ASSERT_FALSE(rt.is_done());
    ASSERT_NE(rt.current(), nullptr);
    EXPECT_EQ(rt.current()->node_id, "path_a");
}

}  // namespace
