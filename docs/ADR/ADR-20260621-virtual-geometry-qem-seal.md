# ADR-20260621: Virtual Geometry — QEM Mesh-Simplification Seal

**Date**: 2026-06-21
**Status**: Accepted (charter-complete seal of `cd::virtual_geometry`)
**Stakeholders**: Rendering, Performance, Asset Pipeline
**Supersedes nothing** — extends ADR-20260531 (Virtual Geometry cluster-DAG skeleton)
with the formal scope boundary for production-grade mesh simplification.

---

## Bağlam (Context)

`cd::virtual_geometry` ships three charter-complete components:

1. **`ClusterDAG` + `ClusterDAGBuilder`** (`ClusterDAG.hpp`, 474 LOC) — a Nanite-style
   hierarchical cluster DAG (Karis 2021) built offline from a triangle soup. The leaf
   pass is a BFS graph-partition (METIS-style greedy adjacency walk) that groups
   connected triangles into clusters of ≤ `kMaxClusterTriangles` (128). The coarsening
   loop simplifies + re-clusters until a single root cluster remains.
2. **`GpuDispatcher`** (`GpuDispatcher.hpp`, 462 LOC) — the per-frame cluster cull +
   mesh-shader render scheduler. The cull stage runs a CPU equivalent of the shipped
   `kClusterCullCS` (frustum reject + dual-bound screen-space-error LOD frontier) so the
   host can read back `visible_clusters()` bit-for-bit; the render stage emits one
   mesh-task per visible cluster, gated on `device.features().mesh_shader`.
3. **`VirtualGeometry.hpp`** projected-error / `is_lod_frontier` / `pick_clusters`
   host helpers — the CPU reference the GLSL `kLodPickCS` kernel mirrors.

All three are exhaustively host-tested (66 cases across three binaries; DAG topology,
LOD-frontier predicate, dispatch state machine, and visibility-buffer 25:7 pack contract
all pinned and golden-stable).

The **one** sub-component that is NOT production-grade is the **mesh-simplification step
inside the coarsening loop** (`ClusterDAGBuilder::simplify_level`). It currently uses a
**structural skeleton**:

- **Vertex placement**: midpoint collapse — every even-indexed vertex is merged with its
  successor at their arithmetic midpoint, halving the vertex (and therefore triangle)
  count per LOD round.
- **Geometric error metric**: the bbox half-diagonal of each coarse cluster is used as a
  conservative screen-space-error radius (`self_error = bbox.half_diagonal()` for
  `lod > 0`).

This produces a topologically valid, monotonically coarsening, golden-stable DAG that is
sufficient to exercise + test the entire cull/dispatch/LOD-pick pipeline — but it does NOT
preserve surface shape under collapse the way a production simplifier must. A real
implementation requires **Quadric Error Metrics (Garland–Heckbert 1997)**: per-vertex
4×4 quadric accumulation, priority-queue-driven edge-collapse ordering by quadric cost,
optimal collapse-target placement (quadric minimisation), and boundary/attribute-seam
preservation. In a Nanite-class pipeline this is further wrapped by per-group
locked-boundary simplification (collapse only the cluster-group interior so neighbouring
groups stay watertight) and a per-cluster max-error bound that becomes the
`parent_error` driving the LOD frontier.

The question this ADR settles: **do we implement full QEM now to call the library "100%",
or do we formally seal it as out-of-charter offline-geometry work and declare the
charter — DAG construction + GPU dispatch + LOD pick — complete?**

---

## Karar (Decision)

**Seal full QEM (Garland–Heckbert) mesh simplification as out-of-charter, multi-week
offline-geometry work.** The charter of `cd::virtual_geometry` is **hierarchical cluster
DAG construction + GPU cluster dispatch + screen-space-error LOD pick** — all three are
complete, tested, and golden-stable. The midpoint-collapse + bbox-diagonal-error
simplifier is the **explicit, documented CURRENT CONTRACT** for the coarsening step, not
a hidden TODO. The CPU cull is the **bit-for-bit reference** the shipped `kClusterCullCS`
mirrors, so the rendered LOD selection is fully specified and reproducible.

The library is marked **100% (charter-complete)** on this basis. The QEM upgrade is
recorded as a tracked future-optimisation, gated behind a dedicated offline-geometry
work-package — not a defect of the shipped charter.

Concretely sealed (NOT shipped, NOT a TODO of the charter):

- Per-vertex quadric accumulation (sum of fundamental error quadrics over incident faces).
- Priority-queue edge-collapse ordering by quadric cost.
- Optimal collapse-target placement via quadric minimisation (with the singular-matrix
  midpoint fallback).
