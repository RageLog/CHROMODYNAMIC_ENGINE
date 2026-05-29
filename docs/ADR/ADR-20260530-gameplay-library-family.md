# ADR-20260530 — Gameplay Library Family (cd::game::*)

Status: Proposed
Date: 2026-05-30
Branch: dev
Author: architect agent (orchestrated under team-lead)
Iglberger format: Context / Decision / Rejected Alternatives / Consequences

---

## 1. Context

### 1.1 Where the engine stands today

The current CHROMODYNAMIC tree carries a robust *systems* tier — the
layers a game studio expects an engine vendor to ship in source:

- `cd::rhi::*` + `cd::render::*` — rendering core (Vulkan + D3D12 +
  Metal + OpenGL backends, Forward+, RT, post stack, IBL).
- `cd::asset::*` — glTF, KTX2, audio decoders, hot-reload-capable
  asset graph.
- `cd::world::ecs` — sparse-set ECS + `ArchetypeWorld` side-layer.
- `cd::world::scene` — scene graph (LocalTransform / Parent /
  Children / Light / EnvironmentLight / ParticleSystem).
- `cd::world::input` — bindings, key chords, gamepad state, holds,
  double-click, axis abstraction.
- `cd::world::audio` — mixer, positional, HRTF, DSP graph, multiple
  backends.
- `cd::world::physics` — collision primitives + `IPhysicsWorld`
  abstraction + soft body params.
- `cd::world::anim` — `Skeleton`, `Animation`, `PoseBlend`,
  `BlendTree2`, `StateMachine`, `CurveTrack`, `EventTrack`, GPU
  skinning, dual-quat blends.
- `cd::world::net` — UDP, delta writer, snapshot buffer, prediction
  buffer, QoS tiers, retransmit.
- `cd::script` — Lua 5.4 binding stub.
- `cd::ui::*` — ImGui-backed widget kit + editor panels.

### 1.2 What an engine OFFERS beyond systems

Game developers do not ship `cd::rhi::DrawIndexed` calls — they ship
*gameplay*. The libraries they reach for daily are not in the systems
tier but a tier above: behavior authoring (FSM / BT), animation
authoring (anim-graph, IK, root motion), camera authoring
(cinemachine), persistence (save / settings), narrative (dialogue,
quests, l10n), interaction (triggers, spatial queries), and developer
ergonomics (asset hot-reload, gameplay particles, time control).

This tier is what differentiates Unity (Cinemachine, Input System,
Mecanim, Localization) and UE5 (Enhanced Input, AnimGraph, BehaviorTree,
SaveGame) from a "rendering library with ECS bolted on". Without this
family, CHROMODYNAMIC remains a *framework* — not an *engine*.

### 1.3 The gap we are closing

Today, a CHROMODYNAMIC user wanting to ship a 3rd-person action game
must hand-roll:

- A camera that follows the player with damping, collision push-out,
  blend transitions between cinematic shots.
- An action map so "W" and "Left Stick Up" both produce the same
  logical input.
- A save file that restores their world after a crash.
- A behavior tree for the enemy that patrols, sees, chases, attacks.
- A dialogue tree for the NPC, in three languages.
- A quest log that says "kill 5 boars" and ticks down.

None of these are research problems — but they are *expected library
deliverables* of any modern engine. This ADR enumerates the 15
gameplay libraries we will ship as `cd::game::*`, sequences them by
dependency + priority, and defines acceptance criteria.

### 1.4 Constraints inherited from CLAUDE.md and existing ADRs

- §7 Library-oriented: each subsystem its own `cd_<lib>` target with
  isolated `include/cd/<lib>/`, `src/`, `tests/`, CMakeLists.
- §1 Modern C++: C++23, `std::expected`, `[[nodiscard]]`, no raw
  ownership.
- §2 Research-first: each non-trivial library cites SOTA reference
  before implementation.
- DAG rule: no cycles. `cd::game::*` lives strictly above the world
  tier; it depends on world / ecs / scene / script but never the
  reverse.
- Run 35 refactor pattern: namespace nesting matches folder nesting
  (`engine/game/<lib>/include/cd/game/<lib>/`).

