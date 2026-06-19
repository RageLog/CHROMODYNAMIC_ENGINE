// =============================================================================
// CHROMODYNAMIC - tests/test_quest.cpp
// Phase 500 - cd::game::quest unit tests (G4.2).
//
// Covers the 8 contract requirements from the brief plus serialisation
// edge-cases:
//
//   1.  add/activate/progress/complete chain ends with quest kComplete.
//   2.  Auto-complete: quest auto-completes when all objectives done.
//   3.  Counter objective progresses toward target.
//   4.  Auto-fail: failed objective auto-fails the parent quest.
//   5.  Multiple active quests progress in parallel without cross-talk.
//   6.  Inactive quests hidden from active_quests().
//   7.  Completed quest moves from active to completed bucket.
//   8.  serialize -> restore preserves quest + objective state.
//
// Plus extras:
//   9.  add_quest validates ids (duplicate quest / objective ids rejected).
//   10. progress negative delta clamps at zero.
//   11. Restore from empty / corrupt buffer leaves the log empty.
// =============================================================================
#include <cd/game/quest/Quest.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

using cd::game::quest::AddResult;
using cd::game::quest::MutateResult;
using cd::game::quest::Objective;
using cd::game::quest::ObjectiveStatus;
using cd::game::quest::Quest;
using cd::game::quest::QuestLog;
using cd::game::quest::QuestStatus;
using cd::game::quest::RestoreResult;

// Helper: build a 2-objective quest "find the relic, return to mentor".
Quest make_relic_quest()
{
    Quest q;
    q.id          = "main_q01";
    q.title       = "The Relic of Aenor";
    q.description = "Recover the relic from the sunken vault.";
    q.reward      = "200 gold + Ring of Tides";
    {
        Objective o;
        o.id          = "find_relic";
        o.description = "Find the relic in the vault.";
        o.target      = 1;
        q.objectives.push_back(std::move(o));
    }
    {
        Objective o;
        o.id          = "return_mentor";
        o.description = "Return the relic to the mentor.";
        o.target      = 1;
        q.objectives.push_back(std::move(o));
    }
    return q;
}

// Helper: build a counter-style quest "slay 7 wolves".
Quest make_wolf_quest()
{
    Quest q;
    q.id          = "side_wolves";
    q.title       = "The Howling Woods";
    q.description = "Cull the wolf pack threatening the village.";
    q.reward      = "75 gold";
    Objective o;
    o.id          = "slay_wolves";
    o.description = "Slay 7 wolves.";
    o.target      = 7;
    q.objectives.push_back(std::move(o));
    return q;
}

}  // namespace

// ============================================================================
// 1. Add quest + activate + progress + complete chain.
// ============================================================================
TEST(QuestLog, AddActivateProgressCompleteChain)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);

    // After add, the quest sits in the inactive bucket.
    ASSERT_EQ(log.size(), 1U);
    EXPECT_EQ(log.inactive_quests().size(), 1U);
    EXPECT_EQ(log.active_quests().size(),   0U);

    // Activate -> moves to active.
    ASSERT_EQ(log.activate("main_q01"), MutateResult::kOk);
    EXPECT_EQ(log.inactive_quests().size(), 0U);
    EXPECT_EQ(log.active_quests().size(),   1U);

    // Step 1: find_relic.
    ASSERT_EQ(log.progress("main_q01", "find_relic", 1), MutateResult::kOk);

    // Step 2: return to mentor.
    ASSERT_EQ(log.complete_objective("main_q01", "return_mentor"),
              MutateResult::kOk);

    // Auto-complete kicked in.
    const Quest* q = log.find_quest("main_q01");
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->status, QuestStatus::kComplete);
    EXPECT_EQ(log.active_quests().size(),    0U);
    EXPECT_EQ(log.completed_quests().size(), 1U);
}

// ============================================================================
// 2. Quest auto-completes when all objectives done.
// ============================================================================
TEST(QuestLog, AutoCompleteWhenAllObjectivesDone)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("main_q01"), MutateResult::kOk);

    // Quest stays active after one of two objectives done.
    ASSERT_EQ(log.complete_objective("main_q01", "find_relic"),
              MutateResult::kOk);
    const Quest* mid = log.find_quest("main_q01");
    ASSERT_NE(mid, nullptr);
    EXPECT_EQ(mid->status, QuestStatus::kActive);

    // Now finish the last one.
    ASSERT_EQ(log.complete_objective("main_q01", "return_mentor"),
              MutateResult::kOk);
    const Quest* done = log.find_quest("main_q01");
    ASSERT_NE(done, nullptr);
    EXPECT_EQ(done->status, QuestStatus::kComplete);
}

