# cd::profile

**Purpose**: lightweight CPU profiler scope timer. Records named ScopeTimer events into a per-thread ring buffer that the editor + Tracy bridge consume for flamegraph rendering.

**Namespace**: `cd::profile`.

**Headers**: `cd/profile/{ScopeTimer,Ring,Tracy}.hpp`.

**Primary types**:
- `cd::profile::ScopeTimer scope { "Name" };` -- RAII timer that records on destruction.
- `cd::profile::Ring` -- lock-free per-thread event ring (default 4096 events).
- `cd::profile::tracy_bridge` -- optional translation to the Tracy protocol when CD_WITH_TRACY=ON.

**Test command**: `ctest --preset ninja-debug -R cd_test_profile --output-on-failure`.

**Notes**:
- Zero-cost when disabled at compile time (no ring write).
- Tracy backend is optional; default build ships the in-engine ring + editor flamegraph view only.
- Job system + render thread are pre-instrumented; engine modules add ScopeTimer at their public entry points.
