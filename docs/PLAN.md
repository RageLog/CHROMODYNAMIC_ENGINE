# CHROMODYNAMIC Engine — Implementation Plan

- **Version**: 0.1 (Phase 1 Plan Draft)
- **Date**: 2026-05-17
- **Status**: Phase 1 design finished → Phase 2 implementation roadmap

Bu doküman 17 ADR'a dayanan adım-adım implementation WBS'sini sunar. Sprint = 2 hafta (kabul); takvim "kalite önce" disiplinli (E7=a), realistic estimate.

---

## 0. Phase Genel Bakış

| Phase | Süre (estimate) | Çıktı |
|---|---|---|
| **Phase 1 — Design** (mevcut) | ~6 ay tasarım | 17 ADR + DESIGN.md + PLAN.md + research/papers + 33 subagent |
| **Phase 2 — Foundation** | ~6-9 ay | DtForHil salvage + RHI Vulkan MVP + ECS + Shader + Asset MVP |
| **Phase 3 — Low-Level Replace + 3-API** | ~6-12 ay | Custom allocator/audio + D3D12 + OpenGL + Animation + Physics + Audio MVP |
| **Phase 4 — UI + Editor + Net + Ops** | ~12-18 ay | UI dual system + Editor mechanics + Networking + i18n/Tele/Crash + CI/CD |
| **Phase 5+ — Advanced** | uzun vadeli | VR/XR, AI/ML, Scripting, Visual Scripting, Modding |

**Toplam Phase 2-4** (engine v1.0-ready): ~24-39 ay = 2-3.5 yıl. "Kalite önce", realistic.

---

## 1. Phase 2 — Foundation Implementation

### Sprint S2.0 — Build System Bootstrap (2 hafta)

**Hedef**: CMake + Ninja + 4-compiler matrix + sccache + R2 + GHA CI yeşil.

- `CMakeLists.txt` top-level: C++23, sanitizers, clang-tidy, clang-format zorunlu
- `CMakePresets.json`: 8 preset (debug-msvc, debug-clang, release-msvc, vb.)
- vcpkg manifest mode: minimal deps only (gtest, fmt)
- GitHub Actions workflow: 10-job matrix (Win MSVC/clang-cl/clang/gcc-mingw + Linux clang/gcc) × {Debug, RelWithDebInfo}
- sccache + Cloudflare R2 binary cache setup
- `cd_*` CMake helper macros (`cd_add_library`, `cd_add_plugin`, `cd_add_test`)
- ADR-014 implement scope

**Done criteria**: empty `cd::core` library + hello-world test, CI matrix all green, sccache hit rate > 50% on warm build.

### Sprint S2.1 — Salvage P0 (Foundation) — 2 hafta

**Hedef**: DtForHil P0 salvage derlenebilir (ADR-017 P0).

**Deliverables**:
- `cd::core::Result<T>` (`std::expected` polyfill check, libstdc++14+/libc++17+ standart)
- `cd::core::Handle` 32-bit packed `{type, generation, index}`
- `cd::core::HandleStore` (DtForHil store refactor — RHI/ECS uyarla)
- `cd::core::defines.hpp` (genişletilmiş X_DEFINATION.h: platform/compiler/arch/C++26-opt-in)
- `cd::mem::IAllocator` + `LinearAllocator`, `PoolAllocator<T>`, `PageAllocator`
- `cd::mem::TrackingAllocator<Inner>` decorator
- `cd::concurrency::RingBuffer<T,N>` SPSC (DtForHil port)
- `cd::time::IClock` + `SteadyClock`
- `cd::platform::SignalManager` (POSIX `<csignal>`)
- `cd::diag::CrashReporter` (signal handler install)

**Test**: her component için gtest binary; 100% line coverage hot path.

### Sprint S2.2 — Salvage P1 Part A (Core Services) — 2 hafta

**Hedef**: Cross-system messaging foundation.

**Deliverables**:
- `cd::events::EventBus` (DtForHil `AsyncEventBus` 1:1 port + Singleton sök + typed-only)
- `cd::events::ScopedConnection` RAII
- `cd::events::EventRecorder` JSONL + rotation
- `cd::time::TimerQueue` priority-queue (DtForHil port)
- `cd::log::ILogger` + `JsonlBackend`
- `cd::concurrency::DataChannel<T>` named SPSC

