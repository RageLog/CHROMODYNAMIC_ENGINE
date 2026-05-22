# ADR-018: Phase 2 Foundation Closure

**Status:** Accepted
**Date:** 2026-05-18
**Supersedes:** —
**Related:** ADR-005 (Foundation policy bundle), ADR-015 (Concurrency / job system), ADR-017 (DtForHil pattern salvage)

## Context

Phase 1 produced 17 ADRs covering the engine's intended architecture. Phase 2
was scoped to bring the **foundation tier** (everything beneath RHI/scene) to
a state where the rest of the engine can be built on top without re-litigating
low-level choices. The sub-sprints S2.0 – S2.10 in ADR-017 enumerated the
salvageable patterns from DtForHil and the modernisations required.

This ADR closes Phase 2 with the foundation tier shipped, tested, and proven
to compose into a single integration surface (`cd::runtime::EngineContext`).

## Decision

Treat Phase 2 as complete when **all 14 foundation libraries** below ship
with green tests under the project's enforced warnings + sanitizers, and a
sample exercises them end-to-end through `EngineContext`.

### Libraries shipped

| # | Library | Type | Key types | Sprint |
|---|---|---|---|---|
| 1 | `cd::core` | STATIC | Result, ErrorCode, Handle, HandleStore, Defines, Definitions, Compat, CVar | S2.0–S2.4 |
| 2 | `cd::mem` | STATIC | IAllocator, SystemAlloc, Linear, Pool, TypedPool, Page, Tracking, PmrAdapter | S2.1.a |
| 3 | `cd::concurrency` | INTERFACE | Atomics, RingBuffer, DataChannel, SpinLock, Job, CoroTask, Task<T>, ThreadPool, JobGraph, **DeterministicExecutor**, **WorkStealingDeque**, **WorkStealingThreadPool**, **HazardDomain** | S2.1.c, S2.3, S2.5 |
| 4 | `cd::time` | STATIC | Types, IClock, SteadyClock, HiResClock, SimClock, FramePacer, TimerQueue | S2.1.d |
| 5 | `cd::platform` | STATIC | SignalHandler | S2.1.e |
| 6 | `cd::diag` | STATIC | CrashReporter, **DeadlineMonitor** | S2.1.e, S2.4 |
| 7 | `cd::log` | INTERFACE | LogLevel, LogRecord, Format, ILogger, ConsoleLogger, Service, **AuditTrail** | S2.2, S2.4 |
| 8 | `cd::events` | INTERFACE | ScopedConnection, EventBus, EventRecorder | S2.2 |
| 9 | `cd::plugin` | STATIC | IPlugin (kAbiVersion=1, `cd_plugin_create`), Loader | S2.4 |
| 10 | `cd::io` | INTERFACE | Endian, BinaryWriter, BinaryReader, BitWriter, BitReader, LengthPrefixWriter, LengthPrefixDecoder | S2.6 |
| 11 | `cd::math` | INTERFACE | Constants, scalar Functions, Vec2/3/4, Mat3/4, Quat, Transform, look_at, perspective/ortho, slerp | S2.7 |
| 12 | `cd::config` | INTERFACE | binary CVarRegistry persistence (magic='CVAR', canonical-order) | S2.8 |
| 13 | `cd::vfs` | STATIC | IFileSource, MemorySource, FilesystemSource, VirtualFileSystem (overlay layers) | S2.9 |
| 14 | `cd::runtime` | STATIC | EngineContext (composition root: CVars, VFS, watchdog, thread pool, logger) | S2.10 |

### Test posture

- **ctest binaries:** 19 (`cd_test_core` … `cd_test_runtime`), all green.
- **Per-library policy:** every public surface has at least one positive test
  AND one negative / boundary test. Concurrency primitives additionally
  carry stress tests (4-thief stealing, MPSC ring stress, hazard-ptr
  reclamation under concurrent reader/writer load).
- **Determinism:** `cd::config::save` emits lexicographically-sorted output so
  byte-equal blobs imply equal CVar state regardless of insertion order.
- **No flaky tests:** timing-dependent stealing observation was removed (S2.5
  follow-up); pool correctness is verified via task-count conservation.

### Integration sample

`samples/hello_runtime` builds an `EngineContext`, sets CVars, persists them
through `cd::config` into a `MemorySource`-backed VFS layer, reads them back,
and dispatches 256 transform-application jobs across the `WorkStealingThread`
`Pool`. This is the canonical pattern game code will follow.

## Consequences

### Positive

- Foundation tier is composable: any subsystem above can pick services from
  `EngineContext` rather than threading singletons or invoking globals.
- All libraries can be consumed standalone (each has its own headers,
  tests, and CMake target) — fulfils the user's library-oriented mandate.
- Job system has all three execution modes covered: lock-based priority
  pool (`ThreadPool`), single-threaded replay (`DeterministicExecutor`),
  and work-stealing scalable pool (`WorkStealingThreadPool`).
- Hazard-pointer reclamation primitive exists as a building block for
  upcoming lock-free containers (asset registry, resource cache).

### Negative / Deferred

- **`cd::asset::SchemaRegistry`** (typed reflection / validation for
  materials and event payloads) was deferred from S2.4 P2: it belongs in
  the `cd::asset` library which Phase 3 introduces.
- **`cd::vfs::FilesystemSource`** uses synchronous blocking reads. An
  async / overlapped-IO source will be added when the asset pipeline (Phase
  3, S3.2+) requires it.
- **`ISocket`** (network transport abstraction) deferred to its own sprint
  with platform-abstraction layer (winsock vs. POSIX). `LengthPrefixDecoder`
  is already in place to chunk a byte stream into messages once a transport
  exists.
- **HazardDomain → WorkStealingDeque buffer reclamation** is not yet wired.
  The deque retains old buffers across grows (log₂(N) bound for an N-sized
  high-water mark). Acceptable for v1; wiring is a follow-up.
- **Priority-aware steal ordering** in `WorkStealingThreadPool` is a
  follow-up. Today priority is advisory; jobs run LIFO from the owner and
  FIFO when stolen.
- **SIMD vector/matrix** specialisations: none. Scalar `Vec<T,N>` /
  `Mat<T,N>` are designed so a SIMD specialisation can drop in behind the
  same value-semantic interface without callers changing.

### Risk profile

The foundation tier compiles under `-Wall -Wextra -Wpedantic -Werror`
plus `-Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wconversion`
`-Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2`
`-Wmissing-declarations` on clang 21.1, MSVC `cl`-equivalent, and is
exercised by 19 test binaries. No `--no-verify` bypasses, no warnings
disabled at scope.

## Forward plan

Phase 3 (Render tier) begins with `cd::rhi` — RHI abstraction over
Vulkan/D3D12. Bring-up order from ADR-017:

1. **S3.0** — RHI header types (Format, TextureUsage, ResourceState).
2. **S3.1** — Vulkan device bootstrap, swapchain, command pool.
3. **S3.2** — async asset I/O (extending `cd::vfs` and `cd::io`).
4. **S3.3** — `cd::asset` + `SchemaRegistry` (the S2.4 deferral).
5. **S3.4** — material / shader pipeline.

`cd::runtime::EngineContext` will gain `IRhiDevice* rhi()` once `cd::rhi`
ships its dependency-injected device handle. The composition pattern
established in Phase 2 (DI through `EngineContext` rather than singletons)
must continue.

## Reviewed-by

— Cemal Tatlı, 2026-05-18
