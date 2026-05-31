# ADR-20260531: Virtual Geometry (Nanite-style Hierarchical Cluster DAG)

**Date**: 2026-05-31
**Status**: Accepted (phase528 — cluster builder skeleton)
**Stakeholders**: Rendering, Performance, Asset Pipeline

---

## Bağlam

Modern game scenes routinely contain tens of millions of triangles: photogrammetry
assets (e.g., Quixel Megascans), high-resolution vehicles, dense urban foliage.
Traditional LOD pipelines do not scale to this complexity:

- **Manual LOD chains** require per-asset artist labour (typically 4–6 LOD steps
  hand-crafted per mesh) and produce visible pop artefacts at LOD transitions.
- **Per-mesh impostors / billboards** (camera-facing quads) collapse geometry quality
  catastrophically at steep angles and require large imposter atlas textures (128 × 128
  px minimum per object, scaling to gigabyte atlas budgets for large scenes).
- **Mesh simplification without clustering** (e.g., Melax 1998, Garland–Heckbert 1997
  QEM) produces globally simplified meshes with no streaming granularity — the whole
  mesh must be loaded or unloaded.
- **Screen-space ambient occlusion / detail hiding** does not reduce GPU vertex
  throughput and is orthogonal to this problem.

The fundamental requirement is **sub-pixel triangle culling with continuous LOD and
no visible transitions**, across the full depth range of a scene, in a single render
pass. This is only achievable by treating geometry as a hierarchical aggregate of
fixed-size clusters (meshlets) and selecting the finest cluster that falls beneath a
screen-space error threshold — per cluster, not per mesh.

Reference: Karis, B., Stenson, R., Sjödahl, M. "Nanite: A Deep Dive." SIGGRAPH 2021.

---

## Karar

Implement **hierarchical cluster DAG with software rasterizer fallback for sub-pixel
triangles** and a **visibility buffer** (deferred material evaluation):

### 1. Cluster / Meshlet Granularity

Following the Nanite specification, each cluster contains at most **128 triangles and
256 vertices**. This fits a cluster into a mesh shader threadgroup (32 amplification
threads × 4) and aligns to GPU warp granularity.

```
struct Cluster {
    Triangle    triangles[128];   // index triples into shared vertex array
    uint32_t    triangle_count;
    uint32_t    parent_lod;       // index of coarser parent cluster group
    uint32_t    child_count;      // number of finer children
    AABB        bbox;             // tight bounding box for HZB culling
    BoundSphere bounds_sphere;    // sphere for LOD error projection
    float       self_error;       // max geometric error this cluster introduces
    float       parent_error;     // error introduced at parent granularity
};
```

### 2. Cluster DAG Construction (offline / asset-pipeline)

The build pipeline is a two-stage process executed by `cd::virtual_geometry::ClusterDAGBuilder`:

**Stage A — Partition** (METIS-style graph partitioning):
  1. Build a dual graph of the input mesh: one node per triangle, edges weighted by
     shared-edge count.
  2. Partition dual graph into groups of ≤ 128 triangles via greedy BFS (seed from
     vertex with highest degree, flood-fill respecting edge boundaries).
  3. Record spatial bounding sphere and AABB per cluster.

**Stage B — Simplification + re-cluster** (Garland–Heckbert QEM):
  1. Within each cluster group (4–8 adjacent clusters), collapse edges to produce a
     simplified mesh at half triangle count.
  2. Re-partition simplified mesh into new clusters → parent DAG level.
  3. Propagate `parent_error` upward: `parent.self_error = max(child.parent_error)`.
  4. Repeat until entire mesh collapses to ≤ 128 triangles (DAG root = 1 cluster).

This yields a strict DAG: edges only point from fine (children) to coarse (parents).
No cycles are possible by construction — each stage produces a strictly coarser level.

### 3. LOD Selection (runtime, GPU compute)

Per cluster, evaluate:
```
projected_px = (cluster_radius / dist_to_cam) × (viewport_h / (2 × tan(half_fov)))
render_this  = (self_error_px ≤ threshold) AND (parent_error_px > threshold)
```

This produces a cut through the DAG with no overdraw and no gaps — exactly the
Nanite "virtual geometry" guarantee.

### 4. Visibility Buffer

Clusters passing the LOD cut are rasterized into a **128-bit visibility buffer**:
- R64: `cluster_id` (32 bit) | `triangle_id` (24 bit) | depth (32 bit, float)
- Separate full-resolution depth buffer for HZB construction

Material evaluation is deferred: a screen-space compute shader looks up the
cluster+triangle ID, reconstructs barycentrics, samples material parameters.

### 5. Software Rasterizer (sub-pixel triangles)

Triangles covering < 1 pixel are rasterized via a HLSL/GLSL compute shader
(custom scanline rasteriser into the visibility buffer). This avoids GPU hardware
rasterizer overhead for tiny triangles (degenerate quads, distant foliage).

