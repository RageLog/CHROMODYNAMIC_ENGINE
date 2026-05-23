# Bench baselines (Phase 12.A)

This directory holds reference JSON output from `hello_bench`. The
`bench-regression.yml` CI workflow diffs the PR HEAD's bench run
against the base branch's run; this directory is the committed
floor for "what fast should look like".

> **Do not edit by hand.** Re-capture by running
> `./build/.../bin/Debug/hello_bench.exe --json=tests/bench/baseline-debug-nvidia.json`
> after a deliberate perf change has been reviewed.

## Files

| File | Build | Backend | Captured on |
|---|---|---|---|
| `baseline-debug-nvidia.json` | Debug | NVIDIA RTX 3080 Laptop, clang-cl 21.x | Windows 11 |

## How to consume

```bash
# Compare current build's output against the baseline.
./build/ninja-base/bin/Debug/hello_bench --json=current.json
./build/ninja-base/bin/Debug/cd_bench_compare \
    tests/bench/baseline-debug-nvidia.json \
    current.json \
    --threshold=10
```

`cd_bench_compare` reports per-bench diff in percent + a summary line.
Exit code is non-zero on any regression above the threshold.

## Why Debug-only

Release builds vary significantly across compilers + LTO settings.
A single committed baseline that all CI matrix cells try to hit is
either falsely tight (Debug captures, Release CI cell trips the
gate) or falsely loose (Release captures, Debug CI cell trips because
Debug is 3-5x slower).

Debug-only baseline keeps the gate stable across the matrix at the
cost of not catching Release-specific regressions. Release-baseline
follow-up is queued for the next bench wave.

## Numbers at a glance (v0.26.0 baseline)

The 21 micro-benchmarks fall into three classes:

* **Speed-of-light foundation (≤ 10 ns):**
  Result<int> success (4 ns), PoolAllocator alloc+free (4 ns),
  Handle create (6 ns), log_ring_push (48 ns is close).
* **Math kernels (20-600 ns):**
  Quat slerp (26 ns), Vec3 normalize (34 ns), Transform→Mat4 (48 ns),
  Mat4 inverse (132 ns), Mat4 multiply (557 ns).
* **Library-boundary (1-40 µs):**
  ECS for_each over 256 entities (1.2 µs ≈ 4.8 ns/entity),
  CVar set+get (1.2 µs), asset_json parse (13.4 µs),
  log_ring_snapshot 64 records (14.7 µs), scene_serialize 16 nodes
  (336 µs), imgdiff_compare 64x64 (36 µs).

Anything that lands +10% over these in a follow-up build, the gate
should investigate.
