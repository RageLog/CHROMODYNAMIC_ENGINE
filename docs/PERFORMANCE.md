# Performance — how to measure, how to gate, how to read

This document is the entry point for anyone asking "is X fast?" or
"did Y regress?" in the engine. It pairs with:

- `samples/hello_bench` — the bench runner
- `tests/bench/` — committed baselines
- `cmake/CDBench.cmake` — `smoke-bench` / `smoke-bench-capture` targets
- `.github/workflows/bench-regression.yml` — PR-gate CI workflow

## TL;DR

```bash
# Build the bench + run it locally, compare against committed baseline.
cmake --build build/ninja-base --target smoke-bench
```

Exit 0: no bench regressed by more than the threshold (25% for
Debug — see "Why 25%?" below). Exit non-zero: at least one bench
crossed the line. The output lists every bench with its mean and a
NOISE / REGRESS verdict.

## The bench surface (Phase 12.A baseline)

`hello_bench` runs 21 micro-benchmarks across three tiers.

### Speed-of-light foundation (≤ 50 ns / op)

These are the primitives the rest of the engine leans on every
frame. Any regression here cascades.

| Bench | Debug NVIDIA | Notes |
|---|---:|---|
| Result<int>_success      |    4 ns | `std::expected` happy path |
| PoolAllocator_alloc+free |    4 ns | freelist bump + return |
| core_handle_create       |    6 ns | bit-pack (index+gen+type) |
| math_quat_slerp          |   26 ns | hot animation blend |
| math_vec3_normalize      |   34 ns | per-vertex shader CPU prep |
| math_transform_to_mat4   |   45 ns | per-entity world-matrix |
| log_ring_push            |   48 ns | panic-triage mirror per-record |
| math_mat4_inverse        |  132 ns | view-projection inverse |

### Math kernels (100-600 ns / op)

| Bench | Debug NVIDIA | Notes |
|---|---:|---|
| math_mat4_multiply | 557 ns | per-MVP construction |

### Library-boundary (1-40 µs / op)

These cross allocations / hashmap probes / SIMD-able loops. They
won't dominate a frame, but they shape the API ergonomics.

| Bench | Debug NVIDIA | Notes |
|---|---:|---|
| core_handle_store_ins+ers   |   77 ns | sparse-set slot reuse + erase |
| vector<int>_reserve64       | 1718 ns | std::vector ctor + 64 push |
| ecs_for_each_256            | 1210 ns | 4.7 ns/entity steady-state iter |
| core_cvar_set+get           | 1174 ns | string-keyed registry round-trip |
| core_format_literal         |  830 ns | ErrorCode pretty-print (non-owning) |
| core_format_owning          | 1494 ns | ErrorCode pretty-print (shared_ptr alloc) |
| log_ring_snapshot_64        | 14.7 µs | 64-record copy |
| asset_json_parse_S          | 13.4 µs | parse {name,gain,loop,chunks[8]} |
| asset_json_serialize_S      |  5.2 µs | serialize same |
| imgdiff_compare_64x64       | 35.9 µs | per-pixel 4-channel diff |
| scene_serialize_16          |  336 µs | 16-node hierarchy → JSON |

### What's NOT benched (yet)

Worth adding in a follow-up wave:

- GPU command buffer recording (cd::rhi_vulkan path) — out of scope
  for headless bench; needs a synthetic device.
- Vulkan pipeline creation cost (one-shot, large).
- Image upload throughput (PCIe-bound, varies wildly across machines).
- Networking RTT estimation under `cd::net::ReliableChannel`.

These warrant a separate `hello_bench_gpu` and `hello_bench_net`
once Phase 12.B (D3D12) lands a second backend to bench against.

## How the gate works

### Per-PR gate (`.github/workflows/bench-regression.yml`)

On every PR, GitHub Actions:

1. Checks out PR HEAD, builds `hello_bench`, runs it → `current.json`.
2. Checks out the base branch (usually `dev`), builds + runs → `baseline.json`.
3. Pipes both through `cd_bench_compare --threshold=10`.
4. Fails the PR if any bench regresses ≥10%.

The 10% there is *per-merge* drift signal — PRs that incrementally
shave performance shouldn't pile up unnoticed. This is a strict gate.

### Local gate (`smoke-bench` CMake target)

For local validation against the v0.26.0-floor committed baseline:

```bash
cmake --build . --target smoke-bench
```

This compares against `tests/bench/baseline-debug-nvidia.json` with
a **25% threshold** — looser because the comparison is to a static
file, not a fresh side-by-side run, so noise floor is larger.

### Refreshing the committed baseline

```bash
cmake --build . --target smoke-bench-capture
git diff tests/bench/baseline-debug-nvidia.json
# review the deltas, commit if intentional
```

Don't refresh the baseline to bury a regression. If something
genuinely got slower and you can justify it, write the rationale
into the commit message; if it didn't, find and fix the regression.

## Why 25% for the local gate?

The 21 micro-benchmarks cover a 4-order-of-magnitude range
(4 ns → 336 µs). For benches in the ≤ 50 ns tier, Debug build noise
floor on this machine routinely peaks at 15-20% run-to-run. That's
not bug; it's Windows scheduler jitter + Debug allocator quantization
+ low signal at very small `inner_calls × iters` loops.

10% threshold would false-trip 1-2 benches per run. 25% catches
real regressions (the kind that would double a constant factor) and
doesn't fire on noise.

Release builds are 2-5x faster AND have less noise. A Release
baseline + tighter gate is queued for a follow-up wave.

## What "regression" actually means in this surface

A regression here is **a change in mean ns/op that's larger than
the per-bench noise envelope**. Variance per bench:

- ≤ 50 ns tier: ±15-20% Debug variance (high)
- 100-600 ns: ±5-10% Debug variance
- 1-40 µs: ±3-7% Debug variance (relatively stable)

The gate is one threshold across all of them; a future per-bench
tolerance is queued.

## Adding a new benchmark

```cpp
// In samples/hello_bench/main.cpp:

// 1. Declare the bench body.
void bench_my_thing()
{
    auto r = do_my_thing();
    cd::bench::do_not_optimize(r);
}

// 2. Register it in main().
add("my_thing", bench_my_thing);
```

Then re-capture the baseline:

```bash
cmake --build . --target smoke-bench-capture
git add tests/bench/baseline-debug-nvidia.json
```

Keep bench bodies cheap (≤ 10 µs each). The runner loops them to
sample a 30ms budget; bodies that already cost 1ms make the runner
collect only ~30 samples and the percentile numbers get unreliable.

## See also

- [ADR-20260523-wave142-phase12-feature-marathon.md](ADR/ADR-20260523-wave142-phase12-feature-marathon.md) —
  Phase 12.A scope
- `cmake/CDBench.cmake` — target wiring
- `tests/bench/README.md` — baseline file convention
