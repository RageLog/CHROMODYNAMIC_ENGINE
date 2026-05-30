# cd::game::fsm

**Purpose**: Hierarchical Finite State Machine (HFSM) with nested regions and shallow/deep history pseudo-states. Phase 472 / G2.1 in the Phase-G gameplay tier.

**Namespace**: `cd::game::fsm`.

**Headers**: `cd/game/fsm/Fsm.hpp` (header-only template library; the `.cpp` is a translation-unit anchor only).

**Primary types**:

| Type | Role |
|---|---|
| `State<TContext>` | Leaf or composite state with optional `on_enter` / `on_exit` / `on_update(ctx, dt)` `std::function` hooks. |
| `StateMachine<TContext>` | Flat FSM: `add_state`, `add_transition(from, to, predicate)`, `start`, `tick(ctx, dt)`, `set_state`. |
| `HierarchicalFsm<TContext>` | Composite FSM: per-state nested regions via `add_region`, per-state history policy via `set_history`. `start_h` / `tick_h` recurse through the active sub-tree. |
| `HistoryKind` | `kNone` (default) / `kShallow` / `kDeep` — restore semantics on re-entry of a parent state. |

**Transitions are predicate-driven**: at each tick the machine walks the transitions out of the current state and the first whose predicate returns `true` against the user context fires. Self-transitions (`A -> A`) are explicit and run `on_exit` then `on_enter` on the same node.

**Usage example**:

```cpp
#include <cd/game/fsm/Fsm.hpp>

struct AiCtx
{
    bool   see_enemy {false};
    float  patrol_t {0.0F};
};

using namespace cd::game::fsm;

HierarchicalFsm<AiCtx> root;
const auto idle    = root.add_state(std::make_unique<State<AiCtx>>("Idle"));
const auto combat  = root.add_state(std::make_unique<State<AiCtx>>("Combat"));

auto combat_region = std::make_unique<HierarchicalFsm<AiCtx>>();
const auto chase   = combat_region->add_state(std::make_unique<State<AiCtx>>("Chase"));
const auto attack  = combat_region->add_state(std::make_unique<State<AiCtx>>("Attack"));
combat_region->add_transition(chase, attack, [](const AiCtx& c) { return c.see_enemy; });
root.add_region(combat, std::move(combat_region));
root.set_history(combat, HistoryKind::kDeep);

root.add_transition(idle,   combat, [](const AiCtx& c) { return c.see_enemy; });
root.add_transition(combat, idle,   [](const AiCtx& c) { return !c.see_enemy; });

AiCtx ctx;
root.start_h(ctx);
for (int i = 0; i < 60; ++i)
{
    root.tick_h(ctx, 1.0F / 60.0F);
}
```

**Test command**: `ctest --preset ninja-debug -R cd_test_game_fsm --output-on-failure`.

**Notes**:

- Header-only template library. Single dependency on `cd::core` for `CD_NODISCARD`.
- Not thread-safe — one machine per owning thread; wrap externally if shared.
- Hooks are `std::function` so callers can use lambdas, free functions, or member functions without subclassing `State`. Subclass when you need polymorphic state-specific data.
- History snapshots are taken at the moment the parent exits; clearing `kNone` history erases any captured snapshot.
- Deep history captures the full path through the *first* orthogonal region at each level; other orthogonal regions cleanly restart (matches UML 2.5.1 deep-history semantics for AND-states).

**Design references**:

- Harel, David. "Statecharts: A Visual Formalism for Complex Systems." *Science of Computer Programming* 8(3): 231–274, 1987.
- OMG Unified Modeling Language 2.5.1, ch. 14 "State Machines" (regions, history pseudo-states, transition firing).
