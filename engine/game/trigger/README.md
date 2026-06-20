# cd::game::trigger

**Purpose**: Trigger volumes + event sources. Phase 476 / G3.2 in the Phase-G gameplay tier.

**Namespace**: `cd::game::trigger`.

**Header**: `cd/game/trigger/Trigger.hpp`.

**Primary types**:

| Type | Role |
|---|---|
| `TriggerVolume` | Shape (AABB or Sphere) + `on_enter` / `on_stay` / `on_exit` callbacks + layer mask + enabled flag. |
| `Subject` | `{entity, position, layer}` triple fed each tick - the things that may enter / exit the volumes. |
| `TriggerWorld` | Registry of volumes keyed by owner `cd::ecs::Entity`. `add_trigger`, `remove_trigger`, `set_enabled`, `tick(query_world, dt, subjects)`. |
| `LayerMask` | 64-bit channel mask; `kAllLayers` (default) fires on every subject regardless of `Subject::layer`. |

**Event contract** (Unity / Unreal parity):

* **on_enter** fires once on the tick a subject transitions outside -> inside.
* **on_stay** fires every tick a subject is sampled inside *after* its own on_enter.
* **on_exit** fires once on the tick a subject transitions inside -> outside, *or* the tick that subject disappears from the subject list while occupied (implicit despawn-exit).

**Same-frame teleport semantics**: a subject whose sampled position is outside on tick N and outside on tick N+1 fires NO events even if its trajectory crossed the volume. Discrete sampling matches Unity `OnTriggerEnter` and Unreal `OnComponentBeginOverlap`; callers needing swept-CCD overlap wrap this layer with their own pre-pass.

**Usage example**:

```cpp
#include <cd/game/trigger/Trigger.hpp>

using namespace cd::game::trigger;

TriggerWorld world;

TriggerVolume v;
v.name     = "ammo_pickup";
v.shape    = cd::physics::Aabb {{-1,-1,-1}, {1,1,1}};
v.on_enter = [](cd::ecs::Entity who) { /* grant ammo */ };
v.on_exit  = [](cd::ecs::Entity who) { /* clear UI prompt */ };

cd::ecs::Entity pickup_entity {/* from ECS */};
world.add_trigger(pickup_entity, std::move(v));

// Each gameplay tick:
std::vector<Subject> subjects;
subjects.push_back({player_entity, player_position, /*layer*/ 0U});
world.tick(/*query_world*/ nullptr, dt, subjects);
```

**Layer filtering**: each `TriggerVolume::layer_mask` is a 64-channel bitmask; each `Subject::layer` is an integer in `[0, 63]`. The volume fires only when `(1ULL << subject.layer) & volume.layer_mask` is non-zero. Subjects with `layer >= 64` are clamped to channel 0 rather than UB-shifting.

**Dispatch order per tick** (per enabled volume):

1. Filter subjects by `layer_mask`.
2. For each surviving subject, compare last-tick occupancy with this-tick inside test:
   * `inside && !was_in` -> `on_enter`, mark occupied.
   * `inside &&  was_in` -> `on_stay`.
   * `!inside &&  was_in` -> `on_exit`, clear.
3. After the main loop, any occupied (owner, subject) pair NOT touched this tick fires `on_exit` (implicit despawn).

**Spatial backend** (SEALED for G3.2 — out-of-charter): `tick()` accepts an opaque `cd::game::query::QueryWorld*` (forward-declared; cd::game::query is G3.1, already at 100%). The header path is null-tolerant: passing `nullptr` activates the brute-force O(V*S) fallback, which is the only wired path. When the caller wants to exploit the indexed broad-phase, they pass a live `QueryWorld*`; the body-only plumbing is a single future commit with zero API churn. The brute-force path handles all G3.2 use-cases (tens of triggers, single-digit subjects) and is the only path exercised by the test suite.

**Test command**: `ctest --preset ninja-debug -R cd_test_game_trigger --output-on-failure`.

**Notes**:

- Single dependency layer on `cd::core` / `cd::math` / `cd::ecs` / `cd::physics`. No scene / render / world includes.
- Not thread-safe - one TriggerWorld per gameplay thread.
- `remove_trigger` does NOT fire on_exit for current occupants (the callback target is gone). `set_enabled(false)` also does not fire on_exit; re-enabling restarts occupancy fresh so a still-inside subject fires `on_enter` on the next tick.
- `clear_occupancy()` resets every volume's occupancy without removing volumes - useful for level reloads.
- `is_inside(owner, subject)` is the direct occupancy query.

**Design references**:

- Unity Technologies, "Physics.OnTriggerEnter / OnTriggerStay / OnTriggerExit" - Unity Manual / Scripting API.
- Epic Games, "Collision Overlap Events: OnComponentBeginOverlap / OnComponentEndOverlap" - Unreal Engine Documentation.
- Akenine-Moller, Haines, Hoffman. *Real-Time Rendering* 4e, ch. 22 "Intersection Test Methods" - point-in-AABB and point-in-sphere primitives used by the dispatcher.
