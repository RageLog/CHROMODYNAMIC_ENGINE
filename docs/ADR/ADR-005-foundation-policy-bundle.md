# ADR-005 — Foundation Policy Bundle

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Scope**: Build/ABI · Memory · Math · Allocator · VFS · Time · Sync Primitives
- **Related**: ADR-015 (Concurrency), ADR-016 (Vendor Matrix), ADR-017 (DtForHil Salvage)

## Bağlam

Yedi cross-cutting karar birbirine kilitlidir: handle-based ownership allocator hierarchy'ye, tri-clock frame pacer'a, fiber job system hazard-pointer queue'ya bağımlı. Tek bir bundle ADR ile dondurulur.

Kısıtlar: 4-compiler matrix (MSVC/Clang-cl/Clang/GCC) × {Win, Linux} × {x86_64, ARM64, RISC-V plan}. Library-oriented + zero-warning + C++23 baseline + C++26 opt-in. "Kütüphaneleri kendimiz yazacağız" (T5.Q2) prensibi vendor minimum.

## Karar

### A. Build / ABI

- **CI matrix**: native MSVC (Win), native Clang-cl (Win), native Clang (Linux), native GCC (Linux) — 4 compiler × 2 OS × {Debug, RelWithDebInfo, Release}. Linux'ta MSVC/Clang-cl yok → 10 job.
- **Build output**: `CD_BUILD_SHARED_LIBS=OFF` static-by-default; `cd_add_plugin()` CMake helper'ı her zaman shared.
- **Visibility**: `-fvisibility=hidden` + `-fvisibility-inlines-hidden` global; per-library hand-authored `CD_<MODULE>_API` macro (CMake `GenerateExportHeader` reddedildi — sembol stability).
- **Cross-compile**: native runners canonical; mingw-w64 non-blocking smoke (POSIX-isms erken yakala).
- **Symbol stripping**: release `--gc-sections` / `/OPT:REF /OPT:ICF`. PIMPL opt-in per class.

### B. Memory & Performance Policy

- **Frame pacing**: multi-tier accumulator — sim 60 Hz fixed default (configurable 30/120/240), render variable + interpolation, net 30 Hz default. Spiral-of-death cap (max substeps per frame). `cd::time::FramePacer` library.
- **Memory budget**: soft + tracking. Boot-alloc preferred, runtime alloc tagged via `cd::mem::TrackingAllocator`. CI gate `cd_assert_frame_budget(subsystem, kb)` for hot loop subsystems.
- **Ownership**: handle (`cd::core::Handle` — 32-bit index + 16-bit gen + 16-bit type) on hot path; `std::unique_ptr` for subsystem singletons; `std::shared_ptr` only for genuine cross-subsystem shared ownership. Raw owning pointers forbidden (CLAUDE.md §1).

### C. Math / Float Precision

- **Default**: `cd::math::vec3` = float; `cd::math::dvec3` = double; `cd::math::world_pos` typedef compile-time-resolved via `CD_WORLD_DOUBLE_PRECISION` (ON for engine, OFF for standalone `cd::math` users). Camera-relative float rebase for render (large-world-coordinates pattern).
- **Determinism**: opt-in per-TU `CD_DETERMINISTIC` flag enabling `cd::math::fixed32_16` Q-format with deterministic transcendental tables. x86_64 desktop cross-OS strict-FP path (`-ffp-contract=off`, `-fno-fast-math`, `_controlfp` pin). ARM↔x86 lockstep requires fixed-point.
- **Coordinate system**: compile-time configurable; **default Right-Handed Z-up** (Blender/production world). `CD_COORD_RH_ZUP` default flag.

### D. Memory Allocator

- **Global heap**: **mimalloc** vendored (CMake `CD_ALLOCATOR=mimalloc|rpmalloc|system`). Replace-ready: D2 discipline Phase 3 hedefi kendi yazımımız.
- **Hierarchy** (`cd::mem`): `system_allocator`, `linear_allocator`, `pool_allocator<T>`, `freelist_allocator`, `tlsf_allocator`, `tracking_allocator<Inner>`, `page_allocator` (DtForHil ADR-017 P0 port).
- **PMR-compatible**: each adapts `std::pmr::memory_resource`.
- **Per-subsystem**: each engine subsystem declares preferred allocator at construction; fallback `system_allocator`.

### E. VFS / Filesystem

- **In-house `cd::io::vfs`**: PhysFS-pattern mount points, priority-ordered search, pluggable archive backends (raw dir, zip, custom `.cda`), Ed25519-signed manifest verification in ship builds. ZIP via `miniz` vendored (sole exception for ZIP CDC; reviewed per E2 vendor matrix).
- **`std::filesystem`**: only at platform boundary inside `cd::io::detail`; user code consumes `cd::io::path` (UTF-8, forward-slash, sandboxed).
- **Mod overlay**: `dev_overlay` mount in editor builds (raw assets shadow shipped archives).

### F. Time / Clock

- **Tri-clock model**:
  - `cd::time::real_clock` — wall, NTP-affected; save timestamps + analytics only.
  - `cd::time::game_clock` — scaled, pausable; gameplay tick (DtForHil `SimClock` ADR-017 P0 port: pause/step/speed-multiplier 0.1×–10×, deterministic seed).
  - `cd::time::hires_clock` — monotonic raw; profiler + frame pacer.