// ============================================================================
// 3. Counter objective progresses toward target.
// ============================================================================
TEST(QuestLog, CounterObjectiveProgressesTowardTarget)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_wolf_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("side_wolves"), MutateResult::kOk);

    // Three wolves down -- objective still kActive, parent still kActive.
    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 3), MutateResult::kOk);
    const Quest* mid = log.find_quest("side_wolves");
    ASSERT_NE(mid, nullptr);
    ASSERT_EQ(mid->objectives.size(), 1U);
    EXPECT_EQ(mid->objectives[0].progress, 3);
    EXPECT_EQ(mid->objectives[0].status,   ObjectiveStatus::kActive);
    EXPECT_EQ(mid->status,                 QuestStatus::kActive);

    // Three more -- still active (6/7).
    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 3), MutateResult::kOk);
    const Quest* near = log.find_quest("side_wolves");
    ASSERT_NE(near, nullptr);
    EXPECT_EQ(near->objectives[0].progress, 6);
    EXPECT_EQ(near->status,                 QuestStatus::kActive);

    // Last wolf hits 7 -- objective and quest complete.
    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 1), MutateResult::kOk);
    const Quest* done = log.find_quest("side_wolves");
    ASSERT_NE(done, nullptr);
    EXPECT_EQ(done->objectives[0].progress, 7);
    EXPECT_EQ(done->objectives[0].status,   ObjectiveStatus::kComplete);
    EXPECT_EQ(done->status,                 QuestStatus::kComplete);
}

// ============================================================================
// 4. Failed objective fails the parent quest.
// ============================================================================
TEST(QuestLog, FailedObjectiveFailsParentQuest)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("main_q01"), MutateResult::kOk);

    // Complete one objective, then fail the other.
    ASSERT_EQ(log.complete_objective("main_q01", "find_relic"),
              MutateResult::kOk);
    ASSERT_EQ(log.fail_objective("main_q01", "return_mentor"),
              MutateResult::kOk);

    const Quest* q = log.find_quest("main_q01");
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->status, QuestStatus::kFailed);
    EXPECT_EQ(log.failed_quests().size(),    1U);
    EXPECT_EQ(log.active_quests().size(),    0U);
    EXPECT_EQ(log.completed_quests().size(), 0U);
}

// ============================================================================
// 5. Multiple active quests work in parallel without cross-talk.
// ============================================================================
TEST(QuestLog, MultipleActiveQuestsInParallel)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(log.add_quest(make_wolf_quest()),  AddResult::kOk);
    ASSERT_EQ(log.activate("main_q01"),    MutateResult::kOk);
    ASSERT_EQ(log.activate("side_wolves"), MutateResult::kOk);

    EXPECT_EQ(log.active_quests().size(), 2U);

    // Progress one of two on the relic, three of seven on the wolves.
    ASSERT_EQ(log.complete_objective("main_q01", "find_relic"),
              MutateResult::kOk);
    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 3),
              MutateResult::kOk);

    const Quest* r = log.find_quest("main_q01");
    const Quest* w = log.find_quest("side_wolves");
    ASSERT_NE(r, nullptr);
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(r->status, QuestStatus::kActive);
    EXPECT_EQ(w->status, QuestStatus::kActive);
    EXPECT_EQ(r->objectives[0].status,   ObjectiveStatus::kComplete);
    EXPECT_EQ(w->objectives[0].progress, 3);

    // Finish wolves; relic should be unaffected.
    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 4),
              MutateResult::kOk);
    const Quest* w2 = log.find_quest("side_wolves");
    const Quest* r2 = log.find_quest("main_q01");
    ASSERT_NE(w2, nullptr);
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(w2->status, QuestStatus::kComplete);
    EXPECT_EQ(r2->status, QuestStatus::kActive);
    EXPECT_EQ(log.active_quests().size(),    1U);
    EXPECT_EQ(log.completed_quests().size(), 1U);
}

// ============================================================================
// 6. Inactive quests hidden from active_quests().
// ============================================================================
TEST(QuestLog, InactiveQuestsHiddenFromActiveList)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(log.add_quest(make_wolf_quest()),  AddResult::kOk);

    // Neither activated yet.
    EXPECT_EQ(log.inactive_quests().size(), 2U);
    EXPECT_EQ(log.active_quests().size(),   0U);

    ASSERT_EQ(log.activate("side_wolves"), MutateResult::kOk);
    EXPECT_EQ(log.inactive_quests().size(), 1U);
    EXPECT_EQ(log.active_quests().size(),   1U);

    // The remaining one in active_quests() is the wolf quest, not the relic.
    const auto active = log.active_quests();
    ASSERT_EQ(active.size(), 1U);
    EXPECT_EQ(active[0].id, "side_wolves");
}

// ============================================================================
// 7. Completed quest moves from active to completed list.
// ============================================================================
TEST(QuestLog, CompletedQuestMovesToCompletedBucket)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_wolf_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("side_wolves"), MutateResult::kOk);
    EXPECT_EQ(log.active_quests().size(),    1U);
    EXPECT_EQ(log.completed_quests().size(), 0U);

    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 7),
              MutateResult::kOk);
    EXPECT_EQ(log.active_quests().size(),    0U);
    EXPECT_EQ(log.completed_quests().size(), 1U);

    const auto done = log.completed_quests();
    ASSERT_EQ(done.size(), 1U);
    EXPECT_EQ(done[0].id,      "side_wolves");
    EXPECT_EQ(done[0].status,  QuestStatus::kComplete);
    EXPECT_EQ(done[0].reward,  "75 gold");
}

