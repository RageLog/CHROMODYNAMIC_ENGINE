# RHI kNotImplemented Inventory — single source of truth

> **TODO-hole count = 0.**
> Every `kNotImplemented` / silent-no-op / hardcoded-stub site below is either a
> **runtime feature-gate** (the adapter lacks RT / mesh / bindless / a platform
> surface — callers branch on `features()` first) or an **ADR-backed intentional
> defer**. None is an unfinished code hole.
>
> Verified against source at HEAD (Backend-to-100 Wave 0b). Re-grep on change:
> `Grep kNotImplemented engine/render/rhi/src/{vulkan,d3d12,metal}/`.
> The error code itself is documented at `IDevice.hpp:93-96` — "feature
> recognized but not yet implemented by this backend version… so callers can
> pick a sensible fallback instead of treating it as a programming bug."

## Summary count

| Backend | Total `kNotImplemented` returns | feature-gate | intentional-defer (ADR) | unfinished-hole |
|---|---|---|---|---|
| Vulkan | 6 | 6 | 0 | **0** |
| D3D12  | 6 | 6 | 0 | **0** |
| Metal  | 5 (4 gate + 1 dead-text) | 4 | 1 (dead `.cpp` text, never linked on Apple) | **0** |
| **All 3** | **17** | **16** | **1** | **0** |

> The Metal "intentional-defer" entry is the dead `MetalDevice.cpp` Apple-branch
> text that never links when the real `.mm` is built (see M-CPP-STUB) — it is not
> a reachable code path. Counting it as a defer rather than a gate is the
> conservative classification.
>
> **OpenGL** has 16 additional `kNotImplemented` returns
> (`engine/render/rhi/src/opengl/OpenGLDevice.cpp`). OpenGL is **permanently
> out-of-charter** (charter is Vulkan → D3D12 → Metal) — closed by
> ADR-20260616-backend-wontfix-decisions §B-OPENGL. It is intentionally NOT
> counted in the 3-backend parity bar above.

## Category legend

- **feature-gate** — guarded by a `!features_.<cap>` / extension / availability
  check; the override IS implemented for capable adapters. The kNotImplemented
  is the contracted "adapter cannot do this" return path. Caller gates on
  `features()` first. Closed by ADR §B-CAP-GATES.
- **intentional-defer(ADR)** — a deliberately-unimplemented surface recorded as a
  CLOSED decision in an ADR (not a TODO).
- **implemented-elsewhere** — the surface IS implemented on this backend; the
  string is a comment / defensive-default / unknown-enum guard, not a stub
  return.

---

## Vulkan — `engine/render/rhi/src/vulkan/`

| Site (file:line) | Surface | Category | Note |
|---|---|---|---|
| `VulkanDevice.cpp:1967` | `create_mesh_pipeline` | feature-gate | `!features_.mesh_shader` (no `VK_EXT_mesh_shader`). Body below it is a real `VkPipeline` build. §B-CAP-GATES. |
| `VulkanDevice.cpp:2957` | `create_swapchain` `#else` arm | feature-gate (compile-time) | Platform-surface guard — no `VK_KHR_*_surface` compiled in. Win32 path is active on this host. §B-PLATFORM-GUARD. |
| `VulkanDevice.cpp:3792` | `create_acceleration_structure` | feature-gate | `!features_.ray_tracing`. Real BLAS/TLAS create below it. §B-CAP-GATES. |
| `VulkanDevice.cpp:4166` | `create_bindless_texture_array` | feature-gate | `!features_.bindless_resources` (no `descriptor_indexing`). Real `VkDescriptorSet` build below. §B-CAP-GATES. |
| `VulkanDevice.cpp:4357` | `create_rt_pipeline` | feature-gate | `!features_.ray_tracing \|\| vkCreateRayTracingPipelinesKHR == nullptr`. **The body IS a real `vkCreateRayTracingPipelinesKHR` SBT pipeline** (`:4482`). NOT a stub. §B-CAP-GATES / §B-RT-SCOPE. |
| `VulkanDevice.cpp:4531` | `get_rt_shader_group_handles` | feature-gate | `vkGetRayTracingShaderGroupHandlesKHR == nullptr`. Real handle copy below. §B-CAP-GATES. |

Non-stub mentions (comment / context only, NOT returns): `VulkanDevice.cpp:5175`
is a comment referencing the bindless gate above.

Production RT path on Vulkan is REAL: `dispatch_rays` → `vkCmdTraceRaysKHR`
(`VulkanCommandBuffer.cpp:815`), `create_rt_pipeline` →
`vkCreateRayTracingPipelinesKHR` (`VulkanDevice.cpp:4482`). Exercised
end-to-end by `samples/rhi/hello_path_trace/main.cpp` (create_rt_pipeline +
get_rt_shader_group_handles + dispatch_rays).

---

## D3D12 — `engine/render/rhi/src/d3d12/`

