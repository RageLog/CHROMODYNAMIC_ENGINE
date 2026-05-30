// =============================================================================
// CHROMODYNAMIC - samples/game/hello_narrative
// Phase 505 (G4.4 - closes Phase G4).
//
// Demonstrates the Wave-2 gameplay narrative stack end-to-end:
//   * cd::game::dialogue   - branching dialogue VM (G4.1, phase 499)
//   * cd::game::quest      - QuestLog + objective tracker     (G4.2, phase 500)
//   * cd::game::l10n       - StringTable + CLDR plurals + RTL (G4.3, phase 501)
//
// The story:
//   A villager NPC offers the player a small fetch quest ("Find 3 apples").
//   The player can answer Yes or No:
//     * Yes -> the QuestLog activates "find_apples" with one Objective whose
//              target is 3. The objective progress ticks up while the quest
//              is active, and the per-frame status banner pluralises the
//              remaining-apples line via L10nManager::get_plural.
//     * No  -> the dialogue ends on the "Maybe next time" speech and the
//              QuestLog is left empty for that pass.
//
// Every 5 s of sim-time the locale rotates en -> tr -> ja -> en. The same
// dialogue tree and the same QuestLog are reused -- only the rendered text
// changes. The is_rtl() helper is still exercised on every code path so a
// future RTL locale (Arabic / Hebrew / Persian) drops in via one .kv file.
// Japanese is LTR; its CLDR plural rule is "other only" so the get_plural
// path falls back to apples.other for every n.
//
// The sample is a headless console program (no window / no Vulkan). It runs
// for a fixed wall-clock budget (default ~15.5 s of sim-time = three locale
// passes); passing any argv stretches the budget for live demo use.
//
// Dependencies (CLAUDE.md S7): cd::core, cd::game_dialogue, cd::game_quest,
// cd::game_l10n.
// =============================================================================
#include <cd/core/Defines.hpp>
#include <cd/game/dialogue/Dialogue.hpp>
#include <cd/game/l10n/L10n.hpp>
#include <cd/game/quest/Quest.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

// -----------------------------------------------------------------------------
// Stable keys -- referenced from both code and the .kv tables. Keeping them
// here (rather than as inline string literals) makes them easy to grep when
// adding new locales.
// -----------------------------------------------------------------------------
constexpr const char* kNodeIntro         = "intro";
constexpr const char* kNodeAccept        = "accept";
constexpr const char* kNodeDecline       = "decline";

constexpr const char* kChoiceYes         = "yes";
constexpr const char* kChoiceNo          = "no";

constexpr const char* kQuestId           = "find_apples";
constexpr const char* kObjectiveId       = "apples";
constexpr std::int32_t kAppleTarget      = 3;

// L10n keys for dialogue copy ------------------------------------------------
constexpr const char* kKeyIntroSpeaker   = "intro.speaker";
constexpr const char* kKeyIntroText      = "intro.text";
constexpr const char* kKeyIntroChoiceYes = "intro.choice.yes";
constexpr const char* kKeyIntroChoiceNo  = "intro.choice.no";

constexpr const char* kKeyAcceptSpeaker  = "accept.speaker";
constexpr const char* kKeyAcceptText     = "accept.text";

constexpr const char* kKeyDeclineSpeaker = "decline.speaker";
constexpr const char* kKeyDeclineText    = "decline.text";

// L10n keys for quest journal copy + status banner --------------------------
constexpr const char* kKeyQuestTitle     = "quest.title";
constexpr const char* kKeyQuestDesc      = "quest.description";
constexpr const char* kKeyQuestReward    = "quest.reward";
constexpr const char* kKeyApples         = "apples"; // pluralised: apples.one / .other / ...

constexpr const char* kKeyStatusActive   = "quest.status.active";
constexpr const char* kKeyStatusComplete = "quest.status.completed";
constexpr const char* kKeyStatusFailed   = "quest.status.failed";
constexpr const char* kKeyStatusInactive = "quest.status.inactive";

// -----------------------------------------------------------------------------
// Locate the locales/ directory regardless of which working directory the
// binary was launched from. The CMake POST_BUILD step stages the folder next
// to the executable, but a developer running from the repo root should also
// just work.
// -----------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path resolve_locales_dir()
{
    namespace fs = std::filesystem;
    const std::array<fs::path, 3> candidates {
        fs::path {"locales"},
        fs::path {"samples"} / "game" / "hello_narrative" / "locales",
        fs::path {".."} / "samples" / "game" / "hello_narrative" / "locales",
    };
    for (const auto& c : candidates)
    {
        if (fs::exists(c / "en.kv"))
        {
            return c;
        }
    }
    return candidates[0]; // best-effort; load() will report kFileNotFound
}

