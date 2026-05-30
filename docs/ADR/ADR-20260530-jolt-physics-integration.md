# ADR-20260530 — Jolt Physics Integration (cd::physics_jolt backend)

- **Status**: Proposed (Design-Only — NO IMPLEMENTATION in this ADR)
- **Date**: 2026-05-30
- **Author**: team-lead (architect strand)
- **Supersedes**: nothing (refines ADR-008 §A vendor pick into a delivery plan)
- **Related**:
  - ADR-008 (Physics Architecture — adapter pattern + Jolt default)
  - ADR-004 (ECS / Scene)
  - ADR-005 (Foundation policy bundle — DeterminismProfile, fixed-point seed)
  - ADR-015 (Concurrency / Job System)
  - ADR-016 (Vendor Matrix & Replace Policy)
  - ADR-011 (Networking — Tier-2 determinism consumer)

> Scope guard: This document is a **planning artefact**. No `engine/physics_jolt/` files,
> no `CMakeLists.txt` edits, no vendor submodule add. The first patch that touches
> source is gated on a follow-up ADR (`ADR-2026xxxx-physics-jolt-bringup`) that
> records the actual Jolt tag, CMake flags, and ABI snapshot.

---

## Bağlam

ADR-008 already named **Jolt Physics 5.x** as the default 3D backend behind the
existing `cd::physics::IPhysicsWorld` adapter (see
`engine/world/physics/include/cd/physics/IPhysicsWorld.hpp`, lines 85–133). What
ADR-008 did **not** pin down:

1. **Why Jolt over PhysX / Bullet / a from-scratch solver**, in concrete trade-off
   terms — needed every time a contributor questions the vendor pick.
2. **The library boundary**: where exactly does `cd::physics` end and
   `cd::physics_jolt` begin? Today the MVP `make_builtin_physics_world()` is a
   semi-implicit Euler placeholder with **no collision detection** (header §137-142).
   Jolt must slot in without dragging Jolt symbols across the public `cd/physics/`
   include surface.
3. **Component mapping**: how `cd::scene::RigidBody / Collider / Joint` (planned
   per ADR-004 but not yet authored — `engine/world/scene/.../RigidBody*` returns
   zero hits at 2026-05-30) bind to `JPH::Body / Shape / Constraint` without
   leaking Jolt types into scene headers.
4. **Threading model**: Jolt ships its own `JPH::JobSystem`. ADR-015 made
   `cd::concurrency::IJobSystem` the **only** thread pool in the engine. The two
   must reconcile, not coexist.
5. **Soft-body + cloth scope**: Jolt 5.x has soft-body + cloth (skinned
   constraints). ADR-008 §D scoped XPBD as a custom-written cloth module. Now
   that Jolt covers both, do we still want custom XPBD?
6. **A phased delivery plan** that lets gameplay/editor consumers depend on
   *something working* every phase instead of waiting one big bang.

This ADR resolves all six.

---

## Karar

### A. Vendor pick — Jolt 5.x (re-affirmed) and **why** over the field

