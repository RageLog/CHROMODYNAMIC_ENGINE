# CHROMODYNAMIC Engine — Master Design Document

- **Version**: 0.1 (Phase 1 Design Freeze)
- **Date**: 2026-05-17
- **Status**: Phase 1 Tasarım Sentezi — implementation Phase 2'den başlar

Bu doküman 17 ADR'ı entegre bir engine vizyonuna sentezler. Detay her bir ADR'a referans verilerek tutulur (DRY); bu doc **birleştirici** bakış, modüller arası bağımlılıklar, ve Phase Roadmap'i içerir.

---

## 1. Vizyon

CHROMODYNAMIC, sadece bir oyun motoru değil; **hybrid 2D+3D rendering + general-purpose framework**. Game + scientific visualization + tooling first-class kullanım senaryoları. Mevcut state-of-the-art (Unreal Engine, Filament, bgfx, EnTT, Bevy, Jolt) **aşılacak hedef** — kopya değil.

### 1.1 Temel Prensipler

1. **Library-Oriented**: Her alt-sistem (`cd::<lib>`) bağımsız static/shared library; engine'siz tek başına kullanılabilir.
2. **Replace-Ready Vendor Discipline** (ADR-016 D1): Her vendor adapter arkasında; ileride kendi yazımımız swap edilir.
3. **Low-Level Custom-First Bias** (ADR-016 D2): mimalloc, miniaudio, Crashpad, Tracy Phase 2-4 hedef değişim.
4. **DtForHil Pattern Salvage** (ADR-017): 24 üretim-kalite pattern miras (yozlaştırmadan).
5. **C++23 Baseline + C++26 Opt-in**: `CD_HAS_CXX26_*` macro toolchain olgunlaşınca flip.
6. **A++ Kalite**: zero warning, `[[nodiscard]]`, `std::expected`, sanitizers, clang-tidy.
7. **Research-First**: implementasyondan önce daima araştırma + ADR.

### 1.2 Hedef Donanım & Platformlar

| Platform | Phase 1 Tasarım | Phase 2 Impl | Phase 3+ |
|---|---|---|---|
| Windows 11 (x86_64) | ✅ Primary | ✅ | — |
| Ubuntu 22.04 LTS (x86_64) | ✅ Primary | ✅ | — |
| Steam Deck (Steam Runtime 3.0) | ✅ iskelet | ✅ | — |
| macOS arm64 (Apple Silicon) | ⚠️ MoltenVK | ⏳ | ✅ Native Metal |
| iOS / Android | ⚠️ iskelet | ⏳ | ✅ Mobile build |
| PS5 / Xbox Series / Switch 2 | ⚠️ iskelet | ⏳ NDA | ✅ Console |
| Web (WASM/WebGPU) | ❌ out-of-scope | — | ⏳ Phase 4+ |
| ARM64 Linux + RISC-V | ⚠️ iskelet | ⏳ | ✅ |

### 1.3 Performans Hedefleri

| Profile | FPS | Use case |
|---|---|---|
| Mobile thermal-aware | 60 sustained | Mobil oyun |
| Console standard | 60 | PS5/XSS baseline |
| PC console parity | 60–144 | Variable refresh |
| Competitive | 144–240 | FPS, esports |
| VR-grade | 90/120, motion-to-photon < 20 ms | Quest 3 / Vision Pro |

---

## 2. Library DAG (Bağımlılık Grafiği)