---

## 2. Decision

### 2.1 Top-line decision

Introduce a new top-level folder `engine/game/` containing 15
independently buildable libraries under the `cd::game::*` namespace.
Each library follows the canonical layout:

```
engine/game/<lib>/
  CMakeLists.txt
  README.md
  include/cd/game/<lib>/*.hpp
  src/*.cpp
  tests/test_<lib>.cpp
```

The umbrella target `cd_game` aggregates the family, parallelling
`cd_world` and `cd_render`.

### 2.2 The 15 libraries

#### G-01. `cd::game::time` — game clock + tick separation

Foundation for every other gameplay library. Separates *fixed tick*
(physics / network / determinism-required) from *variable tick*
(rendering / input poll / camera). Supports global time scale, pause,
slow-mo, hitstop, replay scrub.

Public surface (sketch): `Clock`, `FixedStep`, `VariableStep`,
`TimeScale`, `PauseToken`, `ReplayCursor`.

Reference: UE `FApp::GetDeltaTime` + `MaxSubsteps`, Unity
`Time.fixedDeltaTime`, Glenn Fiedler "Fix Your Timestep!".

#### G-02. `cd::game::fsm` — hierarchical finite state machine

Generic HFSM with transition tables, guards, entry / exit actions,
parallel regions, and an event bus. Distinct from `cd::anim::StateMachine`
(which is pose-driven and lives on the skeleton); this one is *generic*
and drives player state, UI state, NPC mode, game flow.

Public surface: `State<Ctx>`, `Transition<Ctx>`, `Machine<Ctx>`,
`History`, `Region`.

Reference: Mealy / Moore FSM theory, UE State Tree (UE5.3+), Unity
StateMachineBehaviour, Boost.MSM, gameprogrammingpatterns.com FSM.

#### G-03. `cd::game::ai_bt` — behavior tree

Sequence / Selector / Parallel / Inverter / RetryUntilSuccess /
RunUntilFailure decorators + leaf actions (sync + async / multi-frame).
A `Blackboard` shares state between nodes. The tree is *data-defined*
(loaded from JSON or built via fluent API), executable headless for
tests.

Public surface: `BT::Node`, `BT::Composite`, `BT::Decorator`,
`BT::Action`, `BT::Blackboard`, `BT::Tick`, `BT::Status`.

Reference: Champandard "Behavior Trees for Next-Gen AI" (2007), UE
BehaviorTree + EQS, Game AI Pro 3 chapter 6, Bevy Behave plugin.

#### G-04. `cd::game::anim_graph` — gameplay animation graph

Sits *above* `cd::world::anim`. Where `cd::anim::StateMachine` is the
*low-level* per-skeleton state, `cd::game::anim_graph` is the
*designer-facing* graph: parameter-driven blend trees (1D / 2D), state
machine over states-of-blend-trees, anim notifies (frame-locked
gameplay callbacks), root motion extraction, IK helpers (two-bone IK
for limbs, look-at IK for head, foot-IK with ground probe).

Phase G2 ships state-machine + blend trees only; Phase G5 adds IK +
root motion + events.

Public surface: `AG::Graph`, `AG::Param`, `AG::BlendTree1D`,
`AG::BlendTree2D`, `AG::Notify`, `AG::RootMotion`, `AG::TwoBoneIK`,
`AG::LookAtIK`.

Reference: Unity Mecanim, UE AnimGraph + Anim Notify, Bobby Anguelov
talks on character animation systems.

#### G-05. `cd::game::dialogue` — branching dialogue VM

Nodes: `Line`, `Choice`, `Conditional`, `Action`, `Goto`, `End`.
Variable substitution into lines (`"You have {gold} gold"`),
localization key lookup (defers to `cd::game::l10n`), voice + face
animation hooks (`OnLineStart`, `OnLineEnd` events to
`cd::game::anim_graph`).

Authoring format: YAML-like script compiled to a node graph; runtime
loads compiled form for performance.

Public surface: `Dialog::Script`, `Dialog::Cursor`, `Dialog::Choice`,
`Dialog::Variable`, `Dialog::Event`.

