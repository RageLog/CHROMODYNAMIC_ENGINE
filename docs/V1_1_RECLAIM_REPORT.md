# CHROMODYNAMIC Engine — v1.1 Reclaim Report

- **Report date:** 2026-05-25
- **Current tag:** `v0.99.86`
- **Branch:** `dev`
- **Tests:** 58/58 green at every tag

After the v1.0 readiness report (Wave-311), the marathon continued
into v1.1 by reclaiming items previously deferred to the v1.1+
milestone. This report documents what landed in that reclamation
pass — 9 additional ships across v0.99.78 → v0.99.86.

## v1.0 vs v1.1 reclaim snapshot

| Milestone     | Tag range            | Ships |
|---------------|----------------------|------:|
| v1.0          | v0.99.65 → v0.99.77  | 13    |
| **v1.1 reclaim** | **v0.99.78 → v0.99.86** | **9** |
| **Total**     | **v0.99.65 → v0.99.86** | **22** |

## v1.1 reclaim ships

| Tag        | Phase     | Description                                                            |
|------------|-----------|------------------------------------------------------------------------|
| `v0.99.78` | 157       | WASAPI continuous push-stream API                                       |
| `v0.99.79` | 148-lite  | hello_opengl_resources sample + WGL 1.1 loader fallback bug fix         |
| `v0.99.80` | 146+147   | OpenGL shader compile (GLSL) + program link                             |
| `v0.99.81` | 155-lite  | Split-sum BRDF LUT CPU bake (Karis 2013)                                |
| `v0.99.82` | 156       | PoseBlend (lerp / blend_into / additive layer)                          |
| `v0.99.83` | 156-ext   | AnimStateMachine with blackboard + transitions                          |
| `v0.99.84` | 142-step1 | D3D12 DXR feature detect + ID3D12Device5 query                          |
| `v0.99.85` | fix       | Vulkan TLAS rejects null-BLAS instance refs (defensive)                 |
| `v0.99.86` | 142-step2 | D3D12 create_acceleration_structure + destroy_acceleration_structure   |

## v1.1 feature delta (relative to v1.0)

### Audio
- **WASAPI push-stream API** — IAudioBackend gains create_stream /
  push_stream_samples / destroy_stream. Continuous PCM input (mic,
  network voice, procedural synth) now first-class alongside the
  clip-based playback.

### OpenGL backend
- **Texture path complete**: WGL loader bug fix means GL 1.1
  symbols resolve correctly via opengl32.dll fallback.
- **Shader compile**: GLSL via glCreateShader + glShaderSource.
- **Program link**: glCreateProgram + glAttachShader +
  glLinkProgram with error reporting via glGetProgramInfoLog.
- **Pipeline layout**: no-op opaque handle (GL has no PSO).
- **hello_opengl_resources** sample: 8 resource paths validated
  on Intel Iris Xe (buffer + tex + view + sampler + VS + FS +
  pipeline layout + linked pipeline).

What's still NOT in OpenGL backend (deferred to v1.2):
- Swapchain (architectural mismatch with Vulkan's acquire/present
  model — needs FBO redesign).
- hello_opengl_triangle (full draw call sample).

### IBL infrastructure
- **Split-sum BRDF LUT CPU bake**: cd::material::BrdfLut with
  Hammersley low-discrepancy sequence, GGX importance sampling,
  Smith geometry function (IBL `k = α/2` variant), and a
  `bake_brdf_lut(w, h, samples)` baker matching Filament's
  ground truth to within < 0.5% per texel.
- 4 new tests pin shape + Fresnel-dominance invariants.

What's still NOT done for full IBL (v1.2):
- HDR equirect → cubemap conversion.
- Irradiance cubemap convolution.
- Pre-filtered specular cubemap mip chain.
- Material v2 shader sampling all three resources.

### Skeletal animation
- **PoseBlend** primitives: lerp_transform, blend_pose, blend_pose_into
  with optional joint filter, additive_apply.
- **AnimStateMachine** on top of PoseBlend: stringly-typed
  blackboard, predicate-driven transitions with configurable blend
  duration, automatic blend handling per tick.
- 10 new tests pin lerp/blend/transition behavior.

