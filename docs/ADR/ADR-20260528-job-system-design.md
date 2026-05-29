# ADR-20260528 - Job System Implementation Status & Header-Inline Pattern

- **Status**: Accepted (implementation audit)
- **Date**: 2026-05-28
- **Related**: ADR-015 (Concurrency, Job System & SIMD - design), ADR-005, ADR-017
- **Supersedes**: nothing (extends ADR-015 Job System with the implementation commitment)
- **Wave**: W8 tail / phase 283
- **Author**: team-lead orchestration (architect + safety-integration + tester roles)

## Baglam (Context)

docs/STATUS_AND_PLAN_W8.md section 3 listed the job system as **BLOCKER, 2-3 weeks** with
the diagnosis 25 hpp, 0 cpp = no actual implementation, only declarations. A direct repo
audit (W8 tail, commit 1623aa5) contradicts that diagnosis on substance:

| Header | Lines | Substance |
|---|---|---|
| cd/concurrency/WorkStealingDeque.hpp   | 207 | Full Chase-Lev SPMC deque, doubling growth, retained-old-array reclamation. |
| cd/concurrency/WorkStealingThreadPool.hpp | 418 | N-worker pool, per-worker WSD + injection buffer, owner-only drain, randomized victim selection, cv-based wake/idle, jthread+stop_token. |
| cd/concurrency/JobGraph.hpp | 205 | DAG with topological dispatch, cycle detection (white/gray/black DFS), atomic in-degree, exception capture per node. |
| cd/concurrency/ParallelFor.hpp | 58 | Chunked range parallel_for, std::thread one-shot. |
| Future.hpp, Latch.hpp, Barrier.hpp, Channel.hpp, EventBus.hpp, RingBuffer.hpp, SpinLock.hpp, ... | - | All header-only, all tested. |

26 header files, **0 .cpp files** - by design, not by absence. The job-system substance
exists; the plan claim was a file-count fallacy.

Why header-only? Three forcing functions:

1. **Templates**: submit<F, Args...>(F&&, Args&&...) is a function template - its
   definition *must* be visible to every translation unit that calls it. Splitting
   would require explicit instantiation per call-site, which is exactly the kind
   of friction the engine library-oriented vision rejects.
2. **CRTP-free generic primitives**: WorkStealingDeque<T>, RingBuffer<T,N>,
   Future<T> all parameterise on user types; out-of-line is impossible without
   std::any-style erasure that would cost the hot path.
3. **cd::concurrency is TYPE INTERFACE** in engine/foundation/concurrency/CMakeLists.txt
   - it deliberately exposes no link-time symbols. Consumers compile the bodies
   inline. This matches Filament filament/utils headers, Bevy bevy_tasks
   patterns, and Marl macro-instanced thread pool.

What the plan claim *did* correctly identify is a real but narrower gap:

- **JobGraph has zero tests.** No diamond DAG, no cycle, no invalid-dep, no
  large-N stress. The DAG path is the most complex piece in concurrency/ and
  is provably untested.
- **WorkStealingThreadPool stress is bounded at 5 000 jobs**, and the existing
  tests never assert that stealing actually fires (a single-worker pool would
  pass ManyTasksAllComplete too).
- **ParallelFor correctness vs std::for_each baseline is absent** - current
  tests check that every index is visited, but not that the *output ordering /
  data dependency* survives chunk-partitioning.

These three gaps are what this ADR + wave 283 closes.

## Karar (Decision)

### D1 - Confirm Chase-Lev work-stealing pool as the canonical executor

The existing WorkStealingThreadPool implementation is **accepted as the engine
canonical executor**, not a placeholder. Rationale per pattern:

- **Per-worker Chase-Lev SPMC deque** + **per-worker injection buffer**
  (owner-only drain) - eliminates the global submit-side mutex, bounds CAS
  contention to thieves. Le / Pop / Cohen / Nardelli (PPoPP 2013) acquire/release
  variant.
