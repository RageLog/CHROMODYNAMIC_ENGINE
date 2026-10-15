# cd::rhi

**Purpose**: cross-API render hardware interface. Abstract IDevice / ICommandBuffer / Pipeline / handle-based texture-buffer-sampler types so engine code targets Vulkan today, D3D12 / Metal / WebGPU tomorrow without touching every call site.

**Namespace**: `cd::rhi`.

**Headers**: `cd/rhi/{IDevice,ICommandBuffer,Pipeline,Handles,Descriptors,Format,Enums,Barriers}.hpp` plus preset helpers `RasterStatePresets`, `BlendPresets`, `DepthStencilPresets`, `VertexLayoutBuilder`.

**Primary types**:
- `cd::rhi::IDevice` -- abstract device interface (create_texture, create_buffer, create_pipeline, create_sampler, queue submit). Returned by backend factories.
- `cd::rhi::ICommandBuffer` -- abstract command recording surface (begin_render_pass, bind_pipeline, push_constants, draw, dispatch, barriers).
- `cd::rhi::TextureHandle / BufferHandle / SamplerHandle / PipelineHandle` -- opaque ID handles. Engine code never sees backend resources directly.
- `cd::rhi::PipelineDesc` -- declarative state struct (shader stages, vertex layout, raster, blend, depth-stencil, descriptor bindings, push ranges, MRT color formats).
- `cd::rhi::Barrier` / `cd::rhi::ResourceState` -- explicit resource-state transitions for the per-pass barrier graph.

**Usage**:
```cpp
#include <cd/rhi/IDevice.hpp>

cd::rhi::BufferDesc bd {};
bd.size = 64 * 1024;
bd.usage = cd::rhi::BufferUsage::kStorage;
bd.memory = cd::rhi::MemoryUsage::kGpuOnly;
auto ssbo = device.create_buffer(bd);
```

**Test command**: `ctest --preset ninja-debug -R cd_test_rhi --output-on-failure`.

**Notes**:
- Header-only interface; backends live in `cd::rhi_vulkan` / `cd::rhi_d3d12` / `cd::rhi_metal` / `cd::rhi_opengl` / `cd::rhi_webgpu`.
- Handle-based design: pipelines compare by `PipelineCacheKey` hash so duplicate descs alias the same backend object.
- `NullDevice` + `NullCommandBuffer` provide a record-only backend for tests that exercise the engine without a real GPU.

## Backend status (FAZ 9 Close-Out — 100% Complete)

| Backend | Status | Notes |
|---|---|---|
| **Vulkan 1.3** (`cd::rhi_vulkan`) | Production primary (100%) | Dynamic rendering, bindless descriptor indexing, hardware RT (`VK_KHR_ray_tracing_pipeline`), mesh shaders, async compute. |
| **Direct3D 12** (`cd::rhi_d3d12`) | Production parity (100%) | Full parity with Vulkan (Phases 1215–1225 + FAZ 9 Wave 9.1): DXIL shader toolchain, bindless descriptor heap, DXR 1.1 BLAS/TLAS/TraceRays, mesh shaders, enhanced barriers. |
| **Metal 3** (`cd::rhi_metal`) | Production parity (100%) | Full 75-virtual `IDevice` + 46-virtual `ICommandBuffer` surface (Phases 1215–1225 + FAZ 9 Wave 9.1): SPIR-V → MSL 3.0 via `cd::spirv_cross_glue`, Argument Buffers Tier 2, `CAMetalLayer` swapchain, RT acceleration structures. |
| **OpenGL 4.6** (`cd::rhi_opengl`) | Fallback complete (100%) | Direct State Access (DSA) buffers/textures/samplers/FBOs, SPIR-V → GLSL 460 translation, raster & compute fallback for legacy GPUs (FAZ 9 Wave 9.2). |
| **WebGPU** (`cd::rhi_webgpu`) | Production complete (100%) | `WebGpuDevice` (`cd::rhi::webgpu::create_webgpu_device`), GLSL → WGSL translation (`translate_glsl_to_wgsl`), and Dawn/Emscripten `cd::ui_renderer_webgpu` integration (FAZ 9 Wave 9.2). |

## TLAS coverage contract (Phase 656 / T1.11)

`ICommandBuffer::build_acceleration_structure` accepts ALL eligible
geometry through the TLAS path. **There is no geometry-kind filter at
the RHI level** -- not for `kSphere`, not for `kGltf`, not for `kFloor`,
not for any future prim type. The TLAS sees whatever the caller hands it
via `AccelStructureDesc::instances`.

The host-side eligibility gate is `TlasInstanceCandidate::tlas_eligible`
(see `Descriptors.hpp`), which **defaults to true** so a glTF asset
dropped into a scene enters the chrome PBR sphere's RT reflection
without per-asset boilerplate. Higher-level code (scene ingest, ECS
render pass) may flip the flag to false for a specific instance (an
editor-only gizmo, a debug visualiser, a ghost-shadow placeholder) but
must never gate on the BLAS source asset type.

The recommended pipeline is:

```cpp
#include <cd/rhi/TlasBuilder.hpp>

std::vector<cd::rhi::TlasInstanceCandidate> candidates;
candidates.push_back({ make_accel_instance(blas_sphere, m_sphere) });  // default eligible
candidates.push_back({ make_accel_instance(blas_gltf,   m_gltf)   });  // default eligible
candidates.push_back({ make_accel_instance(blas_ghost,  m_ghost),
                       /*tlas_eligible=*/ false });                    // explicit opt-out

std::vector<cd::rhi::AccelInstance> live;
cd::rhi::build_tlas_instances(candidates, live);

cd::rhi::AccelStructureDesc tld {};
tld.kind      = cd::rhi::AccelStructureKind::kTopLevel;
tld.instances = std::span<const cd::rhi::AccelInstance>(live);
auto tlas = device.create_acceleration_structure(tld).value();
cmd.build_acceleration_structure(tlas);
```

Regression tests: `cd_test_tlas_coverage` (`tests/test_tlas_coverage.cpp`)
locks the contract on three cases -- default-flag glTF candidate enters
the live list, default-flag sphere candidate enters the live list, a
candidate with `tlas_eligible = false` is excluded.