**Test**: EventBus stress test (10k subscribers, 100k events/sec); TimerQueue determinism test.

### Sprint S2.3 — Salvage P1 Part B (Coroutine + Concurrency) — 2 hafta

**Hedef**: Job system foundation + coroutine task.

**Deliverables**:
- `cd::concurrency::task<T>` C++20 coroutine (DtForHil pattern, stop_token propagation)
- `cd::concurrency::ThreadPool` (interim, Chase-Lev upgrade S2.5)
- `cd::concurrency::JobGraph` DAG primitive
- `cd::time::FramePacer` multi-tier accumulator
- `cd::time::SimClock` (DtForHil port — pause/step/speed-multiplier 0.1×-10×)

**Test**: Coroutine determinism; SimClock fixed-timestep stability test.

### Sprint S2.4 — Salvage P2 (Engine Patterns) — 2 hafta

**Hedef**: CVar, watchdog, plugin loader.

**Deliverables**:
- `cd::core::CVarRegistry` (DtForHil `ParameterRegistry` port — Singleton sök)
- `cd::diag::DeadlineMonitor` (DtForHil `watchdog` port)
- `cd::asset::SchemaRegistry` (DtForHil schema port)
- `cd::plugin::Loader` (DtForHil `PluginLoader` 315-line port + IPlugin coupling sök)
- `cd::log::AuditTrail` (DtForHil auditlogger port)

**Test**: CVar hot-set integration test; plugin hot-load + ABI mismatch test.

### Sprint S2.5 — Concurrency Upgrade — 2 hafta

**Hedef**: Chase-Lev work-stealing + hazard pointers.

**Deliverables**:
- `cd::concurrency::ChaseLevDeque<T>` (mutex deque replace, ADR-015 upgrade)
- `cd::concurrency::Executor` work-stealing (N worker, cache-line `WorkerCell`)
- `cd::concurrency::HazardPointer` (Michael 2004 reclamation)
- `cd::concurrency::mpmc_queue<T>` unbounded with HP
- `cd::concurrency::spsc_queue<T>` Vyukov bounded
- `cd::concurrency::DeterministicExecutor` peer

**Test**: TSan-clean; stress test 1M jobs/sec; deterministic replay binary-equal.

### Sprint S2.6 — Salvage P3 (Serialization + Net Foundation) — 2 hafta

**Hedef**: BitStream + framing + Socket interface.

**Deliverables**:
- `cd::serialization::BinaryStream` (DtForHil port, read/write + bit-level)
- `cd::serialization::BitReader` + `BitWriter`
- `cd::serialization::HexCodec`, `BinaryCodec`
- `cd::net::ITransport` interface
- `cd::net::Framing` (LengthHeader, Delimiter — DtForHil port)
- `cd::net::PacketAssembler` (lock-free SPSC, DtForHil StreamAssembler upgrade)

**Test**: bit-pack round-trip; framing fuzz test.

### Sprint S2.7 — Salvage P4 (Optional) — 2 hafta

**Hedef**: Config + Platform utilities.

**Deliverables**:
- `cd::config::Value` variant tree (DtForHil port, `throw` → `expected`)
- `cd::config::IProvider` + JSON/TOML/YAML backends
- `cd::platform::Timer`, `Path`, `Environment` (DtForHil port)
- `cd::io::vfs` mount-point + zip backend (miniz vendor)
- `cd::io::path` UTF-8 sandbox

**Test**: VFS mod overlay scenario; path traversal sandbox.

### Sprint S2.8 — RHI Vulkan Bootstrap (Triangle) — 2 hafta

**Hedef**: Vulkan instance + device + swapchain + first triangle.

**Deliverables**:
- `cd::rhi::vendor::vulkan::Instance`, `Device`, `Surface`, `Swapchain`
- `cd::rhi::core::CommandList` minimal
- `cd::rhi::Handle<Buffer>`, `Handle<Texture>`, `Handle<Pipeline>` stub
- Validation layer integration debug build
- Triangle render: hardcoded SPIR-V shader

**Test**: golden image diff (triangle screenshot pixel-equal).

### Sprint S2.9 — RHI Resource Model + Bindless — 2 hafta

**Hedef**: Strongly-typed handles + intrusive Rc + bindless descriptor heap.

