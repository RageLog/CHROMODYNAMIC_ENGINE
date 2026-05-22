# ADR-004 — ECS + Scene Architecture

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-001 (RHI), ADR-002 (Renderer), ADR-005 (Foundation), ADR-006 (Asset), ADR-015 (Concurrency), ADR-017 (DtForHil Salvage)

## Bağlam

CHROMODYNAMIC needs an ECS that (a) beats EnTT in iteration throughput on wide queries, (b) matches Bevy in scheduler ergonomics, (c) approaches Unity DOTS in chunk-cache behavior, (d) stays library-oriented so `cd::ecs` ships without `cd::scene`.

User decisions:
- T14.Q1 = C+D — hybrid chunk-archetype hot + sparse-set cold + custom chunk-based with bitmask
- T14.Q2 = A — hierarchy via ECS components
- T14.Q3-Q7 = state-of-art araştır

Determinism opt-in (T3.Q2 = D) → rollback netcode (ADR-011) infrastructure ready.

## Karar

### A. Storage — Hybrid Chunk-Archetype + Sparse-Set + Bitmask Tag

**Layout**:
- **Hot path**: chunk-archetype (16 KiB SoA chunks, bitmask tag-per-chunk) for frequently iterated component combinations.
- **Cold path**: sparse-set for rarely-iterated, large, or singleton components.
- **Component categorization**: declared at registration (`hot` / `cold` / `tag` / `singleton`), audited by profile-guided tool `cd::ecs::tooling::StorageAudit`.

**Storage class declaration** at registration time, no competitor surfaces this as an explicit library-oriented contract.

```cpp
namespace cd::ecs {
    template <ComponentSchema T> void register_component(StorageClass = StorageClass::Hot);
    enum class StorageClass { Hot, Cold, Tag, Singleton };
}
```

Cold store disable at compile-time via `CD_ECS_DISABLE_SPARSE` for embedded users wanting fixed-archetype only.

### B. Hierarchy — ECS-Internal (T14.Q2 = A)

Hierarchy is ECS components: `Parent`, `Children`, `LocalTransform`, `WorldTransform`, `HierarchyDirty`. `cd::scene::SceneGraphView` is a **non-owning projection** built lazily for editor/tooling.

Transform propagation runs in a `HierarchyPhase` between `Update` and `PostUpdate`, using parallel BFS over "roots-then-depth-buckets" partitioning (Flecs cascade pattern).

`cd::ecs` standalone shippable (no scene dep). `cd::scene` thin (camera, layer masks, editor projection), depends on `cd::ecs` but not vice versa.

**Determinism hook**: parent-resolution order deterministic when `DeterministicScheduler` on (entity-ID sort tiebreak); otherwise parallelize without ordering guarantee.

### C. Query/Scheduling (T14.Q3)

Function-signature query (template deduce `Query<Pos, &Vel, With<Enemy>, Without<Dead>>`) + explicit Phases (PreUpdate/Update/PostUpdate/Render/UI) + Tag systems + **Reactive observers** (`OnAdd<T>`, `OnRemove<T>`, `OnSet<T>`).

**Chunk-aware observers** — most competitors fire OnAdd per entity; ours fires per **chunk transition** (entire moving slab gets one observer call with span). O(N) → O(chunks), ~10× speedup target.

Compile-time conflict detection for parallel scheduler (Bevy ECS book §schedule). Phase scheduler is thin layer atop `cd::concurrency::JobGraph` (ADR-015). `DeterministicExecutor` (parallel reduction with fixed reduction tree) for determinism mode.

### D. Prefab (T14.Q4)

Entity-tree template + variant override list + nested prefab reference, stored as **archetype delta** (sparse override map). Unity prefab UX, archetype-delta storage keeps hot path clean and makes hot-reload diffable.

Override resolution at spawn; precompiled to "flat instantiation recipe" for runtime use.

### E. Hot Reload (T14.Q5) — 3-tier

1. **Data-only**: assets, prefab JSON — always-on, file-watcher driven.
2. **Schema migration**: versioned components (`cd::ecs::ComponentSchema<T, Version>`) + auto-migrator.
3. **Opt-in DLL hot-swap**: behind `CD_HOTRELOAD_DLL` flag (Sprint 12+).

Tiered approach keeps Phase 1 risk low.

### F. Spatial Structures (T14.Q6)

Pluggable `cd::ecs::spatial::IBroadPhase`:
- **Dynamic BVH** (default, 3D, à la PhysX/Jolt — Jolt SOTA reference).
- **Uniform grid** (2D, lattice scenes).
- **Loose octree** (static large worlds, opt-in).
- **Portal/PVS** (indoor/Doom-style, plugin).

Pluggability — `cd::ecs` works without renderer.

### G. Culling (T14.Q7 = D pluggable)

Pluggable `cd::ecs::ICullingStrategy`:
- **Frustum** (default, CPU).
- **HZB occlusion** (opt-in, requires RHI dep — lives in `cd::renderer::culling` adapter).
- **GPU-driven** (compute culling, opt-in).
- **Cluster culling** (many-mesh scenes, opt-in).

