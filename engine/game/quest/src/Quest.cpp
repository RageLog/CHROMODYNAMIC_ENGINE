// =============================================================================
// CHROMODYNAMIC - cd/game/quest/Quest.cpp
// Phase 500 - cd::game::quest (G4.2: Objective tracker + journal)
//
// Implementation notes:
//
// * add_quest validates the quest + every objective in one pass and either
//   commits the whole record or rolls back to the previous log state. We
//   normalise `target` to >= 1 here (the public API documents this) so
//   later progress() calls don't have to special-case authoring noise.
//
// * activate() promotes a kInactive quest to kActive and flips every one
//   of its objectives to kActive. A zero-objective quest is a valid
//   "discovered location" entry and auto-completes on activation.
//
// * progress() / complete_objective() / fail_objective() all funnel into
//   recompute_quest_after_objective_change() so the auto-complete /
//   auto-fail rules live in exactly one place. The rule is:
//     - any kFailed objective  -> quest is kFailed.
//     - all objectives kComplete (and none kFailed) -> quest is kComplete.
//     - otherwise the quest remains kActive.
//
// * serialize() emits a magic + version + quest_count header followed by
//   length-prefixed records. Strings are written as u32 length + raw
//   bytes. Status enums travel as u8. Integers travel as little-endian
//   u32 / i32. No padding, no alignment assumptions in the reader -- we
//   memcpy each scalar out of the byte stream and bounds-check after every
//   read so a truncated buffer can only ever yield kTruncated, never a
//   read past the end.
//
// * restore() validates structural invariants by reusing add_quest()
//   against a temporary log, then commits via move-assignment. That keeps
//   the duplicate-id detection in one place and guarantees a failed
//   restore leaves the log empty.
// =============================================================================
#include <cd/game/quest/Quest.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::game::quest
{

// =============================================================================
// Helpers
// =============================================================================

namespace
{

constexpr std::array<char, 4> kMagic   {'C', 'D', 'Q', 'L'};
constexpr std::uint32_t       kVersion = 1U;

// --- writer helpers ----------------------------------------------------------
void write_u32(std::vector<std::byte>& out, std::uint32_t v)
{
    std::array<std::byte, 4> buf {};
    std::memcpy(buf.data(), &v, sizeof(v));
    out.insert(out.end(), buf.begin(), buf.end());
}

void write_i32(std::vector<std::byte>& out, std::int32_t v)
{
    std::array<std::byte, 4> buf {};
    std::memcpy(buf.data(), &v, sizeof(v));
    out.insert(out.end(), buf.begin(), buf.end());
}

void write_u8(std::vector<std::byte>& out, std::uint8_t v)
{
    out.push_back(static_cast<std::byte>(v));
}

void write_string(std::vector<std::byte>& out, const std::string& s)
{
    write_u32(out, static_cast<std::uint32_t>(s.size()));
    const auto* base = reinterpret_cast<const std::byte*>(s.data());
    out.insert(out.end(), base, base + s.size());
}

// --- reader helpers ----------------------------------------------------------
// Each reader returns true on success and false on out-of-buffer; on success
// it advances `pos` past the consumed bytes.

bool read_u32(std::span<const std::byte> in, std::size_t& pos, std::uint32_t& v) noexcept
{
    if (pos + sizeof(v) > in.size())
    {
        return false;
    }
    std::memcpy(&v, in.data() + pos, sizeof(v));
    pos += sizeof(v);
    return true;
}

bool read_i32(std::span<const std::byte> in, std::size_t& pos, std::int32_t& v) noexcept
{
    if (pos + sizeof(v) > in.size())
    {
        return false;
    }
    std::memcpy(&v, in.data() + pos, sizeof(v));
    pos += sizeof(v);
    return true;
}

bool read_u8(std::span<const std::byte> in, std::size_t& pos, std::uint8_t& v) noexcept
{
    if (pos + 1 > in.size())
    {
        return false;
    }
    v   = static_cast<std::uint8_t>(in[pos]);
    pos += 1;
    return true;
}

bool read_string(std::span<const std::byte> in, std::size_t& pos, std::string& s)
{
    std::uint32_t len = 0;
    if (!read_u32(in, pos, len))
    {
        return false;
    }
    if (pos + len > in.size())
    {
        return false;
    }
    s.assign(reinterpret_cast<const char*>(in.data() + pos), len);
    pos += len;
    return true;
}

}  // namespace

// =============================================================================
// QuestLog - lookup
// =============================================================================

const Quest* QuestLog::find_quest(std::string_view quest_id) const noexcept
{
    // Linear over the index_'s string -> idx map needs string lookup; we
    // build a temporary string only on miss-fast paths so keep it simple:
    // unordered_map::find on the key takes a const string&. Since the API
    // accepts string_view, materialise once.
    const auto it = index_.find(std::string {quest_id});
    if (it == index_.end())
    {
        return nullptr;
    }
    return &quests_.at(it->second);
}

Quest* QuestLog::find_mut(std::string_view quest_id) noexcept
{
    const auto it = index_.find(std::string {quest_id});
    if (it == index_.end())
    {
        return nullptr;
    }
    return &quests_.at(it->second);
}

Objective* QuestLog::find_objective_mut(Quest& q, std::string_view obj_id) noexcept
{
    for (auto& o : q.objectives)
    {
        if (o.id == obj_id)
        {
            return &o;
        }
    }
    return nullptr;
}

// =============================================================================
// QuestLog - add_quest
// =============================================================================

AddResult QuestLog::add_quest(Quest q)
{
    if (q.id.empty())
    {
        return AddResult::kEmptyQuestId;
    }
    if (index_.find(q.id) != index_.end())
    {
        return AddResult::kDuplicateQuestId;
    }

    // Validate objectives.
    for (std::size_t a = 0; a < q.objectives.size(); ++a)
    {
        if (q.objectives[a].id.empty())
        {
            return AddResult::kEmptyObjectiveId;
        }
        for (std::size_t b = a + 1; b < q.objectives.size(); ++b)
        {
            if (q.objectives[a].id == q.objectives[b].id)
            {
                return AddResult::kDuplicateObjectiveId;
            }
        }
    }

    // Normalise objective targets (clamp to >= 1) and clear runtime state
    // so re-adding from a fresh authoring source always starts in the
    // canonical "inactive, zero progress" position.
    for (auto& o : q.objectives)
    {
        if (o.target < 1)
        {
            o.target = 1;
        }
        if (o.progress < 0)
        {
            o.progress = 0;
        }
    }

    // Commit.
    const std::size_t idx = quests_.size();
    index_.emplace(q.id, idx);
    quests_.push_back(std::move(q));
    views_dirty_ = true;
    return AddResult::kOk;
}

// =============================================================================
// QuestLog - activate
// =============================================================================

MutateResult QuestLog::activate(std::string_view quest_id)
{
    Quest* q = find_mut(quest_id);
    if (q == nullptr)
    {
        return MutateResult::kUnknownQuest;
    }
    switch (q->status)
    {
        case QuestStatus::kActive:
            return MutateResult::kAlreadyActive;
        case QuestStatus::kComplete:
            return MutateResult::kAlreadyCompleted;
        case QuestStatus::kFailed:
            return MutateResult::kAlreadyFailed;
        case QuestStatus::kInactive:
        default:
            break;
    }

    // Promote.
    q->status = QuestStatus::kActive;
    for (auto& o : q->objectives)
    {
        // Only awaken objectives that were authored as kInactive. Leave any
        // already-complete/failed objectives alone (callers can pre-author
        // staged steps).
        if (o.status == ObjectiveStatus::kInactive)
        {
            o.status = ObjectiveStatus::kActive;
        }
    }

    // Zero-objective quests auto-complete on activation. Multi-objective
    // quests that happen to start with every objective pre-completed also
    // auto-complete -- recompute handles both paths.
    recompute_quest_after_objective_change(*q);
    views_dirty_ = true;
    return MutateResult::kOk;
}

// =============================================================================
// QuestLog - progress / complete / fail
// =============================================================================

MutateResult QuestLog::progress(std::string_view quest_id,
                                std::string_view obj_id,
                                std::int32_t     delta)
{
    Quest* q = find_mut(quest_id);
    if (q == nullptr)
    {
        return MutateResult::kUnknownQuest;
    }
    if (q->status != QuestStatus::kActive)
    {
        return MutateResult::kQuestNotActive;
    }
    Objective* o = find_objective_mut(*q, obj_id);
    if (o == nullptr)
    {
        return MutateResult::kUnknownObjective;
    }
    if (o->status != ObjectiveStatus::kActive)
    {
        return MutateResult::kObjectiveNotActive;
    }

    // Apply delta with floor at zero and ceiling at target.
    const std::int64_t next = static_cast<std::int64_t>(o->progress)
                              + static_cast<std::int64_t>(delta);
    if (next < 0)
    {
        o->progress = 0;
    }
    else if (next > static_cast<std::int64_t>(o->target))
    {
        o->progress = o->target;
    }
    else
    {
        o->progress = static_cast<std::int32_t>(next);
    }

    if (o->progress >= o->target)
    {
        o->status = ObjectiveStatus::kComplete;
    }

    recompute_quest_after_objective_change(*q);
    views_dirty_ = true;
    return MutateResult::kOk;
}

MutateResult QuestLog::complete_objective(std::string_view quest_id,
                                          std::string_view obj_id)
{
    Quest* q = find_mut(quest_id);
    if (q == nullptr)
    {
        return MutateResult::kUnknownQuest;
    }
    if (q->status != QuestStatus::kActive)
    {
        return MutateResult::kQuestNotActive;
    }
    Objective* o = find_objective_mut(*q, obj_id);
    if (o == nullptr)
    {
        return MutateResult::kUnknownObjective;
    }
    // Allow completing an objective that is currently kActive. Already
    // completed/failed objectives are idempotent no-ops for the success
    // case but report kObjectiveNotActive so callers can detect the drift.
    if (o->status != ObjectiveStatus::kActive)
    {
        return MutateResult::kObjectiveNotActive;
    }

    o->progress = o->target;
    o->status   = ObjectiveStatus::kComplete;
    recompute_quest_after_objective_change(*q);
    views_dirty_ = true;
    return MutateResult::kOk;
}

MutateResult QuestLog::fail_objective(std::string_view quest_id,
                                      std::string_view obj_id)
{
    Quest* q = find_mut(quest_id);
    if (q == nullptr)
    {
        return MutateResult::kUnknownQuest;
    }
    if (q->status != QuestStatus::kActive)
    {
        return MutateResult::kQuestNotActive;
    }
    Objective* o = find_objective_mut(*q, obj_id);
    if (o == nullptr)
    {
        return MutateResult::kUnknownObjective;
    }
    if (o->status != ObjectiveStatus::kActive)
    {
        return MutateResult::kObjectiveNotActive;
    }

    o->status = ObjectiveStatus::kFailed;
    recompute_quest_after_objective_change(*q);
    views_dirty_ = true;
    return MutateResult::kOk;
}

// =============================================================================
// QuestLog - auto-complete / auto-fail rule
// =============================================================================

void QuestLog::recompute_quest_after_objective_change(Quest& q) noexcept
{
    if (q.status != QuestStatus::kActive)
    {
        return;
    }

    // A failed objective auto-fails the parent quest.
    bool any_failed = false;
    bool all_done   = true;
    for (const auto& o : q.objectives)
    {
        if (o.status == ObjectiveStatus::kFailed)
        {
            any_failed = true;
            break;
        }
        if (o.status != ObjectiveStatus::kComplete)
        {
            all_done = false;
        }
    }
    if (any_failed)
    {
        q.status = QuestStatus::kFailed;
        return;
    }
    // A zero-objective quest has all_done == true vacuously -- intentional.
    if (all_done)
    {
        q.status = QuestStatus::kComplete;
    }
}

// =============================================================================
// QuestLog - bucket views
// =============================================================================

std::span<const Quest> QuestLog::refresh_view(std::vector<Quest>& view,
                                              QuestStatus         status) const noexcept
{
    if (views_dirty_)
    {
        // Rebuild ALL four buckets in one pass; cheaper than maintaining a
        // dirty flag per bucket and the journal is small.
        inactive_view_.clear();
        active_view_.clear();
        completed_view_.clear();
        failed_view_.clear();
        for (const auto& q : quests_)
        {
            switch (q.status)
            {
                case QuestStatus::kInactive: inactive_view_.push_back(q);  break;
                case QuestStatus::kActive:   active_view_.push_back(q);    break;
                case QuestStatus::kComplete: completed_view_.push_back(q); break;
                case QuestStatus::kFailed:   failed_view_.push_back(q);    break;
            }
        }
        views_dirty_ = false;
    }
    // `view` is one of the four bucket caches; std::span over it is stable
    // until the next mutating call sets views_dirty_ again.
    (void)status;
    return std::span<const Quest> {view};
}

// =============================================================================
// QuestLog - serialize
// =============================================================================

std::vector<std::byte> QuestLog::serialize() const
{
    std::vector<std::byte> out;
    // Conservative pre-reservation: header + 64B per quest + 32B per
    // objective. Resizes are harmless but spare them for the common case.
    std::size_t obj_total = 0;
    for (const auto& q : quests_)
    {
        obj_total += q.objectives.size();
    }
    out.reserve(16U + (quests_.size() * 64U) + (obj_total * 32U));

    // ---- header ----
    for (const char c : kMagic)
    {
        out.push_back(static_cast<std::byte>(c));
    }
    write_u32(out, kVersion);
    write_u32(out, static_cast<std::uint32_t>(quests_.size()));

    // ---- payload: per-quest record ----
    for (const auto& q : quests_)
    {
        write_string(out, q.id);
        write_string(out, q.title);
        write_string(out, q.description);
        write_string(out, q.reward);
        write_u8(out, static_cast<std::uint8_t>(q.status));
        write_u32(out, static_cast<std::uint32_t>(q.objectives.size()));
        for (const auto& o : q.objectives)
        {
            write_string(out, o.id);
            write_string(out, o.description);
            write_u8(out, static_cast<std::uint8_t>(o.status));
            write_i32(out, o.progress);
            write_i32(out, o.target);
        }
    }

    return out;
}

// =============================================================================
// QuestLog - restore
// =============================================================================

RestoreResult QuestLog::restore(std::span<const std::byte> bytes)
{
    // Always reset to empty first so a failed restore leaves no half-state.
    quests_.clear();
    index_.clear();
    inactive_view_.clear();
    active_view_.clear();
    completed_view_.clear();
    failed_view_.clear();
    views_dirty_ = true;

    if (bytes.empty())
    {
        return RestoreResult::kEmptyBuffer;
    }

    std::size_t pos = 0;

    // ---- header ----
    if (bytes.size() < 12U)
    {
        return RestoreResult::kTruncated;
    }
    for (std::size_t i = 0; i < kMagic.size(); ++i)
    {
        const auto expected = static_cast<std::byte>(kMagic[i]);
        if (bytes[pos + i] != expected)
        {
            return RestoreResult::kMagicMismatch;
        }
    }
    pos += kMagic.size();

    std::uint32_t version = 0;
    if (!read_u32(bytes, pos, version))
    {
        return RestoreResult::kTruncated;
    }
    if (version != kVersion)
    {
        return RestoreResult::kVersionMismatch;
    }

    std::uint32_t quest_count = 0;
    if (!read_u32(bytes, pos, quest_count))
    {
        return RestoreResult::kTruncated;
    }

    // ---- payload: parse into a temporary log to reuse add_quest()'s
    // duplicate-id detection. We then patch quest + objective statuses
    // back on top, because add_quest() resets them to the canonical
    // "inactive, zero progress" state.
    QuestLog tmp;
    std::vector<std::pair<QuestStatus, std::vector<std::pair<ObjectiveStatus, std::int32_t>>>>
        runtime_state;
    runtime_state.reserve(quest_count);

    for (std::uint32_t qi = 0; qi < quest_count; ++qi)
    {
        Quest q;
        if (!read_string(bytes, pos, q.id))         { return RestoreResult::kTruncated; }
        if (!read_string(bytes, pos, q.title))      { return RestoreResult::kTruncated; }
        if (!read_string(bytes, pos, q.description)){ return RestoreResult::kTruncated; }
        if (!read_string(bytes, pos, q.reward))     { return RestoreResult::kTruncated; }

        std::uint8_t status_byte = 0;
        if (!read_u8(bytes, pos, status_byte))      { return RestoreResult::kTruncated; }
        if (status_byte > static_cast<std::uint8_t>(QuestStatus::kFailed))
        {
            return RestoreResult::kCorrupt;
        }
        const auto q_status = static_cast<QuestStatus>(status_byte);

        std::uint32_t obj_count = 0;
        if (!read_u32(bytes, pos, obj_count))       { return RestoreResult::kTruncated; }

        std::vector<std::pair<ObjectiveStatus, std::int32_t>> obj_runtime;
        obj_runtime.reserve(obj_count);

        for (std::uint32_t oi = 0; oi < obj_count; ++oi)
        {
            Objective o;
            if (!read_string(bytes, pos, o.id))          { return RestoreResult::kTruncated; }
            if (!read_string(bytes, pos, o.description)){ return RestoreResult::kTruncated; }

            std::uint8_t o_status_byte = 0;
            if (!read_u8(bytes, pos, o_status_byte))     { return RestoreResult::kTruncated; }
            if (o_status_byte > static_cast<std::uint8_t>(ObjectiveStatus::kFailed))
            {
                return RestoreResult::kCorrupt;
            }
            const auto o_status = static_cast<ObjectiveStatus>(o_status_byte);

            if (!read_i32(bytes, pos, o.progress))       { return RestoreResult::kTruncated; }
            if (!read_i32(bytes, pos, o.target))         { return RestoreResult::kTruncated; }

            obj_runtime.emplace_back(o_status, o.progress);

            // Stash the objective with its progress; status will be patched
            // back in after the move into tmp.
            o.status = ObjectiveStatus::kInactive;
            q.objectives.push_back(std::move(o));
        }

        runtime_state.emplace_back(q_status, std::move(obj_runtime));

        const AddResult add_res = tmp.add_quest(std::move(q));
        if (add_res != AddResult::kOk)
        {
            return RestoreResult::kCorrupt;
        }
    }

    // Patch quest + objective runtime state back onto the temporary log.
    for (std::size_t i = 0; i < tmp.quests_.size(); ++i)
    {
        Quest& q = tmp.quests_[i];
        q.status = runtime_state[i].first;
        const auto& obj_runtime = runtime_state[i].second;
        // add_quest normalised target/progress, but we want the saved
        // progress + status. Restore them now.
        for (std::size_t j = 0; j < q.objectives.size(); ++j)
        {
            q.objectives[j].status   = obj_runtime[j].first;
            q.objectives[j].progress = obj_runtime[j].second;
        }
    }

    // Commit.
    quests_ = std::move(tmp.quests_);
    index_  = std::move(tmp.index_);
    views_dirty_ = true;
    return RestoreResult::kOk;
}

}  // namespace cd::game::quest
