# cd::core

**Purpose**: tier-0 utilities consumed by every other library — counters, profile-span timing, small helpers.

**Namespace**: `cd::core`.

**Headers**: `cd/core/CounterTable.hpp`, `cd/core/ProfileSpan.hpp`.

**Primary types**:
- `cd::core::CounterTable` — name-keyed atomic counter store. Used by the engine self-stats panel + the per-frame "draws / frames / commands" diagnostic.
- `cd::core::ProfileSpan` — RAII timing scope with mutex-protected sink. Lightweight; not a replacement for the real cd::profile pipeline (BufferSink / ChromeTraceSink / CsvSink live there), but useful for ad-hoc timing inside a TU.

**Usage example**:
```cpp
#include <cd/core/CounterTable.hpp>

cd::core::CounterTable counters;
counters.bump("frames");
counters.add("draws", 17);
const auto snapshot = counters.snapshot();
```

**Test command**: `ctest --preset ninja-debug -R cd_test_core --output-on-failure`.

**Notes**:
- Header-inline implementation; no cd_core.lib required for header-only consumers, but a TU-side `cd_core.cpp` is linked anyway so debug builds get debug info for the inlined helpers.
- Marathon Run 11 phase B2 swapped the ProfileSpan std::lock_guard sites to std::scoped_lock (clang-tidy modernize-use-scoped-lock).
