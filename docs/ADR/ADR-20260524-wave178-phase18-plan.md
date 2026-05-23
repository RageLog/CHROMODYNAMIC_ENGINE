# ADR — Wave 178 — Phase 18 plan

- Date: 2026-05-24
- Status: Accepted
- Wave: 178
- Predecessor: Phase 17 (wave176) closed at v0.48.0.

## Context

Phase 17 deferred multiple items (RT pipeline, material v2, OpenGL,
AMD CI). Phase 18 picks them up + adds Phase-15-listed leftover
candidates (networking client prediction, job system priority,
editor gizmo).

User priority refresh: **Vulkan #1, D3D12 #2, OpenGL #3**.

## Decision

Phase 18 opens with six sub-phases. Headline = the OpenGL backend
boot (3rd API joins the family) + the Vulkan TLAS path.

### 18.A — Vulkan TLAS construction

Mirror 17.A's BLAS shape, swap kind:
- `AccelStructureKind::kTopLevel` path in
  `create_acceleration_structure` allocates a TLAS instead of
  rejecting it.
- Per-instance descriptor list lands as a separate API surface
  (`AccelInstanceList`) since AccelStructureDesc.triangles
  is BLAS-specific.

### 18.B — OpenGL backend (cd::rhi_opengl) boot

3rd API priority. v0.49.0 ships:
- `cd::rhi_opengl::create_gl_device(GLCreateInfo)` factory.
- `OpenGLDevice` implements IDevice with boot-only methods:
  - GL 4.6 context (GLAD or volk-equivalent).
  - `backend()` returns kOpenGL; `adapter_name()` returns
    `glGetString(GL_RENDERER)`.
  - Rest of IDevice returns kNotImplemented (same shape
    as D3D12 v0.27.0).
- `hello_opengl_boot` sample mirrors hello_d3d12_boot.

### 18.C — AMD CI workflow_dispatch placeholder

Workflow file `linux-vulkan-amd.yml` that documents the AMD
Mesa/RADV test matrix. Dispatch-only (no runtime when no
runner exists); same shape as the lavapipe workflow we shipped
in Phase 13.B.

### 18.D — Editor selection outline + gizmo (partial)

Per Phase 17.G design:
- Selected cube re-rendered with `cull=kFront` + slight scale
  inflation to produce a flat-color silhouette outline.
- Three axis-aligned translation arrows (lines, not meshes,
  to avoid shipping new geometry); hit-test against mouse
  click in screen space.
- Marathon scope cut: rotation + scale gizmos deferred.

### 18.E — Networking client-side prediction primitive

cd::net gains `PredictionBuffer<T>` header — ring-buffered
sequence-stamped state snapshots so a client can replay
inputs after a server correction. Headless test confirms
roll-back-and-replay produces deterministic output.

### 18.F — Job system priority benchmark

cd::concurrency::WorkStealingThreadPool already supports
priorities (per ADR-015). Phase 18 ships a `hello_bench`
extension that measures throughput at three priority levels
and verifies the linear-priority-cost claim.

## Out of scope

- Full RT pipeline + SBT (Phase 19 candidate — needs DXC/glslang
  RT-stage shader compile).
- Material v2 PBR multi-pass (Phase 19 — needs shader-variant
  cache design).
- Mobile runtime impl (still no device hardware).

## Tag

Single tag v0.49.0 at Phase 18 close.

## References

- ADR-20260524-wave176-phase17-plan.md
- ADR-20260524-wave170 (RT extension enable)
- engine/render/rhi_vulkan/src/VulkanDevice.cpp (17.A BLAS path)