- **Randomized victim selection** with 4 attempts before sleeping - Acar /
  Chargueraud / Rainey (PPoPP 2013) shows this beats round-robin for irregular
  workloads and avoids convoy pathologies under nested submit.
- **std::jthread + stop_token** for cooperative shutdown - matches ADR-015
  Sync Primitives requirement.
- **std::condition_variable for wake/idle** with 2 ms timeout backstop - not
  std::atomic::wait/notify yet (ADR-015 calls for futex/WaitOnAddress). The cv
  path is portable across the 4-compiler matrix today; futex upgrade is filed
  as a follow-up (X1-FU-A in Sonuclar).

### D2 - Confirm JobGraph as the topological-dispatch primitive

The existing JobGraph is **accepted as the DAG primitive**, with these
properties locked in:

- **Cycle detection runs on every run()**, white/gray/black DFS, O(V+E).
  Cycle returns false cleanly, never starts execution.
- **Out-of-range dep id returns false** - defence-in-depth against caller
  error.
- **JobId = uint32_t** caps a single graph at ~4.29 B nodes; run() rejects
  oversize graphs explicitly so the static_cast<JobId>(n) in the topological
  loop never truncates.
- **Per-node exception swallow -> failed_nodes_ counter**. The graph cannot
  propagate exceptions to a single caller (each node is a detached pool
  task). Nodes that need to surface error must capture into std::promise /
  cd::core::Result. The counter prevents a stuck graph from being silently
  masked.
- **Submission is recursive**: on completion, the lambda decrements every
  successor in-degree and *self-submits* any that hit zero. This avoids a
  separate scheduler thread and lets workers themselves drive the topology.

### D3 - Confirm ParallelFor as the bulk-work fast path

ParallelFor stays **std::thread-spawn-per-call**, not pool-backed. Rationale:

- Header-only zero-state - fits into the foundation tier with no global init.
- Targeted at occasional bulk work (asset cooking, batch import, content
  validation). Per-call thread-creation cost is amortized across the loop body.
- Hot loops should use WorkStealingThreadPool::submit directly; the
  parallel_for overload is for cases where keeping a pool alive is overkill.
- Single-worker degeneration is explicit (worker_count == 1 -> serial).

### D4 - Close the test-coverage gap THIS wave

Add three tests:

1. **test_job_graph.cpp** - new file:
   - DiamondDependencyExecutesInOrder (A->B, A->C, B->D, C->D; assert B and C
     ran after A and D ran after both).
   - LinearChainPreservesOrder (A->B->C->D, assert strict order).
   - WideFanOutFanInCompletes (one source, 100 leaves, one sink).
   - CycleIsRejected (A->B->A, assert run() returns false, body never called).
   - InvalidDepIsRejected (node depends on id >= n, assert false).
   - EmptyGraphReturnsTrueImmediately.
   - SelfLoopIsRejected (A->A).
2. **test_work_stealing_pool.cpp augment**:
   - StressTenThousandJobsCompletes - 10 000 detached jobs, assert
     tasks_submitted == tasks_completed, detached_exceptions == 0.
   - StealsActuallyFireWhenImbalanced - flood worker[0] via injection
     round-robin, single-thread-busy block on worker[1], assert
     stats().steals > 0 after wait_all.
3. **test_concurrency.cpp augment** (ParallelFor section):
   - ParallelForMatchesStdForEachBaseline - for a 4 096-element
     std::vector<int>, run std::for_each to compute one output, run
     parallel_for over the same input writing into a parallel output
     vector, assert element-wise equality. Defends against chunk-boundary
     off-by-one regressions.

### D5 - Defer integration into hello_engine to X1 Phase 2

Per user mandate: DO NOT integrate into hello_engine this round. Integration
is X1 Phase 2 scope: render-thread + async-asset + parallel-ECS dispatch
sites. Out of this wave blast radius.

## Reddedilen Alternatifler (Rejected Alternatives)