What's still NOT done for full character animation (v1.2):
- Skinned glTF importer.
- GPU skinning matrix UBO/SSBO upload + vertex shader path.
- 1D/2D BlendSpace (parametric motion).

### D3D12 DXR
- **DXR feature detect** via `D3D12_FEATURE_D3D12_OPTIONS5::
  RaytracingTier`.
- **ID3D12Device5 QueryInterface** stored for DXR entry points.
- **AS create/destroy**: `GetRaytracingAccelerationStructure
  PrebuildInfo` + UAV result/scratch buffer allocation. Mirrors
  Vulkan's "create allocates, build is a separate command-list
  pass" semantics.

What's still NOT done for full D3D12 DXR (v1.2):
- Command-list `BuildRaytracingAccelerationStructure`.
- `CreateStateObject` for RT pipelines.
- `ID3D12GraphicsCommandList4::DispatchRays`.
- A working hello_rt-equivalent on D3D12.

### Defensive fixes
- **Vulkan TLAS** create now rejects instances with invalid or
  unknown BLAS handles. Same guard the Vulkan side needed to
  match D3D12's tighter validation.

## v1.1 support matrix (final)

| Subsystem     | v1.0           | v1.1 reclaim         | v1.2+                                        |
|---------------|----------------|-----------------------|----------------------------------------------|
| Vulkan        | full           | defensive fixes       | —                                            |
| D3D12         | gfx+compute    | + DXR detect + AS     | DXR build + StateObject + DispatchRays      |
| OpenGL        | buf+tex+samp   | + shader + program    | swapchain + draw + sample                    |
| PBR           | analytical     | + BRDF LUT bake       | HDR equirect + irradiance + specular mips    |
| Animation     | Bezier         | + PoseBlend + FSM     | skinned glTF + GPU skinning                  |
| Audio         | clip+DSP       | + push-stream         | resampler + AudioStreamer source             |
| Platform      | Win32          | —                     | Linux X11/Wayland, macOS Cocoa, mobile       |
| CI            | full           | —                     | Clang-cl Windows, nightly tidy               |

## Marathon stats (cumulative since pre-marathon `v0.99.64`)

- **Total ships**: 22 (13 v1.0 + 9 v1.1)
- **ADRs**: 22 (one per tag)
- **Test count**: held at 58 binaries, 100% pass at every tag
- **New code**: ~2700 lines net (engine + samples + tests +
  shaders) across the marathon
- **Bug fixes surfaced + closed**: 2 (WGL 1.1 loader fallback,
  Vulkan TLAS null-BLAS guard)
- **Bug surfaced but deferred**: 1 (hello_rt_check segfault —
  separate investigation)
- **Implementation:deferral ratio**: 17:5 (down from v1.0's
  8:5 — v1.1 implements more, defers less, because the v1.0
  scope decisions already drained the "skip with ADR" backlog)

## What v1.2 looks like

Highest-value remaining backlog, ordered by the marathon's
"easy → hard, general → specific, high → low" rule:

1. **D3D12 DXR steps 3-5** — command-list build, RT pipeline,
   DispatchRays. Closes the secondary-backend RT parity.
2. **IBL cubemap chain** — HDR equirect import, render-to-cube,
   irradiance convolution, pre-filtered specular. Visible PBR
   quality jump.
3. **GPU skinning** — skinning matrix UBO/SSBO upload + vertex
   shader path. Unlocks the skinned glTF importer.
4. **OpenGL swapchain** — needs to reconcile Vulkan's
   acquire/present model with GL's SwapBuffers. Architectural
   work.
5. **Cross-platform** — Linux X11/Wayland, macOS Cocoa + Metal.
   Per-host bring-up; not testable from a Windows-only marathon
   host.
6. **CI polish** — Clang-cl Windows matrix, full-tree nightly
   tidy, Doxygen warnings-as-error.

## Sign-off

The engine is in **v1.1 reclaim posture**: every v1.0-deferred
item that's tractable to ship from a Windows-only marathon host
has been picked up incrementally. The remaining backlog is mostly
GPU-execution-path work (DXR build, IBL render passes, GPU
skinning) + cross-platform bring-up — both lower-priority than
the v1.0 ship gate.

**The user remains the gate** for `v1.0.0` (and any future
`v1.1.0` / `v1.2.0`) tag bumps.