**Deliverables**:
- `cd::Rc<T>` intrusive ref-counted
- `cd::rhi::BufferDesc/TextureDesc/PipelineDesc`
- `cd::rhi::core::DescriptorHeap` bindless-first
- Auto state tracking default
- 3 queue (graphics + compute + transfer) with single-queue fallback

**Test**: bindless 10K texture stress; queue async compute verify.

### Sprint S2.10 — RHI Vulkan MVP Completion — 2 hafta

**Hedef**: E4=b MVP — mesh + texture + camera + 1 directional light.

**Deliverables**:
- Vertex buffer upload + index buffer
- Uniform buffer (camera matrix + light dir)
- Texture upload (R8G8B8A8 + mipmaps)
- Single material instance
- Basic forward render pass

**Test**: golden image diff; perf budget < 5ms CPU @ 1080p.

### Sprint S2.11 — Shader Pipeline Slang Bootstrap — 2 hafta

**Hedef**: Slang vendor integration + first cross-target compile.

**Deliverables**:
- `cd::shader::vendor::slang::Compiler` integration
- `cd::shader::Module` (Slang module wrap)
- Single Slang source → SPIR-V output for Vulkan backend
- `cd::shader::Reflection` (Slang reflection API wrap)

**Test**: Slang module compile success; SPIR-V validation.

### Sprint S2.12 — Material System MVP — 2 hafta

**Hedef**: `MaterialDesc` + macro reflection + parameter block.

**Deliverables**:
- `cd::material::MaterialDesc` macro `CD_REFLECT`
- `cd::material::MaterialInstance` parameter binding
- Slang `ParameterBlock<MaterialParams>` mapping
- Validation layer (debug build) reflection cross-check

**Test**: 5 material variants, parameter set/get round-trip.

### Sprint S2.13 — Shader Hot-Reload + PSO Cache — 2 hafta

**Hedef**: ADR-003 D + E hot-reload + 3-tier cache.

**Deliverables**:
- File watcher (cross-platform: ReadDirectoryChangesW, FSEvents, inotify)
- Slang recompile → re-link affected materials → PSO swap atomic
- 3-tier PSO cache (driver + app-level hash + async warm-up)
- `.cdshd` binary asset format

**Test**: hot-reload material in running render; PSO cache speedup measurement.

### Sprint S2.14 — ECS Storage Foundation — 2 hafta

**Hedef**: chunk-archetype + sparse-set hybrid.

**Deliverables**:
- `cd::ecs::Registry`
- `cd::ecs::ChunkArchetype` 16 KiB SoA storage
- `cd::ecs::SparseSet<T>` cold path
- Component registration with `StorageClass` enum
- Entity ID 32-bit + 32-bit gen handle

**Test**: 1M entities iteration benchmark; storage class auto-promote test.

### Sprint S2.15 — ECS Query + Scheduling — 2 hafta

**Hedef**: Function-signature query + phase scheduler + reactive observers.

**Deliverables**:
- `Query<Pos, &Vel, With<Enemy>, Without<Dead>>` template
- Phases: PreUpdate / Update / PostUpdate / Render / UI
- Reactive observers: `OnAdd<T>`, `OnRemove<T>`, `OnSet<T>` chunk-aware
- Parallel scheduler conflict detection (concept-based)

**Test**: 100k entities tag flip observer 10× speedup target verify.

### Sprint S2.16 — ECS Hierarchy + Prefab — 2 hafta

**Hedef**: ECS-internal hierarchy + prefab archetype-delta.

**Deliverables**:
- `Parent`, `Children`, `LocalTransform`, `WorldTransform`, `HierarchyDirty` components
- Transform propagation parallel BFS (roots-then-depth-buckets)
- `cd::scene::SceneGraphView` non-owning projection
- Prefab `.cdprefab` JSON + binary
- Variant override + nested prefab

**Test**: 10k hierarchy transform; prefab spawn benchmark.

### Sprint S2.17 — Renderer Foundation (cluster forward+) — 2 hafta

**Hedef**: ADR-002 baseline (cluster forward+, NOT visibility buffer yet).

**Deliverables**:
- `cd::render::FrameGraph` minimal (subgraph composition algebra)
- 3D froxel cluster light list
- Cook-Torrance + Disney diffuse baseline
- CSM + PCSS shadows
- TAA default

