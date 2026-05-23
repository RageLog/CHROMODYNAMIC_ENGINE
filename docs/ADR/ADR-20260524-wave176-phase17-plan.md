# ADR — Wave 176 — Phase 17 plan

- Date: 2026-05-24
- Status: Accepted
- Wave: 176
- Predecessor: Phase 16 (wave172), closed at v0.47.0.

## Context

Phase 16 deferred three "headline" items to Phase 17:
- Vulkan RT pipeline (the engineering chunk)
- Material v2 PBR multi-pass
- AMD CI workflow

Plus the user-stated priority: **Vulkan #1, D3D12 #2, OpenGL #3.**
OpenGL backend (cd::rhi_opengl) belongs in Phase 17 as the 3rd API
priority's introduction.

Phase 17 also wires the cross-asset hot-reload that Phase 16's
FileWatcher primitive made possible.

## Decision

Phase 17 opens with seven sub-phases ordered by dependency + risk.

### 17.A — Vulkan RT BLAS construction (real impl) → close

Picks up where Phase 15.E (extension enablement) stopped. Real
BLAS build:
- `VulkanDevice::create_acceleration_structure` builds a BLAS:
  query sizes via `vkGetAccelerationStructureBuildSizesKHR`,
  allocate AS-storage buffer + scratch buffer via VMA, call
  `vkCreateAccelerationStructureKHR`.
- `VulkanCommandBuffer::build_acceleration_structure` records
  `vkCmdBuildAccelerationStructuresKHR`.
- Skip RT pipeline + SBT + dispatch_rays — those land in 17.B.
- Headless unit test verifies an AS handle is returned with
  non-zero size.

### 17.B — Vulkan RT pipeline + SBT + dispatch_rays → close

- `vkCreateRayTracingPipelinesKHR` consuming raygen + miss +
  closest-hit shader modules + max-recursion depth.
- SBT layout: tightly-packed groups aligned to
  `shaderGroupBaseAlignment`. Helper builder
  `cd::rhi_vulkan::sbt::pack(...)`.
- `VulkanCommandBuffer::dispatch_rays` records vkCmdTraceRaysKHR
  with the strided-buffer SBT descriptor.
- `hello_rt_triangle` sample: single triangle BLAS + one TLAS
  instance + raygen returning red on hit, gradient on miss.
- Validate on NVIDIA RTX 3080 Laptop.

### 17.C — Material v2 PBR multi-pass → close

cd::material rev:
- `LayeredMaterialDesc` adds base + clearcoat + sheen layers.
- Per-material shader-variant cache (hash of
  `(layout, definitions, attachment_formats)`).
- `hello_pbr_layered` sample with sphere + tunable clearcoat
  intensity / sheen roughness.

### 17.D — OpenGL backend (cd::rhi_opengl) → introduction

3rd API priority. v0.48.0 ships the boot path only:
- `cd::rhi_opengl::create_gl_device(GLCreateInfo)` factory.
- `OpenGLDevice` implements IDevice with a minimal subset:
  - GL 4.6 context creation (GLAD loader).
  - `create_buffer` / `create_texture` / `create_swapchain` /
    `clear` path through a default framebuffer.
  - Rest of IDevice surface returns kNotImplemented.
- `hello_opengl_boot` sample (color-clear; mirrors v0.27.0
  D3D12 boot wave).

### 17.E — Asset hot-reload (textures + scenes via FileWatcher) → close

cd::asset::AssetRegistry gains a `FileWatcher` member:
- Asset load registers the source path + a per-asset reload
  callback.
- A `tick()` method polls the watcher; reloads dispatch back
  to the asset's loader.
- hello_hot_reload extended to handle texture file changes
  (previous behaviour was shader-only).

### 17.F — AMD CI workflow_dispatch placeholder → close

Workflow file that documents the AMD lavapipe / Mesa RADV test
matrix and provides the dispatch trigger. No runtime when no
AMD runner exists; the workflow is the API contract.

### 17.G — Editor selection-outline + gizmo design ADR → close

Editor UX polish — the v0.47.0 viewport renders the selected
entity but doesn't visually distinguish it. Design ADR sketches
the wire-frame outline + per-axis translation gizmo. Impl
deferred to Phase 18.

## Consequences

- Tag v0.48.0 covers all of Phase 17.
- Phase 18 plan to follow at close.

## References

- ADR-20260524-wave170 (RT extension enable — 17.A builds on it)
- ADR-20260524-wave172 (Phase 16 plan — 17.C/F items deferred from)
- engine/asset/include/cd/asset/FileWatcher.hpp (Phase 16.C —
  17.E consumes)