// ============================================================================
// 8. serialize -> restore roundtrip preserves quest + objective state.
// ============================================================================
TEST(QuestLog, SerializeRestoreRoundtrip)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(log.add_quest(make_wolf_quest()),  AddResult::kOk);

    // Activate both; finish wolves; progress relic halfway; fail nothing.
    ASSERT_EQ(log.activate("main_q01"),    MutateResult::kOk);
    ASSERT_EQ(log.activate("side_wolves"), MutateResult::kOk);
    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 7), MutateResult::kOk);
    ASSERT_EQ(log.complete_objective("main_q01", "find_relic"),
              MutateResult::kOk);

    const std::vector<std::byte> blob = log.serialize();
    EXPECT_FALSE(blob.empty());

    // Round-trip into a fresh log.
    QuestLog restored;
    ASSERT_EQ(restored.restore(std::span<const std::byte> {blob}),
              RestoreResult::kOk);

    // Same shape and same runtime state.
    ASSERT_EQ(restored.size(), 2U);
    const Quest* r1 = restored.find_quest("main_q01");
    const Quest* w1 = restored.find_quest("side_wolves");
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(w1, nullptr);

    EXPECT_EQ(r1->title,                 "The Relic of Aenor");
    EXPECT_EQ(r1->reward,                "200 gold + Ring of Tides");
    EXPECT_EQ(r1->status,                QuestStatus::kActive);
    ASSERT_EQ(r1->objectives.size(),     2U);
    EXPECT_EQ(r1->objectives[0].id,      "find_relic");
    EXPECT_EQ(r1->objectives[0].status,  ObjectiveStatus::kComplete);
    EXPECT_EQ(r1->objectives[1].id,      "return_mentor");
    EXPECT_EQ(r1->objectives[1].status,  ObjectiveStatus::kActive);

    EXPECT_EQ(w1->title,                 "The Howling Woods");
    EXPECT_EQ(w1->status,                QuestStatus::kComplete);
    ASSERT_EQ(w1->objectives.size(),     1U);
    EXPECT_EQ(w1->objectives[0].progress, 7);
    EXPECT_EQ(w1->objectives[0].target,   7);
    EXPECT_EQ(w1->objectives[0].status,   ObjectiveStatus::kComplete);

    EXPECT_EQ(restored.active_quests().size(),    1U);
    EXPECT_EQ(restored.completed_quests().size(), 1U);
}

// ============================================================================
// 9. Validation: duplicate quest id and duplicate objective id are rejected.
// ============================================================================
TEST(QuestLog, ValidationDuplicateIds)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    EXPECT_EQ(log.add_quest(make_relic_quest()),
              AddResult::kDuplicateQuestId);

    Quest dup_obj;
    dup_obj.id = "dup_obj_quest";
    {
        Objective a;
        a.id     = "step";
        a.target = 1;
        dup_obj.objectives.push_back(std::move(a));
    }
    {
        Objective b;
        b.id     = "step";  // duplicate
        b.target = 1;
        dup_obj.objectives.push_back(std::move(b));
    }
    EXPECT_EQ(log.add_quest(std::move(dup_obj)),
              AddResult::kDuplicateObjectiveId);

    Quest empty_id;  // empty quest id rejected
    empty_id.id = "";
    EXPECT_EQ(log.add_quest(std::move(empty_id)),
              AddResult::kEmptyQuestId);
}

// ============================================================================
// 10. Negative progress delta clamps at zero.
// ============================================================================
TEST(QuestLog, NegativeProgressClampsAtZero)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_wolf_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("side_wolves"), MutateResult::kOk);

    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 2),
              MutateResult::kOk);
    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", -10),
              MutateResult::kOk);
    const Quest* q = log.find_quest("side_wolves");
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->objectives[0].progress, 0);
    EXPECT_EQ(q->objectives[0].status,   ObjectiveStatus::kActive);
    EXPECT_EQ(q->status,                 QuestStatus::kActive);
}

// ============================================================================
// 11. Restore from empty / corrupt buffer leaves the log empty.
// ============================================================================
TEST(QuestLog, RestoreFromEmptyOrCorruptBufferLeavesLogEmpty)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    EXPECT_FALSE(log.empty());

    // Empty buffer.
    const std::vector<std::byte> empty_blob;
    EXPECT_EQ(log.restore(std::span<const std::byte> {empty_blob}),
              RestoreResult::kEmptyBuffer);
    EXPECT_TRUE(log.empty());

    // Magic mismatch -- 12 bytes of zeros is a long-enough header but wrong
    // magic.
    const std::vector<std::byte> bad_magic(12U, std::byte {0});
    EXPECT_EQ(log.restore(std::span<const std::byte> {bad_magic}),
              RestoreResult::kMagicMismatch);
    EXPECT_TRUE(log.empty());

    // Truncated header (only 4 bytes -- "CDQL" magic but no version yet).
    std::vector<std::byte> truncated;
    truncated.push_back(static_cast<std::byte>('C'));
    truncated.push_back(static_cast<std::byte>('D'));
    truncated.push_back(static_cast<std::byte>('Q'));
    truncated.push_back(static_cast<std::byte>('L'));
    EXPECT_EQ(log.restore(std::span<const std::byte> {truncated}),
              RestoreResult::kTruncated);
    EXPECT_TRUE(log.empty());
}

