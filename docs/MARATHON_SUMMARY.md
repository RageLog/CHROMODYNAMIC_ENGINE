# Marathon summary — Phase 13 → Phase 125 (v0.30.0 → v0.99.52)

Generated **2026-05-24** during the third autonomous-marathon stretch.
**71 minor + 52 patch releases = 123 tags total** shipped from the
marathon's start (v0.30 baseline).
**v0.99.0 reached — v1.0 still user-only per ADR-wave125 rollback.**
After v0.99.0, marathon switched to **patch-level tagging** (v0.99.1
through v0.99.52). v1.0.0 sign-off authority is reserved to the user.

**🎉 100-tag milestone achieved at v0.99.29 (Phase 97).**
**🎯 Phase 100 centennial reached at v0.99.32.**
**🎬 Demo reel published at v0.99.33 (Phase 101) — see [DEMO_REEL.md](DEMO_REEL.md).**
**🎨 PBR + skybox + editor live-drag polish (Phases 102-103) at v0.99.35.**
**📦 Engine-side StandardPbr + AnalyticalSky materials + procedural primitives (Phases 104-106) at v0.99.37.**
**🎮 Editor PropertyDrawer + Frustum sphere cull + SceneCameraController + serializer metadata (Phases 107-110, 121) at v0.99.40.**
**🚀 CommandPalette wired + hello_random + AssetRegistry::evict + AsyncStreamer + DrawBucket (Phases 111-114, 108) at v0.99.45.**
**⚡ TLAS + RT-pipeline interface + outline/gizmo state + parity audit + RADV CI (Phases 117-118, 115-116, 122-123) at v0.99.50.**
**💎 D3D12 real create_texture_view + sync primitives (Phases 124-125) at v0.99.52.**
This doc is the single scan-in-the-morning record.

## Tag chain