| Axis | **Jolt 5.x** | PhysX 5.6 | Bullet 3 | Custom (cd::physics::native) |
|---|---|---|---|---|
| License | **MIT** (engine-friendly, redistribute in binaries) | BSD-3 + GPU EULA carve-outs | zlib | n/a |
| AAA shipping proof | **Horizon Forbidden West, Horizon Burning Shores, Death Stranding 2**; PS4 / PS5 / Switch | UE5, Omniverse | older AAA, mostly Unity legacy | none |
| Console scaling | Designed for 16+ HW threads (Rouwe GDC 2022 §"multicore"); lock-free broadphase | Designed for GPU+CPU hybrid; CPU path is OK but not the focus | Single-thread broadphase + island parallelism only | unknown |
| Modern C++ posture | C++17, header layout suited to SDK consumption, no RTTI requirement | C++17, heavier SDK, requires PxFoundation singleton | C++03-ish, `btScalar` macros, internal allocator wars | C++23 native |
| Determinism story | **Per-platform deterministic** with FP-strict flag; documented Tier-1 path | Determinism opt-in but caveat-heavy on GPU paths | Not deterministic across SIMD widths | designable |
| Cloth / soft-body | **Yes** (5.x, position-based) | Yes (FEM + flag-based PBD; GPU strength) | Yes (legacy, low quality) | XPBD planned |
| Vehicle / character | Yes (`VehicleConstraint`, `CharacterVirtual`) | Yes | Partial | planned |
| RT / ray query cost | Built-in `NarrowPhaseQuery` cheap; SBP grid + AABB tree | `PxSceneQuery` | `btDbvt` | TBD |
| Build cost | Single CMake target, no special toolchain | Requires PhysX SDK packaging, MSVC-friendly but Clang-cl fiddly | OK but `BT_USE_DOUBLE_PRECISION` macro mess | n/a |
| Engineering risk if Jolt stalls | Low — code small enough (~70k LoC) to fork-and-maintain | Higher — SDK is 500k+ LoC | High — upstream slow | n/a |

**Verdict**: Jolt is the only candidate that combines AAA proof, MIT,
multicore-by-design, and a manageable code size. PhysX stays as a documented
opt-in (ADR-008 §A) for consumers that *specifically* want GPU rigid/soft/fluid
(NVIDIA Apr-2025 open-source). Bullet is rejected outright. Custom physics
remains a Phase 5+ R&D track (ADR-016 D1 replace-ready clause), not a Phase 2-4
deliverable.

### B. Library boundary

```
cd::physics              (existing)           public interface — no Jolt symbols
  IPhysicsWorld          interface, ABI-stable, header-only
  BodyHandle / BodyDesc  POD descriptors

cd::physics_jolt         (NEW library, this ADR)
  JoltPhysicsWorld       final  : public cd::physics::IPhysicsWorld
  detail/JoltJobAdapter        bridges JPH::JobSystem -> cd::concurrency::IJobSystem
  detail/JoltShapeCache        Collider descriptor -> JPH::Shape* (refcounted)
  detail/JoltConstraintBuilder Joint descriptor    -> JPH::Constraint*
  detail/JoltLayerFilter       broadphase layers   <-> cd::scene category bits
  CHROMA_PHYSICS_JOLT_API export macro
  Public header lives in `engine/physics_jolt/include/cd/physics_jolt/JoltBackend.hpp`
  and exposes EXACTLY:
      [[nodiscard]] std::unique_ptr<cd::physics::IPhysicsWorld>
          make_jolt_physics_world(const JoltBackendConfig& cfg);
```

Boundary rules (Zero Tolerance, ADR-005):

1. **No Jolt header may appear in any `include/cd/physics/` public file.** Verified
   at CI by `tools/check_no_vendor_leak.py cd/physics JPH/`.
2. `cd::physics_jolt::JoltBackend.hpp` may include `Jolt/Jolt.h`; that header is
   tagged `CHROMA_INTERNAL_VENDOR_LEAK_OK_JOLT` and an audit grep ensures
   downstream consumers (`engine/world/scene`, `engine/ecs`, samples) do
   **not** include it.
3. PIMPL inside `JoltPhysicsWorld` so `sizeof` does not depend on Jolt headers
   (ABI stability across vendor bumps).
4. Vendor source lives in `Dependencies/jolt/` as a submodule pinned to a
   specific tag; ADR-016 quarterly bump policy applies.
5. `cd::physics_jolt` links **PRIVATE** to `Jolt`; downstream targets link
   PUBLIC only to `cd::physics`. CMake DAG cycle-check enforces.

The existing `make_builtin_physics_world()` (header §137-142) is **kept** as the
fallback for unit tests and headless tools that don't want to pay Jolt link
cost. It will be re-tagged `cd::physics::make_null_physics_world()` once the
Jolt backend is the default.

