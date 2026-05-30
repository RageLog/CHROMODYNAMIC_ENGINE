# cd::game_anim_graph

## Purpose
Composable **animation graph** for gameplay-driven character animation. Wraps
`cd::anim::SkinnedClip / Skeleton / Pose / blend_pose` behind a single
`AnimNode` polymorphic base so leaves (clip players), 1D / 2D blend trees,
and predicate-driven state machines can be nested into arbitrary trees.

Phase G2.3 in the engine roadmap (phase 496).

## Namespace
`cd::game::anim_graph`

## Public headers
- `include/cd/game/anim_graph/AnimGraph.hpp` — `Blackboard`, `AnimNode`
  base, `PlayClipNode`, `Blend1DNode`, `Blend2DNode`, `StateMachineNode`,
  and the `AnimGraph` wrapper.

## Primary types
| Type                | Role                                                            |
|---------------------|-----------------------------------------------------------------|
| `Blackboard`        | `string -> float` scratch store; consulted by blend / FSM nodes.|
| `AnimNode`          | Abstract base; subclasses override `tick(t, bb, skel)`.         |
| `PlayClipNode`      | Wraps a `cd::anim::SkinnedClip` with loop / clamp + speed.      |
| `Blend1DNode`       | N inputs sorted by threshold; param selects bracketing pair.    |
| `Blend2DNode`       | 4-corner Cartesian bilinear blend (Mecanim Freeform Cartesian). |
| `StateMachineNode`  | Predicate-driven transitions between any `AnimNode` subtrees.   |
| `AnimGraph`         | Owns the root + blackboard; `evaluate(skel, t)` per frame.      |

## Semantics
- **Sampling model**: every node returns a fresh `cd::anim::Pose`. The
  caller drives the time axis (`AnimGraph::evaluate(skel, t)`); leaves
  map `t` onto their clip's local axis (`fmod` when looped,
  `clamp` otherwise).
- **Blend1D**: thresholds are strictly ascending; `add_input` rejects
  out-of-order entries. Param outside the range pins to the endpoint.
- **Blend2D**: two y-axis 1D blends followed by one x-axis blend —
  bitwise-equivalent to the closed-form `(1-u)(1-v)·P00 + ...` weight
  sum, but allocates 4 poses instead of 4 simultaneous blends.
- **StateMachine**: the first matching predicate per tick wins. Optional
  `blend_duration` LERPs the outgoing state's last pose toward the new
  state's current sample over that many seconds.
- **Defensive defaults**: a null clip, null root, empty blend tree, or
  state-less FSM all return `Pose::bind_pose(skel)` so callers never
  see a crash during asset hot-swap or graph construction.
- **Ownership**: every child is held via `std::unique_ptr<AnimNode>`;
  swapping or dropping the root releases the entire owned subtree.

## Usage example
```cpp
#include <cd/anim/Skeleton.hpp>
#include <cd/game/anim_graph/AnimGraph.hpp>

using namespace cd::game::anim_graph;

// Build "idle <-> walk" with a speed-driven Blend1D under the walk state.
auto idle = std::make_unique<PlayClipNode>(&idle_clip, /*loop=*/true);

auto walk_blend = std::make_unique<Blend1DNode>("speed");
walk_blend->add_input(1.0F, std::make_unique<PlayClipNode>(&walk_clip,  true));
walk_blend->add_input(5.0F, std::make_unique<PlayClipNode>(&run_clip,   true));

auto fsm = std::make_unique<StateMachineNode>();
const auto idle_idx = fsm->add_state("idle", std::move(idle));
const auto walk_idx = fsm->add_state("walk", std::move(walk_blend));
fsm->add_transition(idle_idx, walk_idx,
    [](const Blackboard& bb, float) { return bb.get("speed") > 0.1F; },
    /*blend_duration=*/0.15F);
fsm->add_transition(walk_idx, idle_idx,
    [](const Blackboard& bb, float) { return bb.get("speed") < 0.05F; },
    0.15F);

AnimGraph graph;
graph.set_root(std::move(fsm));
graph.set_param("speed", 0.0F);

const auto pose = graph.evaluate(skeleton, /*t=*/0.016F);
```

## Build / Test
```bash
cmake --build --preset ninja-debug --target cd_game_anim_graph
ctest --preset ninja-debug -R game_anim_graph --output-on-failure
```

## Test coverage
8+ gtest cases under `tests/test_anim_graph.cpp`:
1. PlayClip returns clip pose at given time.
2. Blend1D at param=0 returns input A; at param=1 returns input B.
3. Blend1D at param=0.5 returns the mid LERP.
4. Blend2D 4-corner mix at the rectangle's center equals the average.
5. StateMachine transitions when its predicate fires.
6. Param accessor read / write through the blackboard.
7. Missing node returns the default (bind) pose without crashing.
8. Time scrubbing forward and backward through PlayClipNode.
9. (extra) GC: replacing the root releases the previous subtree safely.

## Dependencies
- `cd::core` — `Defines.hpp`.
- `cd::math` — `Transform`, `Vector`, `Quaternion` (via `cd::anim`).
- `cd::anim` — `Skeleton`, `Pose`, `SkinnedClip`, `blend_pose`.

No reverse edges: `cd::game::anim_graph` sits strictly above `cd::anim`
and strictly below the scene / sample / editor tiers (CLAUDE.md §7).

## References
- Mecanim (Unity) animator graph + Blend Tree (1D / 2D Simple
  Directional / 2D Freeform Cartesian).
- Persson. *Animation in production engine* (GDC 2010) — production
  AnimGraph + AnimStateMachine.
- Bevy `bevy_animation_graph` crate — `BlendNode` + `AnimationClip`
  drivers.
- glTF 2.0 skinning spec — clip time / pose layout reference.

## Notes
- Single TU (`src/AnimGraph.cpp`); header carries inline-trivial
  accessors only.
- Thread-safety: not thread-safe by design. One graph per controller,
  ticked from the owning thread (same contract as
  `cd::anim::AnimStateMachine`, `cd::game::ai_bt::BehaviorTree`).
- Future work (out of scope for G2.3): N-way Cartesian + polar Blend2D,
  IK / additive layers, JSON / glTF graph loader, frame-allocator pose
  buffers (see ADR-W7-S3 pose-arena draft).