// ============================================================================
// BAND-1 branch/failure-path + rollback depth (ADR-20260616 §2.3).
// ============================================================================

// 12. Every mutation on a non-active quest reports kQuestNotActive and leaves
//     the quest untouched (no silent progress on an inactive/complete quest).
TEST(QuestLog, MutationsOnNonActiveQuestRejected)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);

    // Quest is still kInactive (never activated).
    EXPECT_EQ(log.progress("main_q01", "find_relic", 1),
              MutateResult::kQuestNotActive);
    EXPECT_EQ(log.complete_objective("main_q01", "find_relic"),
              MutateResult::kQuestNotActive);
    EXPECT_EQ(log.fail_objective("main_q01", "find_relic"),
              MutateResult::kQuestNotActive);

    // Objective state unchanged.
    const Quest* q = log.find_quest("main_q01");
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->status, QuestStatus::kInactive);
    EXPECT_EQ(q->objectives[0].status, ObjectiveStatus::kInactive);
    EXPECT_EQ(q->objectives[0].progress, 0);
}

// 13. Mutating an already-completed/failed objective reports
//     kObjectiveNotActive (drift detection), not a silent no-op.
TEST(QuestLog, MutatingFinishedObjectiveReportsNotActive)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("main_q01"), MutateResult::kOk);

    // Complete one objective, then try to mutate it again.
    ASSERT_EQ(log.complete_objective("main_q01", "find_relic"),
              MutateResult::kOk);
    EXPECT_EQ(log.complete_objective("main_q01", "find_relic"),
              MutateResult::kObjectiveNotActive);
    EXPECT_EQ(log.progress("main_q01", "find_relic", 1),
              MutateResult::kObjectiveNotActive);
    EXPECT_EQ(log.fail_objective("main_q01", "find_relic"),
              MutateResult::kObjectiveNotActive);

    // The other objective is still mutable.
    EXPECT_EQ(log.complete_objective("main_q01", "return_mentor"),
              MutateResult::kOk);
    const Quest* q = log.find_quest("main_q01");
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->status, QuestStatus::kComplete);
}

// 14. Unknown quest / unknown objective produce granular error codes.
TEST(QuestLog, UnknownQuestAndObjectiveReported)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("main_q01"), MutateResult::kOk);

    EXPECT_EQ(log.activate("ghost_quest"),     MutateResult::kUnknownQuest);
    EXPECT_EQ(log.progress("ghost_quest", "x", 1), MutateResult::kUnknownQuest);
    EXPECT_EQ(log.progress("main_q01", "ghost_obj", 1),
              MutateResult::kUnknownObjective);
    EXPECT_EQ(log.complete_objective("main_q01", "ghost_obj"),
              MutateResult::kUnknownObjective);
    EXPECT_EQ(log.fail_objective("main_q01", "ghost_obj"),
              MutateResult::kUnknownObjective);
}

// 15. Re-activate signals: activate on active/complete/failed quest returns
//     the matching drift code without mutating state.
TEST(QuestLog, ReactivateReportsDriftCodes)
{
    QuestLog log;
    ASSERT_EQ(log.add_quest(make_wolf_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("side_wolves"), MutateResult::kOk);
    EXPECT_EQ(log.activate("side_wolves"), MutateResult::kAlreadyActive);

    // Drive it to complete, then activate again.
    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 7), MutateResult::kOk);
    EXPECT_EQ(log.activate("side_wolves"), MutateResult::kAlreadyCompleted);

    // A separate quest taken to failure.
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("main_q01"), MutateResult::kOk);
    ASSERT_EQ(log.fail_objective("main_q01", "find_relic"), MutateResult::kOk);
    EXPECT_EQ(log.activate("main_q01"), MutateResult::kAlreadyFailed);
}

// 16. Zero-objective quest auto-completes on activation; a quest authored
//     with every objective pre-completed also auto-completes.
TEST(QuestLog, ActivateAutoCompletesVacuousQuests)
{
    QuestLog log;

    // (a) zero-objective "discovered location" pseudo-quest.
    Quest loc;
    loc.id    = "found_cave";
    loc.title = "Discovered: Echo Cave";
    ASSERT_EQ(log.add_quest(std::move(loc)), AddResult::kOk);
    ASSERT_EQ(log.activate("found_cave"), MutateResult::kOk);
    {
        const Quest* q = log.find_quest("found_cave");
        ASSERT_NE(q, nullptr);
        EXPECT_EQ(q->status, QuestStatus::kComplete);
    }

    // (b) all objectives pre-authored kComplete -> auto-complete on activate.
    Quest pre;
    pre.id = "prefab_done";
    {
        Objective o;
        o.id     = "step";
        o.target = 1;
        o.status = ObjectiveStatus::kComplete;  // pre-completed by author
        pre.objectives.push_back(std::move(o));
    }
    ASSERT_EQ(log.add_quest(std::move(pre)), AddResult::kOk);
    ASSERT_EQ(log.activate("prefab_done"), MutateResult::kOk);
    {
        const Quest* q = log.find_quest("prefab_done");
        ASSERT_NE(q, nullptr);
        EXPECT_EQ(q->status, QuestStatus::kComplete);
    }
}

