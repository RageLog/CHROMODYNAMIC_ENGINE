# cd::ecs

**Purpose**: data-oriented Entity-Component-System. Primary storage is a
sparse-set `World` (EnTT-style O(1) add/remove/lookup, dense O(N)
iteration); an `ArchetypeWorld` side-layer offers archetype-table row
iteration for full-row workloads. A `Scheduler` orders systems by declared
component read/write conflicts (sequential + parallel dispatch), and a
`SystemGraph` topologically orders named systems.

**Namespace**: `cd::ecs`.

**Headers**: `cd/ecs/{World,Entity,ComponentStorage,EntityRange,ComponentMask,Lifecycle,Generation,QuerySig,FilterFn,Scheduler,SystemGraph,TagHelpers,ArchetypeWorld}.hpp`.

**Primary types**:
- `cd::ecs::World` -- sparse-set registry. `create()`, `destroy(e)`,
  `emplace<T>(e, args...)`, `remove<T>(e)`, `get<T>(e)`, `has<T>(e)`,
  `for_each<T>(fn)`, `each<T, Rest...>(fn)` (drives off the smallest
  matching pool; snapshots so mid-walk add/remove is safe). `query<T,
  Rest...>()` returns a cached `Query` that snapshots pool pointers once
  and tracks `structural_version()` for invalidation.
- `cd::ecs::Entity` -- `{ id, generation }`. Generational id guards against
  use-after-destroy; the slot is recycled with a bumped (odd=alive,
  even=free) generation so stale handles fail `is_alive`.
- `cd::ecs::SparseSet<T>` -- the per-component sparse-set storage behind
  the type-erased `IComponentStorage`.
- `cd::ecs::Scheduler` -- conflict-DAG scheduler. `tick(World&)`
  (sequential, registration-order-stable), `tick_parallel(World&)` /
  `tick_parallel(World&, ThreadPool&)` (per-stage parallel). Returns
  `cd::core::Result<void>`.
- `cd::ecs::SystemGraph` -- named-dependency topological sort with cycle
  reporting.
- `cd::ecs::ArchetypeWorld` -- archetype-table side-layer (chunked dense
  rows + cross-archetype `add_component`/`remove_component` migration).

**Usage**:
```cpp
#include <cd/ecs/World.hpp>

struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };

cd::ecs::World w;
auto e = w.create();
w.emplace<Position>(e, 0.0F, 1.0F, 2.0F);
w.emplace<Velocity>(e, 0.0F, 0.0F, 0.5F);

w.each<Velocity, Position>(
    [](cd::ecs::Entity, Velocity& v, Position& p) {
        p.x += v.dx; p.y += v.dy; p.z += v.dz;
    });
```

**Test command**: `ctest --preset ninja-debug -R cd_test_ecs --output-on-failure`.

**Notes**:
- `World::each<T, Rest...>` snapshots the smallest matching pool's entity
  list before walking, so callbacks may add/remove components mid-walk
  without invalidating iteration. The cached `Query::each` walks the `T`
  pool LIVE (hot path) -- structural mutation of `T` mid-walk must use the
  uncached `World::each` overload.
- The `Scheduler` conflict DAG only adds edges from earlier- to
  later-registered systems, so it is acyclic by construction; genuine
  cycle reporting lives in `SystemGraph::build_order()`.
- `ArchetypeWorld` is a side-layer, not a replacement: the sparse-set
  `World` remains the load-bearing path (per ADR-20260529-X7).
