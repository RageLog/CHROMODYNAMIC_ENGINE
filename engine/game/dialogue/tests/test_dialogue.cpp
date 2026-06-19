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

#include <stdexcept>
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

// =============================================================================
// Extended edge / negative tests for genuine 100% coverage
// =============================================================================

// -----------------------------------------------------------------------------
// 14. DSL error paths: unknown directive surfaces ParseError.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, UnknownDirectiveIsError)
{
    constexpr std::string_view kSource =
        "NODE intro\n"
        "TEXT Hello.\n"
        "JUMP outro\n"  // unknown keyword
        "END\n";

    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParseError>(result));
    const auto& err = std::get<ParseError>(result);
    EXPECT_EQ(err.line, 3U);
    EXPECT_FALSE(err.message.empty());
}

// -----------------------------------------------------------------------------
// 15. DSL error paths: TEXT before any NODE.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, TextBeforeNodeIsError)
{
    constexpr std::string_view kSource = "TEXT orphan line\n";
    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParseError>(result));
    EXPECT_EQ(std::get<ParseError>(result).line, 1U);
}

// -----------------------------------------------------------------------------
// 16. DSL error paths: CHOICE before any NODE.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, ChoiceBeforeNodeIsError)
{
    constexpr std::string_view kSource = "CHOICE c -> n : text\n";
    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParseError>(result));
    EXPECT_EQ(std::get<ParseError>(result).line, 1U);
}

// -----------------------------------------------------------------------------
// 17. DSL error paths: NODE missing id.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, NodeMissingIdIsError)
{
    constexpr std::string_view kSource = "NODE\nEND\n";
    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParseError>(result));
    EXPECT_EQ(std::get<ParseError>(result).line, 1U);
}

// -----------------------------------------------------------------------------
// 18. DSL error paths: SPEAKER keyword present but name missing.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, SpeakerMissingNameIsError)
{
    constexpr std::string_view kSource = "NODE intro SPEAKER\nEND\n";
    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParseError>(result));
    EXPECT_EQ(std::get<ParseError>(result).line, 1U);
}

// -----------------------------------------------------------------------------
// 19. DSL error paths: END with trailing arguments is rejected.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, EndWithArgsIsError)
{
    constexpr std::string_view kSource = "NODE intro\nEND extra\n";
    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParseError>(result));
    EXPECT_EQ(std::get<ParseError>(result).line, 2U);
}

// -----------------------------------------------------------------------------
// 20. DSL error paths: CHOICE with malformed grammar (missing '->' arrow).
// -----------------------------------------------------------------------------
TEST(DialogueDsl, ChoiceMissingArrowIsError)
{
    constexpr std::string_view kSource =
        "NODE intro\n"
        "CHOICE c : no-arrow-here\n"
        "END\n";
    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParseError>(result));
    EXPECT_EQ(std::get<ParseError>(result).line, 2U);
}

// -----------------------------------------------------------------------------
// 21. DSL error paths: CHOICE with missing colon separator.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, ChoiceMissingColonIsError)
{
    constexpr std::string_view kSource =
        "NODE intro\n"
        "CHOICE c -> dest no-colon\n"
        "END\n";
    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParseError>(result));
    EXPECT_EQ(std::get<ParseError>(result).line, 2U);
}

// -----------------------------------------------------------------------------
// 22. DSL error paths: CHOICE with empty id.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, ChoiceEmptyIdIsError)
{
    // Arrow before any non-whitespace id token → empty id after trim.
    constexpr std::string_view kSource =
        "NODE intro\n"
        "CHOICE  -> dest : prompt\n"
        "END\n";
    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParseError>(result));
    EXPECT_EQ(std::get<ParseError>(result).line, 2U);
}

// -----------------------------------------------------------------------------
// 23. DSL: empty source produces an empty ParsedTree (not an error).
// -----------------------------------------------------------------------------
TEST(DialogueDsl, EmptySourceProducesEmptyTree)
{
    const auto result = cd::game::dialogue::parse_tree("");
    ASSERT_TRUE(std::holds_alternative<ParsedTree>(result));
    EXPECT_TRUE(std::get<ParsedTree>(result).nodes.empty());
}

