# cd::time

## Purpose
High-resolution timing and frame-timing utilities: wall-clock queries, frame budgets, delta-time tracking, and deadline-based frame throttling. Foundation for precise frame-pacing and performance analysis.

## Namespace
`cd::time::` — all public symbols.

## Public headers
- `Clock.hpp` — High-resolution timer, delta-time accumulation
- `FrameBudget.hpp` — Per-frame time allowance + overage detection
- `Deadline.hpp` — Absolute time target for deadline-driven frames

## Primary types
- `Clock` — Monotonic timer; tracks frame time and delta-time
- `FrameBudget` — Frame time quota (e.g., 16.67ms for 60 FPS); reports overage
- `Deadline` — Next frame time target; supports frame skipping on misses

## Usage example
```cpp
#include <cd/time/Clock.hpp>

// High-resolution wall clock
cd::time::Clock clock;
clock.tick();

float delta_time = clock.delta_seconds();
float total_time = clock.total_seconds();

// Frame budgeting
cd::time::FrameBudget budget{/*target_fps=*/60};
if (budget.check_overage()) {
  log_frame_miss();
}
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_time
```

## Test
```bash
ctest --preset ninja-debug -R time
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types, logging

## Platform notes
- Uses `std::chrono` for wall-clock queries
- On Windows: falls back to QueryPerformanceCounter if std::chrono lacks sufficient precision
- Monotonic timer (always increases; immune to system clock adjustments)

## Notes
- Header-only library
- Designed for game loops and deadline-driven frame pacing
- Frame budgets report overage in milliseconds
- Integrates with cd::diag deadline monitor

## References
- Game Loop architecture patterns
- High-precision timing best practices
