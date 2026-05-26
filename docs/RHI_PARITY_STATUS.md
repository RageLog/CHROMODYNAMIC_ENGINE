# RHI Parity Status — v1.8 Roadmap

CHROMODYNAMIC supports four render hardware interface backends with
different maturity levels. This document tracks the parity gap between
each backend and the Vulkan flagship.

## Backend maturity snapshot (as of v0.99.101)

| Backend  | LOC  | Buffer | Image | Swapchain | Cmd Buffer | Pipeline | Descriptors | Shaders | RT/AS  |
|----------|------|--------|-------|-----------|------------|----------|-------------|---------|--------|
| Vulkan   | ~25k | YES    | YES   | YES       | YES        | YES      | YES         | YES     | YES    |
| D3D12    | ~3k  | YES    | YES   | YES       | partial    | stub     | stub        | partial | partial|
| OpenGL   | ~1.2k| YES    | YES   | YES       | partial    | partial  | partial     | partial | NO     |
| Metal    | 31   | NO     | NO    | NO        | NO         | NO       | NO          | NO      | NO     |

Legend: YES = production; partial = sufficient for hello_*_clear sample
but missing features the engine consumer requires; stub = surfaces
kNotImplemented; NO = not started.

## v1.8 ship plan

The full parity push is multi-week and split into four ships, each
self-contained behind a feature flag in the RHI selector. Targets are
ordered by leverage (Vulkan-compatible API surface = least work,
divergent API surface = most work):

### v1.8.1 — D3D12 to production parity
- PSO compile via D3D12RootSignature + ID3D12PipelineState
- Descriptor heap path for CBV/SRV/UAV/Sampler
- DXC shader compile already started (phase 142 — DXR feature detect +
  create_acceleration_structure landed)
- Inline RT (RayQuery) wire-in matching the Vulkan one
- Pass the hello_engine sample identically through the D3D12 device

### v1.8.2 — OpenGL to feature-complete parity
- Bindless texture array path (GL_ARB_bindless_texture)
- SSBO descriptors for the multi-light UBO + LightSlot std140 layout
- Compute shader pipelines for post-fx (bloom/GTAO)
- No ray-tracing support (OpenGL has no RT extension); RT toggles
  silently degrade to RHI-default fallback (CSM-only shadows)
- Pass the prim pipeline through GL; PBR path requires GL 4.6 + ARB
  bindless

### v1.8.3 — Metal MVP (Apple)
- MTLDevice / MTLCommandQueue / MTLLibrary skeleton -> production
- argument buffer descriptor model (different from Vk/D3D12)
- MSL shader translation (use SPIRV-Cross from the existing Vulkan
  SPIR-V output)
- iOS / iPadOS / macOS targets only — no RT (Metal 3 RT could land
  later)

### v1.8.4 — WebGPU (Dawn)
- Reach feature parity with the OpenGL ES 3.1 baseline
- WGSL shader translation (SPIRV-Cross -> WGSL backend)
- Web deployment target; same hello_engine sample running in a
  browser tab
- No RT (WebGPU has no RT spec yet)

## Cross-cutting work

- IDevice::caps() — add a feature flag query so the prim FS can
  branch on RT availability (already partial via VK_KHR_ray_query).
- ShaderCompiler — central glslang -> SPIR-V -> per-backend shader
  blob cache so the engine produces one SPIR-V at build time and each
  backend translates exactly once at runtime.
- Render-graph (cd::framegraph) — pass execution should be backend-
  agnostic so v1.8 backends fall out of v1.7 frame-graph work.

## Validation matrix

Before tagging v1.8 final, each backend must pass:
- ctest -L rhi (all 88 tests, no per-backend opt-out)
- hello_d3d12_clear / hello_opengl_clear / hello_metal_clear /
  hello_webgpu_clear -> rendered colour matches Vulkan reference
- hello_engine running on the backend at >=60 FPS at 1080p on a
  mid-tier 2024 GPU

The Vulkan backend remains canonical; backend bugs that don't
reproduce on Vulkan are backend bugs.
