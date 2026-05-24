# Marathon Run 3 — Summary (v0.99.33 → v0.99.56)

Generated **2026-05-24**. 24 tags shipped across this stretch
(v0.99.33 demo-reel kickoff through v0.99.56 D3D12 input-attachment).
**v1.0.0 still user-only** per the ADR-wave125 rollback rule.

## Tag chain

| Tag       | Phase    | One-line                                                                                  |
|-----------|----------|-------------------------------------------------------------------------------------------|
| v0.99.33  | 101      | Demo reel: hello_audio_chain + hello_net_sim + hello_command_palette + DEMO_REEL.md       |
| v0.99.34  | 102      | PBR overhaul + editor inspector UX                                                        |
| v0.99.35  | 103      | Editor live-drag sliders + procedural sphere/cone meshes + docs.yml lint                  |
| v0.99.36  | 104+105  | StandardPbrMaterial + AnalyticalSkyMaterial engine-side headers                           |
| v0.99.37  | 106      | cd::asset::Primitives (cube/sphere/cone/cylinder/plane/torus/capsule)                     |
| v0.99.38  | 107      | cd::editor::PropertyDrawer DragSession                                                    |
| v0.99.39  | 109+110  | Frustum::contains_sphere + SceneCameraController                                          |
| v0.99.40  | 121      | Serializer metadata round-trip (entity name + tint + mesh)                                |
| v0.99.41  | 111      | CommandPalette wired into hello_editor (Ctrl+Shift+P)                                     |
| v0.99.42  | 112      | hello_random distribution viz (PCG32 + Box-Muller)                                        |
| v0.99.43  | 113      | AssetRegistry::evict consuming MemoryCache                                                |
| v0.99.44  | 114      | AsyncStreamer worker thread                                                               |
| v0.99.45  | 108      | DrawBucket SortKey-driven draw bucket                                                     |
| v0.99.46  | 117      | TLAS interface contract (AccelInstance + instances span)                                  |
| v0.99.47  | 118      | RT pipeline + SBT interface (RtShaderEntry/RtPipelineDesc/SbtRegion + DispatchRaysDesc)   |
| v0.99.48  | 115+116  | SelectionOutline + AxisGizmo state primitives                                             |
| v0.99.49  | 122      | D3D12_PARITY_AUDIT.md per-method matrix                                                   |
| v0.99.50  | 123      | AMD RADV CI lane (dormant; self-hosted)                                                   |
| v0.99.51  | 124      | D3D12 create_texture_view real impl (RTV/DSV/SRV/UAV pools)                               |
| v0.99.52  | 125      | D3D12 sync primitives (ID3D12Fence + Win32 event)                                         |
| v0.99.53  | 126      | D3D12 cube-map view + descriptor-set update path                                          |
| v0.99.54  | 127      | Vulkan TLAS create path                                                                   |
| v0.99.55  | 128      | hello_rt_check extended (BLAS+TLAS probes + size table)                                   |
| v0.99.56  | 129      | D3D12 input-attachment branch (last update_descriptor_set stub cleared)                   |

## Themes

1. **Editor UX polish (101-103, 111).** Demo reel + live-drag
   inspector + primitive meshes + fuzzy command palette wired.
2. **Engine-side reusable primitives (104-110, 117-118).** Lifting
   shaders + primitive builders + drag-session helper + scene-aware
   camera + frustum-sphere cull + RT descriptor types out of samples
   into headers.
3. **Asset pipeline plumbing (113-114, 121).** Registry eviction,
   async streamer worker, serializer metadata round-trip.
4. **D3D12 parity push (122-126, 129).** Audit doc + 6 real
   implementations brought the D3D12 backend's `kNotImplemented`
   count from 9 → 3.
5. **Vulkan RT progress (117, 127).** Interface descriptor + TLAS
   create wired; cmd-buffer build path + RT pipeline are the
   remaining gap.
6. **CI / docs infrastructure (122-123).** D3D12_PARITY_AUDIT.md
   + dormant RADV self-hosted lane.

## Backend `kNotImplemented` status

| Backend  | Before run | After run |
|----------|------------|-----------|
| Vulkan   | 3          | 3 (unchanged; TLAS create not a numbered stub) |
| D3D12    | 9          | 3                                              |
| OpenGL   | ~30        | ~30 (deferred; needs WGL/GLX loader)           |
| Metal    | ~30        | ~30 (deferred)                                 |

## Outstanding (deferred until a fresh marathon stretch)

| Item                                                          | Estimated effort |
|---------------------------------------------------------------|------------------|
| Vulkan AS-build cmd-buffer path (BLAS + TLAS)                 | 4-6 hours        |
| Vulkan RT pipeline create + SBT alloc + dispatch_rays         | 6-8 hours        |
| hello_rt full sample (real ray dispatch)                      | 2-3 hours after above |
| OpenGL real resource paths (WGL/GLX loader + GL function table) | 1-2 days        |
| D3D12 acceleration structures (DXR Tier 1.1 mirror)           | 4-6 hours        |
| Mobile platform (Android / iOS) — user-deprioritised lowest   | multi-day        |
| Phase FINAL — ULTIMATE hello-app (DtRuntime-style architecture) | user-triggered  |

## User-side discipline still in force

- v1.0.0 sign-off authority reserved to the user. Marathon stays
  at patch-level v0.99.x.
- Ultimate hello-app deferred until user explicitly asks.
- Auto-mode marathon scope: header-only primitives + bounded engine
  features + sample/CI/doc improvements + interface contracts.
  Avoids open-ended multi-hour single-phase work.

## Repo state at v0.99.56

- 56 patch releases on v0.99 line.
- Engine + 33 samples + tools all build clean across Vulkan-enabled
  presets (ninja-base, llvm-win-base, clangcl-win-base, ...).
- 33 ADRs added this run (waves 268 → 291).
- Sample count: 50 (`hello_audio_chain`, `hello_net_sim`,
  `hello_command_palette`, `hello_random` newly shipped; the rest
  unchanged).