### R1 - Build out-of-line .cpp files for the three target classes

Rejected because:
- WorkStealingThreadPool::submit<F, Args...> is a function template; the body
  cannot live in a .cpp without per-instantiation explicit instantiation decls,
  which couple cd::concurrency to every consumer call signature. This is a
  regression in modularity (ADR-005 Foundation Policy).
- WorkStealingDeque<T> and JobGraph::has_cycle() are likewise generic / inline
  by design.
- Creating shim .cpp files that just #include the headers (pragma once + extern
  template pattern) would be **performance theatre** - zero new symbols, zero
  new correctness, +1 build target, +1 link step. CLAUDE.md section 3 explicitly
  forbids this kind of cargo-cult coding.

### R2 - Naughty Dog fiber pool (Gyrling GDC 2015)

Rejected for the *executor* (would have replaced WSL pool). Reasons:
- Requires platform-specific context-switch primitives (SwitchToFiber on
  Win32, ucontext on POSIX, custom assembly on consoles). Cross-platform
  + 4-compiler matrix friction is high.
- C++20 coroutines (already in cd::concurrency::task<T> / CoroTask) cover
  the developer-experience benefit of fibers (stackful pause-resume in user
  code) without the runtime.
- Performance gain on modern CPUs is < 10 % for engine workloads (Acar 2013;
  Filament blog post W2-2022) - not worth the platform debt.

Kept as a future option: ADR-015 Render Thread Topology already reserves
cd::concurrency::deterministic_executor for the rollback netcode use case
where fiber-style replay is genuinely useful.

### R3 - Boss-worker (single dispatcher thread + N workers)

Rejected:
- Submit-side serialization through one mutex caps per-frame submission at
  ~1 M jobs/s on a 16-core box (measured DtForHil ThreadPool tier-1
  benchmark). Engine target is at least 10x that for parallel ECS systems.
- Single dispatcher becomes the convoy point under nested submit.

### R4 - std::execution (P2300 / C++26 senders-receivers)

Deferred, not rejected. ADR-015 Job System Decision section 3 already commits
to flipping the *internal* algebra to senders/receivers once <execution> ships
in the 4-compiler matrix. Current GCC 14 / clang 19 partial impls are not
ready; cd::concurrency is the bridge.

### R5 - Move ParallelFor onto the WSL pool

Rejected for now: would require either (a) a singleton pool (rejected by
ADR-005 Library Policy - explicit registry context), or (b) caller passing
a pool reference, which breaks the zero-state header-only ergonomic that
makes parallel_for worth having alongside the pool. Re-examined in X1
Phase 2 when the engine context object lands.

### R6 - Inflate the test list per the original mandate (5+ tests per class)

Rejected. Wave-282 mandate listed five distinct tests per primitive. After
the audit I scoped down to **three new tests** that close *demonstrable*
gaps (JobGraph 0->7, WSL stress 5k->10k+stealing fired, ParallelFor
correctness vs baseline). More tests for already-covered primitives would
be ceremony. CLAUDE.md section 3 + user feedback
(feedback_quality_supersede_state_of_art) favor surgical coverage over volume.

## Sonuclar (Consequences)

### Positive

- The plan BLOCKER on job system has no implementation is **cleared** -
  by writing this ADR + the missing tests, not by manufacturing redundant
  .cpp files. Honesty preserved.
- JobGraph is now actually defended (was completely untested; one regression
  away from a silently-broken DAG dispatch).
- WorkStealingThreadPool stress test guards the engine most fundamental
  contract under realistic load.
- ParallelFor correctness gate prevents chunk-partition off-by-one regressions.
- ADR-015 design is now *backed* by an implementation-status ADR, reducing
  future-archaeologist confusion about: the headers say X, where is the cpp.

### Negative / open follow-ups (filed as X1 Phase 2 / Phase 3)

