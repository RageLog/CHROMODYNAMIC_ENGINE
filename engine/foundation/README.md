# engine/foundation

**Phase 2 Sprint S2.0+** — the bottom tier of the CHROMODYNAMIC
dependency DAG. Every library here is **OS-aware** (where it has to
be) but **engine-agnostic** — the foundation tier owns the C++23
primitives that every higher tier consumes. Nothing in `foundation/`
depends on `render/`, `world/`, `ui/`, or `game/`.

This directory is an **umbrella** — there is no `cd::foundation`
library. Each subdirectory is its own self-contained library with its
own namespace.

## Layout

```
                              cd::core
                                  │
                ┌─────────────────┼─────────────────┐
                │                 │                 │
              cd::mem        cd::math        cd::concurrency
              cd::time       cd::config      cd::events
              cd::log        cd::diag        cd::profile
              cd::io         cd::vfs         cd::bench
              cd::platform   cd::plugin      cd::frame_timing
                                  │
                            cd::foundation_utils  (umbrella over the small libs)
                                  │
                cd::profile::cpu_marker_overlay
                cd::profile::frame_graph_timeline
                cd::profile::gpu_marker
```

`cd::core` is the single root — every other foundation library
depends on it transitively. The arrows above are dependency
direction (callee → caller).

## Sub-library purpose at a glance

| Library                                 | Role |
|-----------------------------------------|------|
| `cd::core`                              | Defines, `Result`, `expected`, error code machinery, `CD_NODISCARD`, alignment macros. |
| `cd::mem`                               | Arena / pool / stack allocators (`Allocator` interface, RAII bindings). |
| `cd::concurrency`                       | Job system, work-stealing thread pool, latch / barrier primitives. |
| `cd::time`                              | Monotonic clock vocabulary; the *only* time-source the rest of the engine should use. |
| `cd::platform`                          | Window + input + event-loop abstraction (Win32 / X11 / Wayland / iOS / Android / Web). |
| `cd::diag`                              | Assertion + fatal-error + crash-dump helpers. |
| `cd::log`                               | Levelled logging (the *only* stdout endpoint the engine should use). |
| `cd::events`                            | In-process publish/subscribe event bus. |
| `cd::plugin`                            | Dynamic-library load + symbol resolution + hot-reload hooks. |
| `cd::io`                                | File / stream / blocking + async I/O primitives. |
| `cd::math`                              | Vec / Mat / Quaternion / AABB / Sphere / transform helpers. |
| `cd::config`                            | `.json` / `.cdc` config schema + load / save. |
| `cd::vfs`                               | Virtual file system (mount points, `.pak`, OS file fallthrough). |
| `cd::profile`                           | RAII marker scope + sample storage + lock-free aggregator. |
| `cd::bench`                             | Microbenchmark harness wrapping `<chrono>` + statistics. |
| `cd::frame_timing`                      | Header-only `dt` ring buffer + per-frame stats (Phase 218). |
| `cd::foundation_utils`                  | Umbrella consolidating ~7 trivial helper libraries (Phase 234). |
| `cd::profile::cpu_marker_overlay`       | Tracy-style CPU bar-chart overlay (Phase 587). |
| `cd::profile::frame_graph_timeline`     | GPU Gantt-chart overlay (Phase 593). |
| `cd::profile::gpu_marker`               | GPU marker recorder + RAII Scope companion (Phase 606). |

## Convention rules

* **No global state.** Libraries here that look like singletons
  (`cd::log`) take an explicit context object at construction; the
  process-wide instance, if any, is owned at the application boundary.
* **No upward includes.** A foundation library never `#include`s a
  header from `render/`, `world/`, `ui/`, or `game/`. Violations are
  reviewed before merge.
* **No exception-based error vocabulary.** Foundation libraries use
  `cd::expected<T, Error>` (returning) and `CD_ASSERT(...)` (fatal).
  Constructors that can fail expose a `static create()` factory
  returning `expected`.
* **C++23 only.** `std::expected`, `std::span`, `std::format`,
  `<ranges>` views, etc. — all gated for the four compilers in the
  matrix (MSVC, Clang-cl, Clang, GCC) but used freely.

## Build / test

Each sub-library has its own gtest binary; no umbrella aggregate
exists by design (cohesion stays per-library). Run the foundation
slice via:

```bash
ctest --preset ninja-debug -R '^cd_test_(core|mem|concurrency|math|...)$'
```

## See also

* `CLAUDE.md §7` — layered DAG enforcement rule.
* `docs/ADR/ADR-20260530-jolt-physics-integration.md` — example of
  a library that explicitly does NOT live here (rigid-body sits in
  the world tier).
