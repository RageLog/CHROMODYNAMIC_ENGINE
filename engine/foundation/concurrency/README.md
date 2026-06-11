# cd::concurrency

**Purpose**: tier-0 job system + parallel-for + work-stealing thread pool. The runtime substrate for every CPU-parallel boot task (X1C IBL bake graph) and per-frame parallel pass (X1B TLAS instance build).

**Namespace**: `cd::concurrency`.

**Headers**: `cd/concurrency/JobGraph.hpp`, `cd/concurrency/ParallelFor.hpp`, `cd/concurrency/WorkStealingThreadPool.hpp`, `cd/concurrency/ThreadPool.hpp`, `cd/concurrency/Future.hpp`, `cd/concurrency/JobToken.hpp`.

**Primary types**:
- `cd::concurrency::JobGraph` — DAG of dependent jobs with explicit dependency edges. `add(callable, {dep_a, dep_b, ...})` returns a NodeId; `run(pool)` executes the DAG and reports `failed_nodes()`. Used by the X1C boot bake graph.
- `cd::concurrency::WorkStealingThreadPool` — N-worker work-stealing pool; default N = `std::thread::hardware_concurrency()`.
- `cd::concurrency::ThreadPool` — older simpler pool kept for niche call sites (incl. the coroutine `spawn_detached` path); new code should pick WorkStealingThreadPool.
- `cd::concurrency::parallel_for(begin, end, callable)` — closed range parallel-for atop WorkStealingThreadPool.
- `cd::concurrency::Future<T>` / `cd::concurrency::JobToken` — thin sync primitives.

**Usage example**:
```cpp
#include <cd/concurrency/JobGraph.hpp>
#include <cd/concurrency/WorkStealingThreadPool.hpp>

cd::concurrency::WorkStealingThreadPool pool { 0 };  // 0 = hardware_concurrency
cd::concurrency::JobGraph graph;
const auto a = graph.add([]{ bake_env_cube(); });
const auto b = graph.add([]{ bake_diffuse(); },  { a });
const auto c = graph.add([]{ bake_specular(); }, { a });
const bool ok = graph.run(pool);
```

## Design rationale (X1-FU-E)

**Why header-inline?** Every type here sits on hot paths (per-frame
TLAS fan-out, parallel-for over instance arrays) where call overhead
and missed inlining are measurable; header-inline also keeps the
library consumable standalone (modularity rule: each lib builds + tests
in isolation) with zero link-order coupling into `cd::core`. The small
TUs that do exist hold only cold-path code.

**Why two pools?** `WorkStealingThreadPool` is the default substrate
(per-worker deques, steal-on-empty — scales for many small jobs).
`ThreadPool` survives for the coroutine `spawn_detached` surface and
niche FIFO consumers; collapsing the two is deliberately deferred until
X1-FU-C (hazard-pointer reclamation) settles the WSD's memory story.

**Exception policy**: jobs are `noexcept`-expected; a throwing job is a
defect. JobGraph records failures in `failed_nodes()` instead of
propagating, because a half-executed DAG must stay inspectable.

## Invariants (paid for in deadlocks — keep them true)

1. **Exactly-once submission** (phase 1050 RCA): the JobGraph kick-off
   scan reads the IMMUTABLE topology (`dependencies.empty()`), never
   the mutable `in_degree` counters that workers decrement
   concurrently — the mutable-read version double-submitted nodes and
   deadlocked `run()`. Stress tests assert per-node execution counts,
   not just completion.
2. **Coroutine frame ownership** (phase 1052 RCA): `spawn_detached`
   destroys the coroutine frame at `handle.done()` on the WORKER side,
   and rolls the frame back if the enqueue is rejected (`enqueue`
   returns bool). `release()` without a matching `destroy()` is a
   frame leak; there is exactly ONE resumer (the worker) by design —
   revisit the ownership model before adding off-worker awaiters (note
   in `ThreadPool.hpp`).
3. **Notify under the mutex** (phase 1052): completion notification
   happens while holding `idle_mutex_`, and `in_flight_` rises BEFORE
   `queued_` falls, so an observer can never see the pool "idle"
   between the two updates.
4. **No `sleep_for` in tests** (CLAUDE.md): all synchronization in
   tests is condition-variable/event based; every test target carries a
   ctest `TIMEOUT` (default 120 s via `cd_add_test`) so a regression
   deadlock fails fast instead of hanging CI for hours.

**Test command**: `ctest --preset ninja-debug -R "cd_test_concurrency|cd_test_job_graph|cd_test_thread_pool|cd_test_work_stealing|cd_test_parallel_for|cd_test_future|cd_test_job_token" --output-on-failure`.

**Notes**:

- TSan-preset compatible; the work-stealing deque + the JobGraph cycle-detector are stress-tested under cd_test_work_stealing and cd_test_job_graph.
- Marathon Run 11 phase B2 migrated all `std::lock_guard` sites to `std::scoped_lock`; Run 14 caught the 5 sites the Run 11 sweep missed in `tests/`.
- The Run 11 `bugprone-unhandled-exception-at-new` finding was FIXED in Run 12 (spawn_detached new-failure path) and the rule is in WarningsAsErrors.
- Related reading: ADR-20260528-job-system-design (X1 follow-ups A-H), the X1-FU-G threading contract in `cd/rhi/IDevice.hpp`, and `research/reports/X1FUG_upload_buffer_thread_safety_audit.md`.