// 17. Restore ROLLBACK: a populated log fed a structurally-corrupt buffer
//     (duplicate quest ids) is wiped to empty (never half-restored), and a
//     version-mismatch / bad-status-byte buffer likewise leaves it empty.
TEST(QuestLog, RestoreRollbackLeavesLogEmptyOnStructuralCorruption)
{
    // Build a valid two-quest blob, then corrupt it two ways. Because
    // add_quest forbids duplicate ids, we cannot serialize a duplicate-id
    // log directly; instead we corrupt the version (kVersionMismatch) and a
    // status byte (kCorrupt) — both must roll the destination log back to
    // empty rather than leaving a half-restored state.
    QuestLog src;
    ASSERT_EQ(src.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(src.add_quest(make_wolf_quest()),  AddResult::kOk);
    const std::vector<std::byte> blob = src.serialize();

    // (a) Version mismatch: byte 4..7 hold the u32 version (==1). Bump it.
    {
        std::vector<std::byte> bad_ver = blob;
        ASSERT_GE(bad_ver.size(), 8U);
        bad_ver[4] = static_cast<std::byte>(0x09);  // version 9 != 1

        QuestLog dst;
        ASSERT_EQ(dst.add_quest(make_relic_quest()), AddResult::kOk);
        EXPECT_FALSE(dst.empty());
        EXPECT_EQ(dst.restore(std::span<const std::byte> {bad_ver}),
                  RestoreResult::kVersionMismatch);
        EXPECT_TRUE(dst.empty());  // rolled back to empty
    }

    // (b) Corrupt status byte: flip the first quest's status byte to an
    //     out-of-range value (> kFailed == 3) -> kCorrupt + empty.
    {
        // Status byte sits after id+title+description+reward strings. Rather
        // than hand-compute the offset, build a minimal one-quest blob and
        // poke its single status byte.
        QuestLog one;
        Quest q;
        q.id = "q";
        ASSERT_EQ(one.add_quest(std::move(q)), AddResult::kOk);
        std::vector<std::byte> mini = one.serialize();
        // header(12) + id-string("q": 4 len + 1) + title(4) + desc(4) +
        // reward(4) -> status byte at index 12 + 5 + 4 + 4 + 4 = 29.
        constexpr std::size_t kStatusByte = 12U + 5U + 4U + 4U + 4U;
        ASSERT_GT(mini.size(), kStatusByte);
        mini[kStatusByte] = static_cast<std::byte>(0x7F);  // > kFailed

        QuestLog dst;
        ASSERT_EQ(dst.add_quest(make_relic_quest()), AddResult::kOk);
        EXPECT_EQ(dst.restore(std::span<const std::byte> {mini}),
                  RestoreResult::kCorrupt);
        EXPECT_TRUE(dst.empty());
    }
}

// 18. Restore-then-mutate: a restored mid-progress log accepts further
//     mutations and the auto-complete rule still fires post-restore.
TEST(QuestLog, RestoredLogContinuesToProgress)
{
    QuestLog src;
    ASSERT_EQ(src.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(src.activate("main_q01"), MutateResult::kOk);
    ASSERT_EQ(src.complete_objective("main_q01", "find_relic"),
              MutateResult::kOk);
    const std::vector<std::byte> blob = src.serialize();

    QuestLog dst;
    ASSERT_EQ(dst.restore(std::span<const std::byte> {blob}),
              RestoreResult::kOk);

    // Quest is still active mid-progress; finishing the last objective
    // auto-completes it after restore.
    EXPECT_EQ(dst.active_quests().size(), 1U);
    ASSERT_EQ(dst.complete_objective("main_q01", "return_mentor"),
              MutateResult::kOk);
    const Quest* q = dst.find_quest("main_q01");
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->status, QuestStatus::kComplete);
    EXPECT_EQ(dst.completed_quests().size(), 1U);
}

// ============================================================================
// Edge / negative tests added for 100% coverage (tests 19-28).
// ============================================================================

// 19. add_quest rejects a quest that has an objective with an empty id.
//     The log must be left fully unmodified (commit-or-rollback contract).
TEST(QuestLog, AddQuestRejectsEmptyObjectiveId)
{
    QuestLog log;
    // Pre-populate to verify size is preserved after a rejected add.
    ASSERT_EQ(log.add_quest(make_wolf_quest()), AddResult::kOk);
    ASSERT_EQ(log.size(), 1U);

    Quest bad;
    bad.id = "bad_quest";
    {
        Objective o;
        o.id     = "";   // empty — should be rejected
        o.target = 1;
        bad.objectives.push_back(std::move(o));
    }
    EXPECT_EQ(log.add_quest(std::move(bad)), AddResult::kEmptyObjectiveId);

    // Log unchanged: only the original wolf quest.
    EXPECT_EQ(log.size(), 1U);
    EXPECT_EQ(log.find_quest("bad_quest"), nullptr);
}

// 20. add_quest rollback: a quest with a second duplicate objective id is
//     rejected and the log contains no partial record for that quest.
TEST(QuestLog, AddQuestRollbackOnDuplicateObjectiveId)
{
    QuestLog log;

    Quest bad;
    bad.id = "dup_mid";
    {
        Objective a; a.id = "alpha"; a.target = 1;
        Objective b; b.id = "beta";  b.target = 1;
        Objective c; c.id = "alpha"; c.target = 1;  // duplicate of a
        bad.objectives.push_back(std::move(a));
        bad.objectives.push_back(std::move(b));
        bad.objectives.push_back(std::move(c));
    }
    EXPECT_EQ(log.add_quest(std::move(bad)), AddResult::kDuplicateObjectiveId);

    // Nothing committed — log is empty.
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(log.size(), 0U);
    EXPECT_EQ(log.find_quest("dup_mid"), nullptr);
}

// 21. A quest authored with a pre-failed objective: after activation that
//     objective remains kFailed and is NOT mutatable (kObjectiveNotActive),
//     while the parent quest is auto-failed by recompute.
TEST(QuestLog, PreFailedObjectiveNotMutableAfterActivation)
{
    QuestLog log;

    Quest q;
    q.id = "pre_fail";
    {
        Objective ok;
        ok.id     = "step_ok";
        ok.target = 1;
        q.objectives.push_back(std::move(ok));
    }
    {
        Objective bad;
        bad.id     = "step_bad";
        bad.status = ObjectiveStatus::kFailed;  // pre-authored as failed
        bad.target = 1;
        q.objectives.push_back(std::move(bad));
    }
    ASSERT_EQ(log.add_quest(std::move(q)), AddResult::kOk);
    ASSERT_EQ(log.activate("pre_fail"), MutateResult::kOk);

    // Recompute should have auto-failed the quest because step_bad is kFailed.
    const Quest* qp = log.find_quest("pre_fail");
    ASSERT_NE(qp, nullptr);
    EXPECT_EQ(qp->status, QuestStatus::kFailed);
    EXPECT_EQ(log.failed_quests().size(), 1U);

    // Mutations on the pre-failed objective return kQuestNotActive (quest is
    // now kFailed) — not a silent no-op.
    EXPECT_EQ(log.progress("pre_fail", "step_bad", 1),
              MutateResult::kQuestNotActive);
    EXPECT_EQ(log.complete_objective("pre_fail", "step_bad"),
              MutateResult::kQuestNotActive);
}

// 22. find_quest returns nullptr for an absent id; size() and empty() track
//     correctly across add + activate + fail cycles.
TEST(QuestLog, FindQuestNullptrAndSizeEmpty)
{
    QuestLog log;
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(log.size(), 0U);
    EXPECT_EQ(log.find_quest("nowhere"), nullptr);

    ASSERT_EQ(log.add_quest(make_wolf_quest()), AddResult::kOk);
    EXPECT_FALSE(log.empty());
    EXPECT_EQ(log.size(), 1U);
    EXPECT_NE(log.find_quest("side_wolves"), nullptr);
    EXPECT_EQ(log.find_quest("main_q01"), nullptr);

    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    EXPECT_EQ(log.size(), 2U);

    // size() does not change as quests transition between buckets.
    ASSERT_EQ(log.activate("side_wolves"), MutateResult::kOk);
    EXPECT_EQ(log.size(), 2U);
    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 7), MutateResult::kOk);
    EXPECT_EQ(log.size(), 2U);  // completed quest still in log
}

