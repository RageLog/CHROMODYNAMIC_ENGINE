// =============================================================================
// CHROMODYNAMIC - tests/test_dialogue.cpp
// Phase 484 - cd::game::dialogue unit tests (G4.1).
//
// Covers the 8 contract requirements from the brief plus DSL parser cases:
//
//   1. Linear dialogue advances to end.
//   2. Branching dialogue: choice A -> node X, choice B -> node Y.
//   3. Conditional choice hidden when predicate false.
//   4. Reset returns to start.
//   5. Speaker accessor returns the active node's speaker.
//   6. End-of-tree returns terminal state (is_at_end + selecting reports
//      kAtEnd).
//   7. Unknown choice id = no-op (kNoOp, current_node unchanged).
//   8. Multiple VMs are independent.
//
// Plus extras:
//   9.  Conditional choice with predicate=true is selectable.
//   10. Empty next_node terminates the conversation cleanly.
//   11. Broken-link choice yields kBrokenLink without advancing.
//   12. parse_tree DSL round-trips a small branching tree.
//   13. Validation: duplicate node id / duplicate choice id / empty start.
// =============================================================================
#include <cd/game/dialogue/Dialogue.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using cd::game::dialogue::Blackboard;
using cd::game::dialogue::DialogueChoice;
using cd::game::dialogue::DialogueNode;
using cd::game::dialogue::DialogueVM;
using cd::game::dialogue::LoadResult;
using cd::game::dialogue::ParsedTree;
using cd::game::dialogue::ParseError;
using cd::game::dialogue::SelectResult;

// Helper: simple two-node linear tree intro -> end.
std::vector<DialogueNode> make_linear_tree()
{
    std::vector<DialogueNode> nodes;
    {
        DialogueNode n;
        n.id      = "intro";
        n.speaker = "Narrator";
        n.text    = "It begins.";
        DialogueChoice c;
        c.id        = "go";
        c.text      = "Continue";
        c.next_node = "end";
        n.choices.push_back(std::move(c));
        nodes.push_back(std::move(n));
    }
    {
        DialogueNode n;
        n.id      = "end";
        n.speaker = "Narrator";
        n.text    = "It ends.";
        nodes.push_back(std::move(n));
    }
    return nodes;
}

// Helper: a branching tree with two visible choices.
//   intro --(left)--> nodeX
//         --(right)-> nodeY
std::vector<DialogueNode> make_branching_tree()
{
    std::vector<DialogueNode> nodes;
    {
        DialogueNode n;
        n.id      = "intro";
        n.speaker = "Guide";
        n.text    = "Pick a path.";
        DialogueChoice a;
        a.id        = "left";
        a.text      = "Take the left road";
        a.next_node = "nodeX";
        DialogueChoice b;
        b.id        = "right";
        b.text      = "Take the right road";
        b.next_node = "nodeY";
        n.choices.push_back(std::move(a));
        n.choices.push_back(std::move(b));
        nodes.push_back(std::move(n));
    }
    nodes.push_back(DialogueNode {"nodeX", "Narrator", "Left road taken.", {}});
    nodes.push_back(DialogueNode {"nodeY", "Narrator", "Right road taken.", {}});
    return nodes;
}

}  // namespace

// -----------------------------------------------------------------------------
// 1. Linear dialogue advances to end.
// -----------------------------------------------------------------------------
TEST(DialogueVm, LinearAdvancesToEnd)
{
    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(make_linear_tree()), LoadResult::kOk);

    ASSERT_NE(vm.current_node(), nullptr);
    EXPECT_EQ(vm.current_node()->id, "intro");
    EXPECT_FALSE(vm.is_at_end());

    EXPECT_EQ(vm.select_choice("go"), SelectResult::kAdvanced);
    ASSERT_NE(vm.current_node(), nullptr);
    EXPECT_EQ(vm.current_node()->id, "end");
    EXPECT_TRUE(vm.is_at_end());
}

// -----------------------------------------------------------------------------
// 2. Branching: choice "left" -> nodeX, "right" -> nodeY (in a fresh VM each).
// -----------------------------------------------------------------------------
TEST(DialogueVm, BranchingChoicesGoToDistinctNodes)
{
    DialogueVM vm_a;
    DialogueVM vm_b;
    ASSERT_EQ(vm_a.load_tree(make_branching_tree()), LoadResult::kOk);
    ASSERT_EQ(vm_b.load_tree(make_branching_tree()), LoadResult::kOk);

    EXPECT_EQ(vm_a.select_choice("left"),  SelectResult::kAdvanced);
    EXPECT_EQ(vm_b.select_choice("right"), SelectResult::kAdvanced);

    ASSERT_NE(vm_a.current_node(), nullptr);
    ASSERT_NE(vm_b.current_node(), nullptr);
    EXPECT_EQ(vm_a.current_node()->id, "nodeX");
    EXPECT_EQ(vm_b.current_node()->id, "nodeY");
}