// -----------------------------------------------------------------------------
// Build the dialogue graph. Node body text is just the L10n key; UI code
// resolves the key against the active locale at render time. This is the
// standard "text-id authoring" pattern from Yarn Spinner / Ink / Fluent --
// content authors edit .kv files, code holds stable ids.
// -----------------------------------------------------------------------------
[[nodiscard]] std::vector<cd::game::dialogue::DialogueNode> build_dialogue_tree()
{
    using namespace cd::game::dialogue;

    std::vector<DialogueNode> nodes;

    // -- intro node ---------------------------------------------------------
    DialogueNode intro;
    intro.id      = kNodeIntro;
    intro.speaker = kKeyIntroSpeaker;
    intro.text    = kKeyIntroText;
    intro.choices.push_back(DialogueChoice {
        kChoiceYes, kKeyIntroChoiceYes, {}, kNodeAccept});
    intro.choices.push_back(DialogueChoice {
        kChoiceNo,  kKeyIntroChoiceNo,  {}, kNodeDecline});
    nodes.push_back(std::move(intro));

    // -- accept terminal ----------------------------------------------------
    DialogueNode accept;
    accept.id      = kNodeAccept;
    accept.speaker = kKeyAcceptSpeaker;
    accept.text    = kKeyAcceptText;
    nodes.push_back(std::move(accept));

    // -- decline terminal ---------------------------------------------------
    DialogueNode decline;
    decline.id      = kNodeDecline;
    decline.speaker = kKeyDeclineSpeaker;
    decline.text    = kKeyDeclineText;
    nodes.push_back(std::move(decline));

    return nodes;
}

// -----------------------------------------------------------------------------
// Print one dialogue node + its current choices, resolving every id through
// the L10n manager so the same code path renders en, tr and ar identically.
// -----------------------------------------------------------------------------
void print_dialogue_node(const cd::game::dialogue::DialogueNode&      node,
                         const std::vector<const cd::game::dialogue::DialogueChoice*>& choices,
                         const cd::game::l10n::L10nManager&            l10n,
                         std::string_view                              locale)
{
    const char* dir = cd::game::l10n::is_rtl(locale) ? "[RTL] " : "";
    const std::string speaker = l10n.get(node.speaker);
    const std::string text    = l10n.get(node.text);
    std::printf("  %s%s: \"%s\"\n", dir, speaker.c_str(), text.c_str());

    if (choices.empty())
    {
        std::printf("    (end of conversation)\n");
        return;
    }
    std::printf("    Choices:\n");
    for (const auto* c : choices)
    {
        const std::string prompt = l10n.get(c->text);
        std::printf("      [%s] %s\n", c->id.c_str(), prompt.c_str());
    }
}

