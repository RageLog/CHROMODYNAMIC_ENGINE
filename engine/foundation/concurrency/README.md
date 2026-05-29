# cd::concurrency

**Purpose**: tier-0 job system + parallel-for + work-stealing thread pool. The runtime substrate for every CPU-parallel boot task (X1C IBL bake graph) and per-frame parallel pass (X1B TLAS instance build).

**Namespace**: `cd::concurrency`.

**Headers**: `cd/concurrency/JobGraph.hpp`, `cd/concurrency/ParallelFor.hpp`, `cd/concurrency/WorkStealingThreadPool.hpp`, `cd/concurrency/ThreadPool.hpp`, `cd/concurrency/Future.hpp`, `cd/concurrency/JobToken.hpp`.

**Primary types**:
- `cd::concurrency::JobGraph` — DAG of dependent jobs with explicit dependency edges. `add(callable, {dep_a, dep_b, ...})` returns a NodeId; `run(pool)` executes the DAG and reports `failed_nodes()`. Used by the X1C boot bake graph.
- `cd::concurrency::WorkStealingThreadPool` — N-worker work-stealing pool; default N = `std::thread::hardware_concurrency()`.
- `cd::concurrency::ThreadPool` — older simpler pool kept for niche call sites; new code should pick WorkStealingThreadPool.
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

**Test command**: `ctest --preset ninja-debug -R "cd_test_concurrency|cd_test_job_graph|cd_test_thread_pool|cd_test_work_stealing|cd_test_parallel_for|cd_test_future|cd_test_job_token" --output-on-failure`.

**Notes**:
- Header-inline plus a small TU per type.
- TSan-preset compatible; the work-stealing deque + the JobGraph cycle-detector are stress-tested under cd_test_work_stealing and cd_test_job_graph.
- Marathon Run 11 phase B2 migrated all `std::lock_guard` sites to `std::scoped_lock` (modernize-use-scoped-lock).
- `bugprone-unhandled-exception-at-new` remains an open finding at line 192; queued for Run 12 site fix (replace bare `new Job` with `std::make_unique<Job>` or nothrow placement).
- See ADR-015 concurrency-job-system + the W8 phase283 closure (BLOCKER cleared for Phase 1 substance; Phase 2 integration to render / asset / ECS dispatch sites tracked as X1 BLOCKER).