// 23. Serialize an empty log then restore it: result is kOk and the
//     restored log is empty (NOT kEmptyBuffer — the blob is 12 bytes).
TEST(QuestLog, SerializeAndRestoreEmptyLog)
{
    QuestLog empty_log;
    EXPECT_TRUE(empty_log.empty());

    const std::vector<std::byte> blob = empty_log.serialize();
    // A non-empty blob (magic CDQL + version + quest_count=0) is produced.
    EXPECT_FALSE(blob.empty());

    QuestLog dst;
    ASSERT_EQ(dst.add_quest(make_wolf_quest()), AddResult::kOk);
    EXPECT_FALSE(dst.empty());

    // Restoring an empty-log blob replaces the destination with an empty log.
    EXPECT_EQ(dst.restore(std::span<const std::byte> {blob}), RestoreResult::kOk);
    EXPECT_TRUE(dst.empty());
    EXPECT_EQ(dst.size(), 0U);
    EXPECT_EQ(dst.inactive_quests().size(), 0U);
    EXPECT_EQ(dst.active_quests().size(),   0U);
}

// 24. Truncated mid-payload: valid header (quest_count=1) but the quest
//     body is cut off.  Must return kTruncated, not UB or crash.
TEST(QuestLog, RestoreTruncatedMidPayload)
{
    // Build a valid single-quest blob, then trim it to just past the header
    // (12 bytes) + the quest_count field (4 bytes) = 16 bytes, which does
    // NOT include any of the quest's id string.
    QuestLog src;
    ASSERT_EQ(src.add_quest(make_wolf_quest()), AddResult::kOk);
    const std::vector<std::byte> full = src.serialize();
    ASSERT_GT(full.size(), 16U);

    // Clip at 16 bytes: header(12) + quest_count(4). The first read_string
    // for q.id will attempt to read 4 bytes for the length prefix — fails.
    const std::vector<std::byte> clipped(full.begin(), full.begin() + 16);

    QuestLog dst;
    EXPECT_EQ(dst.restore(std::span<const std::byte> {clipped}),
              RestoreResult::kTruncated);
    EXPECT_TRUE(dst.empty());

    // Also test a clip that provides the id-length u32 but not the id bytes.
    // header(12) + quest_count(4) + id_len(4) = 20 bytes.
    if (full.size() > 20U)
    {
        const std::vector<std::byte> clipped2(full.begin(), full.begin() + 20);
        QuestLog dst2;
        EXPECT_EQ(dst2.restore(std::span<const std::byte> {clipped2}),
                  RestoreResult::kTruncated);
        EXPECT_TRUE(dst2.empty());
    }
}