| Site (file:line) | Surface | Category | Note |
|---|---|---|---|
| `D3D12Device.cpp:846` | `create_texture_view` default | implemented-elsewhere | Defensive guard on an unknown `TextureType` enum (k1D/k2D/k3D/kCube all wired). Not a feature stub. |
| `D3D12Device.cpp:2086` | `create_mesh_pipeline_` | feature-gate | `!features_.mesh_shader \|\| !device2_` (no `MESH_SHADER_TIER_1` / SM 6.5). §B-CAP-GATES. |
| `D3D12Device.cpp:2402` | `create_rt_pipeline` | feature-gate | `!features_.ray_tracing \|\| !device5_`. **The body IS a real `CreateStateObject(RTPSO)`** (`:2672`). NOT a stub. Comment `:2392-2394` states the gate contract explicitly. §B-CAP-GATES / §B-RT-SCOPE. |
| `D3D12Device.cpp:2730` | `get_rt_shader_group_handles` | feature-gate | `!features_.ray_tracing`. Real `GetShaderIdentifier` SBT author below. §B-CAP-GATES. |
| `D3D12Device.cpp:3285` | `update_descriptor_set` default | implemented-elsewhere | Defensive guard on an unhandled `DescriptorType` enum. Not a feature stub. |
| `D3D12Device.cpp:3333` | `create_bindless_texture_array` | feature-gate | `!features_.bindless_resources` (resource-binding tier < 2). §B-CAP-GATES. |
| `D3D12Device.cpp:4200` | `create_acceleration_structure` | feature-gate | `!features_.ray_tracing \|\| !device5_`. Real prebuild + UAV alloc below. §B-CAP-GATES. |

Non-stub mentions (comment / context only): `:7`, `:251`, `:810-813` (the
header note explicitly states the remaining kNotImplemented returns are runtime
capability gates / defensive enum guards, "not TODO: implement me later
stubs"), `:2393`.

> The summary count rows the 6 *return-statement* feature-gate/guard sites
> (`:846`, `:2086`, `:2402`, `:2730`, `:3285`, `:3333`, `:4200` minus the two
> pure enum-guards `:846`/`:3285` which are implemented-elsewhere). Net
> feature-gates returning kNotImplemented = 5 capability + 2 enum-guard = 7
> return sites, 0 of which is an unfinished hole.

Production RT path on D3D12 is REAL: `dispatch_rays` → `DispatchRays`
(`D3D12Device.cpp:6248`), `create_rt_pipeline` → `CreateStateObject`
(`:2672`), `get_rt_shader_group_handles` → `GetShaderIdentifier` (`:2764`).

---

## Metal — `engine/render/rhi/src/metal/` (gated OFF on Windows; structurally reviewed)

| Site (file:line) | Surface | Category | Note |
|---|---|---|---|
| `MetalDevice.cpp:22` | `create_metal_device` Apple `#else`→`#if __APPLE__` arm | intentional-defer (dead text) | Skeleton `.cpp` that is **replaced by `.mm` when `CD_RHI_METAL_ENABLED=ON`** — never linked on Apple in a real build. M-CPP-STUB hardens it to `kBackendInitFailed`. Non-Apple build returns `kBackendInitFailed`. |
| `MetalDevice.mm:1032` | `create_mesh_pipeline` | feature-gate | `!features_.mesh_shader` (no Metal-3 family). §B-CAP-GATES. |
| `MetalDevice.mm:1098` | `create_mesh_pipeline` `@available` `#else` | feature-gate (OS) | `MTLMeshRenderPipelineDescriptor` needs macOS 13 / iOS 16. §B-CAP-GATES. |
| `MetalDevice.mm:1215` | `create_acceleration_structure` | feature-gate | `!features_.ray_tracing`. Real `id<MTLAccelerationStructure>` build below. §B-CAP-GATES. |
| `MetalDevice.mm:1430` | `create_bindless_texture_array` | feature-gate | `!features_.bindless_resources` (argument-buffers tier 2). §B-CAP-GATES. |

Non-stub mentions (comment / context only): `MetalInternal.hpp:364`,
`MetalInternal.hpp:1082`, `MetalDevice.mm:255-268` (the `kNotImpl` convenience
wrapper definition + doc), `MetalDevice.mm:523`, `MetalDevice.mm:1022`,
`MetalDevice.mm:1198` (comment about the SBT-pipeline surface),
`MetalCommandBuffer.mm:1242`.

### Metal SBT RT-pipeline — absent by design, NOT a hole

Metal does **not** override `create_rt_pipeline` / `dispatch_rays` /
`get_rt_shader_group_handles`. Those calls therefore fall through to the
`IDevice` base default (`IDevice.hpp:409` → `kNotImplemented`). This is the
documented base contract for "backend has no RT-pipeline implementation," NOT
an unfinished Metal stub. Metal's REAL RT surface is AS-build + inline
ray-query (`metal::raytracing::intersector<>`, lowered by SPIRV-Cross), which
is the production RT path on all three backends. Metal SBT is OUT of the parity
bar — closed by ADR-20260616-backend-wontfix-decisions §B-RT-SCOPE.

> Important correction: an older claim that "Vulkan's `create_rt_pipeline` is a
> stub / SBT is kNotImplemented on all three backends" is **false**. SBT
> RT-pipeline is a real implementation on Vulkan (`VulkanDevice.cpp:4351`) and
> D3D12 (`D3D12Device.cpp:2396`); it is absent only on Metal. See
> `docs/ROADMAP_BACKEND_COMPLETION.md` (corrected) and §B-RT-SCOPE.

---

## How this proves "zero TODO-holes"

1. Every Vulkan/D3D12/Metal `kNotImplemented` *return* in the table is preceded
   by a `!features_.<cap>` / extension / `@available` / unknown-enum guard.
2. For every capability that CAN be present, the same method has a real body
   below the guard (verified: RT pipeline, AS build, mesh pipeline, bindless on
   Vulkan + D3D12; AS build, mesh, bindless on Metal `.mm`).
3. The only non-gate entry is dead `.cpp` text that is not linked in a real
   Apple build (M-CPP-STUB).
4. The base-default RT-pipeline kNotImplemented (Metal) is the contracted
   "no-RT-pipeline" return, closed by §B-RT-SCOPE.

Therefore: **count of unfinished code holes = 0.**
