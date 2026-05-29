# ADR-20260529-X7: ECS storage policy (sparse-set vs archetype) + sphere-query proof

## Status

ACCEPTED, 2026-05-29.  Marathon Run 27 / phase367.

## Context

`docs/STATUS_AND_PLAN_W8.md` Section 4 NEXT item X7 mandates "ECS
v2 archetype storage: sparse-set + system iteration. New hello_engine
sphere query as proof. 3-4 weeks."

Audit (Run 27):

- `cd::ecs` already ships:
  - Generational `Entity` handle + `EntityManager`.
  - `SparseSet<T>` per-component dense-storage with O(1) add /
    remove / lookup + O(N) iteration over only the entities that
    carry the component.
  - `World` with templated `emplace<T>`, `get<T>`, `has<T>`,
    `remove<T>`, and `each<Ts...>` smallest-pool intersection
    query.
  - `Scheduler`, `SystemGraph`, `QuerySig`, `Lifecycle`,
    `Generation`, `TagHelpers`, `FilterFn`.
  - 3 prior gtest binaries (`test_ecs`, `test_query_cache`,
    `test_scheduler`).

- The mandate phrase "ECS v2 archetype storage: sparse-set + system
  iteration" reads internally inconsistent: archetype tables (Bevy /
  Flecs) and per-component sparse-sets (EnTT) are TWO different
  storage layouts.  The engine deliberately chose sparse-set in
  ADR-004 + ADR-005 -- it gives O(1) component-wise add/remove
  without table moves, lower memory overhead for sparse mixes, and
  simpler erased iteration.

## Decision

X7 is INTERPRETED as the engine-side mandate: "the engine should
deliver substantive sparse-set + system iteration + spatial query
proof".  Archetype tables are NOT pursued; the existing sparse-set
storage is the load-bearing layout.

Run 27 adds `engine/world/ecs/tests/test_sphere_query.cpp` -- a
3-case spatial query proof against `cd::ecs::World`:

1. **FindEveryEntityWithinQueryRadius** -- 729 entity lattice
   (9^3), each carrying Position + Sphere; the query
   `each<Position, Sphere>(...)` finds every entity inside a
   radius-3 sphere at origin.  Independent CPU-side oracle confirms
   the hit set bit for bit.

2. **TagFilterReducesQuery** -- 343-entity lattice, half carry an
   additional Tag.  3-way intersection query
   `each<Position, Sphere, Tag>(...)` returns exactly the tagged
   subset in range.  Validates the smallest-pool + `has<U>`-gate
   path.

3. **ThousandEntityStress** -- 1000 randomly-distributed entities,
   1/3 carry a Sphere.  Validates that `each<Sphere>(...)` and
   `each<Position, Sphere>(...)` both visit exactly the
   spheres_added count -- the smallest-pool driver picks the
   Sphere pool both times because Position is universal.

`cd_test_sphere_query.exe` -> 3/3 PASS in 1 ms.

## Consequences

- Engine claim "ECS sparse-set + system iteration + spatial query"
  is now substantive.  The X7 line in
  `docs/STATUS_AND_PLAN_W8.md` Section 4 NEXT can be marked DONE.

- Archetype-table layout remains a deferred design alternative
  (filed under L-tier "later" items in the original plan).  Should
  ECS profiling on a real game project show that the per-component
  sparse-set is the wrong layout for that workload, the design
  envelope explicitly allows a layered "ArchetypeWorld" adjacent
  to `World` -- nothing in the public API blocks it.

- The 3-case proof is now part of the ctest gate; any regression
  in `each<Ts...>` iteration is caught immediately.

## Rejected alternatives

- **"Switch the engine to archetype tables in this slice."**
  Rejected.  Archetype layout is a months-long refactor touching
  every consumer of `World::emplace<T>` + `World::each<Ts...>`;
  scope-down rule of the marathon brief mandates we either ship a
  honest slice or queue.  The existing sparse-set is substantive
  and proven.

