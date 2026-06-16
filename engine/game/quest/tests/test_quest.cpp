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