// -----------------------------------------------------------------------------
// Render a one-line journal status for the apple quest.
// Active   -> "Active: <title> - <plural_status>"
// Complete -> "Completed: <title> -> <reward>"
// Inactive -> "Inactive: <title>"  (e.g. after a "No" branch)
// -----------------------------------------------------------------------------
void print_quest_status(const cd::game::quest::QuestLog&  log,
                        const cd::game::l10n::L10nManager& l10n,
                        std::string_view                   locale)
{
    using cd::game::quest::QuestStatus;

    const auto* q = log.find_quest(kQuestId);
    if (q == nullptr)
    {
        std::printf("  [journal] (no quest)\n");
        return;
    }

    const char* dir = cd::game::l10n::is_rtl(locale) ? "[RTL] " : "";
    const std::string title = l10n.get(kKeyQuestTitle);

    switch (q->status)
    {
        case QuestStatus::kActive:
        {
            // Find the apples objective, pluralise its remaining count.
            std::int32_t remaining = kAppleTarget;
            for (const auto& obj : q->objectives)
            {
                if (obj.id == kObjectiveId)
                {
                    remaining = obj.target - obj.progress;
                    if (remaining < 0) { remaining = 0; }
                }
            }
            const std::string status   = l10n.get(kKeyStatusActive);
            const std::string apples   = l10n.get_plural(kKeyApples, remaining);
            std::printf("  %s[journal] %s: %s - %s\n",
                        dir, status.c_str(), title.c_str(), apples.c_str());
            break;
        }
        case QuestStatus::kComplete:
        {
            const std::string status = l10n.get(kKeyStatusComplete);
            const std::string reward = l10n.get(kKeyQuestReward);
            std::printf("  %s[journal] %s: %s -> %s\n",
                        dir, status.c_str(), title.c_str(), reward.c_str());
            break;
        }
        case QuestStatus::kFailed:
        {
            const std::string status = l10n.get(kKeyStatusFailed);
            std::printf("  %s[journal] %s: %s\n",
                        dir, status.c_str(), title.c_str());
            break;
        }
        case QuestStatus::kInactive:
        {
            const std::string status = l10n.get(kKeyStatusInactive);
            std::printf("  %s[journal] %s: %s\n",
                        dir, status.c_str(), title.c_str());
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// One narrative pass under the currently active locale:
//   1. reset the dialogue VM to the intro node
//   2. print the intro + choices, pick "Yes" or "No" deterministically
//   3. advance, print the terminal speech, and mirror the choice in the
//      QuestLog (activate find_apples on Yes; leave it untouched on No)
//   4. simulate three apple pickups so plural rendering exercises both
//      n=3 -> n=2 -> n=1 -> n=0 and the auto-complete edge.
//   5. print the final journal banner
//
// Pass index 0/3/... -> Yes branch; 1/4/... -> No branch; this way every
// locale rotation surfaces both branches at least once across the run.
// -----------------------------------------------------------------------------
void run_pass(std::size_t                       pass_index,
              cd::game::dialogue::DialogueVM&   vm,
              cd::game::quest::QuestLog&        log,
              const cd::game::l10n::L10nManager& l10n,
              std::string_view                   locale)
{
    using namespace cd::game::dialogue;

    std::printf("---- pass %zu | locale = '%.*s' ----\n",
                pass_index,
                static_cast<int>(locale.size()),
                locale.data());

    // The QuestLog is reset every pass so that each locale's Yes branch
    // re-exercises the get_plural() path on the same n=3 -> 0 traversal.
    // Restoring an empty serialised blob is the documented "wipe to a
    // known-good state" recipe for QuestLog.
    log.restore(std::span<const std::byte> {});

    vm.reset();
    const Blackboard bb {}; // unused -- all choices are unconditional.

    // Step 1: intro.
    {
        const auto* node = vm.current_node();
        const auto  vis  = vm.available_choices(bb);
        if (node != nullptr)
        {
            print_dialogue_node(*node, vis, l10n, locale);
        }
    }

    // Step 2: pick a choice. Alternate Yes / No so both branches fire across
    // the rotation. A real UI would source this from input; the brief calls
    // for an automated demo so we drive it from pass_index.
    const bool say_yes = ((pass_index % 2U) == 0U);
    const std::string_view picked = say_yes ? kChoiceYes : kChoiceNo;
    std::printf("    -> player picks [%.*s]\n",
                static_cast<int>(picked.size()), picked.data());
    const SelectResult sr = vm.select_choice(picked);
    if (sr != SelectResult::kAdvanced)
    {
        std::printf("    !! dialogue advance failed (sr=%d)\n", static_cast<int>(sr));
    }

    // Step 3: terminal speech.
    {
        const auto* node = vm.current_node();
        const auto  vis  = vm.available_choices(bb);
        if (node != nullptr)
        {
            print_dialogue_node(*node, vis, l10n, locale);
        }
    }

    // Step 4: if Yes, activate the quest (idempotent across passes: the
    // QuestLog rejects a re-add of the same id, and activate() is a no-op
    // once active). Then simulate three pickups to drive the plural display
    // from "3 apples remaining" through to "0 apples remaining" -> completed.
    if (say_yes)
    {
        if (log.find_quest(kQuestId) == nullptr)
        {
            cd::game::quest::Quest q;
            q.id          = kQuestId;
            q.title       = kKeyQuestTitle;        // l10n keys; UI resolves them
            q.description = kKeyQuestDesc;
            q.reward      = kKeyQuestReward;
            q.objectives.push_back(cd::game::quest::Objective {
                kObjectiveId,
                kKeyApples,
                cd::game::quest::ObjectiveStatus::kInactive,
                0,
                kAppleTarget,
            });
            const auto ar = log.add_quest(std::move(q));
            if (ar != cd::game::quest::AddResult::kOk)
            {
                std::printf("    !! quest add failed (ar=%d)\n", static_cast<int>(ar));
            }
        }
        // Activate only if inactive (otherwise activate() returns kAlreadyActive
        // / kAlreadyCompleted which we treat as no-ops by design).
        if (const auto* qptr = log.find_quest(kQuestId);
            qptr != nullptr
            && qptr->status == cd::game::quest::QuestStatus::kInactive)
        {
            const auto mr = log.activate(kQuestId);
            if (mr != cd::game::quest::MutateResult::kOk)
            {
                std::printf("    !! quest activate failed (mr=%d)\n",
                            static_cast<int>(mr));
            }
        }

        // Print the journal once at the start of this pass.
        print_quest_status(log, l10n, locale);

        // Drive three apple pickups -- only if the quest is still active.
        for (std::int32_t i = 0; i < kAppleTarget; ++i)
        {
            if (const auto* qptr = log.find_quest(kQuestId);
                qptr == nullptr
                || qptr->status != cd::game::quest::QuestStatus::kActive)
            {
                break;
            }
            const auto mr = log.progress(kQuestId, kObjectiveId, 1);
            if (mr != cd::game::quest::MutateResult::kOk)
            {
                std::printf("    !! progress failed (mr=%d)\n",
                            static_cast<int>(mr));
                break;
            }
            print_quest_status(log, l10n, locale);
        }
    }
    else
    {
        // No-branch: still show the journal so the operator can verify the
        // quest stays untouched.
        print_quest_status(log, l10n, locale);
    }
}

} // namespace

// =============================================================================
// main - drive the rotation. Argv is parsed only as a "live demo" flag:
//   no argv -> short deterministic run (3 passes = en, tr, ar)
//   any argv -> extended run (6 passes -> covers Yes+No for each locale)
// =============================================================================
int main(int argc, char* argv[])
{
    static_cast<void>(argv); // only argc is consulted (live-demo flag).
    namespace fs = std::filesystem;
    using cd::game::dialogue::DialogueVM;
    using cd::game::dialogue::LoadResult;
    using cd::game::l10n::L10nManager;
    using cd::game::l10n::StringTable;
    using cd::game::quest::QuestLog;

    const bool extended = (argc > 1);
    const std::size_t pass_count = extended ? 6U : 3U;

    std::printf("hello_narrative (Phase 505 / G4.4) - branching dialogue + quest\n");
    std::printf("          locales rotating en -> tr -> ja every 5 s sim-time\n");
    std::printf("          run mode: %s (%zu passes)\n",
                extended ? "extended" : "deterministic", pass_count);

    // -- L10n: load three tables ----------------------------------------------
    L10nManager l10n;
    l10n.set_base_locale("en");
    const fs::path locales_dir = resolve_locales_dir();
    std::printf("locales dir: %s\n", locales_dir.string().c_str());

    // phase510 — ar -> ja swap per user request ("japonca ve japon
    // harfleriyle olsun"). Japanese is LTR (is_rtl("ja") returns false)
    // and its CLDR plural rule is "other only" so get_plural falls back
    // to apples.other for every n. The is_rtl branch below stays so a
    // future RTL locale (.kv file under locales/) drops in without code.
    const std::array<std::string_view, 3> locale_codes {"en", "tr", "ja"};
    for (const auto code : locale_codes)
    {
        StringTable tbl;
        const std::string code_str {code};
        const fs::path    path = locales_dir / (code_str + ".kv");
        const auto lr = tbl.load(path.string(), code_str);
        if (lr != cd::game::l10n::LoadResult::kOk)
        {
            std::fprintf(stderr,
                         "FATAL: failed to load locale '%s' from '%s' (lr=%d)\n",
                         code_str.c_str(), path.string().c_str(),
                         static_cast<int>(lr));
            return EXIT_FAILURE;
        }
        std::printf("  loaded locale %s (%zu entries)%s\n",
                    code_str.c_str(), tbl.size(),
                    cd::game::l10n::is_rtl(code) ? " [RTL]" : "");
        l10n.add_locale(std::move(tbl));
    }
    l10n.set_locale("en");

    // Observer hook -- a stand-in for what a UI layer would wire up to
    // re-flow paragraphs / re-shape glyph runs on language switch.
    l10n.on_locale_change([](const std::string& code)
    {
        std::printf("[l10n] active locale switched to '%s'%s\n",
                    code.c_str(),
                    cd::game::l10n::is_rtl(code) ? " (RTL)" : "");
    });

    // -- Dialogue VM ---------------------------------------------------------
    DialogueVM vm;
    if (const auto lr = vm.load_tree(build_dialogue_tree());
        lr != LoadResult::kOk)
    {
        std::fprintf(stderr,
                     "FATAL: failed to load dialogue tree (lr=%d)\n",
                     static_cast<int>(lr));
        return EXIT_FAILURE;
    }

    // -- QuestLog ------------------------------------------------------------
    QuestLog log;

    // -- Rotation loop -------------------------------------------------------
    // sim_time advances in 5 s steps; each step changes the active locale
    // before running the next narrative pass.
    constexpr float kStepSeconds = 5.0F;
    float           sim_time     = 0.0F;
    for (std::size_t i = 0; i < pass_count; ++i)
    {
        const std::string_view code = locale_codes[i % locale_codes.size()];
        const std::string      code_str {code};
        l10n.set_locale(code_str);
        std::printf("[sim] t=%.1fs, switching locale to '%s'\n",
                    static_cast<double>(sim_time), code_str.c_str());
        run_pass(i, vm, log, l10n, code);
        sim_time += kStepSeconds;
    }

    std::printf("---- final journal snapshot ----\n");
    print_quest_status(log, l10n, l10n.current_locale());

    std::printf("\nhello_narrative done.\n");
    return EXIT_SUCCESS;
}