// -----------------------------------------------------------------------------
// 3. Conditional choice is hidden when the predicate returns false.
//    The same choice becomes visible when the predicate returns true.
// -----------------------------------------------------------------------------
TEST(DialogueVm, ConditionalChoiceHiddenWhenPredicateFalse)
{
    std::vector<DialogueNode> nodes;
    {
        DialogueNode n;
        n.id      = "intro";
        n.speaker = "NPC";
        n.text    = "Greetings.";

        DialogueChoice plain;
        plain.id        = "hello";
        plain.text      = "Hello.";
        plain.next_node = "end";

        DialogueChoice gated;
        gated.id        = "secret";
        gated.text      = "Tell me the secret.";
        gated.next_node = "end";
        gated.condition = [](const Blackboard& bb) {
            return bb.get_bool("has_key");
        };

        n.choices.push_back(std::move(plain));
        n.choices.push_back(std::move(gated));
        nodes.push_back(std::move(n));
    }
    nodes.push_back(DialogueNode {"end", "NPC", "Goodbye.", {}});

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);

    Blackboard bb;
    {
        const auto vis = vm.available_choices(bb);
        ASSERT_EQ(vis.size(), 1U);
        EXPECT_EQ(vis[0]->id, "hello");
    }

    bb.set_bool("has_key", true);
    {
        const auto vis = vm.available_choices(bb);
        ASSERT_EQ(vis.size(), 2U);
        EXPECT_EQ(vis[0]->id, "hello");
        EXPECT_EQ(vis[1]->id, "secret");
    }

    // all_choices() always returns both regardless of predicate.
    const auto all = vm.all_choices();
    EXPECT_EQ(all.size(), 2U);
}

// -----------------------------------------------------------------------------
// 4. reset() returns to the start node.
// -----------------------------------------------------------------------------
TEST(DialogueVm, ResetReturnsToStart)
{
    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(make_branching_tree()), LoadResult::kOk);
    ASSERT_EQ(vm.current_node_id(), "intro");

    EXPECT_EQ(vm.select_choice("left"), SelectResult::kAdvanced);
    EXPECT_EQ(vm.current_node_id(), "nodeX");

    vm.reset();
    EXPECT_EQ(vm.current_node_id(), "intro");
    ASSERT_NE(vm.current_node(), nullptr);
    EXPECT_EQ(vm.current_node()->id, "intro");
}

// -----------------------------------------------------------------------------
// 5. Speaker accessor reports the active node's speaker.
// -----------------------------------------------------------------------------
TEST(DialogueVm, SpeakerAccessor)
{
    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(make_branching_tree()), LoadResult::kOk);
    EXPECT_EQ(vm.current_speaker(), std::string_view {"Guide"});

    EXPECT_EQ(vm.select_choice("right"), SelectResult::kAdvanced);
    EXPECT_EQ(vm.current_speaker(), std::string_view {"Narrator"});
}

// -----------------------------------------------------------------------------
// 6. Terminal node: is_at_end + selecting reports kAtEnd.
// -----------------------------------------------------------------------------
TEST(DialogueVm, TerminalNodeRejectsFurtherSelection)
{
    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(make_linear_tree()), LoadResult::kOk);
    EXPECT_EQ(vm.select_choice("go"), SelectResult::kAdvanced);
    EXPECT_TRUE(vm.is_at_end());
    EXPECT_EQ(vm.available_choices(Blackboard {}).size(), 0U);
    EXPECT_EQ(vm.select_choice("anything"), SelectResult::kAtEnd);
}

// -----------------------------------------------------------------------------
// 7. Unknown choice id is a no-op (does not advance, does not crash).
// -----------------------------------------------------------------------------
TEST(DialogueVm, UnknownChoiceIdIsNoOp)
{
    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(make_branching_tree()), LoadResult::kOk);
    const std::string before = vm.current_node_id();

    EXPECT_EQ(vm.select_choice("does-not-exist"), SelectResult::kNoOp);
    EXPECT_EQ(vm.current_node_id(), before);

    // Still valid afterwards -- a real choice still advances.
    EXPECT_EQ(vm.select_choice("left"), SelectResult::kAdvanced);
    EXPECT_EQ(vm.current_node_id(), "nodeX");
}

