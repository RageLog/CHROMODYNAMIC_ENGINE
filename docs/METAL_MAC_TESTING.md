# CHROMODYNAMIC — Metal Backend: macOS Build + Test Guide

> **Status**: M1-M9 source written (phases 1197-1201). Windows ninja-debug
> build clean (Metal gated off, stub path). Mac compile + GPU golden pending
> on-device. This doc is the checklist the developer follows on a physical
> Mac to verify the full Metal surface.
>
> **Mirror**: style follows `docs/ROADMAP_BACKEND_PARITY.md`.
> **Authoritative ADR**: `docs/ADR/ADR-20260615-metal-backend-completion.md`.

---

## §0. Prerequisites

| Requirement | Minimum | Notes |
| --- | --- | --- |
| macOS | 13 Ventura | Metal 3 / MSL 2.4 floor for ray-query |
| Xcode | 15.x | Provides `xcrun`, Apple Clang, Metal SDK |
| CMake | 3.28+ | `ENABLE_LANGUAGE(OBJCXX)` support |
| Ninja | any | matches the ninja-debug preset |
| vcpkg | latest | manifest mode, same as Windows |
| Git | any | dev branch |

Verify Apple Clang supports Objective-C++:

```bash
xcrun clang++ --version   # must report Apple clang 15+
xcrun metal --version     # must report Metal 31001+
```

---

## §1. CMake configure (macOS)

```bash
cmake -S . -B build/ninja-metal \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCD_RHI_METAL_ENABLED=ON \
  -DCD_ENABLE_TESTING=ON \
  -DVCPKG_TARGET_TRIPLET=arm64-osx   # or x64-osx on Intel
```

Key flags:

- `-DCD_RHI_METAL_ENABLED=ON` — unlocks the `.mm` source list in
  `engine/render/rhi/CMakeLists.txt:195-231`. Without it the plain-C++ stub
  (`MetalDevice.cpp`) compiles and `create_metal_device()` returns
  `kBackendInitFailed` at runtime.
- `ENABLE_LANGUAGE(OBJCXX)` is already set in the top-level `CMakeLists.txt`;
  CMake detects `.mm` extensions automatically once the language is active.
- `find_library(Metal/Foundation/QuartzCore REQUIRED)` resolves from the
  active Xcode SDK automatically.

The configure step must **not** produce any `CD_RHI_METAL_ENABLED=ON but the
host platform is not Apple` warning. If it does, the host is not Apple — stop.

---

## §2. Build

```bash
cmake --build build/ninja-metal -- -j$(sysctl -n hw.logicalcpu)
```

Expected: zero errors, zero warnings escalated to errors (`-Werror`).
The `-fobjc-arc` flag is applied to `cd_rhi_metal` automatically
(`CMakeLists.txt:229`); ARC is mandatory for all `.mm` TUs.

**Source list compiled under `CD_RHI_METAL_ENABLED=ON`** (verify with
`ninja -t commands | grep metal`):

| File | Phase | Surface |
| --- | --- | --- |
| `src/metal/MetalDevice.mm` | M1/M2/M4/M6 | device, registry, descriptors, shader |
| `src/metal/MetalCommandBuffer.mm` | M4/M5/M8/M9 | bind, barrier, render-pass, RT |
| `src/metal/MetalPipeline.mm` | M2 | graphics PSO builder |
| `src/metal/MetalSwapchain.mm` | M7 | CAMetalLayer, nextDrawable, present |

`src/metal/MetalShaderToolchain.cpp` is **not** in this list — it builds
unconditionally into `cd::rhi_metal_shader` (host-side, no Metal framework).

---

## §3. Tests — two tiers

### Tier 1: host-side toolchain (runs on Windows too — already green)

```bash
ctest --preset ninja-debug -R cd_test_metal_shader_toolchain --output-on-failure
```

These 11 tests exercise `compose_glsl_to_msl` (GLSL → SPIR-V → MSL text) with
no `MTLDevice`. They pass on Windows (ninja-debug, CD_RHI_METAL_ENABLED=OFF)
and must continue to pass after any Mac-side change.

Run them on macOS with the metal preset too:

```bash
ctest --build-dir build/ninja-metal -R cd_test_metal_shader_toolchain --output-on-failure
```

Expected: **11/11 PASS**.

### Tier 2: Mac GPU tests (require physical Metal device)

```bash
ctest --build-dir build/ninja-metal -R cd_test_metal --output-on-failure
```

These tests instantiate a real `MTLDevice` (Apple GPU). They are
**Mac-only** and do not run in the Windows CI. Categories:

| Test binary | What it verifies |
| --- | --- |
| `cd_test_metal_device` | `create_metal_device()` returns `kOk`; adapter name non-empty |
| `cd_test_metal_buffer` | `create_buffer` → lookup → `map/unmap` round-trip (M1 registry) |
| `cd_test_metal_texture` | `create_texture` → lookup; copy_buffer_to_image (M1 + bytesPerRow) |
| `cd_test_metal_pipeline` | `create_graphics_pipeline` from a real VertexLayout + BlendState (M2) |
| `cd_test_metal_shader` | `create_shader_module(kGlsl)` → GLSL → MSL → `MTLLibrary` (M6) |
| `cd_test_metal_descriptor` | `allocate/update/bind_descriptor_set` argument-buffer round-trip (M4) |
| `cd_test_metal_barrier` | `barrier(BufferBarrier)` does not crash; fence signal+wait (M5) |
| `cd_test_metal_swapchain` | offscreen CAMetalLayer acquire → clear → present (M7) |
| `cd_test_metal_rt` | BLAS + TLAS build; ray-query dispatch in a compute shader (M9) |

