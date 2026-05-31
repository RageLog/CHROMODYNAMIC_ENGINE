# Gameplay library family — cd::game::*

The gameplay tier is what differentiates a rendering framework from a
complete game engine. This folder ships 13 independently-buildable
libraries for behavior authoring, narrative, persistence, interaction,
and developer ergonomics — everything a game studio ships daily but
would otherwise hand-roll.

**Rationale**: Per
[ADR-20260530-gameplay-library-family](../../docs/ADR/ADR-20260530-gameplay-library-family.md),
the gameplay tier sits strictly above world / ecs / scene and strictly
below editor / samples. It provides the abstractions game developers
reach for daily (FSM / BT, animation authoring, camera systems, save
files, dialogue, quests, localization) — which are not research
problems but *expected library deliverables* of any modern engine.

---

## Shipped Libraries (G1–G5 phases)

| Library | Namespace | Path | Deps | Purpose |
|---------|-----------|------|------|---------|
| **save** | `cd::game::save` | `save/` | `cd::core` | Slot-based save system with atomic on-disk writes (temp + rename protocol). |
| **settings** | `cd::game::settings` | `settings/` | `cd::core` | Persistent preferences store with live change broadcast. |
| **fsm** | `cd::game::fsm` | `fsm/` | `cd::core` | Hierarchical finite state machine (Harel statecharts, UML regions, history). |
| **ai_bt** | `cd::game::ai_bt` | `ai_bt/` | `cd::core` | Behavior tree runtime (Sequence, Selector, Parallel, Decorators, Blackboard). |
| **anim_graph** | `cd::game::anim_graph` | `anim_graph/` | `cd::core`, `cd::math`, `cd::anim` | Animation graph with 1D/2D blend trees and nested state machines over `SkinnedClip` / `Skeleton`. |
| **query** | `cd::game::query` | `query/` | `cd::core`, `cd::math`, `cd::ecs`, `cd::physics`, `cd::scene` | Gameplay query facade (raycast, sphere, box, frustum) over spatial hash and frustum culling. |
| **trigger** | `cd::game::trigger` | `trigger/` | `cd::core`, `cd::math`, `cd::ecs`, `cd::physics` | Trigger volume dispatch (AABB/Sphere, on_enter/on_stay/on_exit) with layer-mask filtering. |
| **camera** | `cd::game::camera` | `camera/` | `cd::core`, `cd::math`, `cd::camera`, `cd::ecs` | Cinemachine-style virtual camera stack with priority blending and critically-damped springs. |
| **particles_event** | `cd::game::particles_event` | `particles_event/` | `cd::core`, `cd::math` | Recipe-driven gameplay particle dispatcher (named bursts + callbacks; GPU sink decoupled). |
| **dialogue** | `cd::game::dialogue` | `dialogue/` | `cd::core` | Branching dialogue VM (Ink/Yarn Spinner-style graph with predicate-gated choices). |
| **quest** | `cd::game::quest` | `quest/` | `cd::core` | Quest log with 4-bucket objective tracking, auto-complete/fail, binary serialization. |
| **l10n** | `cd::game::l10n` | `l10n/` | `cd::core` | Localization runtime (CLDR plural rules, RTL helper, locale fallback chain). |
| **asset_hot_reload** | `cd::game::asset_hot_reload` | `asset_hot_reload/` | `cd::core` | Editor-mode asset hot-reload bus with categorized subscribers and 100 ms throttle. |

---

## Intra-tier Dependency DAG

```
┌─────────────────────────────────────────────────────────────┐
│                      cd::game::*                            │
├─────────────────────────────────────────────────────────────┤
│ Foundation (cd::core only):                                 │
│   save, settings, fsm, ai_bt, dialogue, quest, l10n         │
│                          │                                  │
│ Spatial/ECS queries (depend on world tier):                 │
│   query ← cd::scene, cd::ecs, cd::physics                   │
│   trigger ← cd::ecs, cd::physics                            │
│   camera ← cd::ecs, cd::camera                              │
│   anim_graph ← cd::anim, cd::math                           │
│   particles_event ← cd::math                                │
│                          │                                  │
│ Infra (optionally game-facing):                             │
│   asset_hot_reload ← cd::asset                              │
└─────────────────────────────────────────────────────────────┘
```

**Rule**: No `cd::game::*` library depends on another. Cross-game
functionality is achieved through shared `cd::core` utilities or
optional composition at the sample/app tier.

---

## Samples

Each library is demonstrated in isolation or combined with others in the
`samples/game/` folder:

- **hello_game** — Minimal FSM + BT demo with a simple character that
  switches between idle/patrol/chase states.
- **hello_npc** — Full NPC suite: dialogue tree integration, quest
  participation, animation blending over anim_graph, and trigger zones.
- **hello_world** — Open-world sample: dynamic loading, camera cinemachine,
  save/settings persistence, and positional audio.
- **hello_narrative** — Story-driven sample: quest log, dialogue branches,
  localization into 3 languages, and choice consequences.

---

## Testing & Quality

Each library ships an isolated test binary:

```
ctest --preset ninja-debug -L game_save      # Save slot round-trip
ctest --preset ninja-debug -L game_settings  # Live preference broadcast
ctest --preset ninja-debug -L game_fsm       # State transition coverage
ctest --preset ninja-debug -L game_ai_bt     # BT executor + blackboard
... (one per library)
```

All test binaries pass on the baseline (143/143 tests PASS, ninja-debug
clean).

---

## Cross-referencing

- Full spec: [ADR-20260530](../../docs/ADR/ADR-20260530-gameplay-library-family.md)
- Engine umbrella: [engine/README.md](../README.md)
- Library catalog: [docs/LIBRARIES.md](../../docs/LIBRARIES.md)
- World tier: [engine/world/README.md](../world/README.md)