**Test**: 64 light cluster scene golden image.

### Sprint S2.18 — Renderer Post-FX Pipeline DSL — 2 hafta

**Hedef**: User-editable post-FX chain.

**Deliverables**:
- `cd::render::PipelineBuilder` DSL
- TAA + Bloom + Tonemap (ACES) default chain
- PipelinePreset facade (Mobile, Console, PC-Ultra, Cinematic)

**Test**: preset switch hot-reload; custom chain composability.

### Sprint S2.19 — Renderer Cross-Cutting (Asset Streaming Hook) — 2 hafta

**Hedef**: Renderer asset stream + culling integration.

**Deliverables**:
- `cd::render::Queue` SoA from ECS
- Frustum culling default
- HZB occlusion culling opt-in
- Asset streaming hook (LOD selection screen-space error)

**Test**: 100k mesh scene culling perf.

### Sprint S2.20 — Asset Pipeline Bootstrap — 2 hafta

**Hedef**: `cd_asset_compiler` CLI + glTF native importer.

**Deliverables**:
- `cd_asset_compiler` CLI (main binary)
- glTF 2.0 native parser (KHR extensions)
- `.cdmesh` custom binary format (no meshlet yet — Sprint 2.22)
- DDC Blake3 + Merkle dep graph
- SQLite registry MVP

**Test**: glTF import → `.cdmesh` round-trip; DDC cache hit verify.

### Sprint S2.21 — Asset Pipeline (KTX2 + Audio + Anim) — 2 hafta

**Hedef**: Texture + audio + animation asset support.

**Deliverables**:
- `.cdtex` KTX2 + Basis UASTC supertranscode (libktx + basisu vendor)
- `.cdaud` Opus + dr_wav + dr_flac
- `.cdanim` ACL compressed stub (full integration Sprint 3.x)
- File-watcher hot-reload bindless slot swap

**Test**: per-asset round-trip; hot-reload texture swap visual.

### Sprint S2.22 — Asset Pipeline (Meshlet + LOD + Streaming) — 2 hafta

**Hedef**: Meshlet cluster + LOD DAG + virtual texture base.

**Deliverables**:
- Meshlet generator (meshoptimizer vendor MIT)
- 128 tri × 64 vert clusters + cone culling
- LOD DAG (Nanite-vari)
- Virtual texture page table + feedback buffer (compute)

**Test**: 1M tri scene meshlet culling; VT page load benchmark.

### Sprint S2.23 — RHI Conformance Test Suite — 2 hafta

**Hedef**: Vulkan backend stress + golden image regression.

**Deliverables**:
- Golden image diff harness (FLIP / SSIM)
- 50 conformance scenes
- Performance regression dashboard
- Asset DDC remote upload CI

**Test**: All scenes pixel-equal; CI dashboard live.

### Sprint S2.24 — Phase 2 Integration & Hardening — 2 hafta

**Hedef**: End-to-end "hello scene" demo + freeze.

**Deliverables**:
- Sample app `samples/hello_scene`: glTF scene → Vulkan render
- All ADR-001..006 + 015 + 017 P0-P4 functional
- ASan/UBSan/TSan clean all matrix
- clang-tidy zero warning
- Documentation pass

**Done**: Phase 2 freeze, retrospective, Phase 3 kickoff plan.

**Phase 2 Toplam: 25 sprint × 2 hafta = 50 hafta ≈ 11.5 ay** (realistik, "kalite önce").

---

## 2. Phase 3 — Low-Level Replace + 3-API + Subsystems

### Block A: D3D12 + OpenGL Backend Port (Sprint S3.1-S3.6)

- S3.1-S3.2: D3D12 instance + device + swapchain
- S3.3-S3.4: D3D12 resource model + bindless (DescriptorIndexing)
- S3.5: D3D12 MVP feature parity (Vulkan triangle → mesh demo)
- S3.6: OpenGL 4.6 backend + Classic descriptor emulation

### Block B: Custom Allocator (ADR-016 K4) Sprint S3.7-S3.12