35 phases × 50 minor tags total. Vulkan is primary, D3D12 is 2nd
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
| v0.59.0 | 28    | Vec2Ops (length/normalize/lerp/dot) |
| v0.60.0 | 29    | core::BitOps + net::PacketHeader |
| v0.61.0 | 30    | scene::Frustum (AABB cull) + audio::SimpleReverb |
| v0.62.0 | 31    | anim::AdditiveBlend + input::DoubleClick |
| v0.63.0 | 32    | editor::CommandPalette (fuzzy) + ui::Anchor |
| v0.64.0 | 33    | math::Mat3 inverse / normal_matrix + concurrency::Latch |
| v0.65.0 | 34    | rhi::BlendPresets + core::Bitset |
| v0.66.0 | 35    | io::Crc32 + asset::AssetTag (FNV-1a 32) |
| v0.67.0 | 36    | ecs::TagHelpers + platform::StableTime |
| v0.68.0 | 37    | render::SortKey (64-bit packed) + core::ArenaScope |
| v0.69.0 | 38    | camera::CameraPath (Catmull-Rom) + scene::VisibilityMask |
| v0.70.0 | 39    | physics::RaySphere + math::AngleUnits (_deg/_rad UDLs) |
| v0.71.0 | 40    | net::SequenceWindow + concurrency::Backoff |
| v0.72.0 | 41    | audio::Limiter (peak) + ui::Theme palette |
| v0.73.0 | 42    | anim::CurveTrack + core::StringSplit |
| v0.74.0 | 43    | rhi::DepthStencilPresets + framegraph::PassTopology (Kahn topo-sort) |
| v0.75.0 | 44    | material::PbrParams (glTF 2.0) + asset::MemoryCache (LRU) |
| v0.76.0 | 45    | input::KeyChord (Ctrl+Shift+S) + scene::NameRegistry |
| v0.77.0 | 46    | physics::CapsuleSphere + asset::StreamQueue (priority) |
| v0.78.0 | 47    | math::Range + ecs::EntityRange (pagination) |
| v0.79.0 | 48    | scene::EnvironmentLight (IBL) + concurrency::Barrier |
| v0.80.0 | 49    | ui::Tooltip + math::Noise (value noise + fbm) |
| v0.81.0 | 50    | editor::SelectionSet + asset::LoadProfile |
| v0.82.0 | 51    | render::PostProcessChain + ecs::Lifecycle |
| v0.83.0 | 52    | shader::ShaderStageDesc + core::ProfileSpan |
| v0.84.0 | 53    | net::DeltaWriter + camera::Lens (focal-length) |
| v0.85.0 | 54    | physics::SweepResult + concurrency::ParallelFor |
| v0.86.0 | 55    | audio::Mixer + asset::HotReloadQueue |
| v0.87.0 | 56    | ecs::FilterFn + math::Statistics (Welford) |
| v0.88.0 | 57    | ui::ProgressBar + concurrency::Channel (+ Anchor IntRect rename fix) |
| v0.89.0 | 58    | scene::HeightField + math::BarycentricInterp |
| v0.90.0 | 59    | input::GamepadState + concurrency::EventBus |
| v0.91.0 | 60    | anim::EventTrack + scene::Marker |
| v0.92.0 | 61    | rhi::VertexLayoutBuilder + io::PathUtils |
| v0.93.0 | 62    | physics::InertiaTensor + camera::ViewportInfo |
| v0.94.0 | 63    | ui::Spinner + math::Histogram |
| v0.95.0 | 64    | render::Tonemap (Reinhard/ACES/Uncharted2) + concurrency::JobToken |
| v0.96.0 | 65    | net::SnapshotBuffer + ecs::SystemGraph (Kahn topo) |
| v0.97.0 | 66    | audio::LowPass + asset::PathResolver ({VAR} substitution) |
| v0.98.0 | 67    | core::RetryPolicy + editor::Bookmark (camera snapshots) |
| v0.99.0 | 68    | math::QuatLog/Exp/Pow + ui::Toast notifications |
| v0.99.1 | 69    | core::Bytes (format_bytes) + render::MeshStats — *patch-level start* |
| v0.99.2 | 70    | anim::BoneMask + scene::Layer (named layer registry) |
| v0.99.3 | 71    | net::RleCodec + core::PoolAllocator |
| v0.99.4 | 72    | input::ActionBindings + math::Aabb2 |
| v0.99.5 | 73    | rhi::DebugMarkerScope + asset::BundleMeta (32-byte header) |
| v0.99.6 | 74    | physics::SoftBodyParams + io::ByteBuffer |
| v0.99.7 | 75    | net::SequenceId (Fiedler wrap-aware) + ecs::QuerySig |
| v0.99.8 | 76    | audio::Compressor + scene::LightProbe (SH9) |
| v0.99.9 | 77    | render::TextLayoutMetrics + concurrency::Future (Promise pair) |
| v0.99.10 | 78   | editor::HierarchyView + math::Smootherstep + smoothstep_remap |
| v0.99.11 | 79   | scene::Polyline3D + concurrency::TaskGroup (RAII fork-join) |
| v0.99.12 | 80   | input::MouseDragState + math::RangeMap |
| v0.99.13 | 81   | rhi::PipelineCacheKey + asset::DependencyGraph |
| v0.99.14 | 82   | audio::PanLaw + math::Damping (critical spring) |
| v0.99.15 | 83   | editor::MenuBar + core::Ref (non-owning) — *wave 250 milestone* |
| v0.99.16 | 84   | scene::Light + math::QuadraticSolver (Vieta stable form) |
| v0.99.17 | 85   | net::LatencyStats (RFC 6298) + ecs::ComponentMask |
| v0.99.18 | 86   | ui::TabBar + core::EnumFlags (CD_ENUM_FLAGS macro) |
| v0.99.19 | 87   | anim::PoseAlign (root motion) + render::ClearColorPreset |
| v0.99.20 | 88   | net::QoSTier + physics::ContactPoint manifold |
| v0.99.21 | 89   | editor::PreferencesStore (variant) + io::Hex codec |
| v0.99.22 | 90   | scene::Skybox + audio::PitchShift (resample) |
| v0.99.23 | 91   | input::Hold + math::GammaSpace (Vec3 + approx) |
| v0.99.24 | 92   | anim::BoneSocket + render::DrawBatchKey |
| v0.99.25 | 93   | net::Throttle (token bucket) + ecs::Generation helpers |
| v0.99.26 | 94   | physics::SpringJoint + ui::ContextMenu |
| v0.99.27 | 95   | audio::Voice + concurrency::Flag (atomic bool) |
| v0.99.28 | 96   | scene::Trigger + math::EulerAngles |
| **v0.99.29** | **97** | **core::Assert + scene::SceneStats — 100-TAG MILESTONE** |
| v0.99.30 | 98   | rhi::RasterStatePresets + asset::AssetRefCount |
| v0.99.31 | 99   | input::Cursor + core::Singleton (CRTP) |
| **v0.99.32** | **100** | **scene::TagBucket + core::CounterTable — CENTENNIAL** |
| v0.99.33 | 101 | **Demo reel** — hello_audio_chain (DSP graph) + hello_net_sim (replication sim) + hello_command_palette (fuzzy filter) + DEMO_REEL.md walkthrough |
| v0.99.34 | 102 | **Demo quality fixes** — hello_pbr overhaul (3-light + analytical-sky IBL + real skybox pass + ACES tonemap + copper albedo) makes the metallic axis legible; hello_editor Inspector restructured (480 px wide, SeparatorText sections, label/widget split, fixed-width Apply + reset) |
| v0.99.35 | 103 | **Editor live-drag + primitive meshes** — hello_editor now spawns procedural sphere + cone (in addition to cube) and the Inspector edits position/scale/rotation via DragFloat3 sliders that apply live; drag-release pushes a single delta command into EditHistory. docs.yml YAML linter workaround (schema pin + .vscode/settings.json) |
| v0.99.36 | 104+105 | **Engine-side StandardPbr + AnalyticalSky materials** — Cook-Torrance + GGX + Schlick + Smith G + split-sum IBL + ACES (StandardPbrMaterial.hpp) and 3-band procedural atmosphere (AnalyticalSkyMaterial.hpp) lifted out of hello_pbr into cd::material headers; sample shrinks by ~280 GLSL lines |
| v0.99.37 | 106 | **cd::asset::Primitives** — header-only builders for cube/sphere/cone/cylinder/plane/torus/capsule with a canonical 44 B pos+normal+uv+color vertex layout |
| v0.99.38 | 107 | **cd::editor::PropertyDrawer** — DragSession helper (pre-drag snapshot + release flag) for the live-drag-then-push-command pattern; imgui-agnostic (function-pointer callbacks) |
| v0.99.39 | 109+110 | **Frustum::contains_sphere + SceneCameraController** — sphere cull (inclusive + strict variants, +5 unit tests) and a scene-aware orbit-camera facade that follows a target entity's world transform |
| v0.99.40 | 121 | **Serializer metadata round-trip** — serialize_scene_with / deserialize_scene_with template overloads accept a per-node write/read callback; hello_editor save/load preserves entity name + tint + mesh kind across reloads |
| v0.99.41 | 111 | **CommandPalette wired into hello_editor** — Ctrl+Shift+P opens a VS-Code-style fuzzy palette with 8 commands (Undo/Redo/Select Cube/Sphere/Cone/Reset/Help); Enter fast-path on first match |
| v0.99.42 | 112 | **hello_random distribution viz** — ASCII histograms for PCG32 uniform + Box-Muller normal + 2-stream Pearson correlation + determinism check |
| v0.99.43 | 113 | **AssetRegistry::evict** — LRU eviction wired through MemoryCache; drops victim assets from both the registry cache and the memory accountant; 2 new gtests; MemoryCache.hpp `<algorithm>` include fix |
| v0.99.44 | 114 | **AsyncStreamer** — header-only worker thread wrapping StreamQueue; cv-driven sleep, load_fn outside lock, idempotent start/stop; 3 new gtests |
| v0.99.45 | 108 | **DrawBucket** — SortKey-driven draw-call bucket with deferred emit callbacks; stable_sort by 64-bit key; 3 new gtests |
| v0.99.46 | 117 | **TLAS interface contract** — `cd::rhi::AccelInstance` POD (3x4 transform + blas handle + instance_id/mask/hit_offset/flags) + `AccelStructureDesc::instances` span; Vulkan backend implementation deferred to Phase 118 |
| v0.99.47 | 118 | **RT pipeline + SBT interface** — `RtShaderStage` enum, `RtShaderEntry`, `RtPipelineDesc` (recursion / payload / attribute budgets), `SbtRegion` (strided buffer region), `DispatchRaysDesc` extended with raygen/miss/hit/callable SBT regions |
| v0.99.48 | 115+116 | **SelectionOutline + AxisGizmo state** — header-only editor primitives: selection set + outline style/color/thickness/opacity, and a translation-gizmo state machine (hover/drag axis + origin snapshot + total-delta on end_drag) |
| v0.99.49 | 122 | **D3D12 parity audit** — docs/D3D12_PARITY_AUDIT.md per-method matrix across Vulkan/D3D12/OpenGL/Metal; outstanding-gap list with source line refs; recommended next-phase priorities |
| v0.99.50 | 123 | **AMD RADV CI lane** — dormant self-hosted workflow targeting `[self-hosted, amd-gpu, linux]`; preflight probes (`lspci` + `vulkaninfo`); RADV-pinned VK_ICD_FILENAMES; golden-compare against `tests/golden/radv/`; CI_SELF_HOSTED.md runbook |
| v0.99.51 | 124 | **D3D12 create_texture_view** — real implementation w/ device-level RTV (256) + DSV (256) bump-alloc pools, shared SRV/UAV via existing CBV_SRV_UAV cpu_heap (4096); inferred view kind from parent texture usage flags; 2D only (cube/3D in follow-up) |
| v0.99.52 | 125 | **D3D12 sync primitives** — ID3D12Fence-backed Semaphore + Fence + TimelineSemaphore wrappers; SetEventOnCompletion + WaitForSingleObject host wait w/ ns→ms timeout; D3D12 NotImpl count 8 → 4 |
| v0.99.53 | 126 | **D3D12 cube-map view** — `TextureType::kCube` now handled by create_texture_view (TEXTURECUBE SRV + TEXTURE2DARRAY UAV with ArraySize=6); TextureViewRecord gains `is_cube`; update_descriptor_set's SRV branches pick TEXTURECUBE dimension; clears Phase 122 audit "cube SRV follow-up" remark |
| v0.99.54 | 127 | **Vulkan TLAS create** — mirrors Phase 17.A BLAS path: INSTANCES geometry + placeholder device-address for size query + TOP_LEVEL AS type; VMA-backed storage buffer; build still deferred (cmd-buffer path no-op) |
| v0.99.55 | 128 | **hello_rt_check extended** — probes BLAS + TLAS create, static_asserts `AccelInstance==64B`, prints `RtShaderEntry/RtPipelineDesc/SbtRegion` sizes; validates Phase 117/118/127 layout contract |
| v0.99.56 | 129 | **D3D12 input-attachment** — `kInputAttachment` branch in update_descriptor_set wired to the Texture2D SRV path (D3D12 binds the same resource as both RTV + SRV — no subpass distinction); D3D12 NotImpl count 4 → 3 |
| v0.99.57 | 130 | **Vulkan AccelRecord build-info** — deep-copies BLAS triangles + TLAS instances into the record at create time so a future cmd-buffer build path can rebuild the VkAccelerationStructureGeometryKHR list without the caller re-passing them |
| v0.99.58 | 131 | **cd::rhi::DeferredDestroy** — header-only frame-fence-keyed destroy queue for transient scratch / staging buffers; enqueue(frame, action) + flush_completed(frame_id); unblocks Phase 132 Vulkan AS build cmd path |
| v0.99.59 | 132+133 | **Vulkan AS build cmd path** — `VulkanCommandBuffer::build_acceleration_structure` wired for BLAS + TLAS; AccelRecord caches Vk-format geometries + per-AS scratch + TLAS instance buffer + AS device addresses; static lookup callback through ResourceTables |
| v0.99.60 | 134 | **RT pipeline handle + IDevice virtuals** — `RtPipelineHandle` + `create_rt_pipeline` + `destroy_rt_pipeline` + SBT-authoring helpers (handle size / alignment / base alignment / `get_rt_shader_group_handles`); defaults kNotImplemented so existing backends compile unchanged |
| v0.99.61 | 135 | **Vulkan RT pipeline + dispatch_rays impl** — `vkCreateRayTracingPipelinesKHR` (groups derived from `RtShaderEntry::group` index, hit/general type chosen automatically), SBT handle bytes via `vkGetRayTracingShaderGroupHandlesKHR`, `bind_rt_pipeline` + `vkCmdTraceRaysKHR` overrides; `RtShaderEntry` ABI now takes pre-compiled `ShaderModuleHandle` |

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
Roughly 70 new headers across phases 19-47, each with unit tests.
A non-exhaustive index:

