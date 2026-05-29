# cd::runtime

## Purpose
Composition root and service locator for foundation subsystems. Centralizes engine context initialization (asset system, concurrency pool, diagnostics, logging, virtual file system) so applications don't wire everything manually.

## Namespace
`cd::<foundation>::runtime::`

## Public headers
- `include/cd/runtime/EngineContext.hpp` — Service container with lifecycle management

## Primary types
- `Runtime::EngineContext` — Aggregates cd::asset, cd::concurrency, cd::diag, cd::log, cd::vfs instances
- `Runtime::Config` — Startup parameters (thread count, log level, asset path, etc.)

## Usage example
```cpp
#include <cd/runtime/EngineContext.hpp>

// Initialize all foundation services at once.
cd::runtime::EngineContext engine;
engine.initialize({
  .thread_pool_size = 4,
  .log_level = cd::log::Level::INFO,
  .asset_root = "assets/",
  .vfs_mounts = {{"content", "./content/"}}
});

// Services accessible throughout application.
auto& logger = engine.log();
auto& assets = engine.asset_manager();
auto& jobs = engine.thread_pool();
auto& diag = engine.diagnostics();

// Shutdown (cleanup in reverse order).
engine.shutdown();
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_runtime
ctest --preset ninja-debug -R runtime
```

## Dependencies
- `cd::core` — engine types
- `cd::asset` — asset management
- `cd::concurrency` — thread pool
- `cd::diag` — diagnostics (profiler, memory tracker)
- `cd::log` — logging
- `cd::vfs` — virtual file system

## References
- Service Locator pattern (GOF)
- Dependency Injection containers (Spring, Castle Windsor)
- ADR path (Sprint S2.10): `docs/ADR/ADR-*-runtime-*.md`

## Notes
- STATIC library (application links once).
- Simplifies initialization for samples and tools; advanced apps can instantiate services individually.
- Not a full-featured DI container — just a convenience wrapper for typical engine setup.
- Called before entering main game loop; shutdown called on application exit.