`ICullingStrategy` lives in `cd::ecs::spatial::culling` as pure interface; implementations live in `cd::renderer::culling`. ECS owns spatial structure; renderer owns GPU buffers.

### H. Reflection Strategy

C++26 reflection (when stable) replaces macro-based component registry with zero-boilerplate auto-registration. Fallback: macro `CD_REFLECT(T, fields…)`.

Reflection feeds:
- Serialization (asset pipeline ADR-006)
- Editor inspector (ADR-012)
- Network replication (ADR-011)
- Hot-reload schema migration

### I. Determinism Mode

`Registry<Determinism::Off>` (default) vs `Registry<Determinism::On>` — entity-ID allocator and scheduler policy chosen at type level; **no virtual dispatch hit when off**.

## Reddedilen Alternatifler

| Alternatif | Sebep |
|---|---|
| **EnTT-only sparse-set** | Chunk SIMD wins lost, ergonomic phase scheduler lost (perf ceiling) |
| **Pure archetype (Flecs)** | Structural-change cost on cold/churn components too high |
| **Unity DOTS clone (chunk-only)** | Forces all components into chunks → poor for singletons/sparse |
| **Unreal Mass processor model** | Tightly coupled to Unreal TaskGraph; not library-oriented |
| **Scene-graph-as-truth (gameplay3d/UE AActor)** | Violates DOD baseline, two-truths problem |
| **Two-way sync (Unity GameObject↔Entity baking)** | Invasive editor pipeline before Sprint 8 makes no sense |
| **No hierarchy (flat-only)** | Insufficient for 3D scene |
| **DLL hot-swap from day one** | Too invasive; tiered approach reduces risk |
| **Singleton ECS registry** | CLAUDE.md §7; embedded use cases blocked |

## Sonuçlar

**Pozitif**:
- `cd::ecs` standalone library shippable.
- Hot path matches Unity DOTS class; cold path matches EnTT class.
- Determinism slot reserved without runtime tax.
- C++26 reflection upgrade path non-breaking.
- Chunk-aware reactive observers = differentiator.

**Negatif / Risk**:
- Storage-class declaration learning curve (mitigated by audit tool).
- Two storage backends = doubled test matrix.
- Chunk-archetype migration cost during structural change persists (measured + budgeted Sprint 5).
- Hierarchy parallelism + deterministic mode interaction needs joint Sprint 6 test.
- Prefab archetype-delta vs hot-reload schema migration needs joint Sprint 7 spec.

**Replace-Ready (D1)**: ECS is fully custom (no vendor); replace policy N/A.

## Açık Sorular

| ID | Soru | Çözüm noktası |
|---|---|---|
| Q1 | Hot vs cold threshold (entity-count / query-freq auto-promote) | Sprint 5 profiling tool |
| Q2 | Entity ID width: 32-bit vs 64-bit | Sprint 5 memory bench |
| Q3 | Flecs-style relations vs only Parent component | Sprint 6 |
| Q4 | Observer ordering (OnAdd before/after OnSet same frame)? | Spec needed (esp. determinism mode) |
| Q5 | Prefab override merge semantics (nested)? | Designer test cases Sprint 7 |
| Q6 | C++26 reflection availability date — slip beyond 2027? | Macro fallback permanent option |
| Q7 | Singleton/resource components: Bevy `Resource` vs ECS singleton entity? | Lean Bevy `Resource` (cleaner API) |
| Q8 | GPU-driven culling tie-in: ECS exposes entity AABB on GPU SB or via renderer adapter? | Adapter only — cross-cutting boundary |

## Cross-Cutting

- **ADR-001 (RHI)**: `cd::ecs::components::RenderHandle { rhi::ResourceId }` opaque; `cd::ecs::ExternHandle<Tag>` type-erased.
- **ADR-002 (Renderer)**: `ICullingStrategy` in ECS, implementations in renderer; joint Sprint 6 spec.
- **ADR-006 (Asset)**: Prefab JSON + binary fast-path; `cd::asset::prefab` depends on `cd::ecs::ComponentSchema<T>` for round-trip; versioning shared with hot-reload.
- **ADR-015 (Concurrency)**: ECS phase scheduler thin layer atop `cd::concurrency::JobGraph`; conflict-graph in ECS, dispatch in concurrency. `DeterministicExecutor` for determinism mode.
- **ADR-012 (Editor)**: `cd::scene::SceneGraphView` editor's read model; updates reactively via observer mechanism (single integration point).

## Kanıt

- EnTT (sparse-set): https://github.com/skypjack/entt
- Flecs (archetype + relations): https://www.flecs.dev/flecs/md_docs_2Relationships.html
- Bevy ECS book: https://bevy-cheatbook.github.io/programming/ec.html
- Unity DOTS: https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/
- Jolt dynamic BVH: https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md
- Acton — Data-Oriented Design CppCon 2014 — **STUB**
- Caini — EnTT ECS back and forth — engineering blog
- Bevy parallel scheduler — Bevy Book — **STUB**