- **core**: SmallVector, FixedString, ScopeGuard, FrameAllocator
  (+rewind), ArenaScope, RingBuffer, BitOps, Bitset, StringSplit
- **concurrency**: Stopwatch, AtomicCounter, Once, Latch, Backoff
- **math**: Easing, Random (PCG32), SmoothingFilter, CubicBezier,
  Spline, Plane, Color, QuatSlerp, Vec2Ops, Mat3Inverse, AngleUnits,
  Range
- **physics**: Aabb, Sphere, Ray, Obb, Capsule, Triangle, RaySphere,
  CapsuleSphere
- **world primitives**: ParticleSystem, LodSelector, SpatialHash,
  BlendTree2, Axis, AdditiveBlend, CurveTrack, DoubleClick, KeyChord
- **render / ui**: CameraPath, SortKey, BlendPresets,
  DepthStencilPresets, Frustum, VisibilityMask, NameRegistry, Theme,
  Anchor, CommandPalette
- **net / io / asset / platform**: PacketHeader, SequenceWindow,
  Crc32, AssetTag, MemoryCache, StreamQueue, StableTime, PbrParams
- **framegraph**: PassTopology (Kahn topo-sort)
- **ecs**: TagHelpers, EntityRange (pagination)

## Outstanding items (Phase 48+ candidates)

