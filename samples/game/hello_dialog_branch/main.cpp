// =============================================================================
// CHROMODYNAMIC — samples/game/hello_dialog_branch/main.cpp
// Phase 652  (M10 W5A) — hello_dialog_branch
//
// PURPOSE
// -------
// Demonstrate cd::game::dialog_tree: a BG3-style directed graph of typed
// nodes that a designer authors as plain data and the runtime walks with
// player choices and boolean condition variables — without recompiling.
//
// TREE LAYOUT  (7 nodes)
// ----------------------
//
//                           [intro]  kSay
//                              |
//                         [choice1]  kChoice
//                        /    |    \
//              (0) "Yes" /    |     \ (2) "Tell me about the dragon"
//                       /    |(1)    \
//              [help_branch]  |    [dragon_branch]  kCondition
//                  kSay       |     var='player_completed_quest_1'
//                    \   [leave_branch]     /         \
//                     \      kSay       true:          false:
//                      \       \   [dragon_secret]  [dragon_locked]
//                       \  [leave_end]   kSay            kSay
//                        \   kEnd         \                \
//                         \            [goodbye]        [goodbye]
//                          \-----------> kEnd  <----------/
//
// THREE SIMULATED PLAY-THROUGHS
// --------------------------------
//  Play 1  — Player picks "Yes"           → help_branch      → goodbye
//  Play 2  — Player picks "No, leave me alone" → leave_branch → leave_end
//  Play 3  — Player picks "Tell me about the dragon"
//              quest_1 NOT completed  → dragon_locked → goodbye
//  (Bonus) Play 4 same choice with quest_1 COMPLETED → dragon_secret → goodbye
//
// READING THIS FILE
// -----------------
// A designer needs only understand three things:
//   1. Build a std::vector<DialogNode> — each node is a plain struct.
//   2. Set root_id in the DialogTree to name the entry point.
//   3. Call load() → set_condition_var() → start() → advance() loop.
// That same pattern scales to a 200-node tree without touching engine code.
// =============================================================================

#include <cd/game/dialog_tree/DialogTree.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// Convenience aliases
using cd::game::dialog_tree::DialogNode;
using cd::game::dialog_tree::DialogTree;
using cd::game::dialog_tree::DialogTreeRuntime;
using cd::game::dialog_tree::NodeKind;

// =============================================================================
// build_village_elder_tree()
// =============================================================================
// Designer-authored graph as plain POD data.
// A real production pipeline would load this from JSON / binary — but the
// C++ initializer list IS the source-of-truth for this demo so the designer
// can read the intent directly without a separate data file.
// =============================================================================
static DialogTree build_village_elder_tree()
{
    // -------------------------------------------------------------------------
    // Node definitions — order in the vector is irrelevant; the runtime
    // indexes by node_id. We list them in logical conversation order so the
    // file is easy to read top-to-bottom.
    // -------------------------------------------------------------------------

    std::vector<DialogNode> nodes;

    // --- [intro] : NPC opens the conversation --------------------------------
    // kSay auto-advances to next_ids[0] without player input.
    nodes.push_back({
        .node_id  = "intro",
        .kind     = NodeKind::kSay,
        .text     = "You look lost. Need help?",
        .next_ids = { "choice1" },
    });

    // --- [choice1] : Three branches for the player to pick -------------------
    // kChoice: runtime exposes next_ids[0..2] as selectable options.
    // The text strings below are the displayed choice labels.
    nodes.push_back({
        .node_id  = "choice1",
        .kind     = NodeKind::kChoice,
        // next_ids index matches player choice index (0, 1, 2)
        .next_ids = { "help_branch", "leave_branch", "dragon_branch" },
    });

    // --- [help_branch] : Player accepted help --------------------------------
    nodes.push_back({
        .node_id  = "help_branch",
        .kind     = NodeKind::kSay,
        .text     = "I can guide you to the village.",
        .next_ids = { "goodbye" },
    });

    // --- [leave_branch] : Player dismissed the NPC ---------------------------
    nodes.push_back({
        .node_id  = "leave_branch",
        .kind     = NodeKind::kSay,
        .text     = "As you wish.",
        .next_ids = { "leave_end" },
    });

    // --- [leave_end] : Abrupt end when player leaves -------------------------
    // kEnd terminates immediately; text is ignored by the runtime but
    // useful as a designer annotation.
    nodes.push_back({
        .node_id = "leave_end",
        .kind    = NodeKind::kEnd,
        .text    = "(The elder turns away.)",
    });

    // --- [dragon_branch] : Condition gate — transparent to the player --------
    // kCondition: runtime evaluates 'player_completed_quest_1'.
    //   next_ids[0] = true  branch → dragon_secret
    //   next_ids[1] = false branch → dragon_locked
    // This node is NEVER surfaced through current() — it is pure routing.
    // NOTE: field order matches DialogNode declaration (condition_var comes
    //       after next_ids in the struct, so we list them in that order).
    nodes.push_back({
        .node_id       = "dragon_branch",
        .kind          = NodeKind::kCondition,
        .next_ids      = { "dragon_secret", "dragon_locked" },
        .condition_var = "player_completed_quest_1",
    });

    // --- [dragon_secret] : Quest completed — NPC shares the lore -------------
    nodes.push_back({
        .node_id  = "dragon_secret",
        .kind     = NodeKind::kSay,
        .text     = "The dragon sleeps in the eastern peaks.",
        .next_ids = { "goodbye" },
    });

    // --- [dragon_locked] : Quest not done — NPC refuses ----------------------
    nodes.push_back({
        .node_id  = "dragon_locked",
        .kind     = NodeKind::kSay,
        .text     = "I don't share that with strangers.",
        .next_ids = { "goodbye" },
    });

    // --- [goodbye] : Shared closing node -------------------------------------
    nodes.push_back({
        .node_id = "goodbye",
        .kind    = NodeKind::kEnd,
        .text    = "(Conversation ends.)",
    });

    return DialogTree{
        .tree_id = "village_elder",
        .root_id = "intro",
        .nodes   = std::move(nodes),
    };
}