```
                                       (highest layer)
                       ┌─────────────────────────────────┐
                       │  cd::editor (Editor framework)  │
                       │  cd::runtime (Engine compose)   │
                       └─────────────┬───────────────────┘
                                     │
       ┌─────────────────────────────┼─────────────────────────────┐
       │                             │                             │
       ▼                             ▼                             ▼
┌─────────────┐               ┌─────────────┐               ┌─────────────┐
│ cd::scene   │               │ cd::render  │               │ cd::editor_ui│
│ (ADR-004    │               │ (ADR-002    │               │ (ADR-009    │
│  Scene proj)│               │  Renderer)  │               │  Retained)  │
└──────┬──────┘               └──────┬──────┘               └──────┬──────┘
       │                             │                             │
       ▼                             ▼                             │
┌─────────────┐               ┌─────────────┐                      │
│  cd::ecs    │◄──────────────│ cd::material│                      │
│ (ADR-004)   │               │ (ADR-003)   │                      │
└──────┬──────┘               └──────┬──────┘                      │
       │                             │                             │
       │                             ▼                             │
       │                      ┌─────────────┐                      │
       │                      │ cd::shader  │                      │
       │                      │ (ADR-003)   │                      │
       │                      └──────┬──────┘                      │
       │                             │                             │
       │                             ▼                             │
       │                      ┌─────────────┐                      │
       │                      │  cd::rhi    │                      │
       │                      │ (ADR-001)   │                      │
       │                      └──────┬──────┘                      │
       │                             │                             │
       │   ┌─────────────────────────┼─────────────────────────────┘
       │   │                         │
       │   ▼                         ▼
       │ ┌─────────────┐      ┌─────────────┐
       │ │ cd::input   │      │ cd::ui      │ (ADR-009 Game IMGUI)
       │ │             │      │             │
       │ └──────┬──────┘      └──────┬──────┘
       │        │                    │
       └────────┼────────────────────┤
                │                    │
                ▼                    ▼
         ┌─────────────────────────────────────┐
         │ cd::audio  cd::physics  cd::anim    │
         │ (ADR-007)  (ADR-008)   (ADR-010)    │
         └──────┬─────────┬────────────┬───────┘
                │         │            │
                └─────────┼────────────┘
                          │
                          ▼
         ┌─────────────────────────────────────┐
         │  cd::net (ADR-011)  cd::asset (ADR-006)│
         │  cd::log  cd::config  cd::plugin    │
         │  cd::events  cd::diag  cd::profile  │
         │  cd::i18n  cd::analytics  cd::platform│
         └──────┬──────────────────────────────┘
                │
                ▼
         ┌─────────────────────────────────────┐
         │  cd::time (ADR-005 F)               │
         │  cd::concurrency (ADR-005G + 015)   │
         │  cd::serialization                  │
         └──────┬──────────────────────────────┘
                │
                ▼
         ┌─────────────────────────────────────┐
         │  cd::mem (ADR-005 D)                │
         │  cd::math (ADR-005 C)               │
         │  cd::io (ADR-005 E VFS)             │
         └──────┬──────────────────────────────┘
                │
                ▼
         ┌─────────────────────────────────────┐
         │  cd::core (ADR-005 A+B)             │
         │  - Result<T>, Handle, defines       │
         │  - CVarRegistry, HandleStore        │
         └─────────────────────────────────────┘
                  (lowest layer — foundation)
```

**DAG kuralı**: Cycle yasak. Yukarıdan aşağıya tek yönlü. `cd::ecs::ecs_adapter`-style cross-cutting interfaces ECS→net, ECS→physics gibi cycle-breaking pattern'i ile çözülür.

---

## 3. Modül Özetleri

### 3.1 Foundation Layer

#### `cd::core` (ADR-005 A+B, ADR-017 P0)
- `Result<T> = std::expected<T, ErrorCode>` (DtForHil port)
- `Handle = {type, gen, index}` 32-bit packed
- `HandleStore` type-erased registry (DtForHil port)
- `CVarRegistry` runtime tuning (DtForHil ADR-017 P2 port)
- `defines.hpp` platform/compiler/arch detection + `CD_<MOD>_API` macros
- `Schema`, schema-versioned components

#### `cd::math` (ADR-005 C)
- vec2/3/4, mat2/3/4, quat, transform — SIMD (Highway-wrap, ADR-015)
- `world_pos` compile-time float/double resolution (UE5 LWC pattern)
- Fixed-point `cd::math::fixed32_16` opt-in determinism
- **Default: Right-Handed Z-up** (Blender/Unreal world coord)
- SIMD: SSE2/AVX2/AVX-512/NEON/SVE/RVV via Highway

