# cd::gameplay_time

## Purpose
Authoritative game-time abstraction for the engine main loop. Provides a
single `TimeKeeper` driven by `tick(real_dt_seconds)` plus three orthogonal
`TimerCategory` channels (gameplay, UI, simulation) so menus can animate
while gameplay is paused and a fixed-step simulation can run at a separate
scale from the variable-step gameplay clock.

## Namespace
`cd::gameplay::time`

## Public headers
- `include/cd/gameplay/time/Time.hpp` — `GameTime` snapshot, `TimerCategory`
  enum, `TimeKeeper` class.

## Primary types
- `GameTime` — POD snapshot: `elapsed_seconds`, `delta_seconds`,
  `frame_index`, `time_scale`.
- `TimerCategory` — `kGameplay` (default, affected by pause + scale), `kUi`
  (menu animations, ignores gameplay pause), `kSimulation` (fixed-step
  physics / determinism).
- `TimeKeeper` — engine clock: `tick()`, `pause()`/`resume()`,
  `set_time_scale()`, `get()`, `frame_index()`, `reset()`. Per-channel
  overloads of `set_paused`, `set_time_scale`, `time_scale`, `is_paused`,
  `get`.

## Semantics
| Property              | Pause              | time_scale         | Negative dt        |
|-----------------------|--------------------|--------------------|--------------------|
| `elapsed_seconds`     | frozen             | multiplied         | rejected, unchanged|
| `delta_seconds`       | real dt reported   | unaffected         | rejected, unchanged|
| `frame_index`         | still increments   | unaffected         | rejected, unchanged|

- Pause halts the channel's `elapsed_seconds` but `delta_seconds` still
  surfaces the real wall-clock delta so UI animations keep ticking.
- Negative scale is clamped to `0.0` (running time backwards desyncs every
  age / animation cursor downstream).
- `tick(real_dt_seconds)` returns `false` on negative dt and performs no
  state mutation — including no `frame_index` bump.

## Usage example
```cpp
#include <cd/gameplay/time/Time.hpp>

cd::gameplay::time::TimeKeeper clock;
while (running) {
    const double real_dt = compute_wall_clock_dt();
    clock.tick(real_dt);

    const auto gameplay = clock.get();
    const auto ui       = clock.get(cd::gameplay::time::TimerCategory::kUi);

    update_gameplay(gameplay.delta_seconds, gameplay.elapsed_seconds);
    update_menus(ui.delta_seconds);  // keeps ticking even while paused

    if (player_opened_pause_menu) clock.pause();
    if (player_closed_pause_menu) clock.resume();
}
```

## Build / Test
```bash
cmake --build --preset ninja-debug --target cd_gameplay_time
ctest --preset ninja-debug -R gameplay_time --output-on-failure
```

## Dependencies
- `cd::core` — `Defines.hpp` (no math, no concurrency).

## References
- Unreal Engine `AWorldSettings` (TimeDilation + GlobalTimeDilation pattern).
- Unity `UnityEngine.Time` (timeScale + unscaledDeltaTime split).
- Bevy `bevy_time::Time` (separated real / virtual / fixed clocks).

## Notes
- Single TU (`src/Time.cpp`) to keep header dependency-free and amortize
  compile cost across consumers.
- All operations are `noexcept`; no allocation; safe for hot-path.
- Not thread-safe by design: drive from a single owner (engine main loop)
  and snapshot `GameTime` POD copies for worker threads.