// -----------------------------------------------------------------------------
// 24. DSL: source with only comments and blank lines → empty tree.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, CommentsOnlyProducesEmptyTree)
{
    constexpr std::string_view kSource =
        "# This is a comment\n"
        "\n"
        "   # Another comment\n";
    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParsedTree>(result));
    EXPECT_TRUE(std::get<ParsedTree>(result).nodes.empty());
}

// -----------------------------------------------------------------------------
// 25. DSL: multi-TEXT lines on one node are joined with '\n'.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, MultiTextLinesJoinedWithNewline)
{
    constexpr std::string_view kSource =
        "NODE n1 SPEAKER A\n"
        "TEXT Line one.\n"
        "TEXT Line two.\n"
        "TEXT Line three.\n"
        "END\n";

    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParsedTree>(result));
    const auto& nodes = std::get<ParsedTree>(result).nodes;
    ASSERT_EQ(nodes.size(), 1U);
    EXPECT_EQ(nodes[0].text, "Line one.\nLine two.\nLine three.");
}

// -----------------------------------------------------------------------------
// 26. DSL: CRLF line endings are handled transparently.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, CrlfLineEndingsAccepted)
{
    // Embed '\r\n' endings manually.
    const std::string kSource =
        "NODE intro SPEAKER NPC\r\n"
        "TEXT Hello.\r\n"
        "END\r\n";

    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParsedTree>(result));
    const auto& nodes = std::get<ParsedTree>(result).nodes;
    ASSERT_EQ(nodes.size(), 1U);
    EXPECT_EQ(nodes[0].id,     "intro");
    EXPECT_EQ(nodes[0].speaker,"NPC");
    EXPECT_EQ(nodes[0].text,   "Hello.");
}

// -----------------------------------------------------------------------------
// 27. DSL: inline comment stripping on TEXT and CHOICE lines.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, InlineCommentsStripped)
{
    constexpr std::string_view kSource =
        "NODE intro # node comment\n"
        "TEXT Hello. # inline comment on text\n"
        "END\n";

    // strip_comment fires first: 'NODE intro # node comment' becomes
    // 'NODE intro ', then trim -> 'NODE intro'. Id = "intro", no SPEAKER suffix.
    // 'TEXT Hello. # inline comment on text' becomes 'TEXT Hello. ', trim -> 'TEXT Hello.'.
    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParsedTree>(result));
    const auto& nodes = std::get<ParsedTree>(result).nodes;
    ASSERT_EQ(nodes.size(), 1U);
    EXPECT_EQ(nodes[0].id, "intro");
    EXPECT_EQ(nodes[0].text, "Hello.");
}

// -----------------------------------------------------------------------------
// 28. DSL: a node without explicit END is implicitly committed when the next
//     NODE statement opens (or at end-of-source).
// -----------------------------------------------------------------------------
TEST(DialogueDsl, ImplicitEndOnNextNode)
{
    constexpr std::string_view kSource =
        "NODE a\n"
        "TEXT First.\n"
        "NODE b\n"   // no END before this — should auto-commit 'a'
        "TEXT Second.\n";

    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParsedTree>(result));
    const auto& nodes = std::get<ParsedTree>(result).nodes;
    ASSERT_EQ(nodes.size(), 2U);
    EXPECT_EQ(nodes[0].id, "a");
    EXPECT_EQ(nodes[0].text, "First.");
    EXPECT_EQ(nodes[1].id, "b");
    EXPECT_EQ(nodes[1].text, "Second.");
}