#### `cd::mem` (ADR-005 D, ADR-016 K4 mimalloc Phase 3 replace)
- `IAllocator` interface
- `LinearAllocator`, `PoolAllocator<T>`, `FreelistAllocator`, `TLSFAllocator`, `PageAllocator`
- `TrackingAllocator<Inner>` decorator
- Global heap: `cd::mem::vendor::mimalloc::SystemAllocator`
- Phase 3 hedef: `cd::mem::native::TLSFArenaFreelist`

#### `cd::concurrency` (ADR-005 G + ADR-015 + ADR-017 P1)
- Coroutine-first `task<T>` (stop_token propagation, DtForHil pattern)
- Chase-Lev work-stealing executor + Vyukov MPSC injector
- `JobGraph` task-graph DAG
- `DeterministicExecutor` peer (rollback/replay/test)
- Async I/O `cd::io::context` (io_uring/IOCP/kqueue)
- `RingBuffer<T,N>` SPSC (DtForHil port)
- `DataChannel<T>` named SPSC (DtForHil port)
- Sync primitives: `spsc_queue`, `mpmc_queue` (hazard pointers), `spin_lock`
- SIMD: Highway wrap `cd::simd::vec<T,N>`

#### `cd::time` (ADR-005 F + ADR-017 P0+P2)
- `IClock` injectable (DtForHil port)
- `real_clock`, `game_clock`, `hires_clock` tri-model
- `SimClock` pause/step/speed-multiplier (DtForHil port, **birinci sınıf**)
- `TimerQueue` priority-queue (DtForHil port)
- `FramePacer` multi-tier accumulator (sim 60Hz / render variable / net 30Hz)

#### `cd::io` (ADR-005 E)
- VFS mount-point (PhysFS pattern in-house)
- Archive backends: raw dir, zip (miniz vendor), custom `.cda`
- Ed25519 signed manifest verification (ship)
- `cd::io::path` UTF-8 sandboxed

#### `cd::serialization` (ADR-017 P3)
- `BinaryStream` (DtForHil port, read/write + bit-level)
- `BitReader`, `HexCodec`, `BinaryCodec`
- Schema-driven `cd::asset::SchemaRegistry`

#### `cd::log` (ADR-013 + ADR-017 P1)
- `ILogger` brace-format + `source_location` (DtForHil port)
- `JsonlBackend`, `SpdLogBackend` opt
- `AuditTrail` ring-history (DtForHil `auditlogger` port)

#### `cd::events` (ADR-017 P1)
- `EventBus` typed-only pub/sub (DtForHil `AsyncEventBus` port, 652 satır → ~400 LOC refactor)
- `ScopedConnection` RAII
- `EventRecorder` JSONL + rotation
- Sync/Async/HighPrio/Deferred delivery modes

#### `cd::diag` (ADR-013 + ADR-017)
- `SignalManager` POSIX `<csignal>` (DtForHil port, P0)
- `CrashReporter` minidump (DtForHil port + Crashpad vendor)
- `DeadlineMonitor` heartbeat (DtForHil `watchdog` port, P2)
- `Sentinel` hot-data ring (S/N frame, log tail, scene)
- `vendor::crashpad` + Phase 4 `native::cdyn_crash` hedef

#### `cd::profile` (ADR-013)
- `cd::profile::Zone` thin wrapper (Tracy varsa zone, yoksa no-op)
- `HudOverlay` custom in-engine HUD
- `cycle_counter` RDTSC profiler-only

#### `cd::platform` (ADR-017 P4)
- `Timer`, `Path`, `Environment` (DtForHil port)
- Per-platform native (WASAPI/io_uring/etc. eventually)

#### `cd::plugin` (ADR-017 P2)
- `IPlugin` ABI gate + manifest cross-check (DtForHil port, SOTA üstü)
- DLL load + RAII handle + use-after-unload guard
- Hot-reload first-class

#### `cd::config` (ADR-017 P4)
- `Value` variant tree (DtForHil port)
- `IProvider` (JSON/YAML/TOML pluggable backend)