// =============================================================================
// player_choice_labels — maps choice1's three branches to display text.
// =============================================================================
// In a real game the labels come from the kChoice node's text field or from
// a companion localisation table.  Here we hard-code them so the console
// output reads as a proper conversation.
// =============================================================================
static constexpr const char* kChoiceLabels[] = {
    "Yes, I could use some help.",
    "No, leave me alone.",
    "Tell me about the dragon.",
};

// =============================================================================
// run_playthrough()
// =============================================================================
// Drives the runtime to completion using a pre-recorded list of choices.
// Each element of synthetic_choices is consumed in order whenever the runtime
// lands on a kChoice node.
//
// The function prints each node the player experiences, mirroring what a
// real dialog UI would render on screen.
// =============================================================================
static void run_playthrough(
    const std::string& title,
    const DialogTree&  tree,
    bool               quest_1_completed,
    const std::vector<std::size_t>& synthetic_choices)
{
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "  [player_completed_quest_1 = "
              << (quest_1_completed ? "true" : "false") << "]\n";
    std::cout << "============================================================\n";

    DialogTreeRuntime rt;

    // Seed condition variables BEFORE start() so kCondition nodes route
    // correctly from the first visit.
    rt.set_condition_var("player_completed_quest_1", quest_1_completed);

    rt.load(tree);
    rt.start();

    std::size_t choice_cursor = 0;  // index into synthetic_choices

    while (!rt.is_done())
    {
        const DialogNode* node = rt.current();
        if (node == nullptr)
        {
            break;
        }

        switch (node->kind)
        {
            case NodeKind::kSay:
            {
                std::cout << "  [NPC]  " << node->text << "\n";
                rt.advance();  // auto-advance; choice_index ignored
                break;
            }

            case NodeKind::kChoice:
            {
                // Print available options — would be rendered as a dialog wheel
                // or numbered list in a real UI.
                std::cout << "  [PLAYER CHOICE]\n";
                for (std::size_t i = 0; i < std::size(kChoiceLabels); ++i)
                {
                    const bool selected =
                        (choice_cursor < synthetic_choices.size()) &&
                        (synthetic_choices[choice_cursor] == i);
                    std::cout << "    [" << i << "] "
                              << kChoiceLabels[i]
                              << (selected ? "  <-- selected" : "")
                              << "\n";
                }

                const std::size_t pick =
                    (choice_cursor < synthetic_choices.size())
                        ? synthetic_choices[choice_cursor]
                        : std::size_t{0};
                ++choice_cursor;

                std::cout << "  > Player picks [" << pick << "]: "
                          << kChoiceLabels[pick] << "\n";
                rt.advance(pick);
                break;
            }

            case NodeKind::kCondition:
            {
                // kCondition is transparent — current() never returns one.
                // This branch is dead code in correct usage; kept for
                // defensive completeness.
                rt.advance();
                break;
            }

            case NodeKind::kEnd:
            {
                // kEnd marks done_=true inside go_to, so is_done() fires
                // before we reach here. Handled defensively.
                break;
            }
        }
    }

    std::cout << "  [END]  Conversation complete.\n";
}

// =============================================================================
// main()
// =============================================================================
int main()
{
    std::cout << "hello_dialog_branch — cd::game::dialog_tree demo\n";
    std::cout << "Phase 652 / M10 W5A — BG3-style branching dialog graph\n";

    // Build the tree once; reuse across all play-throughs (the runtime copies
    // node data on load() so the source tree is never mutated).
    const DialogTree tree = build_village_elder_tree();

    // ------------------------------------------------------------------
    // Play 1: Player asks for help → help_branch → goodbye
    // ------------------------------------------------------------------
    run_playthrough(
        "Play 1 — Player asks for help",
        tree,
        /*quest_1_completed=*/ false,
        /*synthetic_choices=*/ { 0 }   // pick "Yes, I could use some help."
    );

    // ------------------------------------------------------------------
    // Play 2: Player dismisses the NPC → leave_branch → leave_end (kEnd)
    // ------------------------------------------------------------------
    run_playthrough(
        "Play 2 — Player dismisses the NPC",
        tree,
        /*quest_1_completed=*/ false,
        /*synthetic_choices=*/ { 1 }   // pick "No, leave me alone."
    );

    // ------------------------------------------------------------------
    // Play 3: Player asks about the dragon, quest NOT completed
    //         → dragon_branch (kCondition, false) → dragon_locked → goodbye
    // ------------------------------------------------------------------
    run_playthrough(
        "Play 3 — Dragon inquiry, quest NOT completed",
        tree,
        /*quest_1_completed=*/ false,
        /*synthetic_choices=*/ { 2 }   // pick "Tell me about the dragon."
    );

    // ------------------------------------------------------------------
    // Play 4 (bonus): Same dragon choice, quest IS completed
    //         → dragon_branch (kCondition, true) → dragon_secret → goodbye
    // ------------------------------------------------------------------
    run_playthrough(
        "Play 4 (bonus) — Dragon inquiry, quest COMPLETED",
        tree,
        /*quest_1_completed=*/ true,
        /*synthetic_choices=*/ { 2 }   // pick "Tell me about the dragon."
    );

    std::cout << "\n";
    std::cout << "Four play-throughs complete.\n";
    std::cout << "Same 7-node tree; four distinct stories.\n";
    std::cout << "Scale it to 200 nodes without touching engine code.\n";

    return EXIT_SUCCESS;
}