// -----------------------------------------------------------------------------
// 29. DSL: duplicate node id in parsed output is caught by load_tree.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, DuplicateNodeIdCaughtByLoadTree)
{
    constexpr std::string_view kSource =
        "NODE a\n"
        "TEXT First.\n"
        "END\n"
        "NODE a\n"   // duplicate
        "TEXT Second.\n"
        "END\n";

    auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParsedTree>(result));
    auto nodes = std::move(std::get<ParsedTree>(result).nodes);
    ASSERT_EQ(nodes.size(), 2U);

    // load_tree must reject the duplicate.
    DialogueVM vm;
    EXPECT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kDuplicateNodeId);
    EXPECT_FALSE(vm.is_loaded());
}

// -----------------------------------------------------------------------------
// 30. Cyclic node references: A→B→A loop terminates only when the user stops.
//     select_choice must keep returning kAdvanced each time, not crash.
// -----------------------------------------------------------------------------
TEST(DialogueVm, CyclicNodeRefLoop)
{
    std::vector<DialogueNode> nodes;
    {
        DialogueNode a;
        a.id = "A";
        a.text = "At A.";
        DialogueChoice c;
        c.id = "go";
        c.text = "Go to B";
        c.next_node = "B";
        a.choices.push_back(std::move(c));
        nodes.push_back(std::move(a));
    }
    {
        DialogueNode b;
        b.id = "B";
        b.text = "At B.";
        DialogueChoice c;
        c.id = "back";
        c.text = "Go back to A";
        c.next_node = "A";
        b.choices.push_back(std::move(c));
        nodes.push_back(std::move(b));
    }

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);

    // Loop 5 times: A->B->A->B->A->B.
    for (int i = 0; i < 5; ++i)
    {
        const std::string expected_before = (i % 2 == 0) ? "A" : "B";
        const std::string choice_id       = (i % 2 == 0) ? "go" : "back";
        const std::string expected_after  = (i % 2 == 0) ? "B" : "A";
        EXPECT_EQ(vm.current_node_id(), expected_before);
        EXPECT_EQ(vm.select_choice(choice_id), SelectResult::kAdvanced);
        EXPECT_EQ(vm.current_node_id(), expected_after);
    }
}

// -----------------------------------------------------------------------------
// 31. Deep branching chain: 10-node linear chain traversed end-to-end.
// -----------------------------------------------------------------------------
TEST(DialogueVm, DeepBranchingChain)
{
    constexpr int kDepth = 10;
    std::vector<DialogueNode> nodes;
    nodes.reserve(static_cast<std::size_t>(kDepth));

    for (int i = 0; i < kDepth; ++i)
    {
        DialogueNode n;
        n.id   = "n" + std::to_string(i);
        n.text = "Node " + std::to_string(i);
        if (i < kDepth - 1)
        {
            DialogueChoice c;
            c.id        = "next";
            c.next_node = "n" + std::to_string(i + 1);
            n.choices.push_back(std::move(c));
        }
        nodes.push_back(std::move(n));
    }

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);
    EXPECT_EQ(vm.node_count(), static_cast<std::size_t>(kDepth));

    for (int i = 0; i < kDepth - 1; ++i)
    {
        EXPECT_EQ(vm.current_node_id(), "n" + std::to_string(i));
        EXPECT_EQ(vm.select_choice("next"), SelectResult::kAdvanced);
    }
    EXPECT_EQ(vm.current_node_id(), "n" + std::to_string(kDepth - 1));
    EXPECT_TRUE(vm.is_at_end());
}

// -----------------------------------------------------------------------------
// 32. Condition lambda that throws: exception propagates out of available_choices.
//     (The VM must NOT silently swallow exceptions — catch(...) is banned.)
// -----------------------------------------------------------------------------
TEST(DialogueVm, ThrowingConditionPropagates)
{
    std::vector<DialogueNode> nodes;
    {
        DialogueNode n;
        n.id = "intro";
        DialogueChoice c;
        c.id        = "bad";
        c.condition = [](const Blackboard&) -> bool {
            throw std::runtime_error("condition exploded");
        };
        c.next_node = "end";
        n.choices.push_back(std::move(c));
        nodes.push_back(std::move(n));
    }
    nodes.push_back(DialogueNode {"end", "", "", {}});

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);

    const Blackboard bb;
    EXPECT_THROW((void)vm.available_choices(bb), std::runtime_error);
}