### 3.2 Asset & Resource Layer

#### `cd::asset` (ADR-006)
- glTF native importer + plugin FBX (ufbx) + USD (Pixar SDK)
- `.cdtex` (KTX2+Basis), `.cdmesh` (meshlet+LOD), `.cdaud` (Opus), `.cdanim` (ACL), `.cdscene`, `.cdmat`, `.cdshd`
- `cd_asset_compiler` standalone CLI
- Content-addressed DDC (Blake3) + Merkle DAG dep graph
- SQLite + JSON hybrid registry
- File-watcher hot-reload + bindless slot atomic swap
- Vendor: libktx, basisu, opt: ufbx, USD

#### `cd::ui::text` (ADR-009 E + ADR-016)
- Vendor: FreeType + Harfbuzz (K2, replace yok)
- MSDF (msdfgen opt) + raster + subpixel atlas

### 3.3 Render Layer

#### `cd::rhi` (ADR-001)
- 2-tier: `core` (handle imperative encoder) + `graph` (frame graph compiler)
- Bindless-first + classic emulation
- 3-queue (graphics/compute/transfer) + auto-fallback
- Vulkan 1.3 + D3D12 + OpenGL 4.6 paralel
- Compile-time `template <BackendTrait B>` + runtime `IBackend` v-table

#### `cd::shader` + `cd::material` (ADR-003)
- **Slang** tek source + native multi-target codegen
- Material graph → Slang module codegen (not text concat)
- Link-time specialization (no permutation explosion)
- 3-tier PSO cache + reflection bridge

#### `cd::render` (ADR-002)
- Hybrid clustered + visibility-buffer (UCVE)
- `PipelinePreset` facade (Mobile/Console/PC-Ultra/Cinematic)
- `IPlug` strategy matrix (lighting/GI/shadow/AA/post-FX/HDR/RT/atmosphere)
- Vendor SDK upscalers out-of-tree plugin

### 3.4 World & Simulation Layer

#### `cd::ecs` + `cd::scene` (ADR-004)
- Hybrid chunk-archetype + sparse-set + bitmask
- Hierarchy ECS-internal (Parent/Children/Transform)
- Function-signature query + phase scheduler + reactive observers
- Prefab archetype-delta + nested
- 3-tier hot reload
- Pluggable spatial (`IBroadPhase`) + culling (`ICullingStrategy`)
- Determinism opt-in template policy (zero runtime cost off)

#### `cd::physics` + `cd::physics2d` (ADR-008)
- Jolt MIT default + PhysX 5 opt-in adapter
- Box2D v3 vendor (E2 reverse)
- `CharacterVirtual` pattern hybrid
- XPBD cloth/rope kendi yazım
- 3-tier determinism

#### `cd::anim` (ADR-010)
- LBS + DQS + compute skinning
- HSM + blend tree + AnimGraph
- 4-layer IK (two-bone, FABRIK, full-body, procedural foot)
- Motion matching first-class
- ACL vendor compression

#### `cd::audio` (ADR-007)
- miniaudio vendor (device I/O, Phase 3 replace)
- Custom mixer + custom HRTF + Steam Audio opt
- Opus + dr_wav + dr_flac codecs
- SPSC lock-free actor model

#### `cd::input`
- Action-set + direct polling
- KB+M+gamepad+touch+VR+HID önem sırası
- Rebind + accessibility first-class

#### `cd::net` (ADR-011)
- 3-layer: transport + protocol + replication
- DtForHil pattern 1:1 port (11 dosya)
- GNS+ENet+QUIC+WebRTC `ITransport`
- 3 replication strategy plugin (snapshot+delta, lockstep, rollback) runtime-switchable

### 3.5 UI & Editor Layer

#### `cd::ui` (ADR-009 game IMGUI)
- Custom IMGUI (Dear ImGui pattern re-implement)
- 6-9 ay engineering, ~30 KLOC

#### `cd::editor_ui` (ADR-009 retained)
- Custom retained scene-graph UI
- Flexbox subset layout + CSS subset parser + XML markup `.cdui`
- 12-18 ay engineering, ~50 KLOC

