# cd::foundation_utils

## Purpose
Umbrella library aggregating small foundation utility libraries (1-6 headers each). Consumers prefer a single "all foundation utilities" dep over listing bench, config, diag, events, frame_timing, plugin, profile, and mem individually.

## Namespace
`cd::foundation_utils::` — all public symbols (inherits from member libraries).

## Public headers
- Transitively includes all member utility libraries:
  - `cd::bench` — Microbenchmark harness
  - `cd::config` — CVar persistence
  - `cd::diag` — Assertions, crash reporting, deadline monitoring
  - `cd::events` — Event bus and recording
  - `cd::frame_timing` — Frame-time ring buffer (Phase 219)
  - `cd::plugin` — Dynamic loading, hot reload, file watching
  - `cd::profile` — Instrumentation sinks (Chrome, CSV, buffer)
  - `cd::mem` — Allocator interfaces and implementations

## Primary types
- Types from each member library (preserved with their namespaces)

## Usage example
```cpp
#include <cd/foundation_utils/FoundationUtils.hpp>

// All small utilities in one include
auto bm = cd::bench::Benchmark();
cd::diag::set_panic_handler(custom_handler);
auto watcher = cd::plugin::FileWatcher("./plugins");
auto profiler = cd::profile::ChromeTraceSink("trace.json");
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_foundation_utils
```

## Test
```bash
ctest --preset ninja-debug -R foundation_utils
```

## Dependencies (per CMakeLists)
- `cd::bench`
- `cd::config`
- `cd::diag`
- `cd::events`
- `cd::frame_timing`
- `cd::plugin`
- `cd::profile`
- `cd::mem`

## Notes
- Interface library (aggregation only)
- Phase 234 umbrella
- Excludes large foundation libs (core, math, concurrency, io, log, platform, time, vfs) — these stay independent due to their size
- Each member library keeps its own namespace and documentation
- Consumers may still cherry-pick individual deps for minimal builds

## References
- Individual member library READMEs
- ADR-018 — Phase 2 foundation closure