// 25. Corrupt objective status byte triggers kCorrupt, leaving the log empty.
//     Complements test 17b which covers quest-level status corruption.
TEST(QuestLog, RestoreCorruptObjectiveStatusByteReturnsCorrupt)
{
    // Build a minimal quest with one objective and a known serial layout.
    // Quest id="q", objective id="o": header(12) + quest_count(4=16) +
    // id("q": 4+1=5) + title(4=4) + desc(4=4) + reward(4=4) +
    // quest_status(1) + obj_count(4) = 16+5+4+4+4+1+4 = 38 bytes before
    // first objective starts.
    // Objective: id("o": 4+1=5) + desc(4=4) = 9 bytes + obj_status(1) = byte 47.
    QuestLog one;
    {
        Quest q;
        q.id = "q";
        Objective o;
        o.id     = "o";
        o.target = 1;
        q.objectives.push_back(std::move(o));
        ASSERT_EQ(one.add_quest(std::move(q)), AddResult::kOk);
    }
    std::vector<std::byte> blob = one.serialize();

    // Locate the objective status byte: past header(8) + quest_count(4) +
    // id(5) + title(4) + desc(4) + reward(4) + quest_status(1) + obj_count(4) +
    // obj_id(5) + obj_desc(4) = 43. (kMagic is 4 bytes, not 8.)
    constexpr std::size_t kObjStatusByte =
        8U    // magic(4) + version(4)
        + 4U  // quest_count
        + 5U  // id "q" (u32 len + 1 char)
        + 4U  // title (u32 len, empty)
        + 4U  // desc  (u32 len, empty)
        + 4U  // reward (u32 len, empty)
        + 1U  // quest status byte
        + 4U  // obj_count
        + 5U  // obj id "o" (u32 len + 1 char)
        + 4U; // obj desc (u32 len, empty)

    ASSERT_GT(blob.size(), kObjStatusByte);
    blob[kObjStatusByte] = static_cast<std::byte>(0xAA);  // > kFailed (3)

    QuestLog dst;
    ASSERT_EQ(dst.add_quest(make_wolf_quest()), AddResult::kOk);
    EXPECT_EQ(dst.restore(std::span<const std::byte> {blob}),
              RestoreResult::kCorrupt);
    EXPECT_TRUE(dst.empty());  // rolled back to empty
}