Reference: Yarn Spinner (Night in the Woods, A Short Hike), Ink
(80 Days, Heaven's Vault), Dialogic (Godot).

#### G-06. `cd::game::quest` — objective + journal system

A Quest is a graph of Objectives; an Objective is a triggerable
predicate ("HasItem(X) >= 5", "VisitedZone(Y)", "TalkedTo(Z)").
Quests can be linear (chain), parallel (any-order), or branching
(reward-dependent). Persistent state syncs to `cd::game::save`.

Public surface: `Quest::Definition`, `Quest::Objective`,
`Quest::Tracker`, `Quest::JournalEntry`, `Quest::Status`.

Reference: BG3 quest system (Larian internal), UE
QuestSystem plugin, Skyrim radiant quests (Bethesda GDC 2013).

#### G-07. `cd::game::input_binding` — action map

Binds logical actions (`"Jump"`, `"Move"`, `"Aim"`) to physical
inputs (keyboard / mouse / gamepad / touch). Supports composite
inputs (Move = WASD vector), modifier combos
(`Ctrl + Shift + Click`), dead-zones, sensitivity curves, per-device
binding profiles, user rebinding UI, and conflict detection.

Built on top of `cd::world::input` (which provides raw device state).

Public surface: `IB::Action`, `IB::Binding`, `IB::Composite`,
`IB::Modifier`, `IB::Profile`, `IB::RebindSession`.

Reference: Unity InputSystem (1.x), UE5 Enhanced Input, SDL_GameController.

#### G-08. `cd::game::save` — save / load / serialization

World snapshot to disk: ECS state, scene state, gameplay variables,
quest tracker, settings. Auto-save (timed + checkpoint), manual save
(slot-based), and cloud-save hook. Versioning + migration (so v0.1
saves load in v0.2 with a documented upgrade path). Compression via
zstd.

Phase G1 ships the basic framework (ECS-snapshot + slot files);
Phase G5 adds migration + cloud hook.

Public surface: `Save::Slot`, `Save::Writer`, `Save::Reader`,
`Save::Version`, `Save::Migration`, `Save::CompressionMode`.

Reference: UE SaveGame, Unity serialized JSON, RimWorld save schema,
Skyrim CoSave plugins (precedent for migration).

#### G-09. `cd::game::settings` — persistent user preferences

Graphics, audio, controls, accessibility, language, UI scale.
Profile-based (multiple users on one install), persisted to a
schema-versioned config file (TOML), live-update broadcast to
subscribers (so changing graphics mid-game applies immediately
without restart).

Public surface: `Settings::Group`, `Settings::Field`, `Settings::Profile`,
`Settings::Subscriber`, `Settings::Migration`.

Reference: UE GameUserSettings, Unity PlayerPrefs + custom config,
GNU getopt-long for accessibility precedent.

#### G-10. `cd::game::camera` — gameplay camera framework

The single library most users would *miss* the most if absent.
Cinemachine-style:

- *Virtual cameras* (priority-stacked, the highest-priority active one
  drives the render camera).
- *Body* component: how the camera *moves* (Follow, Orbit, FreeLook,
  Fixed, Rail, Look-Ahead).
- *Aim* component: how the camera *rotates* (Look-At, Composer,
  Hard-Look-At, POV).
- *Noise* component: handheld shake, recoil, breath.
- *Blend* between virtual cameras (linear / ease / curve, per-blend
  duration).
- *Collision push-out* (probe sphere, push toward target on
  occlusion).
- *Cinematic sequencer* hook: a timeline can sequence camera
  priorities for cutscenes.
- *Presets*: 1st-person, 3rd-person, top-down, isometric, side-scroller.

Public surface: `Cam::Virtual`, `Cam::Body`, `Cam::Aim`, `Cam::Noise`,
`Cam::Blend`, `Cam::Brain`, `Cam::Preset`.

Reference: Unity Cinemachine 2.x (Adam Myhill GDC 2018), UE5
CineCameraComponent + Sequencer, Itay Keren "Scroll Back" talk
(2D camera taxonomy).

#### G-11. `cd::game::query` — spatial + ECS query helpers

Gameplay-level queries that combine ECS + spatial indexing:

- `EntitiesInRadius(world, p, r)` → range of entities + transforms.
- `EntitiesInFrustum(world, frustum)` → in-view entities.
- `Raycast(world, ray, layer_mask)` → first hit + distance.
- `OverlapShape(world, shape, layer_mask)` → all overlapping entities.
- `LineOfSight(a, b, layer_mask)` → bool.

Built on top of `cd::ecs::World` (component lookup) + `cd::physics::Ray`
(geometric tests) + a per-world spatial hash / BVH (built by this
library, not by physics — physics has its own broadphase, but
gameplay queries need a *layer-masked* index that includes
trigger volumes and tagged-only entities).

Public surface: `Query::Overlap`, `Query::Raycast`, `Query::LOS`,
`Query::Layer`, `Query::Index`.

Reference: Unity Physics.OverlapSphere, UE
UWorld::OverlapMultiByChannel, NVIDIA PhysX scene queries.

#### G-12. `cd::game::l10n` — localization

Key → string lookup with:
- Plural rules (ICU CLDR-compatible: "one", "few", "many", "other").
- Gender variants (he / she / they / neutral).
- RTL marker (Arabic / Hebrew text direction).
- Fallback chain (en-GB → en → key-as-literal).
- Hot-reload of translation tables for in-engine editing.

Public surface: `L10n::Catalog`, `L10n::Key`, `L10n::Plural`,
`L10n::Gender`, `L10n::Locale`.

Reference: ICU MessageFormat, Unity Localization package,
gettext / .po precedent.

#### G-13. `cd::game::asset_hot_reload` — runtime asset reload

Watches the asset folder, dispatches reload events to subscribers
(texture, mesh, audio, dialogue, settings, l10n). Distinct from
`cd::shader::FileWatcher` (which is shader-only). Build-time
disable-able (`-DCD_GAME_HOT_RELOAD=OFF` for shipping).

Public surface: `HotReload::Watcher`, `HotReload::Event`,
`HotReload::Subscriber`, `HotReload::Mode`.

Reference: UE HotReload + Live Coding, Unity Asset Importer,
inotify / ReadDirectoryChangesW.

#### G-14. `cd::game::particles_event` — gameplay particle dispatcher

A thin gameplay layer above `cd::render::gpu_particles`. Designer
defines particle *recipes* ("dust_burst", "footstep_dust",
"hit_spark"); gameplay code emits them by name + transform.
Manages a *pool* + *budget* (so 5000 hit_sparks per second don't
melt the GPU; oldest-or-cheapest culling).

Public surface: `PFX::Recipe`, `PFX::Emit`, `PFX::Pool`,
`PFX::Budget`.

Reference: UE Cascade events / Niagara, Unity ParticleSystem +
Stop / Play API, GDC 2016 "GPU Particle Systems".

#### G-15. `cd::game::trigger` — trigger volumes + event sources

Box / Sphere / Capsule / Polygon trigger volumes. On-Enter / On-Exit /
On-Stay events. Layer-mask filtering. Replication-ready
(so a multiplayer trigger fires on server, syncs to client).

Public surface: `Trigger::Volume`, `Trigger::Event`, `Trigger::Filter`,
`Trigger::Channel`.

Reference: UE TriggerVolume + TriggerBox / TriggerSphere, Unity
OnTriggerEnter / Exit, Source engine `trigger_multiple`.

---

### 2.3 Phased delivery

#### Phase G1 — "Foundations" (~2 weeks)

| Sub | Library | Notes |
|---|---|---|
| G1.1 | `cd::game::time` | Foundation; everything else depends on it. |
| G1.2 | `cd::game::input_binding` | Wraps `cd::world::input`. |
| G1.3 | `cd::game::save` (basic) | World snapshot only; no migration. |
| G1.4 | `cd::game::settings` | Persistent prefs + live broadcast. |
| G1.5 | Sample: `hello_game` | Fixed tick + load/save + input remap. |

Exit criteria: `hello_game` runs, lets the user remap "Jump" to a
custom key, saves to slot 1, restores from slot 1, persists settings
across runs.

#### Phase G2 — "AI + Animation Graph" (~3 weeks)

| Sub | Library | Notes |
|---|---|---|
| G2.1 | `cd::game::fsm` | HFSM with regions + history. |
| G2.2 | `cd::game::ai_bt` | Sequence / Selector / Async actions. |
| G2.3 | `cd::game::anim_graph` (Phase 1) | State machine + 1D / 2D blends. |
| G2.4 | Sample: `hello_npc` | Patrol bot with FSM + BT + blend. |

Exit criteria: `hello_npc` shows an NPC patrolling between waypoints,
chasing on sight (BT), walk → run blend at speed > threshold
(anim_graph).

#### Phase G3 — "World interaction" (~3 weeks)

| Sub | Library | Notes |
|---|---|---|
| G3.1 | `cd::game::query` | Spatial + ECS query helpers. |
| G3.2 | `cd::game::trigger` | Volumes + events. |
| G3.3 | `cd::game::camera` | Cinemachine-style virtual camera stack. |
| G3.4 | `cd::game::particles_event` | Recipe-driven dispatch. |
| G3.5 | Sample: `hello_world` | 3rd-person char with camera + triggers + PFX. |

Exit criteria: `hello_world` shows a 3rd-person character with
damping camera, blend-to-cinematic-shot on trigger enter, dust-burst
PFX on jump-land.

#### Phase G4 — "Narrative" (~2 weeks)

| Sub | Library | Notes |
|---|---|---|
| G4.1 | `cd::game::dialogue` | Branching VM. |
| G4.2 | `cd::game::quest` | Objective tracker + journal. |
| G4.3 | `cd::game::l10n` | Plural + gender + RTL + fallback. |
| G4.4 | Sample: `hello_narrative` | Branching NPC + quest in 3 locales. |

Exit criteria: `hello_narrative` runs the same NPC dialogue in
English, Turkish, and Arabic with correct plural / gender / RTL.

#### Phase G5 — "Polish + dev tooling" (~2 weeks)

| Sub | Library | Notes |
|---|---|---|
| G5.1 | `cd::game::asset_hot_reload` | Texture / mesh / audio / l10n. |
| G5.2 | `cd::game::anim_graph` (Phase 2) | IK + root motion + events. |
| G5.3 | `cd::game::save` (Phase 2) | Versioning + migration + cloud hook. |
| G5.4 | Settings + l10n hot-reload | Polish closure. |

Exit criteria: edit a texture / dialog / locale file while
`hello_narrative` runs → see update without restart. v0.1 save loads
in v0.2 build via documented migration.

Total: ~12 weeks for the full family.

---

## 3. Rejected alternatives

### 3.1 "Game devs write it themselves"

Rejected. This is precisely what an engine *offers*. Without these
libraries CHROMODYNAMIC is a rendering / ECS framework, not an
engine, and our stated quality bar (CLAUDE.md §0 "Filament / bgfx /
EnTT *aşılacak*") is meaningless if we don't ship past their tier.

### 3.2 Use external engines' libs (Bevy plugins, UE5 plugins)

Rejected. We cannot depend on competitor engines. Their *design
patterns* are valid references (and required research input per
CLAUDE.md §2), but code reuse violates licensing and the project
identity. We do permit external *non-engine* libs (zstd, ICU, RapidJSON)
where applicable.

### 3.3 One monolithic `cd::gameplay` library

Rejected. Violates CLAUDE.md §7 (library-oriented modularity). Each
subsystem is its own `cd_<lib>` target, standalone-buildable,
standalone-testable, individually consumable.

### 3.4 C++-only API (no scripting binding)

Partially rejected. C++23 is the *core* surface, but every gameplay
library exposes a Lua-callable façade via `cd::script`. Designers
authoring dialogue, quests, behavior trees, settings, and l10n keys
should not be forced to recompile.

### 3.5 Skip behavior trees, ship only FSM

Rejected. BTs and FSMs serve different niches (BTs for hierarchical
AI decision-making; FSMs for game-flow / UI / player-state). Ten
years of game AI literature (Champandard, Game AI Pro vols 1–3)
treats them as complementary, not substitutes. We ship both.

### 3.6 Skip a dedicated camera library; let users write a `CameraController` per-scene

Rejected. Camera authoring is *the* highest-leverage gameplay library
(Cinemachine's adoption rate inside Unity is near-universal).
Hand-rolled camera controllers are the #1 source of motion-sickness
bugs in indie titles. Shipping `cd::game::camera` with proven
preset configs is more user-time-saved per LOC than any other library
in this family.

### 3.7 Build `cd::game::save` on top of full ECS reflection

Deferred. Full reflection is a Section-D-grade project. Phase G1 uses
explicit per-component `Save::Adapter` registration (intrusive but
predictable). Phase G5 may revisit if reflection lands.

---

## 4. Consequences

### 4.1 Positive

- CHROMODYNAMIC becomes "game-authoring ready", not merely
  "rendering ready".
- Five new sample apps (`hello_game`, `hello_npc`, `hello_world`,
  `hello_narrative`, plus the legacy `hello_engine`) prove
  end-to-end gameplay paths.
- `cd::script` (Lua) becomes meaningfully consumable: every gameplay
  library has a Lua surface.
- The future editor (Section C L5) gains *behaviors* to author, not
  only *scenes*.
- The state-of-the-art comparison (§5) starts to look like a peer
  comparison, not aspirational.

### 4.2 Negative

- ~12 weeks of dedicated work (or parallel teams).
- 15 new libraries; cumulative README + test + CMake burden ≈ +150
  files.
- Boundary risk: `cd::game::query` overlaps with `cd::ecs` queries
  and `cd::physics` raycasts. Boundary is *layer-mask + gameplay-tag*
  awareness; physics raycasts are purely geometric, ECS queries are
  purely component-typed, gameplay queries combine both.
- `cd::game::camera` overlaps with `cd::scene::SceneCameraController`.
  The boundary: scene controller is the *editor / debug* free-fly
  camera; gameplay camera is the *runtime* designer-authored
  camera stack. The two coexist; the gameplay camera *drives* the
  underlying render camera when a virtual camera is active.
- `cd::game::anim_graph` and `cd::anim::StateMachine` could confuse
  users. Renaming `cd::anim::StateMachine` → `cd::anim::PoseStateMachine`
  is recommended for clarity (small follow-up change, not blocking).
- Some libraries (l10n, save versioning, settings migration) are
  *infrastructure-grade* work — necessary but unglamorous; expect
  these to be the longest-lived bug surfaces.

### 4.3 Library DAG impact

Updated dependency DAG:

```
foundation
  └── math / memory / concurrency / io / log
        └── rhi / asset
              └── render / shader / framegraph / material
                    └── ecs / scene / input / audio / physics / anim / net
                          └── script / ui
                                └── game::time
                                      ├── game::input_binding
                                      ├── game::save
                                      ├── game::settings
                                      ├── game::fsm
                                      ├── game::ai_bt
                                      ├── game::anim_graph
                                      ├── game::query
                                      ├── game::trigger
                                      ├── game::camera
                                      ├── game::particles_event
                                      ├── game::dialogue
                                      ├── game::quest
                                      ├── game::l10n
                                      └── game::asset_hot_reload
                                            └── editor / samples
```

No cycles. The gameplay tier sits strictly above world / script /
ui and strictly below editor / samples. Each `cd::game::*` library
depends only on rules at or below its own level.

---

## 5. State-of-the-art comparison

| Subsystem | Unity | UE5 | Godot 4 | CHROMODYNAMIC target |
|---|---|---|---|---|
| Time / fixed-tick | `Time.fixedDeltaTime` | `FApp::GetDeltaTime` | `_physics_process` | `cd::game::time` |
| FSM | `StateMachineBehaviour` | StateTree | Built-in nodes | `cd::game::fsm` (HFSM + regions) |
| Behavior tree | NavMesh + Animator + script | BehaviorTree + EQS | Community plugins | `cd::game::ai_bt` |
| Anim graph | Mecanim | AnimGraph + Notify | AnimationTree | `cd::game::anim_graph` |
| Dialogue | Asset Store (Yarn etc.) | Plugin | Asset Lib | `cd::game::dialogue` |
| Quest | Asset Store | Plugin | Asset Lib | `cd::game::quest` |
| Save | PlayerPrefs + custom | SaveGame | ConfigFile | `cd::game::save` (zstd + migration) |
| Input | InputSystem 1.x | Enhanced Input | InputMap | `cd::game::input_binding` |
| Camera | Cinemachine 2.x | CineCameraComponent + Sequencer | Camera3D | `cd::game::camera` |
| L10n | Localization package | LocalizationManager | `tr()` + TranslationServer | `cd::game::l10n` (ICU CLDR) |
| Spatial query | `Physics.OverlapSphere` | `OverlapMultiByChannel` | `PhysicsDirectSpaceState3D` | `cd::game::query` |
| Trigger | OnTriggerEnter / Exit | TriggerVolume / Box | Area3D | `cd::game::trigger` |
| Hot-reload | Asset importer | Live Coding | Editor reimport | `cd::game::asset_hot_reload` |
| Gameplay PFX | ParticleSystem events | Cascade / Niagara | GPUParticles3D | `cd::game::particles_event` |
| Settings | PlayerPrefs + custom | GameUserSettings | ConfigFile | `cd::game::settings` (TOML + live) |

CHROMODYNAMIC targets *parity-or-better* on each line. Where we
intentionally exceed (e.g. `cd::game::save` versioning + migration
+ cloud hook from day one, vs. UE's bolt-on plugin), that is called
out in the library's own README and ADR.

---

## 6. Integration with existing architecture

### 6.1 Existing libraries depended on (downward edges)

- `cd::ecs::World` + `cd::ecs::ArchetypeWorld` → component storage
  for FSM state, BT blackboard, quest tracker, trigger ownership.
- `cd::world::scene` → transforms (`LocalTransform`), parent / child
  (camera follow targets, trigger volume placement),
  `ParticleSystem` (recipe target).
- `cd::world::input` → raw device state (Bindings, GamepadState,
  Hold, DoubleClick) consumed by `cd::game::input_binding`.
- `cd::world::audio` → voice + positional source for dialogue
  playback hooks and PFX audio.
- `cd::world::physics` → `Ray`, `Sphere`, `Capsule`, `Triangle` for
  query primitives.
- `cd::world::anim` → `Skeleton`, `Animation`, `PoseBlend`,
  `BlendTree2`, `StateMachine` for the *underlying* pose evaluation
  driven by `cd::game::anim_graph`.
- `cd::world::net` → delta + snapshot infrastructure for replicated
  trigger volumes and quest state.
- `cd::script` → Lua façade per gameplay library.
- `cd::ui` → settings UI, rebinding UI, quest journal UI, dialog
  box widget.

### 6.2 No reverse edges

No existing library acquires a dependency on `cd::game::*`. The
gameplay tier is a strict *consumer* of the world / script / ui
tiers. Samples (`hello_*`) and the future editor (`cd::editor`) are
the *only* consumers of `cd::game::*`.

### 6.3 Boundary notes

- `cd::game::query` vs `cd::ecs::QuerySig`: ECS query is *type-
  filtered* ("entities with A + B"). Gameplay query is *spatial +
  layer-mask filtered* ("entities tagged Enemy within radius R").
  Implementation: gameplay query *uses* ECS query as a building
  block but layers a spatial hash / BVH on top.
- `cd::game::camera` vs `cd::scene::SceneCameraController`: scene
  controller is debug / editor free-fly. Gameplay camera is the
  designer-authored virtual camera stack that *drives* the render
  camera at runtime.
- `cd::game::anim_graph` vs `cd::anim::StateMachine`: gameplay graph
  is designer-facing, parameter-driven, frame-event-emitting.
  Underlying state machine is pose-driven. Renaming
  `cd::anim::StateMachine` → `cd::anim::PoseStateMachine` is a
  recommended companion change (separate small ADR).

### 6.4 CMake + build impact

- New `engine/game/CMakeLists.txt` aggregator, parallelling
  `engine/world/CMakeLists.txt`.
- 15 subfolders, each with its own CMakeLists and test target.
- Top-level CMakeLists adds `add_subdirectory(engine/game)`.
- `cd_game` interface target depends on all sub-libraries; users may
  consume the whole family with one link line or pick individual
  libraries.

### 6.5 Test discipline

Each library ships a gtest binary `test_game_<lib>`:

- Headless (no GPU dependency where possible — gameplay logic is GPU-
  agnostic except for PFX which uses `cd::render::gpu_particles`
  test harness).
- Anti-flakiness rule (CLAUDE.md §5): no `sleep_for`; advance
  `cd::game::time::Clock` manually in tests.
- Edge + negative tests mandatory (invalid binding, missing save
  slot, missing l10n key, BT infinite-loop guard, FSM unreachable
  state, etc.).

---

## 7. Recommended Phase G1 kickoff dispatch sequence

When this ADR is approved, the team-lead should dispatch in this
order:

1. **architect** → `cd::game::time` interface + ADR-companion
   (`engine/game/time/include/cd/game/time/Clock.hpp` + design
   notes).
2. In parallel:
   - **architect** → `cd::game::input_binding` interface
     (depends only on `cd::world::input`).
   - **architect** → `cd::game::settings` interface
     (depends only on `cd::io`).
3. After 1 + 2: **developer** → implementations for time +
   input_binding + settings.
4. **architect** → `cd::game::save` interface (depends on `cd::ecs`
   + zstd vcpkg port).
5. **developer** → save implementation (basic, no migration).
6. **developer** → `hello_game` sample exercising the four
   libraries end-to-end.
7. **tester** → smoke + edge tests on the four libraries.
8. **doc-writer** → README for each of the four libraries.

Estimated wall-clock: 10 working days with one architect + two
developers in parallel; 14 days solo.

---

## 8. Open questions (non-blocking)

1. Should `cd::game::time` own `cd::frame_timing` or coexist?
   (Tentative: coexist — frame_timing is render-tier, game::time is
   gameplay-tier; gameplay clock derives variable-step from frame
   timing but adds pause / scale / fixed-step.)
2. Save file format: binary-with-schema vs CBOR vs MessagePack?
   (Tentative: custom binary header + per-component schema versioning
   + zstd body; survey in G1.3 design ADR.)
3. Dialogue authoring DSL: in-house YAML, Yarn-compatible, or Ink-
   compatible? (Tentative: Yarn-compatible subset — broadest tooling
   ecosystem, MIT-licensed CLI compiler available.)
4. L10n key namespace strategy: hierarchical dotted keys vs flat?
   (Tentative: hierarchical — easier to scope per-library and
   per-scene.)

These are deferred to per-library design ADRs filed at the start of
each phase.

---

## 9. References

- Champandard, A. "Behavior Trees for Next-Gen Game AI" (GDC 2007).
- Rabin, S. (ed.) *Game AI Pro* vols 1–3.
- Myhill, A. "Cinemachine + Timeline" (Unity GDC 2018).
- Fiedler, G. "Fix Your Timestep!" (gafferongames.com).
- Keren, I. "Scroll Back: The Theory and Practice of Cameras in
  Side-Scrollers" (GDC 2015).
- Anguelov, B. "Building a Better Animation Authoring System"
  (talks.bobbyanguelov.com).
- ICU CLDR plural rules — unicode.org/reports/tr35-numbers/.
- Yarn Spinner docs — yarnspinner.dev.
- UE5 Enhanced Input — docs.unrealengine.com.
- Unity Input System 1.x — docs.unity3d.com/Packages/com.unity.inputsystem.
- Bevy `behave` plugin — github.com/RJ/behave.
- CHROMODYNAMIC CLAUDE.md §1, §2, §7 (library-oriented + research-first).
- ADR-20260522-ecs-storage-validation (boundary precedent for
  combining ECS storage with side-layers).

---

End of ADR-20260530-gameplay-library-family.
