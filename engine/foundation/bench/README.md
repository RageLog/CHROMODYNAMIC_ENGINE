# cd::bench

## Purpose
Header-only microbenchmarking utility for measuring CPU-bound function performance. Provides simple timing harness, statistics aggregation, and output formatters for performance regression testing and profiling.

## Namespace
`cd::bench::` — all public symbols.

## Public headers
- `Benchmark.hpp` — Timer, statistics aggregation, output formatters

## Primary types
- `Benchmark` — Harness for timed function execution
- Statistics aggregator (min, max, mean, stddev, percentiles)
- Output formatter (plain text, CSV, JSON)

## Usage example
```cpp
#include <cd/bench/Benchmark.hpp>

cd::bench::Benchmark bm;
for (int i = 0; i < 1000; ++i) {
  bm.time([&]() { 
    compute_expensive_function(); 
  });
}

bm.print_stats();  // Prints mean, stddev, min, max
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_bench
```

## Test
```bash
ctest --preset ninja-debug -R bench
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types (INTERFACE-only, for consuming includes)

## Notes
- Header-only library
- Depends only on C++ stdlib (chrono)
- No external profiler required
- Suitable for CI/CD regression detection

## References
- Microbenchmarking best practices
