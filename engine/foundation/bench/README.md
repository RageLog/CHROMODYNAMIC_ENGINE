# cd::bench

## Purpose

Header-only microbenchmarking utility for measuring CPU-bound function performance. Provides
automatic warmup, inner-call calibration (so clock-resolution noise does not dominate fast
bodies), statistics aggregation, and three serialisation formats (human-readable, CSV, JSON).

## Namespace

`cd::bench::` — all public symbols.

## Public headers

- `Benchmark.hpp` — `Config`, `Report`, `run<Body>()`, `do_not_optimize<T>()`,
  `reports_to_json_array()`, `markdown_header()`

## Primary API

### `cd::bench::run(name, body, cfg)` → `Report`

Runs `body` repeatedly according to `Config`, returns a `Report` with full statistics.

```cpp
#include <cd/bench/Benchmark.hpp>

cd::bench::Config cfg;
cfg.min_samples       = 64;       // collect at least 64 timed samples
cfg.min_time_ms       = 50;       // run for at least 50 ms (whichever is later)
cfg.warmup_iterations = 3;        // discard first 3 calls (warm caches)
cfg.label             = "release"; // optional tag for A/B comparisons

auto r = cd::bench::run("my_algo", [&]() {
    cd::bench::do_not_optimize(my_algo());
}, cfg);

r.print(std::cout);    // human-readable one-liner with max + stddev
r.to_csv();            // "name,label,samples,inner_calls,mean,median,min,p99,wall_s"
r.to_json();           // full JSON object (all 13 fields)
r.to_markdown_row();   // one table row; pair with markdown_header()
```

### `cd::bench::do_not_optimize(value)`

Compiler barrier — prevents the optimizer from dead-code-eliminating the benchmarked
expression. Uses `asm volatile("" : : "r,m"(value) : "memory")` on Clang/GCC;
volatile atomic store on MSVC.

### `cd::bench::reports_to_json_array(reports)` → `std::string`

Wraps a sequence of `Report` objects as a JSON array; empty input returns `"[]"`.

### `cd::bench::markdown_header()` → `std::string_view`

Two-line GFM table header matching `Report::to_markdown_row()` column order
(Bench | n | min | mean | median | p90 | p99 | stddev).

## `Config` fields

| Field | Default | Description |
| --- | --- | --- |
| `warmup_iterations` | 3 | Discarded leading calls (warm I-cache + branch predictor) |
| `min_samples` | 32 | Minimum timed samples collected |
| `min_time_ms` | 50 | Minimum wallclock budget; collection continues past `min_samples` until elapsed |
| `label` | `""` | Optional tag included in all serialisation outputs |

## `Report` statistics

| Field | Description |
| --- | --- |
| `mean_ns_per_op` | Arithmetic mean over all samples |
| `median_ns_per_op` | 50th percentile (average of two middle elements for even n) |
| `min_ns_per_op` | Minimum sample |
| `max_ns_per_op` | Maximum sample |
| `stddev_ns_per_op` | Population standard deviation (noise-floor indicator) |
| `p90_ns_per_op` | 90th percentile |
| `p95_ns_per_op` | 95th percentile |
| `p99_ns_per_op` | 99th percentile |
| `inner_calls` | Body invocations per sample (auto-scaled so each sample ≥ 1 µs) |
| `total_seconds` | Total wallclock spent in the sampling phase |

## Build

```bash
cmake --build --preset ninja-debug --target cd_bench
```

## Test

```bash
ctest --preset ninja-debug -R bench
```

## Dependencies (per CMakeLists)

- `cd::core` — INTERFACE-only; provides the include path

## Notes

- Header-only library — zero link-time cost.
- Stdlib-only — no external profiler or timing library required.
- Suitable for CI regression gates: emit `--json > artifact.json` and diff with
  `bench_compare` downstream tooling.
- `name` / `label` strings are NOT JSON-escaped; callers are expected to use
  ASCII identifiers (a quote inside the name corrupts JSON output by design).

## References

- Chandler Carruth, "Tuning C++: Benchmarks, and CPUs, and Compilers! Oh My!" (CppCon 2015)
- Google Benchmark `DoNotOptimize` design notes
