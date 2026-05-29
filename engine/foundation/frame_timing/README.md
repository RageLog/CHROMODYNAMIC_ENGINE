# cd::frame_timing

**Purpose**: per-frame dt + rolling time-ring + jitter measurement. Extracted out of hello_engine in Marathon Run 5 (phase 219 / X4) so the timing logic could be shared across samples + the editor.

**Namespace**: `cd::frame_timing`.

**Headers**: `cd/frame_timing/FrameTimeRing.hpp`.

**Primary types**:
- `cd::frame_timing::FrameTimeRing` — fixed-size ring of recent frame deltas (default 240 entries = 4 s at 60 Hz). Provides smoothed dt, jitter percentile, and the histogram the counters panel renders.

**Usage example**:
```cpp
#include <cd/frame_timing/FrameTimeRing.hpp>

cd::frame_timing::FrameTimeRing ring;
const auto now = std::chrono::steady_clock::now();
ring.tick(now);
const float dt = ring.last_dt();
const float p99 = ring.percentile(0.99F);
```

**Test command**: `ctest --preset ninja-debug -R cd_test_frame_timing --output-on-failure`.

**Notes**:
- Header-only.
- Used by hello_engine's per-frame dt + the Counters panel histogram.
- IMMEDIATE present-mode latency baseline is measured here (W5 phase ~218).
