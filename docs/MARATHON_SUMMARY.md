# Marathon summary — Phase 13 → Phase 27 (v0.30.0 → v0.58.0)

Generated **2026-05-24** at end of the second autonomous-marathon
stretch. Captures everything shipped across the run for a sleeping
user who'll want a single document to scan in the morning.

## Tag chain

15 phases × ~30 minor tags. Vulkan is primary, D3D12 is 2nd
priority, OpenGL was introduced as 3rd.

| Tag    | Phase | Headline |
|--------|-------|----------|
| v0.30.0 | 13.A | Reviewer blockers B1-B5 (LIBRARIES drift, Engine→engine case rename, Vulkan debug-label dangling pointer, CMake SameMinorVersion, catch(...) policy) |
| v0.31.0 | 13.B | Linux lavapipe golden-capture CI infrastructure |
| v0.32.0 | 13.C | D3D12 buffer + texture + swapchain + hello_d3d12_clear |
| v0.33.0 | 13.D | ImGui-driven hello_editor (EditHistory + TransformCommands) |
| v0.34.0 | 14.A | Scene save/load wired into editor |
| v0.35.0 | 14.B | KTX2 encode/write surface |
| v0.35.1 | polish| hello_editor pinned layout |
| v0.36.0 | 14.C | D3D12 PSO + draw + hello_d3d12_triangle |
| v0.36.1 | polish| D3D12 triangle sRGB swapchain |
| v0.37.0 | 14.D | ECS state replication over reliable channel |
| v0.38.0 | 14.E | Editor 3D viewport (tinted cubes + auto-orbit) |
| v0.39.0 | 14.F | DSP graph (Gain + Biquad LP/HP) |
| v0.40.0 | 14.G | RT API shape (AccelStructure + DispatchRays) |
| v0.41.0 | 14.H | Mobile platform scaffold |
| v0.42.0 | 15.A | Editor WASD camera (later refined) |
| v0.46.0 | 15.A-F| Phase 15: D3D12 descriptors, DXC/SM6, HRTF+reverb, Vulkan RT extension enablement, mobile stubs, **WM_CHAR text-input fix**, editor Apply-button reset wiring, ImGui key mapping for A-Z/0-9/F1-F12 |
| v0.47.0 | 16    | DockBuilder + D3D12 UAV/Sampler/CombinedImage + FileWatcher + ProfilerView |
| v0.48.0 | 17    | Vulkan BLAS construction (real impl) |
| v0.49.0 | 18    | OpenGL backend boot + cd::net::PredictionBuffer |
| v0.50.0 | 19    | ParticleSystem + BlendTree2 + LodSelector (**halfway-to-v1.0**) |
| v0.51.0 | 20    | physics::Aabb + concurrency::Stopwatch |
| v0.52.0 | 21    | Sphere + SmallVector + Easing |
| v0.53.0 | 22    | SpatialHash + Axis + Ray + CubicBezier + FixedString |
| v0.54.0 | 23    | Plane + Color helpers + ScopeGuard + AtomicCounter |
| v0.55.0 | 24    | Random PCG32 + SmoothingFilter + hello_particles + hello_bezier |
| v0.56.0 | 25    | FrameAllocator + linear-to-sRGB + Obb |
| v0.57.0 | 26    | RingBuffer + Catmull-Rom Spline + Capsule + Once |
| v0.58.0 | 27    | slerp + Triangle barycentric |

## Major capabilities added across the marathon

**Rendering backends**
- D3D12 went from boot-only to a fully-drawing triangle (Phase 13.C
  → 14.C → 16.B): buffer + texture + swapchain + PSO + draw + DXC
  for Shader Model 6 + descriptor sets covering CBV + Texture2D SRV
  + UAV + CombinedImageSampler.
- Vulkan gained RT extension auto-enable (15.E), bufferDeviceAddress,
  AS feature struct chain, and real BLAS construction via
  vkCreateAccelerationStructureKHR + VMA-backed AS storage (17.A).
- OpenGL 4.6 backend introduction (18.B) with wglCreateContext
  boot path; verified on Intel Iris Xe.

**Engine subsystems**
- Editor: WASD camera, ImGui DockBuilder layout, Scene save/load,
  3D viewport with tinted cubes, full keyboard mapping
  (A-Z/0-9/F1-F12), WM_CHAR routing for text input, Inspector
  with Euler-angle rotation editing.
- Net: PredictionBuffer for client-side prediction roll-back.
- Audio: DspGraph + HRTFNode + FirReverbNode.
- Asset: FileWatcher polling primitive; ProfilerView ImGui widget.

**Foundation primitives (header-only)**
Roughly 30 new headers across phases 19-27, each with unit tests:
SmallVector / FixedString / ScopeGuard / FrameAllocator / RingBuffer
(core); Stopwatch / AtomicCounter / Once (concurrency); Easing /
Random / SmoothingFilter / CubicBezier / Spline / Plane / Color /
QuatSlerp (math); Aabb / Sphere / Ray / Obb / Capsule / Triangle
(physics); ParticleSystem / LodSelector / SpatialHash / BlendTree2
/ Axis (world).

## Outstanding items (Phase 28+ candidates)

- Vulkan TLAS construction (18.A deferred)
- Vulkan RT pipeline + SBT + dispatch_rays (full RT path)
- AMD Mesa/RADV CI workflow
- Editor selection outline + axis-translation gizmo
- Material v2 PBR multi-pass
- OpenGL backend resource paths (buffer/texture/swapchain)
- Real Android/iOS platform-window backends (still no CI hardware)
- hello_random distribution visualization
- CameraController integration in cd::scene
- Frustum::contains_sphere

## Test surface growth

cd_test_* test count up by ~60 across the marathon. Every primitive
landed with at least one test pinning its observable contract.

## Discipline notes

- **One tag per phase close** (user direction adopted Wave 167+).
  Version bumps land at tag time, not per sub-phase.
- **Phase plan ADR + close ADR** per phase. Predecessor / successor
  cross-references intact across all 15 phase ADRs in this run.
- **Deferred work tracked explicitly**: every sub-phase ADR names
  what cycled to the next phase. No silent dropping.
- **v1.0 gate still user-only** per the rollback ADR (wave125).
  Marathon tags v0.x; v1.0 needs the four-axis maturity gate +
  user sign-off.