#### `cd::editor` (ADR-012)
- `gizmo` (UE5 ITF-vari composable)
- `level` (8-core half-edge modeling + spline + terrain + decal + lighting + outliner)
- `viewport` (orbit/fly/fps/ortho + 4-pane quad mandatory)
- EditOp event-sourced (undo + IPC + future collab)

### 3.6 i18n & Localization

#### `cd::i18n` (ADR-013)
- Mozilla Fluent FTL parser (in-engine ~2K LOC)
- ICU optional heavy mode
- BiDi: UAX#9 (fribidi opt)
- Hot-reload double-buffer

#### `cd::analytics` (ADR-013)
- Plugin-only, **zero built-in tracking**
- `ISink` interface, GDPR consent

---

## 4. Cross-Cutting Concerns

### 4.1 Determinism

T3.Q2 = D opt-in cross-cutting:
- `cd::time::SimClock` fixed timestep + seeded RNG (ADR-005 F)
- `cd::concurrency::deterministic_executor` peer (ADR-015)
- `cd::math::fixed32_16` Q-format (ADR-005 C)
- `cd::physics` Tier-1 same-platform + Tier-2 cross-platform fixed-point (ADR-008)
- `cd::net::replication` rollback strategy (ADR-011)

### 4.2 Hot-Reload

5-katmanlı:
1. Asset (mesh/texture/audio) — bindless slot atomic swap (ADR-006)
2. Material/Shader — Slang recompile + PSO swap (ADR-003)
3. CSS/UI markup — Fluent + retained tree invalidate (ADR-009)
4. Script — VM bytecode reload (ADR-future)
5. Plugin DLL — Plugin ABI gate (ADR-017 P2)

### 4.3 Memory Tracking

`cd::mem::TrackingAllocator<Inner>` + `cd::profile::MemoryTag` (compile-time enum, ~0 overhead) → Tracy events + UE5 LLM-vari per-subsystem budget tracking (ADR-005 B + ADR-013 B.2).

### 4.4 C++23 → C++26 Migration Path

`CD_HAS_CXX26_*` macros in `cd::core::defines`:
- `CD_HAS_CXX26_REFLECTION` (P2996)
- `CD_HAS_CXX26_CONTRACTS`
- `CD_HAS_CXX26_EXECUTION` (senders/receivers)
- `CD_HAS_CXX26_PATTERN_MATCHING`

Macro fallback: `CD_REFLECT(...)` for reflection, manual contract checks, `cd::concurrency::detail::sender` algebra.

### 4.5 Library-Oriented Standalone

Her library bağımsız:
- Kendi `CMakeLists.txt` + version
- Kendi gtest binary + sample
- Public header `include/chroma/<lib>/`
- Stable ABI `CD_<MODULE>_API` macro

External tool olarak verilebilir: `cd_asset_compiler`, `cd_shader_compiler`, `cd::audio` DAW plugin host, `cd::net` headless dedicated server, `cd::math` standalone scientific viz.

---

## 5. Phase Roadmap

### Phase 1 — Design (Mevcut, ~6 ay tamamlanma — "ne kadar sürse o kadar")