- **Platform primitives**: `QueryPerformanceCounter` (Win), `clock_gettime(CLOCK_MONOTONIC_RAW)` (Linux), `mach_absolute_time` (macOS). `std::chrono::steady_clock` wrap when sufficient.
- **RDTSC**: confined to `cd::profile::cycle_counter`; thread-relative, not time.
- **`cd::time::IClock` interface** (DtForHil ADR-017 P0 port): injectable for testability (real/sim).

### G. Concurrency Primitives

- **Job system topology**: see ADR-015 (coroutine-first hybrid task-graph + Chase-Lev work-stealing).
- **Render thread**: N-buffered (sim N / record N+1 / GPU N+2) with secondary command buffer recording in parallel jobs; VR low-latency profile collapses to single-frame.
- **Sync primitive kit** (`cd::concurrency`):
  - `std::atomic` wrappers (explicit memory order helpers; relaxed opt-in)
  - `mutex`, `shared_mutex` (debug contention tracking)
  - `spin_lock` (`_mm_pause`/`__yield` calibrated; sub-µs critical sections only)
  - `spsc_queue<T>` (Vyukov SPSC, wait-free single producer)
  - `mpmc_queue<T>` (Vyukov bounded; unbounded via hazard pointers — Michael 2004)
  - `RingBuffer<T,N>` (DtForHil ADR-017 P0 port, lock-free SPSC, batch ops)
  - `DataChannel<T>` (DtForHil ADR-017 P0 port, named SPSC for audio/telemetry/GPU upload)

## Reddedilen

- Linux→Windows cross-compile as sole CI path (MSVC ABI doğrulanmaz).
- `shared_ptr`-only ownership (atomic refcount hot path cost).
- Strict-FP everywhere for determinism (ARM↔x86 breaks).
- jemalloc as global heap (effectively abandoned 2024-25).
- PhysFS vendored as-is (CLAUDE.md §7 prensibi; in-house preferred).
- RDTSC for game timing (Microsoft official guidance; TSC invariance).
- Epoch-based reclamation for unbounded queues (memory blow-up in long editor sessions).
- Single render thread (Vulkan/D3D12 multi-thread record kaybı).
- CMake `GenerateExportHeader` auto symbols (sembol stability ihlali).

## Sonuçlar

**Pozitif**: per-library standalone usage, predictable ABI, SOTA performance ceiling, cross-platform determinism path mevcut, profiler-friendly time abstraction.

**Negatif**: 5+ allocator type training cost; fiber-based debugging requires custom symbolicator; double-precision world coords roughly 2× CPU vector memory (mitigated by camera-rel float render).

**Replace-Ready (D1 + D2)**:
- mimalloc → kendi TLSF+arena+freelist hybrid (Phase 3, 6-12 ay)
- miniz (ZIP) → kendi INFLATE decoder veya `.cda` only (Phase 4)
- Allocator interface'leri stable; backend swap CMake flag

## Açık Sorular

| ID | Soru | Karar veya çözüm noktası |
|---|---|---|
| Q1 | Fiber crash dump tooling Windows'ta custom `MiniDumpWriteDump` mı? | Phase 2 prototyp |
| Q2 | C++23 modules — `import std;` foundation'da kullanılsın mı? | Header v1, modules Sprint 12+ revisit |
| Q3 | ZIP archive: `miniz` mi `.cda` only mı? | `miniz` exception kabul (sole vendored ZIP CDC) |
| Q4 | TSan preset fiber-aware mı? | `__tsan::AnnotateHappensBefore` fiber switch sites; alternatif non-fiber config |
| Q5 | `std::expected` vs `tl::expected` polyfill? | Compiler probe + `cd::core::Result<T>` alias; libstdc++14+, libc++17+ standart |

## Cross-Cutting

- **ADR-001 (RHI)**: handle storage + GPU staging allocator + N-buffered render topology.
- **ADR-015 (Concurrency)**: job system + sync primitives + tri-clock binding.
- **ADR-006 (Asset)**: VFS + freelist allocator for asset chunks + tri-clock for streaming budget.
- **ADR-011 (Networking)**: tri-clock + fixed-point opt-in + lock-free queues for net thread.
- **ADR-013 (Telemetry)**: tracking allocator + hires clock + cycle counter.

## Kanıt

- mimalloc: https://microsoft.github.io/mimalloc/bench.html (acc 2026-05-16)
- production engines Large World Coordinates: https://dev.epicgames.com/documentation/en-us/unreal-engine/large-world-coordinates-in-unreal-engine-5
- Glenn Fiedler — Fix Your Timestep: https://gafferongames.com/post/fix_your_timestep/
- Bruce Dawson — Floating-Point Determinism: https://randomascii.wordpress.com/2013/07/16/floating-point-determinism/
- TLSF: A New Dynamic Memory Allocator for Real-Time Systems (ECRTS'04 PDF): http://www.gii.upv.es/tlsf/files/papers/ecrts04_tlsf.pdf — **STUB**
- Hazard Pointers — Maged Michael WG21 P0233R3: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0233r3.pdf — **STUB**
- Gyrling — Parallelizing Naughty Dog with fibers (GDC15): https://media.gdcvault.com/gdc2015/presentations/Gyrling_Christian_Parallelizing_The_Naughty.pdf
