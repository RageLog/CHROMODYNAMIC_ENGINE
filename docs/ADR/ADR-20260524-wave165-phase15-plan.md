# ADR — Wave 165 — Phase 15 plan

- Date: 2026-05-24
- Status: Accepted
- Wave: 165
- Predecessor: ADR-20260523-wave156 (Phase 14 master) — Phase 14
  closed at v0.41.0 / Wave 164.

## Context

Phase 14 closed every sub-phase in the wave156 plan. The closing
ADR (wave164, v0.41.0) listed six Phase-15 candidates — the
items where Phase 14 deliberately shipped *API shape* or
*scaffold* and the runtime implementation needed deferring
(RT backend, mobile devices, D3D12 descriptors, DXC, audio
HRTF, editor camera).

User's marathon directive (memory:
feedback_long_autonomous_marathon) reapplies: implement all
six, ordered easy → hard, AI-actionable first, and plan
Phase 16 at the close.

## Decision

Phase 15 opens with the six sub-phases below, sequenced by the
marathon's ease/hardware-dependence axis.

### 15.A — Editor WASD camera + DockSpace preset → v0.42.0

**Why first:** pure ImGui + math, no new dependency, marathon-
shippable without hardware testing.

- Replace hello_editor's auto-orbit camera with a free-fly
  camera driven by ImGui::IsKeyDown (W/A/S/D for translation,
  mouse drag for yaw/pitch).
- Add an ImGui::DockSpace setup that opens with the four
  panels (Toolbar / Scene / Inspector / History) pre-docked
  around the central viewport on first launch.
- Camera state survives editor reload (saved in the same
  `.cdscene.json` Scene saves use, with a top-level `camera`
  key).

### 15.B — D3D12 descriptor sets (CBV / SRV / UAV) → v0.43.0

**Why second:** unblocks downstream renderer work on D3D12 and
matches the Vulkan-side descriptor surface that's been
shipped since Phase 3.

- `create_descriptor_set_layout` real impl on D3D12 (root
  signature parameter records).
- `allocate_descriptor_set` builds a CPU-visible descriptor
  heap slot range; `update_descriptor_set` writes
  CBV/SRV/UAV/Sampler descriptors via D3D12 device
  CreateXxxView calls.
- `bind_descriptor_set` on the command buffer →
  CopyDescriptorsSimple to a per-frame GPU-visible heap +
  SetGraphicsRootDescriptorTable.
- A `hello_d3d12_cube` sample (constant buffer with MVP +
  draw a 3D cube via D3D12) closes the loop.

### 15.C — DXC / Shader Model 6 path → v0.44.0

**Why third:** modern D3D12 shaders want SM6; D3DCompile only
emits up to SM5_1. Closes a known v0.36.0 deferred item.

- Vendor `dxcompiler.dll` + `dxil.dll` via FetchContent
  (Microsoft's DirectXShaderCompiler release).
- Extend `cd::rhi_d3d12::compile_hlsl` to accept a
  `kShaderModel { kSM5_1, kSM6_0, kSM6_5 }` enum; route
  SM5_1 through D3DCompile, SM6_x through DXC's
  `IDxcCompiler3::Compile`.
- Fallback to D3DCompile when DXC isn't available at run
  time (graceful degradation for older Windows SDK installs).

### 15.D — Audio HRTF + simple reverb → v0.45.0

**Why fourth:** structural pass; the math is testable, the
*perceptual* tuning still needs human ears and is deferred to
a Phase-16 calibration wave.

- Wrap the existing `cd::audio::AnalyticalHRTF` (Wave 79)
  inside an `HRTFNode : IDspNode` that takes a per-call
  listener pose + source pose, applies the HRTF impulse
  response per-sample.
- Add a minimal FIR-based reverb (`FirReverbNode`) with a
  small built-in impulse-response that approximates a
  small-room early-reflections pattern.
- 4 new unit tests pinning the math (impulse response,
  energy preservation, channel separation).

### 15.E — Real Vulkan RT pipeline + raygen sample → v0.46.0

**Why fifth:** the largest engineering chunk; needs Vulkan-side
VK_KHR_acceleration_structure + VK_KHR_ray_tracing_pipeline
+ SBT design + a raygen shader sample. Single-vendor (NVIDIA
RTX 3080) validation acceptable.

- Detect + enable the extensions in `cd::rhi_vulkan` (already
  detected via Wave 150 features; this wave wires them into
  the actual device-creation extension list).
- Real `create_acceleration_structure` (BLAS) + a separate
  `build_tlas` path with instance descriptors.
- `dispatch_rays` consumes a `DispatchRaysDesc` extended
  with a strided-buffer SBT range (raygen / miss / hit
  group counts and offsets).
- `hello_rt_triangle` — single triangle BLAS, one TLAS
  instance, raygen shader that fires per-pixel, miss returns
  background gradient, closest-hit returns red. Image
  rendered to a swapchain image via vkCmdTraceRaysKHR.
- Driver-validation table updated for NVIDIA RTX 3080 (the
  marathon machine's only RT-capable GPU).

### 15.F — Mobile platform window stubs → v0.47.0

**Why last:** still no device hardware on the marathon machine;
this wave lands the *compile-time* shape so the Android NDK and
iOS Xcode builds produce a binary instead of a link error. The
runtime smoke-test path still waits on a CI runner with mobile
hardware.

- `AndroidWindow.cpp` (gated `#if defined(__ANDROID__)`) wrapping
  `ANativeWindow*` and pumping through Looper. Compiles in the
  Android NDK toolchain; returns the existing kNotImplemented
  on desktop.
- `IosWindow.mm` (gated `#if defined(__APPLE__) &&
  TARGET_OS_IPHONE`) wrapping `UIView*` + display link. Compiles
  in the Xcode iOS toolchain.
- `cd::platform::create_window` factory routes to the right
  backend at compile time; desktop builds skip the new TUs
  entirely.
- `mobile/README.md` updated with the build verification step
  (`cmake --build` against the NDK / Xcode toolchain produces
  a static-lib binary).
- Runtime hello_triangle_android / hello_triangle_ios remain
  deferred until a CI runner with mobile hardware is wired —
  same honesty bar Phase 14.H upheld.

## Rejected alternatives

- *Run all six in parallel.* Rejected: same as Phase 14 — the
  marathon-quality bar is sequential.
- *Skip Phase 15.E (real RT).* Rejected: the v0.40.0 API
  shape is unverified without a working backend. Landing it
  closes the API-shape promise.
- *Land mobile runtime implementations without device CI.*
  Rejected: the v1.0 rollback honesty principle (ADR-wave125)
  still binds. Compile-time stubs ship; runtime validation
  needs a real device.

## Consequences

- Phase 15 runs autonomously per the marathon directive. Stop
  condition per sub-phase: minor tag cut + ADR.
- Phase 16 will be planned at the close.
- v1.0 path remains user-only.

## References

- ADR-20260523-wave164-v0.41.0-phase14h-mobile-scaffold.md —
  Phase 15 candidate list
- ADR-20260523-wave156-phase14-plan.md — predecessor master
- ADR-20260523-wave125-v1.0-rollback.md — honesty principle
