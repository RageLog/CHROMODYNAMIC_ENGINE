# cd::game_quest

## Purpose
Objective tracker + journal for narrative-driven gameplay. Models each quest
as a `Quest` aggregate (id, title, description, ordered list of `Objective`
steps, reward blurb) and partitions the journal into four buckets --
`inactive`, `active`, `completed`, `failed` -- with auto-promotion driven by
objective state. A `QuestLog` owns many quests and exposes spans over each
bucket plus a self-describing binary serialise / restore pair.

Phase 500 / G4.2 in the engine roadmap (Phase-G gameplay tier).

## Namespace
`cd::game::quest`

## Public headers
- `include/cd/game/quest/Quest.hpp` -- `ObjectiveStatus` / `QuestStatus`
  enums, `Objective` / `Quest` aggregates, `AddResult` / `MutateResult` /
  `RestoreResult` status enums, and the `QuestLog` class.

## Primary types
| Type              | Role                                                                       |
|-------------------|----------------------------------------------------------------------------|
| `ObjectiveStatus` | `kInactive` / `kActive` / `kComplete` / `kFailed`.                         |
| `QuestStatus`     | Same lifecycle, applied to a whole `Quest`.                                |
| `Objective`       | One trackable step: `id`, `description`, `status`, `progress`, `target`.   |
| `Quest`           | Journal entry: `id`, `title`, `description`, `objectives`, `reward`.       |
| `QuestLog`        | Owns the quests, partitions them by status, auto-promotes on objective change. |
| `AddResult`       | Validation tags for `add_quest` (empty / duplicate ids).                   |
| `MutateResult`    | Status tags for `activate` / `progress` / `complete_objective` / `fail_objective`. |
| `RestoreResult`   | Status tags for binary `restore`.                                          |

## Semantics
- **Auto-complete**: when every objective in an active quest transitions to
  `kComplete` the parent quest is moved to the completed bucket
  automatically. Callers never call a `complete_quest` API -- objective
  state IS quest state.
- **Auto-fail**: the first `kFailed` objective fails the parent quest. The
  remaining objectives keep their own statuses for journal-rendering
  purposes but the quest itself moves to the failed bucket.
- **Counter-style progress**: `progress(quest_id, obj_id, delta)` increments
  an objective's counter. Negative deltas clamp at zero (Skyrim journal
  parity: dropping a tracked item never reduces the kill counter to a
  negative number). When `progress >= target` the objective auto-completes.
- **Flag-style progress**: use `target == 1` (the default). Set the flag by
  calling `complete_objective` or `progress(..., 1)`.
- **Bucket views**: `inactive_quests` / `active_quests` / `completed_quests`
  / `failed_quests` return `std::span<const Quest>` snapshots in insertion
  order. Spans remain valid until the next mutating call.
- **Zero-objective quests**: a quest with `objectives.empty()` is a valid
  "discovered location" pseudo-quest and auto-completes on activation.
  Matches the "Found landmark" entries in the Skyrim / BG3 journals.
- **Persistence**: `serialize()` returns a self-describing
  `std::vector<std::byte>` blob (magic `CDQL` + u32 version + per-quest
  records). `restore(bytes)` validates magic / version / structural
  invariants; a failed restore leaves the log empty so callers never see
  half-restored state. The library deliberately does NOT depend on
  `cd::game::save` -- the byte blob is the integration point.
- **Tolerant of authoring drift**: unknown quest / objective ids return a
  `kUnknownQuest` / `kUnknownObjective` tag rather than throwing or
  asserting. Re-activating an already-active quest reports `kAlreadyActive`
  without mutating state.

## Usage example
```cpp
#include <cd/game/quest/Quest.hpp>

using namespace cd::game::quest;

Quest q;
q.id          = "main_q01";
q.title       = "The Relic of Aenor";
q.description = "Recover the relic from the sunken vault.";
q.reward      = "200 gold + Ring of Tides";
q.objectives.push_back({"find_relic",    "Find the relic.",       {}, 0, 1});
q.objectives.push_back({"return_mentor", "Return to the mentor.", {}, 0, 1});

QuestLog log;
log.add_quest(std::move(q));
log.activate("main_q01");

// Counter-style example:
Quest w;
w.id          = "side_wolves";
w.title       = "The Howling Woods";
w.objectives.push_back({"slay_wolves", "Slay 7 wolves.", {}, 0, 7});
log.add_quest(std::move(w));
log.activate("side_wolves");

log.progress("side_wolves", "slay_wolves", 3);
log.progress("side_wolves", "slay_wolves", 4);   // -> auto-complete

for (const auto& q : log.active_quests())
{
    // render quest journal entry
}

// Persistence:
const auto blob = log.serialize();
QuestLog round_trip;
round_trip.restore(blob);
```

## Build / Test
```bash
cmake --build --preset ninja-debug --target cd_game_quest
ctest --preset ninja-debug -R game_quest --output-on-failure
```

## Dependencies
- `cd::core` -- `Defines.hpp` (header-level dependency only). No allocator,
  no math, no I/O beyond the in-memory byte-blob serialiser.

## References
- Brown, Chris (BioWare). *Quest System Design Patterns in the Aurora
  Engine.* Game Developers Conference, 2003 -- canonical journal /
  objective / reward triplet.
- Bethesda Game Studios. *Creation Engine Quest Documentation v1.5*
  (Creation Kit Wiki, <https://ck.uesp.net/wiki/Quest>), accessed 2026.
- Heaton, Tom. *A Circular Buffer Quest Tracker.* Game Developer
  Magazine, 2009 -- progress counter / target semantics.

## Notes
- Single translation unit (`src/Quest.cpp`) carries the QuestLog
  implementation and the binary serialiser.
- Thread-safety: not thread-safe by design. One QuestLog per owning thread.
- Future work (out of scope for G4.2): time limits, prerequisite quest
  chains, branching objective DAGs, localisation hooks, JSON loader.
