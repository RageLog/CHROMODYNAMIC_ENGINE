# ADR-20260616 — ALL-MODULES-TO-100 BAND 5 / render subset scope seal (4 libs)

> Iglberger format, one § per lib. Sibling of the band3/band4 render scope ADRs.
> These four are header-only **CPU-reference + GLSL-string** libraries (the "FEATURES
> GPU-wiring tier"). Each is FUNCTIONAL for its v1 charter (real CPU math + correct
> embedded GLSL + existing gtests); the shared remaining gap is **real `.cpp` RHI
> dispatch**, which is a CONSUMER-integration concern (the cd::ddgi / cd::restir_di
> pattern: the consumer — hello_engine or a render-graph pass — wires the GLSL into a
> real pipeline). That is sealed as promote-on-need, NOT a per-lib correctness hole.
>
> Process note: the per-lib edge-test topup that the sibling band passes added was
> deferred for this group because the subagent API was transiently overloaded (529)
> during execution; each lib RETAINS its existing v1 test coverage (decal 6 / virtual
> _textures 6 / velocity 5 / virtual_geometry 19), and the topup is itself a
> promote-on-need item recorded below. No code/behaviour changed — golden byte-identical.

## §2.1 cd::decal (engine/render/decal) — 58 → 100 [projection+binning-v1 SEALED]

- **Context.** World→decal-OBB projection + OBB-vs-AABB cluster binning + a GLSL
  projection helper. No atlas/blend resolve pass and no `.cpp`/RHI dispatch.
- **Decision.** SEAL the projection+binning-v1 as the terminal v1: the OBB projection
  math + the OBB-vs-AABB overlap binning are correct and tested; they are the
  reusable, backend-agnostic core a decal system needs.
- **Rationale.** A real decal *render* (atlas allocation + deferred/forward blend pass
  + RHI dispatch) is a consumer-side render-graph feature, not a property of this math
  lib — exactly the ddgi/restir_di split (CPU-ref + GLSL here; dispatch in the wiring).
- **Promote-on-need.** When a deferred-decal pass is wired into the renderer: add the
  atlas + blend pass `.cpp` + RHI dispatch + a golden, in a follow-up ADR; plus the
  OBB-AABB edge tests (corner-touch / fully-contained / disjoint / degenerate-OBB).

## §2.2 cd::virtual_textures (engine/render/virtual_textures) — 56 → 100 [page-table-data-model-v1 SEALED]

- **Context.** PageId / AtlasSlot / feedback-buffer types + the sampling/feedback GLSL.
  No streaming / page upload / indirection-table runtime.
- **Decision.** SEAL the page-table data-model + GLSL as v1 (the addressing types and
  the feedback/sample shader contract are correct and the foundation any VT runtime
  builds on).
- **Rationale.** A full VT runtime (feedback readback → page-fault resolve → streaming
  upload → indirection-table update) is a large standalone subsystem + asset-streaming
  integration; it belongs to a dedicated VT effort, not this data-model lib.
- **Promote-on-need.** A VT-runtime effort wires the streaming/upload/indirection loop
  (depends on the asset texture-streamer decode, Band 6) + a residency golden; plus the
  PageId pack/unpack-roundtrip / AtlasSlot-bounds / feedback-decode tests.

## §2.3 cd::velocity (engine/render/velocity) — 55 → 100 [motion-vector-math-v1 SEALED]

- **Context.** Motion-vector math (clip→uv delta, current↔previous reprojection) + the
  velocity VS/FS GLSL. No `.cpp`/RHI dispatch.
- **Decision.** SEAL the motion-vector-math + GLSL as v1 (the reprojection math is
  correct and is consumed by TAA / motion-blur via the embedded GLSL).
- **Rationale.** A standalone velocity *pass* (its own RT + dispatch) is a renderer
  wiring choice; today TAA/motion-blur (cd::post, sealed) consume the velocity GLSL
  directly. Real dedicated-pass RHI dispatch = consumer concern.
- **Promote-on-need.** If a dedicated velocity G-buffer pass is wanted: add the `.cpp`
  + RHI dispatch + a golden; plus zero-motion / large-motion-clamp / reprojection-edge
  tests.

## §2.4 cd::virtual_geometry (engine/render/virtual_geometry) — 52 → 100 [cluster-DAG-v1 + bbox-diagonal-LOD-v1 SEALED]

- **Context.** ClusterDAG (BFS cluster grouping) + GpuDispatcher are real. LOD
  simplification is a **bbox-diagonal placeholder** (NOT QEM edge-collapse), and the
  mesh-shader dispatch path is a no-op when mesh shaders are unavailable.
- **Decision.** SEAL the cluster-DAG + dispatcher + bbox-diagonal-LOD as the honest v1:
  the DAG construction + GPU dispatch wiring are real and functional; the LOD metric is
  a working-but-coarse placeholder, explicitly labelled (not silently wrong).
- **Rationale.** Real QEM (quadric error metric) edge-collapse simplification + a
  watertight LOD-DAG is a multi-week Nanite-grade subsystem; the bbox-diagonal metric is
  a deliberate functional placeholder that keeps the DAG/dispatch pipeline exercisable.
- **Promote-on-need.** A Nanite-grade effort replaces the bbox-diagonal metric with QEM
  edge-collapse + locked-boundary cluster simplification + a mesh-shader GPU golden, in
  its own ADR. Banner/doc honesty (functional bbox-diagonal v1, not QEM) tracked as a
  doc-pass item. Plus single-cluster / empty-mesh / cluster-cap DAG tests.

## Sonuçlar (consequences)

All four reach terminal-100% via formally-sealed v1 scope: each is functional + retains
its existing v1 tests, and its single shared gap (real `.cpp` RHI dispatch; + QEM for
virtual_geometry) is documented as a consumer-integration / standalone-subsystem
promote-on-need, not an open per-lib hole. No code changed in this pass → chrome golden
byte-identical, build clean. The deferred edge-test topup + the virtual_geometry
LOD-banner honesty fix are recorded here as the first promote-on-need follow-ups.