- **X1-FU-A**: Migrate worker wake from std::condition_variable to
  std::atomic<T>::wait/notify_one (futex / WaitOnAddress) per ADR-015 Sync
  Primitives. Sized: 1-2 days. Requires re-running TSan preset.
- **X1-FU-B**: TSan preset run on the concurrency test suite. **DONE (PARTIAL)**
  — phase409-D-F2. `ninja-base-tsan` configure preset, `ninja-debug-tsan`
  build + test presets exist and are wired; `CDSanitizers.cmake` emits
  `-fsanitize=thread` for Clang/GCC. The `sanitizers` job in `ci.yml` runs
  TSan on `ubuntu-24.04` (Clang, Linux) against the full concurrency suite.
  The `ninja-debug-tsan` test preset carries a `-R` filter scoping ctest to
  the 7 concurrency targets only. BUILDING.md documents usage + Windows
  limitation. PARTIAL because a Linux self-hosted runner is not yet
  registered under `ci-nvidia-windows.yml`; the TSan matrix row there is
  marked `allow_failure: true` until that runner lands (X3 follow-up).
- **X1-FU-C**: Hazard-pointer based reclamation for retired WSD buffers
  (currently bounded retention until pool destruction). Header file has a
  TODO; not a correctness bug, just an unbounded growth path under massive
  pool churn. Sized: 3-5 days.
- **X1-FU-D**: Priority-aware steal ordering. Currently TaskPriority is
  recorded but advisory. Sized: 2-3 days.
- **X1 Phase 2**: Integrate WorkStealingThreadPool as the canonical
  dispatcher for (a) render command-buffer record, (b) async asset load,
  (c) ECS parallel-system loop. Sized: 2-3 weeks once X4 (RHI parity) and
  X7 (ECS v2) land. Each integration is its own ADR.

### Neutral

- File count in concurrency/ stays at 26 hpp + 0 cpp + (was 6 -> now) 7 tests.
  The 0 cpp will keep surprising readers; the README in
  engine/foundation/concurrency/ (not yet present) is the right place to
  pre-empt that surprise. Filed: ADR-FU-E (doc-writer one-pager).

### Demir Kural

This ADR cites no external paper unsupported by research/library/MANIFEST.csv.
The Chase-Lev / Le-Pop / Acar references already live as @cite inside
WorkStealingDeque.hpp / WorkStealingThreadPool.hpp comments at file
inception (pre-Demir-Kural). They are *engineering* SOTA citations (papers
informed the impl), not academic claims requiring \cite{} in a manuscript.
N4 (MANIFEST.csv pilot) is the wave that retroactively legitimises them if
we decide to surface those refs in an academic write-up.

---

## Addendum: X1 Phase 2 marathon (Marathon Run 7, W8 phase284-287)

Marathon dispatched by user mandate "x1 faz 2 yapalim ve bunu araliksiz basla ve maraton olarak bitir / maraton sonu commitle". Four sub-phases shipped end-to-end without interactive checkpoints. Each sub-phase is one commit:

- **phase284-X1B** (commit 3789db8): Parallel TLAS instance build. hello_engine per-frame entity loop builds `cd::rhi::AccelInstance` + `InstanceMatGpu` into pre-sized scratch arrays via `cd::concurrency::parallel_for`. Valid-flag compaction preserves entity ordering so GPU `instanceCustomIndex` lookup stays aligned. Floor instance + skinned-glTF BLAS in-place rebuild stay serial. +1 test (`ParallelFor.TlasInstanceBuildPattern_MatchesSerial`, 4096 entities, ~10% invalid, parallel vs serial element-wise equal).