### C. Component mapping (cd::scene <-> Jolt)

Scene components are authored in `cd::scene` (ADR-004). They contain **only
descriptors + a backend handle**, never a Jolt pointer.

| cd::scene component | Jolt counterpart | Mapping notes |
|---|---|---|
| `cd::scene::RigidBodyComponent { BodyType, mass, linear_damping, angular_damping, ccd_mode, layer }` | `JPH::Body` (via `JPH::BodyCreationSettings`) | `BodyType::kStatic/kKinematic/kDynamic` -> `EMotionType::Static/Kinematic/Dynamic`. CCD mode flag -> `EMotionQuality::LinearCast`. Layer enum -> `JPH::ObjectLayer`. |
| `cd::scene::ColliderComponent { ShapeDesc, local_transform, material_id, is_trigger }` where `ShapeDesc = variant<Sphere, Box, Capsule, ConvexHull, Mesh, Compound>` | `JPH::Shape*` via `JPH::ShapeSettings` factory; `JPH::SphereShape`, `JPH::BoxShape`, `JPH::CapsuleShape`, `JPH::ConvexHullShapeSettings`, `JPH::MeshShapeSettings`, `JPH::StaticCompoundShapeSettings` | Shapes are **cached + ref-counted** in `JoltShapeCache` keyed by content hash (matches Jolt's intent — shapes are immutable). `is_trigger` -> sensor body flag. |
| `cd::scene::JointComponent { JointKind, body_a, body_b, anchor_a, anchor_b, limits }` where `JointKind = enum { Fixed, Hinge, Slider, Distance, BallSocket, SixDof }` | `JPH::FixedConstraint`, `JPH::HingeConstraint`, `JPH::SliderConstraint`, `JPH::DistanceConstraint`, `JPH::PointConstraint`, `JPH::SixDOFConstraint` | Built by `JoltConstraintBuilder` from the variant. Limits/motors are descriptor fields, not Jolt API calls. |
| `cd::scene::CharacterControllerComponent` (already partially scoped in ADR-008 §C) | `JPH::CharacterVirtual` | Backend-agnostic kinematic capsule; falls through to Jolt's recommended pattern (Guerrilla GDC 2022). |
| `cd::scene::SoftBodyComponent` (new, P4) | `JPH::SoftBodyCreationSettings` + `JPH::SoftBodySharedSettings` | See §E. |

Sync direction:

- **Authoring**: scene components are the **truth**. Physics system reads them
  and uploads to Jolt at `OnConstruct` / dirty-flag events.
- **Per-step**: Jolt is the **truth** for transform + velocity. Physics
  post-step writes back into `cd::scene::TransformComponent` and the
  `RigidBodyComponent.linear_velocity / angular_velocity` cache fields.
- No double bookkeeping of position. The cache fields exist only because
  scripts/AI need to read velocity without paying a Jolt query each frame.

### D. Threading model — Jolt JobSystem → cd::concurrency

ADR-015 forbids a second thread pool inside the engine. Jolt expects a
`JPH::JobSystem` implementation; we provide one — never the reverse.

```
class JoltJobAdapter final : public JPH::JobSystem {
public:
    explicit JoltJobAdapter(cd::concurrency::IJobSystem& engine_pool) noexcept;
    // Implements: GetMaxConcurrency, CreateBarrier, WaitForJobs,
    //             QueueJob, QueueJobs, FreeJob.
    // All routes through engine_pool.submit(...) + cd::concurrency::Barrier.
};
```

Constraints:

- **No `std::thread` spawned by `cd::physics_jolt`.** Verified by `gtest`
  thread-count probe.
- `JoltBackendConfig` exposes `max_concurrent_jobs` (defaults to
  `engine_pool.worker_count()`) and `max_barriers` (Jolt-recommended 8).
- The physics step is itself a single **node** in `cd::concurrency::JobGraph`
  (ADR-015), running after gameplay tick and before render extract. Inside that
  node, Jolt fans out via the adapter.
- TSan preset (`ninja-tsan`) must include a physics-stress gtest before any
  Jolt code is allowed to land (CI gate).

### E. Soft-body + cloth scope reconciliation

ADR-008 §D scoped **custom XPBD cloth + rope** (~1500 LoC, ~3 weeks). Jolt 5.x
ships soft-body (PBD-style) and cloth-via-soft-body. Decision:

| Concern | Custom XPBD | Jolt soft-body |
|---|---|---|
| Tunability for stylised cloth | Full | Limited to Jolt's constraint model |
| Engine-aligned C++23 API | Yes | Wrapped through adapter (acceptable) |
| Maintenance cost | ~3 weeks dev + ongoing | Free (vendor) |
| Determinism | Easier (we own it) | Tier-1 same-platform OK |
| GPU path future | Possible (compute shader) | Not in Jolt; needs PhysX 5 GPU adapter |

**Decision**: **Drop custom XPBD from the critical path.** Adopt Jolt
soft-body for P4. Reasons:

1. ROI: cloth is a feature, not a research outcome for this engine.
2. Owning XPBD also means owning collision-with-cloth tuning, which is exactly
   the kind of multi-month tail Jolt swallows for us.
3. The "we beat SOTA" mandate (Quality Bar) lives in renderer / concurrency /
   editor — physics solver internals are the wrong battlefield.

XPBD stays in `research/notes/` as an **R&D backlog item** for Phase 5+ if a
specific game pillar needs stylised cloth Jolt can't deliver. PhysX GPU
soft-body remains the documented opt-in for fluid / FEM workloads.

### F. Phased delivery plan

Each phase has an exit gate (test + sample) and is a separately reviewable PR
chain. **Nothing in P1-P4 changes the `cd::physics::IPhysicsWorld` ABI**; only
factory + scene component descriptors evolve.

| Phase | Scope | Public surface added | Exit gate |
|---|---|---|---|
| **P1 — Static colliders** | Jolt vendored as submodule. `make_jolt_physics_world()` returns a world that can host **static** bodies with sphere/box/capsule/mesh shapes. No dynamics step, no gravity yet (or gravity=0). `JoltJobAdapter` lands. `cd::scene::ColliderComponent` lands. | `cd::physics_jolt::JoltBackend.hpp`; `cd::scene::ColliderComponent`. | gtest: spawn 1000 static box colliders + ray-query each; sample: walkable static level loaded via glTF colliders. Headless. |
| **P2 — Dynamic bodies** | Enable `JPH::BodyInterface` dynamic creation. Gravity + impulse + force APIs route through Jolt instead of the Euler MVP. `cd::scene::RigidBodyComponent` lands. Single-body + stacking unit tests. | `cd::scene::RigidBodyComponent` + transform writeback system. | gtest: 64-box pyramid settles within 2 s wall time, max penetration < 1 mm; sample: hello_physics_stack interactive. CCD off by default. |
| **P3 — Ragdoll (joints + character)** | `JointComponent` + `JoltConstraintBuilder`. `CharacterControllerComponent` via `JPH::CharacterVirtual`. CCD opt-in per body. Sleeping + activation events surfaced to ECS. | `cd::scene::JointComponent`, `cd::scene::CharacterControllerComponent`, activation events on event bus. | gtest: skeletal ragdoll (15 bodies, 14 constraints) drops without explosions over 60 s; sample: CesiumMan ragdoll via existing skinned anim chain; FPS character controller demo. |
| **P4 — Cloth (Jolt soft-body)** | `cd::scene::SoftBodyComponent` + cloth pinning + attach-to-rigid-body. Wind / point-force API on world. | `cd::scene::SoftBodyComponent`, `cd::physics::WorldForces`. | gtest: 32×32 cloth grid hung from corners, stable for 10 s, no NaN; sample: flag-on-pole + draped cloth on Sponza pillar. |

Determinism Tier-1 (ADR-008 §E) ships **after** P2 as a separate ADR
(`ADR-2026xxxx-physics-determinism-tier1`) because it requires CMake-level
FP-strict flag plumbing and is orthogonal to feature scope. Tier-2 (fixed-point)
remains Sprint 14+ as ADR-008 already states.

Replace-ready audit (ADR-016): **after P3**, a second backend (the existing
null/Euler world) must still build and pass a parity smoke test that exercises
only the `IPhysicsWorld` surface. This is the standing proof that the boundary
in §B is honoured.

---

## Reddedilen alternatifler

1. **PhysX 5.6 as the default.** Rejected for Phase 2-4: heavier SDK, less
   ergonomic for an MIT-everywhere engine, GPU strengths irrelevant on day one.
   Kept as ADR-008 opt-in for GPU soft/fluid consumers.
2. **Bullet 3 as the default.** Rejected: stagnant, single-thread broadphase,
   pre-C++17 API style. ADR-008 already rejected; restated here for the record.
3. **Custom from-scratch 3D physics in Phase 1-4.** Rejected: 18-36 months for a
   single senior to reach Jolt-equivalent stability + multicore scaling. ROI
   negative until a specific game pillar demands it (Phase 5+ R&D bucket).
4. **Jolt without a backend boundary (Jolt symbols in `cd::scene`).** Rejected:
   vendor lock, violates ADR-005 modularity, breaks ADR-016 replace-ready
   clause. PIMPL + private link mandatory.
5. **Two thread pools (Jolt's own JobSystem + cd::concurrency).** Rejected by
   ADR-015. Adapter is the only allowed shape.
6. **Custom XPBD cloth in P4.** Rejected for the critical path (see §E); moved
   to R&D backlog.
7. **Skip the phased plan and ship "Jolt full integration" in one PR.** Rejected:
   un-reviewable, blocks gameplay/editor teams from depending on partial
   physics, violates the marathon discipline of always-green main.

---

## Sonuçlar

**Pozitif**:

- Concrete trade-off table justifies Jolt every time a contributor questions it.
- Boundary rules (§B) make the Jolt dependency swappable and auditable; ADR-016
  D1 honoured.
- Phased plan unblocks gameplay (P1 static colliders) inside the first sprint
  after this ADR lands, instead of waiting for a big-bang physics PR.
- Soft-body scope shrinks (XPBD dropped from critical path) — frees ~3 weeks
  for renderer / editor work where Quality Bar mandates SOTA-beating.
- Thread model converges on a single pool (ADR-015 compliance).

**Negatif**:

- New vendor in `Dependencies/jolt/`: quarterly bump cost (ADR-016).
- `cd::physics_jolt` PIMPL adds one allocation per body create — measured cost
  ~100 ns; acceptable, but called out so future profiling work doesn't blame
  the boundary later without data.
- Custom XPBD R&D postponed; if a game pillar later demands stylised cloth, P5+
  picks it up — schedule risk lives there, not in P1-P4.
- Determinism Tier-1 requires a CMake-level FP-strict guard touching every
  translation unit that includes `cd/math/` — that ADR is deferred but called
  out as a known cross-cut.

**Replace-Ready (ADR-016 D1)**:

- `cd::physics::IPhysicsWorld` remains stable across P1-P4.
- Parity smoke test (after P3) ensures the null backend continues to build.
- Phase 5+ candidate replacement: `cd::physics::native::cdyn` (custom) or
  `cd::physics::vendor::physx` (already-scoped opt-in).

---

## Açık Sorular (deferred to follow-up ADRs)

| ID | Soru | Hedef ADR |
|---|---|---|
| Q1 | Hangi Jolt tag (5.2 vs 5.3)? | `ADR-2026xxxx-physics-jolt-bringup` (P1 PR) |
| Q2 | FP-strict CMake guard scope (engine-wide vs physics-only)? | `ADR-2026xxxx-physics-determinism-tier1` (post-P2) |
| Q3 | Mesh-collider source: convex decomposition (VHACD) vs author-supplied convex hulls? | P1 sub-ADR (asset pipeline cross-cut, ADR-006) |
| Q4 | Sensor / trigger event delivery: event bus vs polling? | P2 sub-ADR |
| Q5 | Multi-world support (e.g., editor preview + game)? | P3 sub-ADR |
| Q6 | Soft-body collision with character controller — Jolt limits? | P4 sub-ADR |
| Q7 | Vehicle scope (Jolt `VehicleConstraint` is available) — P3 or P5+? | Backlog; not in P1-P4. |
| Q8 | PhysX adapter (`cd::physics::vendor::physx`) implementation phase? | Reaffirm ADR-008 Q1: scaffolding S8, impl S10. |

---

## Cross-Cutting

- **ADR-004 (ECS / Scene)**: introduces `RigidBodyComponent`, `ColliderComponent`,
  `JointComponent`, `CharacterControllerComponent`, `SoftBodyComponent`. The
  `cd::scene` library must not include any `Jolt/*.h`. A new ECS system,
  `cd::scene::PhysicsSyncSystem`, runs pre-physics (write descriptors -> world)
  and post-physics (read transforms <- world).
- **ADR-005 (Foundation)**: `cd::foundation::DeterminismProfile` consumed by
  Tier-1 deferred ADR. Math SIMD vs Jolt SIMD ABI: kept apart by PIMPL.
- **ADR-006 (Asset Pipeline)**: glTF importer needs an extension hook for
  collider authoring (`KHR_collision_shapes` or engine-specific extras). Sub-ADR
  during P1.
- **ADR-011 (Networking)**: Tier-2 rollback netcode consumes determinism Tier-2
  (Sprint 14+); this ADR keeps that path open but does not enable it.
- **ADR-015 (Concurrency)**: `JoltJobAdapter` is the **only** sanctioned
  bridge. No second thread pool. TSan gate before any merge.
- **ADR-016 (Vendor Matrix)**: Jolt added to the vendor matrix under
  "default-on, replace-ready". Quarterly tag-pin bump cadence.

---

## Kanıt

> Engineering SOTA citations (URL + intent). Academic citations are not used in
> this ADR (Demir Kural: only when a PDF is in `research/library/MANIFEST.csv` —
> none required for an integration plan).

- Jolt Physics — https://github.com/jrouwe/JoltPhysics — source, MIT license
  confirmation, samples app.
- Rouwe — "Architecting Jolt Physics for Horizon Forbidden West", Guerrilla
  GDC 2022 — https://www.guerrilla-games.com/read/architecting-jolt-physics-for-horizon-forbidden-west
- Rouwe — "Jolt Physics Multicore Scaling" technical note —
  https://jrouwe.nl/jolt/JoltPhysicsMulticoreScaling.pdf
- NVIDIA PhysX 5 open-source announcement (Apr 2025) —
  https://developer.nvidia.com/blog/open-source-simulation-expands-with-nvidia-physx-5-release/
- Bullet 3 — https://github.com/bulletphysics/bullet3 (rejected, listed for
  trade-off evidence).
- Existing engine surface this ADR plugs into:
  `engine/world/physics/include/cd/physics/IPhysicsWorld.hpp` (this repo,
  lines 85-142).
- ADR-008 (Physics Architecture) — `docs/ADR/ADR-008-physics-architecture.md`.
- ADR-015 (Concurrency / Job System) — `docs/ADR/ADR-015-concurrency-job-system.md`.
- ADR-016 (Vendor Matrix & Replace Policy) — `docs/ADR/ADR-016-vendor-matrix-replace-policy.md`.