// -----------------------------------------------------------------------------
// 8. Multiple VMs are independent: state in one does not leak into another.
// -----------------------------------------------------------------------------
TEST(DialogueVm, MultipleVmsAreIndependent)
{
    DialogueVM vm_a;
    DialogueVM vm_b;
    ASSERT_EQ(vm_a.load_tree(make_linear_tree()),     LoadResult::kOk);
    ASSERT_EQ(vm_b.load_tree(make_branching_tree()),  LoadResult::kOk);

    EXPECT_EQ(vm_a.current_node_id(), "intro");
    EXPECT_EQ(vm_b.current_node_id(), "intro");
    EXPECT_EQ(vm_a.node_count(), 2U);
    EXPECT_EQ(vm_b.node_count(), 3U);

    EXPECT_EQ(vm_a.select_choice("go"), SelectResult::kAdvanced);
    EXPECT_EQ(vm_a.current_node_id(), "end");
    EXPECT_EQ(vm_b.current_node_id(), "intro");  // unaffected

    EXPECT_EQ(vm_b.select_choice("right"), SelectResult::kAdvanced);
    EXPECT_EQ(vm_b.current_node_id(), "nodeY");
    EXPECT_EQ(vm_a.current_node_id(), "end");    // unaffected
}

// -----------------------------------------------------------------------------
// 9. Predicate=true: select_choice with a blackboard succeeds.
//    Predicate=false on select_choice yields kConditionFailed.
// -----------------------------------------------------------------------------
TEST(DialogueVm, SelectRespectsCondition)
{
    std::vector<DialogueNode> nodes;
    {
        DialogueNode n;
        n.id = "intro";
        DialogueChoice c;
        c.id        = "open";
        c.text      = "Open door";
        c.next_node = "room";
        c.condition = [](const Blackboard& bb) {
            return bb.get_bool("has_key");
        };
        n.choices.push_back(std::move(c));
        nodes.push_back(std::move(n));
    }
    nodes.push_back(DialogueNode {"room", "Narrator", "Inside.", {}});

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);

    Blackboard bb;
    EXPECT_EQ(vm.select_choice("open", bb), SelectResult::kConditionFailed);
    EXPECT_EQ(vm.current_node_id(), "intro");

    bb.set_bool("has_key", true);
    EXPECT_EQ(vm.select_choice("open", bb), SelectResult::kAdvanced);
    EXPECT_EQ(vm.current_node_id(), "room");
}

// -----------------------------------------------------------------------------
// 10. Empty next_node terminates the conversation: select reports kAdvanced
//     and is_at_end becomes true (no current node).
// -----------------------------------------------------------------------------
TEST(DialogueVm, EmptyNextNodeTerminates)
{
    std::vector<DialogueNode> nodes;
    DialogueNode n;
    n.id   = "intro";
    n.text = "End here.";
    DialogueChoice c;
    c.id        = "stop";
    c.text      = "Goodbye.";
    c.next_node = "";  // terminate
    n.choices.push_back(std::move(c));
    nodes.push_back(std::move(n));

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);
    EXPECT_EQ(vm.select_choice("stop"), SelectResult::kAdvanced);
    EXPECT_EQ(vm.current_node(), nullptr);
}

// -----------------------------------------------------------------------------
// 11. Broken link (next_node references a missing node) yields kBrokenLink
//     and does not advance the cursor.
// -----------------------------------------------------------------------------
TEST(DialogueVm, BrokenLinkReported)
{
    std::vector<DialogueNode> nodes;
    DialogueNode n;
    n.id = "intro";
    DialogueChoice c;
    c.id        = "go";
    c.text      = "Go nowhere";
    c.next_node = "ghost";  // not present
    n.choices.push_back(std::move(c));
    nodes.push_back(std::move(n));

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);
    EXPECT_EQ(vm.select_choice("go"), SelectResult::kBrokenLink);
    EXPECT_EQ(vm.current_node_id(), "intro");
}