- ✅ 17 ADR (this set)
- ✅ DtForHil deep-scan + salvage plan
- ✅ Vendor matrix dondurma
- ⏳ Akademik literatür PDF indirimi (`academic-researcher` ajan, STUB'lardan)
- ⏳ Peer-review simulation (`peer-review-simulator` ajan)
- ⏳ Master Design freeze + signoff

### Phase 2 — Foundation Implementation (~6-9 ay)

**Sprint S2.1-S2.7**: ADR-017 salvage P0-P4 implement (foundation derlenebilir)
**Sprint S2.8-S2.10**: RHI Vulkan MVP (ADR-001) — triangle → mesh+texture+camera+1 light
**Sprint S2.11-S2.13**: Shader pipeline Slang integration (ADR-003)
**Sprint S2.14-S2.16**: ECS chunk-archetype + sparse-set (ADR-004)
**Sprint S2.17-S2.20**: Renderer MVP cluster forward+ (ADR-002)
**Sprint S2.21-S2.24**: Asset pipeline `cd_asset_compiler` MVP (ADR-006)

### Phase 3 — Low-Level Replace Drive (~6-12 ay)

ADR-016 D2 hedefi:
- `cd::mem::native::*` allocator suite (mimalloc reverse)
- `cd::audio::native::*` device I/O (miniaudio reverse)
- D3D12 + OpenGL backend port (ADR-001)
- Physics adapter + XPBD (ADR-008)
- Animation MVP + ACL integration (ADR-010)
- Audio mixer (ADR-007)

### Phase 4 — Editor, UI, Networking (~12-18 ay)

- `cd::ui` custom IMGUI (ADR-009, 6-9 ay)
- `cd::editor_ui` retained + CSS (ADR-009, 12-18 ay parallel)
- `cd::editor` mechanics + gizmo + level editing (ADR-012)
- Networking MVP (ADR-011)
- i18n + Telemetry + Crash (ADR-013)
- CI/CD full pipeline (ADR-014)

### Phase 5+ — Advanced & Specialized

- VR/XR (OpenXR)
- AI/ML inference
- Scripting host (Lua → Python → C#)
- Visual scripting / Blueprint
- Modding pipeline
- Custom audio device (Phase 3+ uzantısı)
- Custom physics (opsiyonel değerlendirme)

---

## 6. Risk Yönetimi

| Risk | Mitigation |
|---|---|
| **C++26 toolchain slip** | C++23 baseline + macro fallback (`CD_HAS_CXX26_*`) |
| **Vendor lock-in** | Replace-Ready discipline (ADR-016 D1), adapter pattern |
| **DtForHil salvage refactor bug-prone** | Per-library gtest izole; CI matrix tüm 4 compiler |
| **Custom IMGUI uzun (6-9 ay)** | API Dear ImGui ile 1:1, geçiş tersine mümkün |
| **18-27 ay UI engineering** | Editor S9 retained ile paralel akış |
| **Slang backend bugs niche features** | SPIRV-Cross fallback flag |
| **Lumen-class SW-RT Phase 2 small-team** | Phase 3+ defer; DDGI first |
| **Determinism cross-platform Jolt+Box2D garanti?** | Tier-2 fixed-point Sprint 14+ |
| **Open source community ramp** | DCO + public from day 1 + Apache 2.0 |

---

## 7. Bağlantılar

- **ADR'lar**: [docs/ADR/](ADR/)
- **Implementation Plan**: [docs/PLAN.md](PLAN.md)
- **Research Library**: [research/library/](../research/library/)
- **Research Papers (architecture books)**: [research/papers/](../research/papers/)
- **Subagent Suite**: [.claude/agents/](../.claude/agents/)
- **Project Rules**: [CLAUDE.md](../CLAUDE.md)

---

## 8. Sentez Notları

CHROMODYNAMIC tasarımı, **mevcut SOTA çözümlerin best-of-breed birleşimi + ölçülebilir aşma noktaları** üzerine kurulmuştur:

- **Library-oriented + replace-ready**: Hiçbir SOTA engine bu kombinasyonu sunmuyor.
- **3 paralel RHI backend + bindless-first + 2-tier graph**: Unreal+Filament+bgfx üzerine.
- **Slang as single source language**: Khronos 2024 dönüşümü; ilk early adopter open-source engine.
- **Hybrid ECS chunk+sparse+chunk-aware observers**: Unity DOTS hot + EnTT cold + Bevy reactive hybrid.
- **DtForHil 24-pattern salvage**: Üretim-kalite kod tabanı miras (yozlaştırmadan).
- **Determinism opt-in foundation**: Rollback netcode + lockstep + replay test "free" gelir.
- **Vendor matrix normative + Replace-Ready**: Low-level custom-first bias gelecekte tam bağımsızlık.

Bu sentez Phase 1 tasarım fazının final çıktısıdır. Phase 2 implementation Sprint S2.1'den başlar.