- S3.7-S3.8: TLSF allocator from scratch (Masmano & Ripoll ECRTS'04)
- S3.9: Arena allocator production-grade
- S3.10: Freelist with coalesce
- S3.11: `cd::mem::native::TLSFArenaFreelist` integration
- S3.12: mimalloc deprecate path; benchmark vs mimalloc parity

### Block C: Custom Audio Device (ADR-016 K4) Sprint S3.13-S3.18

- S3.13-S3.14: WASAPI native (Win exclusive mode + shared)
- S3.15: ALSA native (Linux)
- S3.16: CoreAudio native (macOS)
- S3.17: AAudio native (Android)
- S3.18: `cd::audio::native::*` `IDevice` integration; miniaudio deprecate

### Block D: Physics + Animation + Audio MVP Sprint S3.19-S3.24

- S3.19: Jolt vendor integration + adapter
- S3.20: Box2D v3 vendor + adapter
- S3.21: Character controller + XPBD cloth
- S3.22: Animation skeletal + LBS + compute skinning
- S3.23: AnimGraph + IK 2-bone + FABRIK
- S3.24: Audio mixer + HRTF + Opus codec

**Phase 3 Toplam: ~24 sprint × 2 hafta = ~12 ay**

---

## 3. Phase 4 — UI + Editor + Networking + Ops

### Block E: UI System (paralel akış Sprint S4.1-S4.18)

- **`cd::ui` track** (game IMGUI, 6-9 ay): S4.1-S4.18
- **`cd::editor_ui` track** (retained, 12-18 ay): S4.6-S4.30
- **Text pipeline**: S4.1 FreeType+Harfbuzz integration
- **Vector graphics**: S4.5 NanoVG-vari custom

### Block F: Editor Mechanics Sprint S4.10-S4.20

- Gizmo system (Universal + T/R/S)
- Half-edge modeling 8 core ops
- Spline editor
- Terrain sculpt
- Outliner + viewport 4-pane

### Block G: Networking Sprint S4.20-S4.30

- ITransport + GNS integration
- 3 replication strategy plugin
- DtForHil pattern net layer port
- Voice chat stub (S4.30 ileri ertelenebilir)

### Block H: i18n + Telemetry + Crash Sprint S4.15-S4.25

- Fluent FTL parser
- Tracy integration + custom HUD
- Crashpad + Sentinel hot-data ring

**Phase 4 Toplam: ~30 sprint × 2 hafta = ~15 ay**

---

## 4. Phase 5+ — Advanced Features (uzun vadeli, no fixed plan)

- VR/XR (OpenXR)
- AI/ML inference (ONNX runtime)
- Game AI (BT + UAI + GOAP + FSM)
- Procedural content (noise + spline + L-system + WFC)
- Modding pipeline (VFS overlay + signed scripts)
- Scripting host (Lua → Python → C#)
- Visual scripting (Blueprint-vari)
- Custom physics (opsiyonel — adapter pattern sayesinde değerlendirme)
- Custom profiler (Tracy reverse)
- Custom crash handler (Crashpad reverse)

---

## 5. Sprint Velocity & Risk Buffer

- **Sprint = 2 hafta** kabul.
- **Velocity assumption**: 1 senior C++23 developer ana iş; 1 part-time ek (testler, build).
- **Risk buffer**: her Phase'e %30 ekle (Phase 2: 11.5 → ~15 ay; Phase 3: 12 → ~16 ay; Phase 4: 15 → ~20 ay).
- **Total Phase 2-4 buffer'lı**: ~50 ay ≈ 4 yıl realistic v1.0.

**Düşürme stratejileri**:
- Phase 3 Block B (custom allocator) ertelenebilir → mimalloc Phase 4'e taşınır.
- Phase 3 Block C (custom audio) ertelenebilir → miniaudio Phase 5'e taşınır.
- Phase 4 Block E `cd::editor_ui` paralel akış zorunlu.
- Phase 4 Block G voice chat Phase 5'e ertelenir.

---

## 6. Critical Path Analysis

### Phase 2 Critical Path

```
S2.0 (Build) → S2.1 (Salvage P0) → S2.5 (Concurrency Upgrade)
                                       │
                                       ▼
S2.8 (RHI Vulkan Bootstrap) → S2.10 (RHI MVP) → S2.11 (Shader) → S2.12 (Material)
                                                                     │
                                                                     ▼
S2.14 (ECS Storage) → S2.15 (ECS Query) → S2.16 (Hierarchy)
                                                  │
                                                  ▼
                              S2.17 (Renderer) → S2.20 (Asset) → S2.22 (Meshlet)
                                                                       │
                                                                       ▼
                                                              S2.24 (Integration)
```

**Critical path length**: ~22 sprint sequential (~10 ay) — paralel akışla ~25 sprint toplam (~11.5 ay).

### Phase 3 Critical Path

D3D12 backend port + Custom allocator + Subsystem MVPs paralel akıyor.

### Phase 4 Critical Path

`cd::editor_ui` (12-18 ay) → en uzun chain. Diğer block'lar paralel akışa girer.

---

## 7. Success Metrics & Gates

| Phase | Gate | Metric |
|---|---|---|
| **Phase 1** | Design Freeze | 17 ADR signed off + peer-review passed |
| **Phase 2** | Foundation Demo | `samples/hello_scene` runs on Vulkan, 60 FPS @ 1080p, sanitizers clean |
| **Phase 3** | 3-API Parity | Same demo runs on D3D12 + OpenGL within 10% perf parity |
| **Phase 3** | Subsystem MVP | Physics + Animation + Audio integrated `samples/character` demo |
| **Phase 4** | Editor Alpha | Editor opens, basic level edit + save/load + hot-reload |
| **Phase 4** | Network MVP | 2-player networking demo (authoritative server + rollback test) |
| **Phase 4** | v1.0 Release | Apache 2.0 GitHub release; vcpkg/Conan ports; full CI matrix green |

---

## 8. Subagent Dispatch Per Sprint

Phase 2+'de her sprint için:
1. **architect** ajan: sprint başı `.hpp` interface taslakları + (büyük sprintlerde) ADR ek/update
2. **planner** ajan: sprint WBS detay (sprint-level)
3. **developer × N** ajan: paralel implementasyon
4. **tester** ajan: gtest + golden image
5. **code-consistency** ajan: clang-tidy + clang-format
6. **build-devops** ajan: CMake/preset/CI
7. **safety-integration** ajan: concurrency-touching kod (TSan, race, lifetime)
8. **troubleshooter** ajan: regresyon

Sprint sonu:
9. **citation-verifier** ajan (akademik atıf eklendiyse)
10. **peer-review-simulator** ajan: sprint sonu critical review
11. **release-manager** ajan: SemVer bump, changelog

---

## 9. Dependency Graph (Library DAG)

Bkz. [docs/DESIGN.md §2](DESIGN.md#2-library-dag-bağımlılık-grafiği) — Library bağımlılık DAG'ı.

Phase 2 implement sırası bu DAG'ı bottom-up takip eder:
1. **Foundation layer** (S2.1-S2.7): `core, mem, math, concurrency, time, io, serialization, log, events, diag, profile, platform, plugin, config`
2. **Asset & Resource** (S2.20-S2.22): `asset, ui::text` (FreeType+Harfbuzz integration)
3. **Render Layer** (S2.8-S2.19): `rhi → shader → material → render`
4. **World & Simulation** (S2.14-S2.16 + Phase 3): `ecs, scene` then `physics, anim, audio, input, net`
5. **UI & Editor** (Phase 4): `ui, editor_ui, editor`
6. **i18n & Telemetry** (Phase 4): `i18n, analytics` (telemetry already in `profile`)

---

## 10. Bağlantılar

- [docs/DESIGN.md](DESIGN.md) — Master Design Document
- [docs/ADR/](ADR/) — 17 Architecture Decision Records
- [docs/ADR/README.md](ADR/README.md) — ADR index
- [CLAUDE.md](../CLAUDE.md) — Project rules
- [.claude/agents/README.md](../.claude/agents/README.md) — Subagent suite

---

## 11. Notlar

- **Phase 1 tamamlandı** (Mayıs 2026). Tasarım dondurma süreci: `peer-review-simulator` ajan + akademik PDF indirimi (STUB'lardan finalize) + kullanıcı signoff.
- **Phase 2 kickoff prework**: Phase 1 STUB akademik referans finalize, ek vendor onayı (S6 meshoptimizer), CMakePresets.json review.
- Bu PLAN.md **canlı** doküman — her sprint sonu güncellenir; gerçek velocity vs planlanan delta tracked.
