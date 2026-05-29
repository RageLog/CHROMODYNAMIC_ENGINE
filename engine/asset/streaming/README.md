# cd::asset_streaming

## Purpose
Streaming asset manager supporting asynchronous load/unload queuing with priority, memory budgets, and ref-counting. Orchestrates multi-frame loading to avoid frame stutters and manages resident memory within configured limits.

## Namespace
`cd::<asset>::asset_streaming::`

## Public headers
- `include/cd/asset_streaming/Streamer.hpp` — Request queue, priority scheduling
- `include/cd/asset_streaming/Budget.hpp` — Memory limit tracking and eviction policy
- `include/cd/asset_streaming/Request.hpp` — Load/unload request descriptor

## Primary types
- `AssetStreaming::Streamer` — Async request dispatcher with frame budget
- `AssetStreaming::Budget` — Memory ceiling, current usage, eviction candidate selection
- `AssetStreaming::Request` — Asset handle, load/unload command, priority tier

## Usage example
```cpp
#include <cd/asset_streaming/Streamer.hpp>

cd::asset_streaming::Streamer streamer(
  /*memory_budget_mb*/ 500,
  /*max_requests_per_frame*/ 4
);

// Enqueue load (will be serviced across multiple frames if needed).
auto load_req = streamer.enqueue_load(asset_handle, Priority::HIGH);

// Frame update (services N requests, respecting memory budget).
streamer.update(delta_time);

// Query status.
bool loaded = streamer.is_resident(asset_handle);
auto stats = streamer.memory_stats();
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_asset_streaming
ctest --preset ninja-debug -R asset_streaming
```

## Dependencies
- `cd::core` — engine types

## References
- Streaming patterns: Unreal Engine, Frostbite, Wwise (audio streaming)
- Priority scheduling: standard queue algorithms

## Notes
- Header-only public API; async implementation in cd::rhi/cd::io.
- Prioritizes "near-camera" assets for smooth level transitions.
- Unloads least-recently-used (LRU) when budget exceeded.
- Integrates with level system (cd::world_container) for predictive streaming based on camera bounds.