---

## §4. Chrome golden (cross-backend parity)

The D3D12 parity test (`tests/cd_test_rhi_parity/D16Test`) renders a reference
frame under D3D12 and compares FLIP/SSIM against a Vulkan baseline. The Metal
equivalent must do the same: render `chrome_sponza_baseline` under Metal and
compare against the stored Vulkan golden.

```bash
# Run hello_engine with Metal backend selected, capture 3 frames:
./build/ninja-metal/bin/Debug/hello_engine \
  --backend metal \
  --golden-fixture 5 \
  --golden-out research/reports/parity_metal/metal_wrap.png \
  --golden-frames 3

# Compare against the Vulkan baseline (stored in research/reports/parity1121/):
# FLIP score < 0.05 and SSIM > 0.97 are the parity thresholds (D16 reference).
```

This golden is the **M9 GPU open item** — it cannot run until all of M1-M8 are
on-device verified (the scene needs real buffers, real PSOs, real descriptors).
Track it as the final sign-off gate.

---

## §5. Known Mac-verify items (from structural reviews, phases 1199-1201)

These items are **host-verified** (MSL text or stub logic confirmed) but require
an on-device Mac run to close fully:

### 5.1 Binding-index map — on-device confirm

The disjoint [[buffer(N)]] map is host-verified at the MSL text level
(toolchain golden tests assert the emitted MSL contains the correct indices):

| Range | Role |
| --- | --- |
| `[0..7]` | descriptor-set argument buffers |
| `[8]` | push_constant block (`kMetalPushConstantBufferIndex`) |
| `[9..15]` | vertex-input (stage_in) |
| `[16..19]` | HEADROOM |
| `[20..30]` | SPIRV-Cross aux (pinned) |

**On-device confirm**: run `cd_test_metal_descriptor` and `cd_test_metal_shader`
with Metal API Validation enabled (`MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1`).
If any index collides, the validation layer reports a `MTLArgumentEncoder` slot
conflict immediately.

### 5.2 Barrier scope

`MetalCommandBuffer.mm` emits `memoryBarrierWithScope:MTLBarrierScopeBuffers|
MTLBarrierScopeTextures` for intra-encoder barriers and `MTLFence` for
cross-encoder. **On-device confirm**: `cd_test_metal_barrier` with
Metal API Validation — verify no "hazardous" resource access warning.

### 5.3 AS useResource residency

`bind_descriptor_set` calls `[encoder useResource:a usage:MTLResourceUsageRead
stages:...]` for every `id<MTLAccelerationStructure>` in the set's
`resident_accels()` list (both render and compute encoder paths,
`MetalCommandBuffer.mm`). **On-device confirm**: `cd_test_metal_rt` — if
residency is missing the GPU faults silently and ray-query returns a miss for
every ray.

### 5.4 Swapchain nextDrawable lifetime

`MetalSwapchain.mm` acquires `nextDrawable` at `acquire_next_image()` and
presents at `present()` via `[commandBuffer presentDrawable:drawable_]`.
The drawable must be retained for the full command-buffer lifetime.
**On-device confirm**: run 100+ frames under `cd_test_metal_swapchain` and
confirm no `MTLCommandBuffer` completion errors or drawable over-release in the
Metal debugger timeline.

### 5.5 copy_buffer_to_image bytesPerRow

`MetalCommandBuffer.mm` `copy_buffer_to_image` computes `bytesPerRow`
from `src_desc.size / desc.extent.height`. This is correct for tightly-packed
2D uploads but breaks for block-compressed formats (BCn) where row stride is
`ceil(width/4) * block_bytes`. **On-device confirm**: upload a BC1/BC7 texture
and verify no Metal validation `bytesPerRow not aligned` error. If broken, fix
to `max(1u, (width+3)/4) * block_bytes_per_4x4`.

### 5.6 M9 RT — AS build + ray-query GPU golden

SPIRV-Cross `CompilerMSL` lowering of `SPV_KHR_ray_query` to
`metal::raytracing::intersection_query<>` is **host-verified** (M3 toolchain
tests confirm the emitted MSL text contains `metal::raytracing` /
`metal_raytracing` includes, `intersection_query<>` + `acceleration_structure<>`
declarations, and the `.next()` walk). The open item is executing this MSL on a
real GPU and getting correct intersection results.

**On-device confirm**: `cd_test_metal_rt` renders a scene with a BLAS sphere
and a TLAS instance, fires a ray-query, and asserts the hit distance is within
tolerance of the analytic sphere intersection. This is the Metal analog of
Vulkan's `cd_test_rhi_rt_rayquery` test.

**FLIP/SSIM cross-backend golden** (§4 above) is the final parity gate.

---

## §6. CI integration (future)

When a macOS runner is available:

```yaml
# .github/workflows/metal-ci.yml sketch
- name: Configure Metal
  run: cmake -S . -B build/metal -G Ninja -DCD_RHI_METAL_ENABLED=ON -DCD_ENABLE_TESTING=ON

- name: Build
  run: cmake --build build/metal

- name: Test (host-side, always)
  run: ctest --build-dir build/metal -R cd_test_metal_shader_toolchain --output-on-failure

- name: Test (GPU, Apple runner only)
  if: runner.os == 'macOS'
  run: |
    MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 \
    ctest --build-dir build/metal -R cd_test_metal --output-on-failure
```

The host-side toolchain tests (`cd_test_metal_shader_toolchain`) already run in
the Windows CI (`ninja-debug` preset) — no new runner needed for Tier 1.
Tier 2 GPU tests require a macOS runner with an Apple Silicon or Intel GPU.