// -----------------------------------------------------------------------------
// 12. DSL round-trip: parse_tree returns a usable graph.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, ParsesBranchingTree)
{
    constexpr std::string_view kSource =
        "# small branching dialogue\n"
        "NODE intro SPEAKER Guide\n"
        "TEXT Pick a path.\n"
        "CHOICE left  -> nodeX : Take the left road\n"
        "CHOICE right -> nodeY : Take the right road\n"
        "END\n"
        "NODE nodeX SPEAKER Narrator\n"
        "TEXT Left road taken.\n"
        "END\n"
        "NODE nodeY SPEAKER Narrator\n"
        "TEXT Right road taken.\n"
        "END\n";

    auto parsed = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParsedTree>(parsed))
        << "parse_tree returned error: line "
        << std::get<ParseError>(parsed).line
        << " '" << std::get<ParseError>(parsed).message << "'";

    auto& tree = std::get<ParsedTree>(parsed);
    ASSERT_EQ(tree.nodes.size(), 3U);
    EXPECT_EQ(tree.nodes[0].id, "intro");
    EXPECT_EQ(tree.nodes[0].speaker, "Guide");
    EXPECT_EQ(tree.nodes[0].text, "Pick a path.");
    ASSERT_EQ(tree.nodes[0].choices.size(), 2U);
    EXPECT_EQ(tree.nodes[0].choices[0].id, "left");
    EXPECT_EQ(tree.nodes[0].choices[0].next_node, "nodeX");
    EXPECT_EQ(tree.nodes[0].choices[0].text, "Take the left road");
    EXPECT_EQ(tree.nodes[1].id, "nodeX");
    EXPECT_EQ(tree.nodes[2].id, "nodeY");

    // And the parsed tree is loadable + walkable.
    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(tree.nodes)), LoadResult::kOk);
    EXPECT_EQ(vm.select_choice("left"), SelectResult::kAdvanced);
    EXPECT_EQ(vm.current_node_id(), "nodeX");
}

// -----------------------------------------------------------------------------
// 13. Validation: load_tree rejects malformed trees with the right tag.
// -----------------------------------------------------------------------------
TEST(DialogueVm, LoadTreeValidationFailures)
{
    {
        DialogueVM vm;
        EXPECT_EQ(vm.load_tree({}), LoadResult::kEmptyTree);
        EXPECT_FALSE(vm.is_loaded());
    }
    {
        DialogueVM vm;
        std::vector<DialogueNode> nodes;
        nodes.push_back(DialogueNode {"", "S", "T", {}});
        EXPECT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kEmptyNodeId);
    }
    {
        DialogueVM vm;
        std::vector<DialogueNode> nodes;
        nodes.push_back(DialogueNode {"a", "S", "T", {}});
        nodes.push_back(DialogueNode {"a", "S", "T", {}});
        EXPECT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kDuplicateNodeId);
    }
    {
        DialogueVM vm;
        DialogueNode n;
        n.id = "intro";
        n.choices.push_back(DialogueChoice {"go", "first",  {}, "end"});
        n.choices.push_back(DialogueChoice {"go", "second", {}, "end"});
        std::vector<DialogueNode> nodes;
        nodes.push_back(std::move(n));
        nodes.push_back(DialogueNode {"end", "", "", {}});
        EXPECT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kDuplicateChoiceId);
    }
    {
        DialogueVM vm;
        DialogueNode n;
        n.id = "intro";
        n.choices.push_back(DialogueChoice {"", "x", {}, "end"});
        std::vector<DialogueNode> nodes;
        nodes.push_back(std::move(n));
        nodes.push_back(DialogueNode {"end", "", "", {}});
        EXPECT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kEmptyChoiceId);
    }
    {
        DialogueVM vm;
        EXPECT_EQ(vm.load_tree(make_linear_tree(), "ghost"),
                  LoadResult::kMissingStartNode);
        EXPECT_FALSE(vm.is_loaded());
    }
    {
        // Explicit start id is honored when valid.
        DialogueVM vm;
        ASSERT_EQ(vm.load_tree(make_branching_tree(), "nodeY"), LoadResult::kOk);
        EXPECT_EQ(vm.current_node_id(), "nodeY");
        EXPECT_TRUE(vm.is_at_end());
    }
}

// -----------------------------------------------------------------------------
// Sanity: select_choice on an unloaded VM is kNotStarted.
// -----------------------------------------------------------------------------
TEST(DialogueVm, SelectBeforeLoadIsNotStarted)
{
    DialogueVM vm;
    EXPECT_EQ(vm.select_choice("anything"), SelectResult::kNotStarted);
    EXPECT_EQ(vm.current_node(), nullptr);
    EXPECT_FALSE(vm.is_loaded());
    vm.reset();  // no-op, must not crash
    EXPECT_EQ(vm.current_node(), nullptr);
}
