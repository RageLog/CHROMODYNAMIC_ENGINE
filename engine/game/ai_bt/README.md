# cd::game_ai_bt

## Purpose
Behavior Tree (BT) library for game AI. Provides the canonical composite /
decorator / leaf node families, a `Blackboard` key-value scratch store, and a
`BehaviorTree` wrapper that owns the root node and publishes `dt` per tick.

Phase 473 — G2.2 in the engine roadmap.

## Namespace
`cd::game::ai_bt`

## Public headers
- `include/cd/game/ai_bt/BehaviorTree.hpp` — `Status` enum, `Blackboard`,
  `Node` base, `LeafNode<F>` + `make_leaf()`, composite nodes
  (`SequenceNode`, `SelectorNode`, `ParallelNode`), decorator nodes
  (`InverterNode`, `RepeaterNode`, `UntilSuccessNode`), and the
  `BehaviorTree` wrapper.

## Primary types
| Type                | Role                                                            |
|---------------------|-----------------------------------------------------------------|
| `Status`            | `kRunning` / `kSuccess` / `kFailure` — the universal contract.  |
| `Blackboard`        | `string -> variant<bool,int,float,string>` scratch store.       |
| `Node`              | Abstract base; subclasses override `tick(Blackboard&)`.         |
| `LeafNode<F>`       | Templated leaf adapting any `Status(Blackboard&)` callable.     |
| `SequenceNode`      | AND — short-circuit on first failure, resume from running child.|
| `SelectorNode`      | OR — short-circuit on first success, resume from running child. |
| `ParallelNode`      | Ticks all children; aggregates via success / failure thresholds.|
| `InverterNode`      | Flips `kSuccess` <-> `kFailure`; passes `kRunning` through.     |
| `RepeaterNode`      | Tick the child N times (0 = forever, yields each iteration).    |
| `UntilSuccessNode`  | Retries the child until it reports `kSuccess`.                  |
| `BehaviorTree`      | Owns the root, publishes `dt`, exposes `tick(bb, dt)`.          |

## Semantics
- **Memory sequence / selector**: composites remember the running child and
  resume from it on the next tick instead of re-ticking finished siblings,
  matching Champandard's "memory" formulation.
- **Parallel thresholds**: `success_threshold == 0` means "all children";
  `failure_threshold` defaults to 1 (any failure aborts). This is the
  standard parallel-AND policy used by most engines.
- **Decorator reset**: every composite / decorator resets the relevant
  cursor / iter / child state when a branch wins so the next entry starts
  from a clean state.
- **No allocations on tick**: child storage is owned via `std::unique_ptr`
  built up during tree construction; the tick path touches no heap.

## Usage example
```cpp
#include <cd/game/ai_bt/BehaviorTree.hpp>

using namespace cd::game::ai_bt;

auto root = std::make_unique<SelectorNode>();
{
    // Branch A: enemy in sight -> attack.
    auto seq = std::make_unique<SequenceNode>();
    seq->add_child(make_leaf([](Blackboard& bb) {
        return bb.get_bool("enemy_visible") ? Status::kSuccess : Status::kFailure;
    }));
    seq->add_child(make_leaf([](Blackboard& bb) {
        // ... fire weapon ...
        bb.set_int("rounds_fired", bb.get_int("rounds_fired") + 1);
        return Status::kSuccess;
    }));
    root->add_child(std::move(seq));
}
{
    // Branch B: patrol.
    root->add_child(make_leaf([](Blackboard&) {
        // ... advance patrol waypoint ...
        return Status::kRunning;
    }));
}

BehaviorTree tree(std::move(root));
Blackboard   bb;
tree.tick(bb, /*dt=*/0.016F);
```

## Build / Test
```bash
cmake --build --preset ninja-debug --target cd_game_ai_bt
ctest --preset ninja-debug -R game_ai_bt --output-on-failure
```

## Dependencies
- `cd::core` — `Defines.hpp` (header-level dependency only).

## References
- Champandard, Alex. *Behavior Trees for Next-Gen Game AI*, AIGameDev / GDC,
  2008.
- Marzinotto, Colledanchise, Smith, Ögren. *Towards a Unified Behavior
  Trees Framework for Robot Control*, ICRA 2014.
- Colledanchise, Ögren. *Behavior Trees in Robotics and AI: An Introduction*,
  CRC Press, 2018.
- Reference implementations: `BehaviorTree.CPP`, `py_trees`, Unreal `UBT*`,
  Unity Behavior Designer.

## Notes
- Single TU (`src/BehaviorTree.cpp`); header carries only inline-trivial bits
  + the templated `LeafNode<F>` so most call sites instantiate one leaf type
  per lambda.
- Thread-safety: not thread-safe by design. One tree per controller, ticked
  from the owning thread.
- Future work (out of scope for G2.2): subtree references, condition-on-loop
  decorators, time-budget decorators, parallel "all-running-keep-running"
  policies, JSON / XML loader.