// -----------------------------------------------------------------------------
// 33. Condition lambda that throws on select_choice propagates.
// -----------------------------------------------------------------------------
TEST(DialogueVm, ThrowingConditionOnSelectPropagates)
{
    std::vector<DialogueNode> nodes;
    {
        DialogueNode n;
        n.id = "intro";
        DialogueChoice c;
        c.id        = "bad";
        c.condition = [](const Blackboard&) -> bool {
            throw std::runtime_error("select exploded");
        };
        c.next_node = "end";
        n.choices.push_back(std::move(c));
        nodes.push_back(std::move(n));
    }
    nodes.push_back(DialogueNode {"end", "", "", {}});

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);

    const Blackboard bb;
    EXPECT_THROW(vm.select_choice("bad", bb), std::runtime_error);
}

// -----------------------------------------------------------------------------
// 34. reset() after empty-next-node termination restores the start node.
//     (current_id_ is cleared by the termination path; reset re-seeds it.)
// -----------------------------------------------------------------------------
TEST(DialogueVm, ResetAfterEmptyNextNodeTermination)
{
    std::vector<DialogueNode> nodes;
    {
        DialogueNode n;
        n.id   = "intro";
        n.text = "Start.";
        DialogueChoice c;
        c.id        = "stop";
        c.next_node = "";  // terminal
        n.choices.push_back(std::move(c));
        nodes.push_back(std::move(n));
    }

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);

    EXPECT_EQ(vm.select_choice("stop"), SelectResult::kAdvanced);
    EXPECT_EQ(vm.current_node(), nullptr);
    // is_at_end() returns false when current_node() is nullptr (documented contract).
    EXPECT_FALSE(vm.is_at_end());

    vm.reset();
    ASSERT_NE(vm.current_node(), nullptr);
    EXPECT_EQ(vm.current_node_id(), "intro");
    EXPECT_FALSE(vm.is_at_end());
}

// -----------------------------------------------------------------------------
// 35. select_choice with null blackboard skips condition re-evaluation even
//     for conditional choices (the UI has already pre-filtered).
// -----------------------------------------------------------------------------
TEST(DialogueVm, NullBlackboardSkipsConditionCheck)
{
    std::vector<DialogueNode> nodes;
    {
        DialogueNode n;
        n.id = "intro";
        DialogueChoice c;
        c.id        = "secret";
        c.next_node = "end";
        c.condition = [](const Blackboard& bb) {
            return bb.get_bool("has_key");  // would return false for empty bb
        };
        n.choices.push_back(std::move(c));
        nodes.push_back(std::move(n));
    }
    nodes.push_back(DialogueNode {"end", "", "", {}});

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);

    // Passing nullptr bypasses the condition check — advances unconditionally.
    EXPECT_EQ(vm.select_choice("secret", nullptr), SelectResult::kAdvanced);
    EXPECT_EQ(vm.current_node_id(), "end");
}

// -----------------------------------------------------------------------------
// 36. all_choices() and available_choices() on unloaded VM return empty vectors.
// -----------------------------------------------------------------------------
TEST(DialogueVm, QueriesOnUnloadedVmReturnEmpty)
{
    DialogueVM vm;
    EXPECT_TRUE(vm.all_choices().empty());
    EXPECT_TRUE(vm.available_choices(Blackboard {}).empty());
    EXPECT_EQ(vm.current_speaker(), std::string_view {});
}

// -----------------------------------------------------------------------------
// 37. start_node_id() reflects the configured start after load.
// -----------------------------------------------------------------------------
TEST(DialogueVm, StartNodeIdAccessor)
{
    DialogueVM vm;
    EXPECT_TRUE(vm.start_node_id().empty());  // before load

    ASSERT_EQ(vm.load_tree(make_branching_tree()), LoadResult::kOk);
    EXPECT_EQ(vm.start_node_id(), "intro");

    // With explicit start override.
    DialogueVM vm2;
    ASSERT_EQ(vm2.load_tree(make_branching_tree(), "nodeY"), LoadResult::kOk);
    EXPECT_EQ(vm2.start_node_id(), "nodeY");
}

