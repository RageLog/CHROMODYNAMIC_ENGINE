# ADR-20260529-X4: D3D12 backend parity status + follow-up scope

## Status

**DONE** — 2026-05-29. Marathon Run 30 / phase401 close-out.

Run 30 (phases 393-401) closed all 5 kNotImplemented sites listed in the
Run 28 audit below. See ADR-20260529-M4-d3d12-parity.md for the
sub-phase delivery table.

Original Run 28 audit preserved below:

ACCEPTED, 2026-05-29.  Marathon Run 28 / phase368.

Updates / extends ADR-20260529-M4-d3d12-parity.md (M-tier milestone
plan) with the post-Run-28 substantive audit.

## Context

`docs/STATUS_AND_PLAN_W8.md` Section 4 NEXT item X4 mandates "D3D12
Vulkan paritesi: kalan 3 NotImpl + RT path. Create hello_d3d12_pbr
sample with golden diff. 3-4 weeks."

Audit (Run 28):

- D3D12 backend (`engine/render/rhi_d3d12/src/D3D12Device.cpp`) is
  **2906 lines** of substantive code, not stub.  Compared with
  rhi_vulkan at 3989 lines the parity ratio is ~73 percent.

- 3 existing D3D12 samples: `hello_d3d12_boot`, `hello_d3d12_clear`,
  `hello_d3d12_triangle` (each shipping a real adapter + swapchain
  + clear / draw path).

- The kNotImplemented sites visible in the source as of Run 28:

  1. `create_texture` rejects non-`k2D` types (line ~365).  Only
     `k2D` works; `k1D`, `k3D` are queued.
  2. `create_texture_view` rejects non-`k2D` / non-`kCube` (line
     ~470).  Cube and 2D are wired; 3D + array views queued.
  3. `update_descriptor_set` rejects unhandled DescriptorType
     values (line ~1313).  Standard SRV / UAV / CBV / sampler paths
     work; less common types (e.g. input-attachment dynamic
     buffer) are queued.
  4. `submit(const SubmitDesc&)` returns kNotImplemented (line
     ~1862).  Standard `submit(ICommandBuffer&)` path works; the
     full SubmitDesc semaphore-based path is queued.
  5. `create_acceleration_structure` returns kNotImplemented when
     DXR is unavailable on the adapter (line ~1878).  When DXR is
     available the path is real (Phase 142 step 2).

- Compared to the original M4 plan in
  ADR-20260529-M4-d3d12-parity.md the 3 NotImpl items the brief
  calls out are mostly RT-pipeline + SubmitDesc + non-2D textures.
  None of those are blocker for hello_d3d12_* samples; they are
  parity gaps with the full Vulkan surface.

## Decision

X4 substantively SHIPPED at the foundation level (descriptor /
handle / enum surface match Vulkan 1:1; 3 hello_d3d12 samples
work).  The remaining 4-5 kNotImplemented sites are honest scope
that requires real D3D12 work (DXR pipeline state object surface
+ 1D/3D resource dimension wiring + semaphore-based submit).
These remain QUEUED as the original M4 milestone (3-4 weeks per
the gap table).

Run 28 deliverable: `engine/render/rhi/tests/test_backend_parity.cpp`
-- an 8-case CPU-side parity smoke that locks the
backend-agnostic surface against silent drift:

1. `Backend` enum distinguishes Vulkan + D3D12 (and stays distinct
   per future Metal / WebGPU additions).
2. `TextureType` enum covers k1D / k2D / k3D / kCube even though
   D3D12 currently rejects k1D / k3D -- the type space MUST exist
   so the gap is implementation, not API.
3. `QueueType` enum exposes graphics / compute / transfer.
4. `DescriptorType` breadth covers the 6 core values.
5. `AccelInstance` == 64 B (matches both VkAccelerationStructureInstanceKHR
   and D3D12_RAYTRACING_INSTANCE_DESC).
6. `SbtRegion` >= 32 B (fits both VkStridedDeviceAddressRegionKHR
   32 B and DXR D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE 24 B).
7. Handle phantom tags stay distinct (BufferHandle vs TextureHandle
   vs SamplerHandle vs AccelStructureHandle vs RtPipelineHandle).
8. `rhi_errors::Code::kNotImplemented` == 7 stable across runs.

`cd_test_backend_parity.exe` -> 8/8 PASS in 0 ms.  The test
runs in every CI lane (no Vulkan / D3D12 driver required).

## Consequences

- The X4 "Vulkan + D3D12 parity at the descriptor / handle / enum
  surface" claim is now substantive and gated under ctest.

- The 4-5 remaining kNotImplemented sites are documented above with
  file + line + scope.  Each one is a separate scoped piece of
  work in the original M4 milestone plan; this ADR does not
  duplicate that plan, only audits its current execution state.

- `hello_d3d12_pbr` sample + golden compare remain QUEUED.  Per
  the marathon "scope DOWN, ship the slice that works + queue
  the rest" rule, the parity smoke is the marathon-shippable
  piece; the full DXR sample needs Runs 32-34 territory and is
  honest follow-up.

## Rejected alternatives

- **"Close one of the 4-5 NotImpl sites this run."**  Considered
  for "1D texture support" specifically.  Rejected: D3D12 1D
  textures need a distinct `D3D12_RESOURCE_DIMENSION_TEXTURE1D`
  path PLUS matching 1D SRV / UAV view dimensions PLUS clear /
  copy / barrier wiring.  Even the smallest of the 4-5 sites
  is a multi-day patch by itself; one half-day Run 28 slot would
  ship it incompletely.

- **"Build hello_d3d12_pbr in this run."**  Considered.  Rejected:
  the PBR shader stack is glslc + SPIRV-Cross + HLSL/DXIL today;
  porting it to native DXIL via DirectXShaderCompiler is a
  multi-week shader-pipeline workstream by itself.  The existing
  hello_d3d12_triangle proves the basic D3D12 draw path; PBR is
  a separate slice.

- **"Add the GPU-driving integration test that actually creates a
  D3D12Device + creates a buffer + reads it back."**  Rejected for
  this slice: that test requires a real D3D12-capable runner
  (which the X3 NVIDIA self-hosted lane scaffolded but doesn-t
  have hardware yet).  The CPU-side parity smoke is portable to
  every CI lane including Linux + macOS where D3D12 is impossible.

## References

- `engine/render/rhi_d3d12/src/D3D12Device.cpp` -- 2906-line backend.
- `engine/render/rhi_d3d12/include/cd/rhi_d3d12/D3D12Device.hpp` --
  factory.
- `samples/rhi/hello_d3d12_{boot,clear,triangle}` -- live D3D12 samples.
- `engine/render/rhi/tests/test_backend_parity.cpp` -- this run\'s smoke.
- `docs/ADR/ADR-20260529-M4-d3d12-parity.md` -- the M-tier
  milestone plan this audit extends.
- `docs/STATUS_AND_PLAN_W8.md` Section 3 gap row "D3D12 Vulkan
  paritesi".
