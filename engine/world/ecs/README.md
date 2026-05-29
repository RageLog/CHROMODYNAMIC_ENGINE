# cd::ecs

**Purpose**: archetype-based Entity-Component-System. Sparse-set component storage, archetype bitmask query, deferred lifecycle (create/destroy queued, flushed at frame boundaries), and a SystemGraph scheduler that orders systems by component-read/write dependency.

**Namespace**: `cd::ecs`.

**Headers**: `cd/ecs/{World,Entity,EntityRange,ComponentMask,ComponentStorage,Lifecycle,Generation,QuerySig,FilterFn,Scheduler,SystemGraph,TagHelpers}.hpp`.

**Primary types**:
- `cd::ecs::World` -- top-level registry. `create_entity()`, `add_component<T>(e, ...)`, `remove_component<T>(e)`, `for_each<Q>(fn)`.
- `cd::ecs::Entity` -- { id, generation } pair. Generational ID guards against use-after-destroy bugs.
- `cd::ecs::ComponentStorage<T>` -- sparse-set storage (O(1) lookup + O(N) dense iteration; cache-friendly per-component traversal).
- `cd::ecs::QuerySig<Read..., Write...>` -- compile-time signature; for_each only visits entities matching the signature mask.
- `cd::ecs::SystemGraph` -- topological-sort scheduler over registered systems, automatically parallelising commutative read-only stages.

**Usage**:
```cpp
#include <cd/ecs/World.hpp>

struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };

cd::ecs::World w;
auto e = w.create_entity();
w.add_component<Position>(e, 0, 1, 2);
w.add_component<Velocity>(e, 0, 0, 0.5F);

w.for_each<cd::ecs::QuerySig<cd::ecs::Read<Velocity>, cd::ecs::Write<Position>>>(
    [](const Velocity& v, Position& p) {
        p.x += v.dx; p.y += v.dy; p.z += v.dz;
    });
```

**Test command**: `ctest --preset ninja-debug -R cd_test_ecs --output-on-failure`.

**Notes**:
- hello_engine uses the W8-AR PBR 16-sphere grid as an ECS demo (4x4 metallic/rough sweep stored as { Position, MaterialAttrib, Mesh } components).
- SystemGraph integrates with cd::concurrency for safe parallel-system execution; commutative single-component-write systems run on the work-stealing pool.
- Lifecycle queue is flushed at explicit `world.commit()` calls so per-frame system iteration is invalidation-free.