// -----------------------------------------------------------------------------
// 38. load_tree a second time replaces the previous tree entirely.
// -----------------------------------------------------------------------------
TEST(DialogueVm, ReloadReplacesTree)
{
    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(make_linear_tree()), LoadResult::kOk);
    EXPECT_EQ(vm.node_count(), 2U);
    EXPECT_EQ(vm.current_node_id(), "intro");

    // Advance to terminal node.
    EXPECT_EQ(vm.select_choice("go"), SelectResult::kAdvanced);
    EXPECT_EQ(vm.current_node_id(), "end");

    // Replace with the branching tree.
    ASSERT_EQ(vm.load_tree(make_branching_tree()), LoadResult::kOk);
    EXPECT_EQ(vm.node_count(), 3U);
    EXPECT_EQ(vm.current_node_id(), "intro");
    EXPECT_FALSE(vm.is_at_end());

    // Failed reload leaves VM empty.
    EXPECT_EQ(vm.load_tree({}), LoadResult::kEmptyTree);
    EXPECT_FALSE(vm.is_loaded());
    EXPECT_EQ(vm.current_node(), nullptr);
}

// -----------------------------------------------------------------------------
// 39. Blackboard: all typed accessors with missing keys return their fallback.
// -----------------------------------------------------------------------------
TEST(BlackboardEdge, MissingKeyReturnsFallback)
{
    Blackboard bb;
    EXPECT_EQ(bb.get_bool("x",    true),  true);
    EXPECT_EQ(bb.get_int("x",     42),    42);
    EXPECT_FLOAT_EQ(bb.get_float("x", 3.14F), 3.14F);
    EXPECT_EQ(bb.get_string("x", "def"), "def");
    EXPECT_FALSE(bb.has("x"));
    EXPECT_EQ(bb.size(), 0U);
}

// -----------------------------------------------------------------------------
// 40. Blackboard: wrong-type access returns fallback (type mismatch).
// -----------------------------------------------------------------------------
TEST(BlackboardEdge, WrongTypeReturnsFallback)
{
    Blackboard bb;
    bb.set_bool("flag", true);
    // Access same key as int → fallback (holds bool, not int).
    EXPECT_EQ(bb.get_int("flag", 99), 99);
    EXPECT_FLOAT_EQ(bb.get_float("flag", 7.0F), 7.0F);
    EXPECT_EQ(bb.get_string("flag", "fb"), "fb");
}

// -----------------------------------------------------------------------------
// 41. Blackboard: erase removes a key; clear empties all.
// -----------------------------------------------------------------------------
TEST(BlackboardEdge, EraseAndClear)
{
    Blackboard bb;
    bb.set_int("a", 1);
    bb.set_int("b", 2);
    EXPECT_EQ(bb.size(), 2U);
    bb.erase("a");
    EXPECT_FALSE(bb.has("a"));
    EXPECT_EQ(bb.size(), 1U);
    bb.clear();
    EXPECT_EQ(bb.size(), 0U);
    EXPECT_FALSE(bb.has("b"));
}

// -----------------------------------------------------------------------------
// 42. Blackboard: set(Value) type-erased overload covers all variant legs.
// -----------------------------------------------------------------------------
TEST(BlackboardEdge, TypeErasedSetValue)
{
    Blackboard bb;
    bb.set("b", Blackboard::Value{true});
    bb.set("i", Blackboard::Value{7});
    bb.set("f", Blackboard::Value{1.5F});
    bb.set("s", Blackboard::Value{std::string{"hello"}});
    EXPECT_TRUE(bb.get_bool("b"));
    EXPECT_EQ(bb.get_int("i"), 7);
    EXPECT_FLOAT_EQ(bb.get_float("f"), 1.5F);
    EXPECT_EQ(bb.get_string("s"), "hello");
}

