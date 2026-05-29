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
- Header-only interface; backends live in `cd::rhi_vulkan` / `cd::rhi_d3d12` / `cd::rhi_metal`.
- Handle-based design: pipelines compare by `PipelineCacheKey` hash so duplicate descs alias the same backend object.
- `NullDevice` + `NullCommandBuffer` provide a record-only backend for tests that exercise the engine without a real GPU.