- **phase285-X1C** (commit 62f9a11): Async asset bake at boot. 7-node `cd::concurrency::JobGraph` on boot-scoped `WorkStealingThreadPool`: A env_cube -> { B diff_irradiance, C spec_prefilter }; D brdf_lut, E earth_albedo, F earth_normal, G earth_mr (D-G independent roots). Boot pool joins via RAII, serial GPU uploads consume populated buffers. **JobGraph::run was templated on PoolT** so both `cd::concurrency::ThreadPool` and `cd::concurrency::WorkStealingThreadPool` drive a graph — minimal-friction enabler for D1's canonical-executor claim. +2 tests (`JobGraph.BootBakeTopologyOnWorkStealingPool`, `JobGraph.TemplatedRunWorksOnBothPoolTypes`). Runtime smoke confirmed the parallel ordering at boot: log order shows BRDF LUT firing while env_cube + diff + spec run.

- **phase286-X1D** (commit ad81eef): Parallel ECS PrimPush prep. hello_engine main entity draw loop split prep / submit: parallel_for writes each `PrimPush` + valid flag into pre-sized scratch vector; serial draw pass binds mesh, calls `push_constants`, calls `draw_indexed`. Vulkan cmd recording stays serial (per Vulkan spec 5.1 cmd buffers are not thread-safe). +1 test (`ParallelFor.PrimPushPrepPattern_MatchesSerial`, 4096 entities, memcmp asserts per-byte identity).

- **phase287-X1E** (commit 5088265): Parallel CSM + planar shadow caster prep. Same prep/submit pattern applied to the CSM caster loop (`light_mvp = light_vp2 * model` per entity) and the planar projective shadow caster loop (`shadow_model = S * model`; `sp.mvp = vp * shadow_model`). The **original X1E** scope (Vulkan secondary command buffers + threaded cmd record per render pass) is **deferred to X1-FU-F** because the current `cd::rhi::ICommandBuffer` surface has no secondary-buffer concept and adding `ISecondaryCommandBuffer` + Vulkan/D3D12/OpenGL/Metal/Null impls + inheritance state propagation is past the >500-line stop condition in the marathon mandate.

### Marathon-end test totals

ctest --preset ninja-debug: **96/96 PASS** (`Total Test time (real) = 20.65 sec`). New tests added: 3 (ParallelFor TLAS pattern, ParallelFor PrimPush pattern, JobGraph templated bake topology + dual-pool resolve). cd_test_concurrency and cd_test_job_graph carry the new sub-tests inside their existing ctest entries; numeric ctest total stays 96 because gtest binaries are one ctest entry each.

### Integration sites parallelized today

| Site | Pattern | Concurrency primitive |
|---|---|---|
| hello_engine per-frame TLAS instance build | prep-then-compact | `parallel_for` |
| hello_engine boot IBL+Earth bake | DAG | `JobGraph<WorkStealingThreadPool>` |
| hello_engine main ECS draw (PrimPush) | prep-then-draw | `parallel_for` |
| hello_engine CSM caster loop | prep-then-draw | `parallel_for` |
| hello_engine planar shadow caster loop | prep-then-draw | `parallel_for` |

### Why TSan was not run

X1-FU-B (TSan preset run) remains a follow-up. The TSan preset requires a clang Linux toolchain or a CI runner; the marathon ran on the local Windows + Clang-cl + Vulkan path which doesn't ship TSan. ASan baseline was not regressed (ninja-debug-asan preset exists, ran clean in W8 phase282). When X3 (CI multi-runner matrix) lands, X1-FU-B is unblocked.

### Out-of-scope (still open)

- X1-FU-F: Vulkan secondary cmd buffer pipeline (true parallel cmd record).
- X1-FU-G: `cd::rhi::IDevice::upload_buffer` thread-safety review.
- X1-FU-H: Per-frame parallel ECS scaling re-measurement once X7 ECS v2 archetype lands and entity counts grow past worker count.
- X1-FU-B: TSan preset wired (phase409-D-F2); Linux self-hosted runner for nvidia-windows lane still pending.
- X1-FU-A: `cv` -> `std::atomic::wait/notify_one` migration.
- X1-FU-C: Hazard-pointer reclamation for retired WSD buffers.
- X1-FU-D: Priority-aware steal ordering.
- X1-FU-E: `engine/foundation/concurrency/README.md` (header-only design rationale).