// -----------------------------------------------------------------------------
// 43. Empty dialogue (zero choices, empty text) is a valid terminal node.
// -----------------------------------------------------------------------------
TEST(DialogueVm, EmptyDialogueSingleNodeTerminal)
{
    std::vector<DialogueNode> nodes;
    nodes.push_back(DialogueNode {"only", "", "", {}});

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);
    ASSERT_NE(vm.current_node(), nullptr);
    EXPECT_EQ(vm.current_node_id(), "only");
    EXPECT_TRUE(vm.is_at_end());
    EXPECT_EQ(vm.select_choice("x"), SelectResult::kAtEnd);
    EXPECT_TRUE(vm.all_choices().empty());
    EXPECT_TRUE(vm.available_choices(Blackboard{}).empty());
}

// -----------------------------------------------------------------------------
// 44. DSL: TEXT with empty body (TEXT followed by nothing) stores empty string.
// -----------------------------------------------------------------------------
TEST(DialogueDsl, EmptyTextBody)
{
    constexpr std::string_view kSource =
        "NODE n\n"
        "TEXT\n"   // keyword only, no body after it
        "END\n";

    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParsedTree>(result));
    const auto& nodes = std::get<ParsedTree>(result).nodes;
    ASSERT_EQ(nodes.size(), 1U);
    // match_kw("TEXT", "TEXT") succeeds with rest="" when the line is exactly "TEXT"
    EXPECT_EQ(nodes[0].text, "");
}

// -----------------------------------------------------------------------------
// 45. DSL: CHOICE with empty prompt text is allowed (no error, empty string).
// -----------------------------------------------------------------------------
TEST(DialogueDsl, ChoiceEmptyPromptAllowed)
{
    constexpr std::string_view kSource =
        "NODE n\n"
        "CHOICE c -> dest :\n"   // colon present, nothing after it
        "END\n";

    const auto result = cd::game::dialogue::parse_tree(kSource);
    ASSERT_TRUE(std::holds_alternative<ParsedTree>(result));
    const auto& nodes = std::get<ParsedTree>(result).nodes;
    ASSERT_EQ(nodes.size(), 1U);
    ASSERT_EQ(nodes[0].choices.size(), 1U);
    EXPECT_EQ(nodes[0].choices[0].id, "c");
    EXPECT_EQ(nodes[0].choices[0].next_node, "dest");
    EXPECT_EQ(nodes[0].choices[0].text, "");
}

// -----------------------------------------------------------------------------
// 46. kBrokenLink: current_node_id is preserved (VM does not advance).
// -----------------------------------------------------------------------------
TEST(DialogueVm, BrokenLinkPreservesCurrentNode)
{
    std::vector<DialogueNode> nodes;
    {
        DialogueNode n;
        n.id = "start";
        // good choice and broken choice both present
        DialogueChoice good;
        good.id        = "safe";
        good.next_node = "start";  // self-loop valid
        DialogueChoice bad;
        bad.id        = "danger";
        bad.next_node = "nonexistent";
        n.choices.push_back(std::move(good));
        n.choices.push_back(std::move(bad));
        nodes.push_back(std::move(n));
    }

    DialogueVM vm;
    ASSERT_EQ(vm.load_tree(std::move(nodes)), LoadResult::kOk);
    EXPECT_EQ(vm.select_choice("danger"), SelectResult::kBrokenLink);
    EXPECT_EQ(vm.current_node_id(), "start");  // must not have moved
    // safe choice still works after a broken-link attempt
    EXPECT_EQ(vm.select_choice("safe"), SelectResult::kAdvanced);
    EXPECT_EQ(vm.current_node_id(), "start");
}

// -----------------------------------------------------------------------------
// 47. node_count() is zero before any successful load.
// -----------------------------------------------------------------------------
TEST(DialogueVm, NodeCountZeroBeforeLoad)
{
    DialogueVM vm;
    EXPECT_EQ(vm.node_count(), 0U);
    EXPECT_EQ(vm.load_tree({}), LoadResult::kEmptyTree);
    EXPECT_EQ(vm.node_count(), 0U);
}
