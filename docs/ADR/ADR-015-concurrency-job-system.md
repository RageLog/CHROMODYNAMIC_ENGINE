# ADR-015 — Concurrency, Job System & SIMD

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-005 (Foundation), ADR-017 (DtForHil Salvage)

## Bağlam

CHROMODYNAMIC needs a single, ABI-stable, cross-platform concurrency substrate that:
- supports VR-grade low latency (motion-to-photon < 20 ms),
- opts into deterministic replay for tests and future netcode rollback,
- is usable as a standalone library (`cd::concurrency`),
- interoperates with C++20 coroutines today and C++26 `std::execution` later,
- covers 4-compiler matrix on x86_64, ARM64, RISC-V (plan).

Reference designs: Naughty Dog fibers (Gyrling GDC15), Bevy ParallelExecutor, Filament, Frostbite job graph, Intel TBB, Marl, Taskflow, Folly + libunifex, libdispatch (GCD), plus DtForHil `ThreadPool` + coroutine prior art.

## Karar

### Job System (T9.Q1 = D+E + DtForHil ilham)

**Hybrid task-graph + Chase-Lev work-stealing executor with first-class coroutine surface, senders/receivers algebra internally.**

- `cd::concurrency::JobGraph` — DAG of tasks; topological scheduler with deterministic mode option.
- `cd::concurrency::Executor` — N worker threads (default `std::thread::hardware_concurrency() - 1`), Chase-Lev SPMC deque per worker, Vyukov MPSC global injector.
- `cd::concurrency::WorkerCell` — cache-line-isolated 64 B (deque head/tail, RNG, stats) → no false sharing.
- `cd::concurrency::task<T>` — C++20 coroutine task; promise type with `stop_token` propagation (DtForHil ADR-017 pattern: `requires { promise.stopToken }`).
- **Senders/receivers internally** (P2300-style algebra): `cd::concurrency::detail::sender` is the composition primitive; users see `co_await` / `task<T>`. Future flip to `std::execution` (C++26) is a typedef change.
- **Deterministic mode** (`cd::concurrency::deterministic_executor`) — peer to default, not debug toy: single-threaded, seeded RNG, replayable wake order. Drives rollback netcode (ADR-011) and reproducible tests.

### Render Thread Topology (T9.Q2)

- **Default**: N-buffered pipelining — simulation builds frame N's state, multiple worker threads record secondary command buffers for frame N+1 in parallel, GPU presents frame N+2.
- **VR low-latency profile** (`cd::render::low_latency_profile`): collapse sim→record→submit within one frame; late-latch view matrix at vsync (Oculus phase-sync pattern); target motion-to-photon < 20 ms on Quest 3 / Vision Pro.
- **Buffer rotation**: prevents read/write hazards without per-resource locks.

### Synchronization Primitives (T9.Q3)

`cd::concurrency` exposes:
- `atomic_helpers` — `std::atomic<T>` wrappers with explicit memory-order helpers (`acquire_release`, `seq_cst` only; `relaxed` opt-in).
- `mutex`, `shared_mutex` — debug-mode contention tracking.
- `spin_lock` — `_mm_pause` / `__yield` calibrated; sub-µs critical sections; futex fallback after N spins.
- `spsc_queue<T>` — Vyukov SPSC bounded ring; cache-line head/tail isolation.
- `mpmc_queue<T>` — Vyukov bounded; unbounded variant uses **hazard pointers** (Michael 2004) for reclamation (rejecting EBR — memory blow-up risk in long editor sessions).
- `RingBuffer<T,N>` — DtForHil ADR-017 P0 port; SPSC, batch ops.
- `DataChannel<T>` — DtForHil ADR-017 P0 port; named SPSC for audio/telemetry/GPU upload.

Worker sleep uses `std::atomic::wait/notify_one` (C++20 futex/`WaitOnAddress`) — replaces DtForHil's `condition_variable` for worker wait (DtForHil ADR-017 P1 upgrade).

### Async I/O (T9.Q4 = D)

`cd::io::context` with platform-specific backends behind unified coroutine-awaitable surface:

```cpp
co_await io.read(fd, span)         -> ReadAwaitable
co_await io.write(fd, span)        -> WriteAwaitable
co_await io.connect(endpoint)      -> ConnectAwaitable
co_await io.watch(path)            -> FileWatchAwaitable
co_await io.read_file_async(path)  -> high-level asset stream
```

- **Linux**: `io_uring` 5.6+ baseline; SQPOLL + registered buffers for zero copy.
- **Windows**: IOCP; `SetFileCompletionNotificationModes(FILE_SKIP_*)` fast-path.
- **macOS**: kqueue (`EVFILT_READ/WRITE/VNODE`).
- All backends share `io_op` (cache-line padded) with tag dispatch; sender/receiver compatible.
- Hot-path budget: submit < 200 ns; completion dispatch < 500 ns; asset stream target 2 GB/s NVMe.

### SIMD DSL (T9.Q5 = C, gerçek değerlendirme: wrap)

**Decision overridden**: Custom SIMD DSL **reddedildi** (T9.Q5=C kullanıcı tercihi). Justification:
- 6-12 engineer-months to reach Highway parity on x86_64+ARM64 alone.
- SVE2 / RVV moving target.
- Highway (Google) ships in Chromium, JPEG XL, glibc — production-tested.

