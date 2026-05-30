# cd::game_query

## Purpose
Gameplay-tier spatial + ECS query facade. Wraps a
`cd::scene::SpatialHash<cd::ecs::Entity>` broad phase and the
`cd::scene::Frustum` n/p-vertex cull behind a single `QueryWorld`
object so game code can ask "what entity is under this ray / inside
this volume / nearest to this point?" without coupling to a specific
accelerator implementation.

Phase 475 — G3.1 in the engine roadmap.

## Namespace
`cd::game::query`

## Public headers
- `include/cd/game/query/Query.hpp` — `RayHit`, `SphereOverlap`,
  `AabbHit`, `QueryWorld`, and the header-inline-callable
  `intersect_ray_aabb()` free function.

## Primary types
| Type            | Role                                                            |
|-----------------|-----------------------------------------------------------------|
| `RayHit`        | `{ entity, distance, point, normal }` — single nearest hit.     |
| `SphereOverlap` | `{ entity, distance }` — one entry per overlap.                 |
| `AabbHit`       | `{ t, normal }` — header-free ray vs AABB result.               |
| `QueryWorld`    | Owns `SpatialHash<Entity>` + per-entity AABB records.           |

## Queries
- `raycast(origin, dir, max_dist) -> std::optional<RayHit>` — nearest
  hit along a (possibly non-unit) ray, clipped at `max_dist`.
- `sphere_query(center, radius) -> std::vector<SphereOverlap>` — sorted
  nearest-first.
- `box_query(aabb) -> std::vector<Entity>` — every entity whose AABB
  overlaps the query box.
- `frustum_query(planes[6])` / `frustum_query(Frustum)` — every entity
  whose AABB passes the n/p-vertex cull.

## Build / Test
```bash
cmake --build --preset ninja-debug --target cd_game_query
ctest --preset ninja-debug -R game_query --output-on-failure
```

## Usage example
```cpp
#include <cd/game/query/Query.hpp>

using namespace cd::game::query;

QueryWorld q { /*cell_size=*/4.0F };
q.add_entity(player_entity, player_pos, player_aabb);
for (const auto& enemy : enemies)
    q.add_entity(enemy.entity, enemy.pos, enemy.aabb);
q.rebuild();

// Click-to-pick.
if (auto hit = q.raycast(cam.origin, cam.fwd, /*max=*/200.0F))
{
    select(hit->entity);
}

// AoE damage in a 10-metre sphere.
for (const auto& [e, dist] : q.sphere_query(blast_center, 10.0F))
{
    apply_damage(e, 100.0F * (1.0F - dist / 10.0F));
}

// View-frustum culling for the renderer.
auto visible = q.frustum_query(cam.frustum);
```

## Dependencies
- `cd::core` — `Defines.hpp` (cross-library macros).
- `cd::math` — `Vector.hpp` (Vec3f, dot / cross / length).
- `cd::ecs` — `Entity.hpp` (generational handle).
- `cd::physics` — `Aabb.hpp` (axis-aligned bounding box + overlap).
- `cd::scene` — `SpatialHash.hpp` (uniform-grid broad phase) +
  `Frustum.hpp` (n/p-vertex cull).

## References
- Akenine-Möller, Haines, Hoffman et al. *Real-Time Rendering 4e*,
  CRC Press, 2018. §22.7 (ray / AABB slab test), §16.10 (frustum
  n/p-vertex test).
- Williams, Barrus, Morley, Shirley. *An Efficient and Robust Ray-Box
  Intersection Algorithm*, JGT 10(1):49-54, 2005.
- Production references: Unreal `UPrimitiveComponent::LineTraceSingle`,
  Unity `Physics.OverlapSphere`, Godot `PhysicsDirectSpaceState3D`.

## Notes
- The spatial hash is uniform-grid only. For ranges where the AABB-size
  distribution is wildly heterogeneous (think 0.5 m enemies + 500 m
  static buildings) a loose octree or BVH replacement is queued for a
  follow-up phase — the `QueryWorld` API is intentionally narrow so an
  alternate accelerator can drop in without source-level churn at the
  call site.
- `raycast` walks the spatial hash via a sphere bounding the segment,
  then falls back to a brute-force scan if the broad phase comes up
  empty. That keeps the contract simple at the cost of an extra walk
  for long, sparse rays — both costs are easy to amortise away later by
  swapping the hash for a DDA-style cell stepper.
- Thread-safety: a single writer (`add_entity` / `remove_entity` /
  `rebuild` / `rebuild_from`) at a time; the four queries are `const`
  and re-entrant.
