# cd::rhi — Vulkan backend

**Status**: IMPL ~95% | GPU-verified ~80% (RTX 3080 on this host; lavapipe for CI)

Two-axis: *impl* = code written and on the parity bar; *gpu-verified* = pixels/traces confirmed on real hardware or a software rasteriser.

---

## What is implemented

The Vulkan backend is the reference implementation that D3D12 and Metal are held to.

| Surface | Impl | GPU-verified | Notes |
|---|---|---|---|
| Resources (buffer / texture / views) | Yes | Yes | VMA-backed. All sampler types, MIP chains, cube arrays, BCn/RGBA formats. |
| Graphics + compute pipelines | Yes | Yes | `VkPipeline` create with `PipelineCacheKey` dedup. |
| Mesh-shader pipeline | Yes (gated) | Yes | `VK_EXT_mesh_shader` feature-gate (`features_.mesh_shader`). |
| Pipeline cache (disk/seed) | Yes | Yes | `VkPipelineCache` create/serialize/deserialize (`init_pipeline_cache_`, `get_pipeline_cache_data`). Kills cold-start stutter. |
| Descriptors + bindless | Yes | Yes | `VK_EXT_descriptor_indexing`, variable-count allocation, dedicated bindless set (W8-BE, phase 864). |
| Sync — binary semaphores | Yes | Yes | `VkSemaphore` acquire/submit path. |
| Sync — timeline semaphores | Yes | Yes | Cross-queue overlap via `VkSemaphoreTypeTimeline`. |
| Swapchain + present | Yes | Yes | Win32 surface (compile-time guard on other platforms, §B-PLATFORM-GUARD). |
| HDR colour-space | Yes | Yes | `VkSurfaceFormatKHR` selection; PQ pixel eyeball needs HDR panel. |
| Render pass (dynamic rendering) | Yes | Yes | `VK_KHR_dynamic_rendering`; no legacy render passes. |
| MSAA resolve | Yes | Yes | `VK_RESOLVE_MODE_AVERAGE_BIT` wired per attachment (`begin_rendering_`, V-MSAA-RESOLVE, Wave 3c). |
| Parallel render pass | Yes | Yes | True Vulkan secondary command buffers (`vkCmdExecuteCommands`, `begin_parallel_render_pass`). |
| Readback (blocking) | Yes | Yes | `copy_image_to_buffer` + map; the building block for golden capture. |
| Debug groups + depth | Yes | Yes | `debug_group_depth_` tracked (++ push / -- pop); `vkCmdBeginDebugUtilsLabelEXT`. |
| Feature flags | Yes | Yes | `timestamp_queries` (line 5097), `pipeline_statistics_queries` (line 5105) wired from `VkPhysicalDeviceLimits`. |
| RT — inline ray-query (production) | Yes | Yes | `VK_KHR_ray_query`; BLAS/TLAS build; `rayQueryEXT` in GLSL. Chrome reflection, DDGI, ReSTIR all use this path. |
| RT — SBT pipeline (extra) | Yes | Partial | `vkCreateRayTracingPipelinesKHR` (line 4976) + `dispatch_rays` → `vkCmdTraceRaysKHR` (line 1078). Consumed by `samples/rhi/hello_path_trace`. Functional dispatch on RTX 3080 runnable now. |
| AS build / compaction / refit | Yes | Yes | `kAllowCompaction` + `kAllowUpdate` flags; compact/refit surfaced on Wave 3a. |
| Indirect draw / dispatch | Yes | Yes | `vkCmdDrawIndirect` / `vkCmdDrawIndexedIndirectCount` / `vkCmdDispatchIndirect` — Wave 3b. |
| GPU query subsystem | Yes | Yes | `VkQueryPool` timestamp + pipeline-statistics; `write_timestamp` / `get_query_results` — Wave 3b. |
| Copy image → image | Yes | Yes | `vkCmdCopyImage` / `vkCmdBlitImage` (V-COPY-IMG, Wave 3b). |

### kNotImplemented sites (all are feature-gates, zero TODO-holes)

Six return sites in `VulkanDevice.cpp` — every one is guarded by a `!features_.<cap>` or extension check with a real implementation body below. Full enumeration: `docs/RHI_KNOTIMPL_INVENTORY.md §Vulkan`.

---

## Intentionally deferred / out-of-scope

| Item | ADR section | Reason |
|---|---|---|
| Async / fenced non-blocking readback | §B-NULL-NONBLOCK-READBACK | Blocking one-shot covers current needs; promote when a frame-rate readback feature lands. |
| Sparse / tiled resources | §B-SPARSE | No virtual-texture consumer yet. |
| Multiview / stereo | §B (V-MULTIVIEW closed) | No VR target on the charter. |
| VRS command surface | Tracked (V-FEAT-VRS) | `variable_rate_shading` feature flag intentionally unset until the command surface is added. |
| Validation-clean CI lane | D-VK-VALIDATION-LANE | Operator action: install `VK_LAYER_KHRONOS_validation` on the lavapipe lane. |

ADR: `docs/ADR/ADR-20260616-backend-wontfix-decisions.md`

---

## Remaining verification

- **Lavapipe goldens** (D-VK-GOLDEN): software golden capture infrastructure wired in `ci.yml`; self-skips on empty `tests/golden/lavapipe/`. Operator dispatch needed to populate and gate.
- **RTX 3080 hardware goldens** (D-VK-GOLDEN): the host GPU can capture now; not yet committed to the matrix.
- **NVIDIA self-hosted runner** (D-NVIDIA-RUNNER): the CI yml exists (`ci-nvidia-windows.yml`); runner registration is a one-time operator action to make RT dispatch a standing gate.
- **VK validation-clean gate** (D-VK-VALIDATION-LANE): install the validation layer on the CI lane, then `C-VK-VALIDATION-FULL` becomes a build-failing gate.

Roadmap: `docs/ROADMAP_BACKEND_TO_100.md §GROUP D`

---

## Key files

| File | Role |
|---|---|
| `VulkanDevice.cpp` | Device, resource creation, pipeline, AS, SBT, swapchain, pipeline cache |
| `VulkanCommandBuffer.cpp` | Command recording, render pass, parallel pass, barriers, RT dispatch |
| `VulkanInstance.cpp` | Instance + physical device selection, extension negotiation |
| `VulkanVma.cpp` | VMA allocator integration |
| `NativeHandles.cpp` | Handle-index ↔ `VkImage`/`VkBuffer`/`VkImageView` lookup tables |
| `VulkanDeviceFactory.cpp` | `create_vulkan_device` entry point |

## Related ADRs

| ADR | Topic |
|---|---|
| `ADR-001-rhi-architecture.md` | RHI interface contract, handle model, `IDevice`/`ICommandBuffer` design |
| `ADR-20260615-ndc-y-handedness.md` | NDC-Y / clip-space convention (Vulkan = Y-down reference) |
| `ADR-20260614-d3d12-binding-model.md` | Cross-backend descriptor-space layout (Vulkan is the reference) |
| `ADR-20260616-backend-wontfix-decisions.md` | All CLOSED decisions (platform guard, cap-gates, readback, sparse, …) |
| `ADR-20260606-W8-BE-rt-texture-sampling-bindless.md` | Bindless texture architecture (5-layer; dedicated set requirement) |