- Vulkan TLAS construction (18.A deferred)
- Vulkan RT pipeline + SBT + dispatch_rays (full RT path)
- AMD Mesa/RADV CI workflow
- Editor selection outline + axis-translation gizmo
- Material v2 PBR multi-pass (PbrParams struct landed; integrate
  binding in Material/Renderer)
- OpenGL backend resource paths (buffer/texture/swapchain)
- Real Android/iOS platform-window backends (still no CI hardware)
- hello_random distribution visualization
- CameraController integration in cd::scene
- Frustum::contains_sphere
- Wire CommandPalette / KeyChord into editor frontend
- AssetRegistry::evict() consuming MemoryCache
- Real async streamer consuming StreamQueue
- Renderer::submit_draws using SortKey for ordering

## Test surface growth

cd_test_* test count up by ~160 across the marathon (phases 13-47).
Every primitive landed with at least one test pinning its observable
contract. 58/58 ctest binaries green at every minor tag.

## Lessons captured

- **Anonymous-namespace include trap (phases 34/41/44/45)**:
  `#include <some_header>` inside a TU's `namespace { ... }` block
  re-opens its inner namespaces *under the anonymous one* —
  `namespace cd::X` becomes `::{anon}::cd::X` and detaches from the
  real `::cd::X` symbols. Fix: hoist new header includes to file top.
  This is now a marathon convention.

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
