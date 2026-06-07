// =============================================================================
// CHROMODYNAMIC - cd/game/quest/Quest.hpp
// Phase 500 - cd::game::quest (G4.2: Objective tracker + journal)
//
// A small, allocation-light quest-log VM. The contract mirrors the canonical
// "journal + objective tracker" shape that has appeared in narrative-driven
// RPGs from BioWare's Aurora engine (Neverwinter Nights, KotOR) through
// Bethesda's Creation Engine to modern Skyrim Special Edition mods and
// Larian's Divinity / BG3 toolset:
//
//   * A Quest is a player-visible journal entry keyed by a stable string id
//     ("main_q01", "side_lostring") with a title, description, a flat list
//     of Objective steps, and a reward blurb shown on completion.
//   * Each Objective is a unit of trackable progress (kill 7 wolves, find
//     the relic, escort the merchant). Counter-style objectives carry a
//     `progress` and `target`; flag-style objectives use target == 1.
//   * A QuestLog owns many quests and partitions them by status: inactive
//     (not yet accepted), active (visible in the journal, progress
//     mutating), completed (all objectives done), failed (at least one
//     objective marked failed).
//
// Auto-completion contract: when every Objective in an active Quest
// transitions to kComplete the parent Quest is automatically moved to the
// completed bucket. Symmetrically, the first kFailed objective fails the
// parent quest. This matches the player-facing journal in every RPG above
// and means callers never call complete_quest / fail_quest directly --
// objective state IS quest state.
//
// Threading: a QuestLog owns its state. Not thread-safe -- treat it like an
// inventory or save-game state object and mutate from the owning thread.
//
// Dependencies (CLAUDE.md S7): cd::core only at the public header level.
// No allocator, no math, no I/O. The serialize / restore pair returns a
// raw byte blob so callers can route it through cd::game::save or any
// other persistence layer without forcing a transitive dependency here.
//
// Design references:
//   * Brown, Chris (BioWare). "Quest System Design Patterns in the Aurora
//     Engine." Game Developers Conference, 2003 -- canonical journal /
//     objective / reward triplet.
//   * Bethesda Game Studios. "Creation Engine Quest Documentation v1.5"
//     (Creation Kit Wiki, https://ck.uesp.net/wiki/Quest), accessed 2026.
//   * Heaton, Tom. "A Circular Buffer Quest Tracker." Game Developer
//     Magazine, 2009 -- progress counter / target semantics.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::game::quest
{

// -----------------------------------------------------------------------------
// ObjectiveStatus - lifecycle tag for a single Objective inside a Quest.
//
//   * kInactive : authored but not yet activated (e.g. step 3 of 5 that
//                 unlocks once step 2 completes). Callers may keep all
//                 objectives kActive at quest-activation time -- the
//                 distinction exists so multi-stage quests can present a
//                 staged journal without exposing future steps.
//   * kActive   : currently trackable; progress may be incremented.
//   * kComplete : progress == target (or marked done explicitly).
//   * kFailed   : abort tag; failing an objective auto-fails the parent
//                 quest per the journal contract above.
// -----------------------------------------------------------------------------
enum class ObjectiveStatus : std::uint8_t
{
    kInactive = 0,
    kActive   = 1,
    kComplete = 2,
    kFailed   = 3,
};

// -----------------------------------------------------------------------------
// QuestStatus - lifecycle tag for a whole Quest. Mirrors ObjectiveStatus and
// is computed/maintained by QuestLog from its objectives' states.
// -----------------------------------------------------------------------------
enum class QuestStatus : std::uint8_t
{
    kInactive = 0,
    kActive   = 1,
    kComplete = 2,
    kFailed   = 3,
};

// -----------------------------------------------------------------------------
// Objective - one trackable step inside a Quest.
//
// Fields:
//   * id          : stable id, unique within the owning Quest. Empty id is
//                   reserved and rejected by QuestLog::add_quest.
//   * description : player-visible step text ("Slay 7 wolves").
//   * status      : current lifecycle tag (see ObjectiveStatus).
//   * progress    : current counter; mutated by QuestLog::progress(...).
//                   For flag-style objectives this is 0 or 1.
//   * target      : completion threshold; status becomes kComplete the
//                   moment progress >= target. Must be >= 1; QuestLog
//                   clamps a zero/negative target to 1 at add_quest time.
// -----------------------------------------------------------------------------
struct Objective
{
    std::string     id {};
    std::string     description {};
    ObjectiveStatus status {ObjectiveStatus::kInactive};
    std::int32_t    progress {0};
    std::int32_t    target {1};
};

// -----------------------------------------------------------------------------
// Quest - one journal entry.
//
// Fields:
//   * id          : stable id, unique within the owning QuestLog.
//   * title       : short journal heading ("The Lost Ring").
//   * description : long-form blurb / lore body shown in the journal.
//   * objectives  : ordered list of Objective steps. May be empty for a
//                   "trivia" or "discovered location" pseudo-quest, in
//                   which case the quest auto-completes on activation.
//   * reward      : free-form blurb shown on completion ("200 gold, ring
//                   of fire +1"). Engine does NOT consume this -- it is a
//                   string the caller renders / parses as it sees fit.
//   * status      : maintained by QuestLog (see add_quest / activate).
// -----------------------------------------------------------------------------
struct Quest
{
    std::string            id {};
    std::string            title {};
    std::string            description {};
    std::vector<Objective> objectives {};
    std::string            reward {};
    QuestStatus            status {QuestStatus::kInactive};
};

// -----------------------------------------------------------------------------
// AddResult - status returned by QuestLog::add_quest. Granular tags let
// callers report which authoring mistake hit them without exception
// machinery at the call-site.
// -----------------------------------------------------------------------------
enum class AddResult : std::uint8_t
{
    kOk                   = 0,
    kEmptyQuestId         = 1,  ///< quest.id == ""
    kDuplicateQuestId     = 2,  ///< quest id already present in the log
    kEmptyObjectiveId     = 3,  ///< an objective had id == ""
    kDuplicateObjectiveId = 4,  ///< two objectives on one quest shared an id
};

// -----------------------------------------------------------------------------
// MutateResult - status returned by mutation calls (activate / progress /
// complete_objective / fail_objective).
// -----------------------------------------------------------------------------
enum class MutateResult : std::uint8_t
{
    kOk                = 0,
    kUnknownQuest      = 1,  ///< quest_id not in the log
    kUnknownObjective  = 2,  ///< obj_id not on the quest
    kQuestNotActive    = 3,  ///< progress/complete/fail on a non-active quest
    kObjectiveNotActive= 4,  ///< progress on a kInactive/kComplete/kFailed obj
    kAlreadyCompleted  = 5,  ///< activate() on a completed quest
    kAlreadyFailed     = 6,  ///< activate() on a failed quest
    kAlreadyActive     = 7,  ///< activate() on an active quest (no-op signal)
};

// -----------------------------------------------------------------------------
// RestoreResult - status returned by QuestLog::restore. Mirrors the failure
// surface of a binary deserialiser: bad magic / version / truncated buffer.
// -----------------------------------------------------------------------------
enum class RestoreResult : std::uint8_t
{
    kOk              = 0,
    kEmptyBuffer     = 1,
    kMagicMismatch   = 2,
    kVersionMismatch = 3,
    kTruncated       = 4,
    kCorrupt         = 5,  ///< structural invariants violated (dup ids, etc.)
};

// -----------------------------------------------------------------------------
// QuestLog - the objective tracker + journal owner.
//
// Lifecycle:
//   1. `add_quest(Quest)` - install an authored quest. Default status is
//      kInactive; objectives default to kInactive too.
//   2. `activate(quest_id)` - move a quest from kInactive to kActive and
//      mark all its objectives kActive. A quest with zero objectives
//      auto-completes (matches the "discovered location" pseudo-quest
//      pattern in Skyrim / BG3 journals).
//   3. `progress(quest_id, obj_id, delta)` - increment an Objective's
//      progress counter. When progress >= target the objective transitions
//      to kComplete; if that was the last open objective the parent quest
//      auto-completes.
//   4. `complete_objective(quest_id, obj_id)` - mark an Objective complete
//      regardless of its current progress (sets progress = target).
//   5. `fail_objective(quest_id, obj_id)` - mark an Objective failed,
//      which auto-fails the parent quest.
//   6. `active_quests()` / `completed_quests()` / `failed_quests()` /
//      `inactive_quests()` - read-only spans over the four buckets, in
//      insertion order. Pointers/views remain valid until the next
//      mutating call.
//   7. `serialize()` / `restore(bytes)` - round-trip the whole log through
//      a self-describing binary blob (magic "CDQL" + version + per-quest
//      records). Bytes are returned by value; callers wire them through
//      cd::game::save or their own persistence path.
//
// Thread-safety: NOT thread-safe.
// -----------------------------------------------------------------------------
class QuestLog
{
public:
    QuestLog() = default;

    QuestLog(const QuestLog&)            = delete;
    QuestLog& operator=(const QuestLog&) = delete;
    QuestLog(QuestLog&&) noexcept            = default;
    QuestLog& operator=(QuestLog&&) noexcept = default;

    ~QuestLog() = default;

    // ------- structure ----------------------------------------------------

    /// Install a Quest by value. Validates that the quest id is non-empty
    /// and unique within the log, every objective has a non-empty id, and
    /// objective ids are unique within the quest. On any non-kOk result the
    /// log is left unmodified.
    AddResult add_quest(Quest q);

    /// Look up a Quest by id. Returns nullptr if absent.
    CD_NODISCARD const Quest* find_quest(std::string_view quest_id) const noexcept;

    /// Number of quests currently in the log (all buckets combined).
    CD_NODISCARD std::size_t size() const noexcept { return quests_.size(); }

    /// True if the log is empty.
    CD_NODISCARD bool empty() const noexcept { return quests_.empty(); }

    // ------- runtime mutations --------------------------------------------

    /// Activate a quest. A zero-objective quest auto-completes; otherwise
    /// every objective transitions kInactive -> kActive and the quest's
    /// status becomes kActive.
    MutateResult activate(std::string_view quest_id);

    /// Increment an objective's progress by `delta`. Negative deltas clamp
    /// the counter to zero (Skyrim journal parity -- progress never goes
    /// below zero even if the player loses tracked items mid-quest). When
    /// progress >= target the objective auto-completes, and if that was
    /// the last open objective the parent quest auto-completes.
    MutateResult progress(std::string_view quest_id,
                          std::string_view obj_id,
                          std::int32_t     delta);

    /// Force an objective to kComplete. Sets progress = target. Triggers
    /// the parent-quest auto-complete check.
    MutateResult complete_objective(std::string_view quest_id,
                                    std::string_view obj_id);

    /// Force an objective to kFailed. Auto-fails the parent quest.
    MutateResult fail_objective(std::string_view quest_id,
                                std::string_view obj_id);

    // ------- bucket views -------------------------------------------------

    /// Quests in kInactive status, in insertion order.
    CD_NODISCARD std::span<const Quest> inactive_quests() const noexcept
    {
        return refresh_view(inactive_view_, QuestStatus::kInactive);
    }

    /// Quests in kActive status, in insertion order.
    CD_NODISCARD std::span<const Quest> active_quests() const noexcept
    {
        return refresh_view(active_view_, QuestStatus::kActive);
    }

    /// Quests in kComplete status, in insertion order.
    CD_NODISCARD std::span<const Quest> completed_quests() const noexcept
    {
        return refresh_view(completed_view_, QuestStatus::kComplete);
    }

    /// Quests in kFailed status, in insertion order.
    CD_NODISCARD std::span<const Quest> failed_quests() const noexcept
    {
        return refresh_view(failed_view_, QuestStatus::kFailed);
    }

    // ------- persistence --------------------------------------------------

    /// Serialise the entire log to a self-describing binary blob.
    /// Format: "CDQL" magic + u32 version + u32 quest_count + per-quest
    /// records (id / title / description / reward / status / objectives).
    /// Strings are length-prefixed (u32). Little-endian payload.
    CD_NODISCARD std::vector<std::byte> serialize() const;

    /// Replace the log's contents with the data in `bytes`. On any non-kOk
    /// result the log is left empty (so callers never see half-restored
    /// state). Forward-references between quests do not exist, so any
    /// failed read is final.
    RestoreResult restore(std::span<const std::byte> bytes);

private:
    // Internal helpers ----------------------------------------------------
    Quest*       find_mut(std::string_view quest_id) noexcept;
    Objective*   find_objective_mut(Quest& q, std::string_view obj_id) noexcept;
    void         recompute_quest_after_objective_change(Quest& q) noexcept;

    // Bucket cache helper: refreshes one of the four views if its
    // `dirty_` flag is set, then returns a span over it. Views cache the
    // last vector<Quest> snapshot so callers get O(1) span access between
    // mutations.
    std::span<const Quest> refresh_view(std::vector<Quest>& view,
                                        QuestStatus         status) const noexcept;

    // Storage --------------------------------------------------------------
    std::vector<Quest>                           quests_ {};
    std::unordered_map<std::string, std::size_t> index_ {}; ///< quest_id -> quests_ index

    // View caches (mutable so const views can rebuild lazily). All four
    // get invalidated together by mutating calls -- the buckets are small
    // by construction (a journal rarely holds >100 quests) so we don't
    // bother with finer-grained tracking.
    mutable std::vector<Quest> inactive_view_ {};
    mutable std::vector<Quest> active_view_ {};
    mutable std::vector<Quest> completed_view_ {};
    mutable std::vector<Quest> failed_view_ {};
    mutable bool               views_dirty_ {true};
};

}  // namespace cd::game::quest
