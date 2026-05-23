# ADR — Wave 172 — Phase 16 plan

- Date: 2026-05-24
- Status: Accepted
- Wave: 172
- Predecessor: Phase 15 master (wave165), closed at v0.46.0.

## Context

Phase 15 close ADR (wave171) listed Phase-16 first-priority items.
User direction (2026-05-24): Vulkan = primary, D3D12 = 2nd, OpenGL
= 3rd. Phase 16 reflects that ordering.

## Decision

Phase 16 opens with the seven sub-phases below.

### 16.A — Editor DockBuilder + viewport selection highlight → close

Editor UX polish:
- ImGui::DockBuilder pre-built default layout (Scene-left,
  Inspector-right, Toolbar-top, History-bottom, central
  passthrough for the 3D viewport).
- Selected entity gets a wire-frame outline in the viewport
  (re-render the selected cube with cull=front + slightly
  scaled to produce a flat-color silhouette).

### 16.B — D3D12 samplers + UAVs + cube SRVs → close

Closes the descriptor-set gaps from Phase 15.B:
- Separate D3D12 sampler heap + sampler descriptor table.
- `update_descriptor_set` SRV cube + Texture2DArray
  dimensions.
- `update_descriptor_set` UAV (Texture2D + RWBuffer).
- Static-sampler fallback if a layout requests samplers
  without a sampler heap allocated.

### 16.C — Vulkan RT pipeline + BLAS/TLAS + raygen sample → close

The headline Phase 16 item (Vulkan = primary):
- `VulkanDevice::create_acceleration_structure` real BLAS +
  TLAS via VMA + vkGetAccelerationStructureBuildSizesKHR +
  vkCmdBuildAccelerationStructuresKHR.
- `VulkanCommandBuffer::build_acceleration_structure` records
  the build, transitions the AS buffer.
- `VulkanCommandBuffer::dispatch_rays` records
  vkCmdTraceRaysKHR with a SBT range descriptor.
- Ray-tracing pipeline creation via
  vkCreateRayTracingPipelinesKHR — accepts raygen +
  closest-hit + miss shader modules + max-recursion depth.
- glslang RT-stage GLSL → SPIR-V (raygen / miss /
  closesthit) via the existing cd::shader Compiler with
  the SPV_KHR_ray_tracing capability bit set.
- SBT layout: tightly-packed raygen / miss / hit groups,
  alignment to
  `VkPhysicalDeviceRayTracingPipelinePropertiesKHR::
  shaderGroupBaseAlignment`.
- `hello_rt_triangle` sample — single triangle BLAS + one
  TLAS instance + raygen that fires per-pixel, hit returns
  red, miss returns a gradient. Validated on NVIDIA RTX 3080.

### 16.D — Material system v2 (PBR multi-pass) → close

cd::material rev that supports multi-pass PBR:
- Layered material: base + clearcoat + sheen.
- Per-material shader-variant cache (hash of (layout,
  defines)).
- Sample: `hello_pbr_layered` showing a sphere with base
  + clearcoat + sheen tuning.

### 16.E — Asset hot-reload across all asset types → close

Phase 9 wired shader hot-reload. This wave extends to
textures, meshes, scenes:
- cd::asset registry tracks file mtimes per asset.
- A polling watcher thread fires `on_changed(AssetId)` when
  any tracked file's mtime advances.
- hello_hot_reload extended to handle texture + mesh +
  scene reloads in addition to shaders.

### 16.F — Profiler UI (Tracy-style flame graph) → close

cd::profile already has the sample ring + StatsAggregator.
This wave adds an ImGui flame-graph view:
- Per-thread lane.
- Hover tooltip with sample name + ns count.
- Save-trace button → cd::profile::ChromeTraceSink (already
  shipped at v0.39.0).

### 16.G — AMD cross-vendor validation → close

Phase 11 Track B's "third vendor" gate. The marathon machine
has NVIDIA + Intel iGPU; AMD is the missing axis. This wave:
- Documents the test protocol for AMD (CD_VULKAN_DEVICE_INDEX
  + per-vendor golden capture pipeline).
- Wires a `linux-vulkan-amd` workflow_dispatch CI job that
  can run on an AMD-equipped runner when one is provisioned.

## Mobile + OpenGL position

Per user direction, mobile stays in the candidate pool but
not prioritized for Phase 16. OpenGL backend is Phase 17 (3rd
API priority).

## Consequences

- Single tag v0.47.0 covers all of Phase 16.
- Phase 17 plan to follow at close.

## References

- ADR-20260524-wave171-v0.46.0-phase15f-close.md
- ADR-20260524-wave170 (RT extension enablement — 16.C builds on it)
- ADR-20260524-wave167 (D3D12 descriptor sets — 16.B extends)