// 26. Progress overflow clamp: a large positive delta that would exceed
//     INT32_MAX on the counter is safely clamped at `target`.
TEST(QuestLog, ProgressOverflowClampsAtTarget)
{
    QuestLog log;
    // Wolf quest target == 7.
    ASSERT_EQ(log.add_quest(make_wolf_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("side_wolves"), MutateResult::kOk);

    // First: bring counter to 3 (well below target).
    ASSERT_EQ(log.progress("side_wolves", "slay_wolves", 3), MutateResult::kOk);
    {
        const Quest* q = log.find_quest("side_wolves");
        ASSERT_NE(q, nullptr);
        EXPECT_EQ(q->objectives[0].progress, 3);
        EXPECT_EQ(q->objectives[0].status, ObjectiveStatus::kActive);
    }

    // Now supply INT32_MAX as delta; 3 + INT32_MAX overflows int32 but the
    // i64 accumulator in progress() catches this and clamps to target (7).
    ASSERT_EQ(
        log.progress("side_wolves", "slay_wolves",
                     std::numeric_limits<std::int32_t>::max()),
        MutateResult::kOk);
    {
        const Quest* q = log.find_quest("side_wolves");
        ASSERT_NE(q, nullptr);
        EXPECT_EQ(q->objectives[0].progress, 7);          // clamped at target
        EXPECT_EQ(q->objectives[0].status, ObjectiveStatus::kComplete);
        EXPECT_EQ(q->status, QuestStatus::kComplete);
    }
}

// 27. Out-of-order completion: complete objective B before A; quest stays
//     active until the last open objective (A) is done, then auto-completes.
TEST(QuestLog, OutOfOrderObjectiveCompletionAutoCompletes)
{
    QuestLog log;
    // make_relic_quest: objectives [find_relic, return_mentor] in order.
    ASSERT_EQ(log.add_quest(make_relic_quest()), AddResult::kOk);
    ASSERT_EQ(log.activate("main_q01"), MutateResult::kOk);

    // Complete the second objective first.
    ASSERT_EQ(log.complete_objective("main_q01", "return_mentor"),
              MutateResult::kOk);
    {
        const Quest* q = log.find_quest("main_q01");
        ASSERT_NE(q, nullptr);
        EXPECT_EQ(q->status, QuestStatus::kActive);          // still active
        EXPECT_EQ(q->objectives[0].status, ObjectiveStatus::kActive);
        EXPECT_EQ(q->objectives[1].status, ObjectiveStatus::kComplete);
    }

    // Now complete the first objective -> all done -> auto-complete.
    ASSERT_EQ(log.complete_objective("main_q01", "find_relic"),
              MutateResult::kOk);
    {
        const Quest* q = log.find_quest("main_q01");
        ASSERT_NE(q, nullptr);
        EXPECT_EQ(q->status, QuestStatus::kComplete);
        EXPECT_EQ(q->objectives[0].status, ObjectiveStatus::kComplete);
        EXPECT_EQ(q->objectives[1].status, ObjectiveStatus::kComplete);
    }
    EXPECT_EQ(log.completed_quests().size(), 1U);
    EXPECT_EQ(log.active_quests().size(),    0U);
}

// 28. Serialize/restore round-trip for a large batch: 20 quests across all
//     four status buckets.  Verifies the persistence layer does not truncate
//     or corrupt data at scale and that bucket views rebuild correctly.
TEST(QuestLog, SerializeRestoreLargeBatch)
{
    QuestLog src;

    // Populate 20 quests then activate all: 10 stay active (0-4 partial +
    // 15-19 zero-progress), 5 completed (5-9), 5 failed (10-14).
    for (int i = 0; i < 20; ++i)
    {
        Quest q;
        q.id    = "q" + std::to_string(i);
        q.title = "Quest " + std::to_string(i);
        Objective o;
        o.id     = "step";
        o.target = 10;
        q.objectives.push_back(std::move(o));
        ASSERT_EQ(src.add_quest(std::move(q)), AddResult::kOk);
    }

    // Activate all.
    for (int i = 0; i < 20; ++i)
    {
        ASSERT_EQ(src.activate("q" + std::to_string(i)), MutateResult::kOk);
    }

    // Quests 0-4: active with partial progress (3/10).
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_EQ(src.progress("q" + std::to_string(i), "step", 3),
                  MutateResult::kOk);
    }

    // Quests 5-9: complete.
    for (int i = 5; i < 10; ++i)
    {
        ASSERT_EQ(src.complete_objective("q" + std::to_string(i), "step"),
                  MutateResult::kOk);
    }

    // Quests 10-14: fail.
    for (int i = 10; i < 15; ++i)
    {
        ASSERT_EQ(src.fail_objective("q" + std::to_string(i), "step"),
                  MutateResult::kOk);
    }

    // Quests 15-19: remain active (no progress).
    EXPECT_EQ(src.size(), 20U);
    EXPECT_EQ(src.active_quests().size(),    10U);  // 0-4 in-progress + 15-19
    EXPECT_EQ(src.completed_quests().size(), 5U);
    EXPECT_EQ(src.failed_quests().size(),    5U);

    const std::vector<std::byte> blob = src.serialize();
    EXPECT_FALSE(blob.empty());

    QuestLog dst;
    ASSERT_EQ(dst.restore(std::span<const std::byte> {blob}),
              RestoreResult::kOk);
    EXPECT_EQ(dst.size(), 20U);
    EXPECT_EQ(dst.active_quests().size(),    10U);
    EXPECT_EQ(dst.completed_quests().size(), 5U);
    EXPECT_EQ(dst.failed_quests().size(),    5U);

    // Spot-check a few restored quests.
    const Quest* q2 = dst.find_quest("q2");
    ASSERT_NE(q2, nullptr);
    EXPECT_EQ(q2->status, QuestStatus::kActive);
    EXPECT_EQ(q2->objectives[0].progress, 3);
    EXPECT_EQ(q2->objectives[0].status,   ObjectiveStatus::kActive);

    const Quest* q7 = dst.find_quest("q7");
    ASSERT_NE(q7, nullptr);
    EXPECT_EQ(q7->status, QuestStatus::kComplete);
    EXPECT_EQ(q7->objectives[0].progress, 10);
    EXPECT_EQ(q7->objectives[0].status,   ObjectiveStatus::kComplete);

    const Quest* q12 = dst.find_quest("q12");
    ASSERT_NE(q12, nullptr);
    EXPECT_EQ(q12->status, QuestStatus::kFailed);
    EXPECT_EQ(q12->objectives[0].status,  ObjectiveStatus::kFailed);
}