### Phase 528 Scope (this ADR)

Phase 528 ships only:
- `cd::virtual_geometry::ClusterDAG` — in-memory DAG structure + LOD-select helpers
- `cd::virtual_geometry::ClusterDAGBuilder` — offline mesh-to-DAG converter
  (METIS-style BFS partition + QEM edge-collapse simplification skeleton)
- Unit tests (4+ cases): sphere mesh build, multi-level DAG, cluster size bound,
  cycle-free invariant
- CMake wiring (STATIC library, adds `src/` to existing INTERFACE library)

The visibility buffer, software rasterizer, and GPU compute pipeline are deferred
to Phase 2 (estimated 12-week implementation from this point).

---

## Reddedilen

### Traditional LOD Chain (discrete LOD0–LOD5)

- **Rationale**: pop artefacts at LOD boundaries are unacceptable for close-up
  inspection. Every asset requires manual art work per LOD step (6 meshes × N assets).
  No sub-cluster streaming granularity; the full LOD0 mesh is loaded before any
  simplification occurs. Rejected as not scalable past ~100k polygon scenes.

### Per-Mesh Impostors / Billboards

- **Rationale**: angular quality collapse at camera roll. Billboard atlases consume
  gigabytes for scenes with thousands of unique objects. Incompatible with real-time
  lighting (impostors bake a fixed lighting direction). **Memory cost** alone is
  prohibitive: 1024 objects × 512×512×4-byte atlas = 1 GB VRAM. Rejected.

### Opacity Clipmap / Shell Texture (foliage only)

- **Rationale**: domain-specific (foliage), does not address hard-surface meshes.
  Does not reduce triangle throughput for near-camera geometry. Acceptable as a
  complement but not a replacement. Retained as a parallel system.

### Virtualized Micro-polygon Rendering (Disney's Reyes)

- **Rationale**: requires dice-and-shade per micro-patch, incompatible with the
  deferred material pipeline. Tesselation overhead exceeds the benefit for current
  hardware (verified: Frostbite post-mortem, Harada 2020). Deferred to research track.

---

## Sonuçlar

### Shipped (phase528)

- Library: `engine/render/virtual_geometry/`
  - `include/cd/virtual_geometry/VirtualGeometry.hpp` — `ClusterNode` + LOD pick
    helpers (pre-existing, extended)
  - `include/cd/virtual_geometry/ClusterDAG.hpp` — `Cluster`, `Triangle`, `AABB`,
    `ClusterDAG`, `ClusterDAGBuilder`
  - `src/ClusterDAG.cpp` — builder implementation (BFS partition + QEM collapse)
  - `tests/test_cluster_dag.cpp` — 4 unit tests
- CMake: `cd::virtual_geometry` promoted from INTERFACE to STATIC

### Implementation Schedule (Phase 2, 12-week estimate)

| Week | Milestone |
|------|-----------|
| 1–2  | Integrate METIS library (or MIT-licensed substitute) for graph partition |
| 3–4  | Full QEM edge-collapse (Garland–Heckbert 1997) with seam preservation |
| 5–6  | GPU cluster upload + HZB construction pass |
| 7–8  | Visibility buffer rasterizer (hardware path) |
| 9–10 | Software rasterizer compute shader (sub-pixel triangles) |
| 11   | Deferred material evaluation via visibility buffer |
| 12   | Integration test: Sponza scene at 2M triangles, regression baseline |

### Quality Targets

- Cluster size: ≤ 128 triangles, ≤ 256 vertices (Nanite spec).
- LOD transition: zero pop artefacts (continuous error function).
- GPU throughput: ≥ 100M triangles/frame at 1080p (60 fps target).
- VRAM overhead: DAG node = 64 bytes; 10M triangle scene ≈ 320 MB DAG.

### Known Risks

1. **QEM seam preservation**: edge-collapse at cluster boundaries can introduce
   cracks. Mitigation: locked-vertex boundary policy (Hoppe 1998).
2. **Software rasterizer precision**: 32-bit fixed-point arithmetic required to
   avoid sub-pixel z-fighting. Non-trivial to validate.
3. **METIS license**: METIS itself is LGPL. Plan: implement equivalent BFS graph
   partitioner or adopt `meshoptimizer` (MIT) cluster builder.

---

## Links

- Karis, B., Stenson, R., Sjödahl, M. "Nanite: A Deep Dive." *SIGGRAPH 2021 Advances in
  Real-Time Rendering*. https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
- Garland, M., Heckbert, P. S. "Surface Simplification Using Quadric Error Metrics."
  *SIGGRAPH 1997*.
- Hoppe, H. "Efficient implementation of progressive meshes." *Computers & Graphics*, 1998.
- Harada, T. "Micro-Polygon Rasterization." *SIGGRAPH Advances*, 2020.
- meshoptimizer (MIT): https://github.com/zeux/meshoptimizer — cluster builder reference.
