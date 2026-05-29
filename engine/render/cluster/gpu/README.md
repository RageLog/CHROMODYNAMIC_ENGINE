# cd::cluster_gpu

## Purpose
Vulkan compute shader pipeline wiring for the forward+ light culling algorithm (cluster_assign.comp). Executes the GPU port of the CPU baseline (cd::cluster) against persistent storage buffers. Embedded GLSL source; runtime compilation to SPIR-V.

## Namespace
`cd::cluster_gpu::` — all public symbols.

## Public headers
- `GpuPipeline.hpp` — Compute pipeline manager; wraps descriptor binding + dispatch for cluster assignment

## Primary types
- `GpuPipeline` — Manages compute shader pipeline state and dispatch; reads/writes cluster_offsets and light_indices buffers

## Usage example
```cpp
#include <cd/cluster_gpu/GpuPipeline.hpp>

// Create pipeline (compiles cluster_assign.comp via cd::shader)
auto pipeline = cd::cluster_gpu::create_gpu_pipeline(device, allocator);

// Dispatch cluster assignment
pipeline->dispatch_cluster_assign(cmd, /*light_buffer=*/light_buf, 
                                  /*grid_size=*/{16, 16, 8});
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_cluster_gpu
```

## Test
```bash
ctest --preset ninja-debug -R cluster_gpu
```

## Dependencies (per CMakeLists)
- `cd::cluster` — Light types, reference compute, grid configuration
- `cd::rhi` — Vulkan abstraction (Device, Buffer, Pipeline)
- `cd::shader` — Runtime GLSL → SPIR-V compilation
- `cd::core` — Core types

## Notes
- Static library; embeds GLSL source as string literals
- Excludes from install (transitive on vendored glslang)
- Consume via cd::cluster_fx umbrella library (batch builds all cluster-related libs)

## References
- ADR-083 — Forward+ architecture
- Wave 104 — GPU cluster implementation
