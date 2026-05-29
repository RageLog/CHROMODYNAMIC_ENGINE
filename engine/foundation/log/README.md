# cd::log

**Purpose**: tier-0 logging facade. Compile-time level filtering, multi-sink fan-out, thread-safe message formatting.

**Namespace**: `cd::log`.

**Headers**: `cd/log/Log.hpp` plus per-sink headers.

**Primary types**:
- `cd::log::Logger` — facade; pick a level (`trace` / `debug` / `info` / `warn` / `error` / `fatal`).
- `cd::log::ConsoleSink` — stderr / stdout.
- `cd::log::FileSink` — rolling file.
- `cd::log::BufferSink` — in-memory ring (the History panel of hello_engine reads from this).

**Usage example**:
```cpp
#include <cd/log/Log.hpp>

cd::log::info("hello {}", "world");
cd::log::warn("fps dropped to {:.2f}", current_fps);
```

**Test command**: `ctest --preset ninja-debug -R cd_test_log --output-on-failure`.

**Notes**:
- Format strings via fmt (vendored via Tier-A vcpkg manifest; see ADR-016 vendor matrix).
- Marathon Run 11: no policy changes; cd::log is policy-stable since Phase 1.
- Future work: structured-log JSON sink (queued).