**Replacement decision**: **Wrap Google Highway** under `cd::simd`:
- Façade `cd::simd::vec<T,N>` mapping to `hwy::Vec<D>` (namespace-stable engine API).
- Kernel-dispatch macros: `CD_SIMD_KERNEL(Name) { ... }` → `HWY_BEFORE_NAMESPACE/AFTER_NAMESPACE` + dynamic dispatch table.
- Engine-specific kernels (transform skinning, frustum cull, AABB intersection, audio mix) hand-tuned on top.
- ISPC reserved for hero kernels (ray–box, sparse voxel) where SPMD model genuinely superior.
- Cross-arch: SSE2 baseline; AVX2/AVX-512 (server/desktop); NEON (mobile/Apple); SVE (Graviton/Ampere); RVV 1.0.

This decision **deviates from user T9.Q5=C** — flagged for user review. **Replace-Ready (D1)**: `cd::simd::native::*` namespace reserved for future custom backend if Highway dependency becomes friction (Phase 4+).

## Reddedilen Alternatifler

| Alternatif | Sebep |
|---|---|
| Pure-fiber (Naughty Dog clone) | Stack memory (160×64 KB), ABI risk on ARM64/Windows, debugger pain |
| Pure-callback executor | Poor composability, future-incompatible with `std::execution` |
| `std::experimental::simd` only | SVE/RVV/AVX-512 gaps; compiler vendor coverage uneven |
| Custom SIMD DSL from scratch | 6-12 engineer-month sink vs Highway parity; T9.Q5=C reversed |
| Asio/libuv async I/O | Heavy dep; custom layer fits engine allocator/error model |
| Actor model engine-wide | Throughput cost; adopt selectively (audio mixer, asset I/O daemon) |
| Singleton job system (DtForHil pattern) | Multi-executor + embedded use cases blocked; CLAUDE.md §7 ihlali |
| Epoch-based reclamation | Memory blow-up risk in long-running editor sessions |
| Single render thread | Vulkan/D3D12 multi-thread record capability waste |

## Sonuçlar

**Pozitif**: beats Naughty Dog on coroutine ergonomics; beats Bevy on C++ interop; beats Filament on determinism; VR latency budgets supported; unified I/O surface; cross-arch SIMD with low maintenance.

**Negatif / Risk**: three-backend async I/O conformance testing burden; Highway dependency (mitigation: pinned vendored, abstracted); coroutine ABI quirks across MSVC/clang/gcc need conformance tests; Chase-Lev correctness review.

**Replace-Ready**:
- Highway → custom SIMD backend (Phase 4+ optional, only if friction)
- OS fibers escape hatch (`CD_JOB_FIBER=ON`) — opt-in if coroutines insufficient for legacy code (Sprint 6 prototyp gate)

**DtForHil Salvage**:
- 7 pattern miras: stop_token propagation, TimerQueue, RAII subscription, deadline scope, aliveToken shared_ptr<atomic<bool>>, `std::jthread`+stop_source, `IClock` injection.
- 6 upgrade: mutex deque → lock-free Chase-Lev, `std::function` → 64 B SBO `move_only_function`, singleton → non-singleton + thread-local default, condition_variable → `atomic::wait`, no NUMA → topology-aware, no fiber → optional via `CD_JOB_FIBER`.

## Açık Sorular

| ID | Soru | Çözüm noktası |
|---|---|---|
| Q1 | Fiber escape hatch gerekli mi? | Sprint 6 prototyp gate |
| Q2 | Highway RVV uyumu RISC-V toolchain matrisimizle? | CI matrix doğrulaması |
| Q3 | Deterministic mode altında `cd::io::context` davranışı? | ADR-016 konusu — synthetic clock + mock io |
| Q4 | NUMA-aware allocator job system'le bağlanma? | ADR-005 D cross-sprint koordinasyon |
| Q5 | Render graph + frame-graph topology kontratı (ADR-001) | Sprint 2 conform |
| Q6 | `std::execution` (C++26) binary uyumu? | Compiler probe + wrapper sender'lar |
| Q7 | Coroutine HALO (Heap Allocation eLision) güvenilirliği? | Custom `cd::mem::coro_arena` zorunlu |

## Cross-Cutting

- **ADR-001 (RHI)**: parallel command recording; `cd::concurrency::Executor` ABI stable; `record_on(executor)` API.
- **ADR-004 (ECS)**: parallel system scheduler consumes `cd::concurrency::scheduler_hint`; deterministic mode flag respected.
- **ADR-005 (Foundation)**: tri-clock, NUMA arena, coroutine frame allocator hook.
- **ADR-006 (Asset)**: async streaming via `cd::io::context::read_file_async`; hot-reload via `co_await io.watch(path)`.
- **ADR-007 (Audio)**: dedicated actor (high-priority worker) with MPSC inbox; first concrete consumer of actor pattern carve-out.
- **ADR-011 (Networking)**: rollback netcode leverages `deterministic_executor`; same wake order = same simulation = same hash.

## Kanıt

- Gyrling — Parallelizing The Naughty Dog Engine Using Fibers (GDC 2015 PDF)
- Vyukov MPMC queue: 1024cores.net
- Chase-Lev deque: Lê, Pop, Cohen, Nardelli (PPoPP 2013) — **STUB**
- Hazard Pointers: Michael (IEEE TPDS 2004) — **STUB**
- Axboe — Efficient IO with io_uring (LWN 2019) — **STUB**
- libunifex senders/receivers: https://github.com/facebookexperimental/libunifex
- Google Highway: https://github.com/google/highway
- DtForHil pattern files: `01_Code/libraries/common/threading/threadpool.hpp`, `coroutine/*`, `timing/timerqueue.hpp`, `event/asynceventbus.hpp`, `infrastructure/realtime/deadlinemonitor.hpp`