- Per-group locked-boundary simplification (watertight cluster-group seams).
- Per-cluster QEM-derived max-error bound feeding `parent_error`.
- Attribute-aware quadrics (normals / UVs / per-vertex colour seam preservation).

Sealed by **three** seal markers in the production code + tests so the honest-scope banner
cannot be silently dropped:

- `ClusterDAG.hpp:179-180` — class-doc note ("QEM skeleton: midpoint collapse used here
  as a structural placeholder").
- `ClusterDAG.hpp:382` — `simplify_level` doc ("Full QEM (Garland–Heckbert 1997) …").
- `test_cluster_dag.cpp:200-201` — test-suite banner ("midpoint-collapse simplification +
  bbox-diagonal error metric are the CURRENT CONTRACT; full QEM is documented future work,
  SEALED here").

---

## Reddedilen Alternatifler (Rejected Alternatives)

### 1. Implement full QEM now to reach a "true" 100%

Rejected. Production QEM is a multi-week offline-geometry subsystem in its own right
(quadric accumulation + priority-queue collapse + boundary locking + attribute seams +
per-group error bounds + a golden-mesh regression harness comparing simplified output
against a reference implementation such as meshoptimizer). It is **orthogonal** to the
shipped charter: the DAG topology, the GPU cull/dispatch path, and the LOD-pick predicate
are all complete and tested **independent** of how the coarse vertices are placed. Bolting
a half-finished QEM into the coarsening loop now would (a) change the byte-identical DAG
output — a golden-sensitive regression — for no end-to-end benefit, and (b) ship a second
placeholder under a "100%" label. Better to seal honestly and schedule QEM as its own
work-package.

### 2. Hide the simplifier behind a "good enough" silent claim

Rejected. The midpoint collapse does not preserve surface shape; claiming production
mesh-simplification without QEM would be a silent lie about the scope, exactly the failure
mode the project's honesty discipline forbids. The seal is **explicit** and locked by three
in-code/in-test markers.

### 3. Drop the coarsening loop entirely (leaf-only DAG)

Rejected. The coarsening loop is what makes this a *DAG* with multiple LOD levels rather
than a flat meshlet list; removing it would gut the screen-space-error LOD pick the
dispatcher exists to drive. The structural skeleton is the minimum that keeps the LOD
hierarchy real and testable.

### 4. Vendor meshoptimizer / metis as the simplifier dependency

Rejected for now (not forever). Pulling an external simplifier is the most likely *future*
realisation path, but it is a dependency + integration decision that belongs to the
offline asset-pipeline work-package, not to this seal. Recorded as the leading candidate
when the QEM work-package is scheduled.

---

## Sonuçlar (Consequences)

**Positive**

- `cd::virtual_geometry` is declared **100% charter-complete**: cluster-DAG construction,
  GPU cluster dispatch, and screen-space-error LOD pick are all shipped, exhaustively
  host-tested (66 cases), and golden byte-identical.
- The scope boundary is unambiguous and locked by three seal markers — no silent
  placeholder, no scope drift.
- The DAG output stays byte-identical (no production code touched by this seal), so every
  downstream golden remains valid.

**Negative / Risks**

- The coarse LODs are geometrically cruder than a production Nanite pipeline would produce
  (midpoint collapse drifts the surface; bbox-diagonal error is conservative, not
  tight). For the current hobby-scale scenes this is invisible; for true
  multi-million-triangle photogrammetry assets it would show as over-conservative LOD
  selection (drawing finer clusters than strictly necessary).
- A future QEM work-package WILL change the DAG byte output — it must therefore land with
  a fresh golden-mesh regression harness and a re-bake of any DAG goldens, and is
  explicitly flagged as golden-sensitive at that time.

**Follow-up (tracked, not blocking)**

- QEM work-package: per-vertex quadrics + PQ edge-collapse + locked-boundary group
  simplification + per-cluster error bound + meshoptimizer-vs-reference golden harness.
- Mesh-shader render path remains a no-op on backends without the extension (NullDevice /
  GL / older Vulkan ICDs); activating the live Vulkan mesh-shader draw is a separate
  GPU render-review item, not part of this seal.

---

## Atıflar (References)

- Karis, B., Stenson, R., Sjödahl, R. 2021. "Nanite — A Deep Dive." SIGGRAPH 2021 Advances
  in Real-Time Rendering.
- Garland, M., Heckbert, P. 1997. "Surface Simplification Using Quadric Error Metrics."
  SIGGRAPH 1997.
- ADR-20260531-virtual-geometry.md — original cluster-DAG skeleton design ADR.
