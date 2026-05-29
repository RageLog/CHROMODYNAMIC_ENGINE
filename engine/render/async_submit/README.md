# cd::async_submit

## Purpose
Minimal "one frame deep" worker-thread primitive for asynchronous command construction. Decouples CPU frame logic from GPU submission without exposing RHI types, enabling lighter-weight per-frame parallelism in render passes and post-processing chains.

## Namespace
`cd::<render>::async_submit::`

## Public headers
- `include/cd/async_submit/AsyncSubmit.hpp` — Worker interface and job container

## Primary types
- `AsyncSubmit::Job` — Opaque work functor (std::function) + name for diagnostics
- `AsyncSubmit::Worker` — Single-threaded task consumer with one frame of buffering

## Usage example
```cpp
#include <cd/async_submit/AsyncSubmit.hpp>

// Dispatch work from multiple CPU threads.
cd::async_submit::Worker worker;

worker.enqueue("build_shadow_cmdbuf", [&] {
  // CPU-only work (no RHI calls); outputs go to a thread-safe staging area.
  build_shadow_command_buffer(staging_buffer);
});

worker.flush();  // Wait for all jobs to complete.

// On main thread, pick up results and submit to GPU.
submit_to_gpu(staging_buffer);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_async_submit
ctest --preset ninja-debug -R async_submit
```

## Dependencies
- `cd::core` — engine types

## References
- Worker-thread pattern: `docs/ADR/ADR-*-async-*.md` (if applicable)
- Complements cd::concurrency work-stealing for per-system patterns.

## Notes
- Header-only library (minimal dependencies).
- No RHI or rendering types — can be used for asset streaming, physics, or audio work too.
- Intentionally shallow: one frame of latency (submitted work appears in next frame's GPU submission).
- Thread-safe for multiple enqueues; single consumer recommended.