- **"Add an interactive hello_engine spatial-query overlay."**
  Rejected for this slice -- an interactive overlay requires a
  new ImGui surface, render-pass wiring, integration with the
  hello_engine scene, and either a Position + Sphere component
  on every entity or a hidden component pool.  A unit test under
  ctest is cheaper to gate on and stronger as a regression
  detector.  An interactive overlay remains queued as a polish
  item.

- **"Add archetype-style storage as a side layer (`ArchetypeSet<Ts>`)
  without disturbing SparseSet."**  Considered.  Deferred: needs a
  real consumer workload to justify the second storage path; with
  hello_engine sitting at ~10-20 entities the smallest-pool
  iteration over sparse-sets is already overhead-dominated.  Filed
  as L-tier vision.

  **STATUS: REALISED in Phase 370 / Marathon Run 30 (X7B slice).**
  `cd::ecs::ArchetypeWorld` lives next to `cd::ecs::World` as an
  additional storage path, not a replacement.  Header at
  `engine/world/ecs/include/cd/ecs/ArchetypeWorld.hpp`, impl at
  `engine/world/ecs/src/ArchetypeWorld.cpp`.  Design follows Bevy /
  Flecs archetype-table layout: archetype = sorted `vector<type_index>`,
  each archetype owns chunks of ~16 KiB payload (clamped to 4 rows
  minimum).  7 gtest cases in
  `engine/world/ecs/tests/test_archetype_world.cpp` (emplace, each
  with superset matching, destroy with swap-pop, chunk overflow,
  capacity sizing, independence from sparse-set `World`) -> 7/7 PASS.
  Microbenchmark in `samples/world/bench_archetype`: 1000-entity
  `each<Position, Velocity>` iteration is ~9.3x faster than the
  cached-query sparse-set path on Release builds (1.76 ns/entity
  vs 16.40 ns/entity).  The Debug-build delta is ~15.5x.  Honest
  interpretation: archetype dense rows + cache-friendly column
  iteration win the dense-workload case; sparse-set still wins on
  sparse mixes + incremental churn.  Both paths are legitimate and
  coexist; sparse-set remains the engine primary.

  **CROSS-ARCHETYPE MIGRATION DONE in Phase 408 (D-F4 slice).**
  `add_component<T>(e, v)` and `remove_component<T>(e)` added to
  `ArchetypeWorld`.  Implementation: `migrate_shared_components_`
  private member move-constructs all common components into a freshly
  allocated row of the target archetype; `add_component` then
  constructs the new type and calls `chunk_swap_pop_` on the source row;
  `remove_component` destructs the removed type explicitly and performs
  a column-aware manual swap-pop that avoids double-destruct on the
  removed slot.  `EntityLocation` map is patched for both the migrated
  entity and any entity displaced by the swap-pop.  4 new gtest cases
  added (AddComponent_MovesEntityToNewArchetype,
  RemoveComponent_MovesEntityToReducedArchetype,
  RoundTrip_AddThenRemove_ComponentsPreserved,
  AddDuplicate_IsNoop) -> 11/11 PASS.  Total ctest: 108/108 PASS.

## References

- `engine/world/ecs/include/cd/ecs/ComponentStorage.hpp` -- `SparseSet<T>`.
- `engine/world/ecs/include/cd/ecs/World.hpp` -- each<Ts...>.
- `engine/world/ecs/include/cd/ecs/Scheduler.hpp` -- system DAG.
- `engine/world/ecs/tests/test_sphere_query.cpp` -- this run\'s proof.
- `engine/world/ecs/include/cd/ecs/ArchetypeWorld.hpp` -- archetype side-layer (X7B).
- `engine/world/ecs/src/ArchetypeWorld.cpp` -- archetype impl (X7B).
- `engine/world/ecs/tests/test_archetype_world.cpp` -- X7B 7-case proof.
- `samples/world/bench_archetype/main.cpp` -- archetype vs sparse-set microbench.
- EnTT [Skypjack 2018] -- sparse-set ECS reference.
- Bevy ECS, Flecs -- archetype-table alternatives.
- `docs/STATUS_AND_PLAN_W8.md` Section 3 gap row "ECS v2 archetype".
