# Golden image references (Phase 11 Track A)

This directory holds the **reference PNGs** the Phase 11 visual-correctness
gate compares against. Each file is the swapchain output of a wired
sample, captured at the last headless frame on the project owner's
GPU + driver combo, with `--no-spin` active so the result is
deterministic across runs.

> **Do not edit these files by hand.** Re-capture them deliberately by
> running `cmake --build . --target smoke-golden-capture` after a
> visual change has been reviewed.

## How to consume

```bash
# Compare current build's output against these references.
# Exit code is 0 on pass, 2 on mismatch above tolerance, 1 on script error.
cmake --build build/ninja-base --target smoke-golden-compare
```

The driver script ([scripts/run_golden.ps1](../../scripts/run_golden.ps1)
+ [scripts/run_golden.sh](../../scripts/run_golden.sh)) per-sample
prints a one-line report with `diff=N/total px`, `max delta R/G/B/A`,
`rmse`, and `psnr`. Tolerance is 8/255 per channel by default — wide
enough to absorb driver-roundoff noise, tight enough to catch the
v1.0 rollback bug class (BUGs #1, #2, #5 would have tripped this
gate).

## Reference set

| Sample | Resolution | Captured on |
|---|---|---|
| hello_triangle.png | 800×600   | NVIDIA RTX 3080 Laptop GPU, Vulkan 1.3 |
| hello_cube.png     | 1024×768  | NVIDIA RTX 3080 Laptop GPU, Vulkan 1.3 |
| hello_anim.png     | 1024×768  | NVIDIA RTX 3080 Laptop GPU, Vulkan 1.3 |
| hello_pbr.png      | 1280×720  | NVIDIA RTX 3080 Laptop GPU, Vulkan 1.3 |
| hello_skybox.png   | 1280×720  | NVIDIA RTX 3080 Laptop GPU, Vulkan 1.3 |

## Cross-vendor caveat

These references are not bit-equal to output from a different GPU
backend. A Mesa lavapipe (Phase 11 Track B Part 1) or Intel/AMD/Apple
hardware (Track B Part 2) needs its own golden set; the Linux CI job
captures lavapipe references separately. The 8/255 tolerance is the
single-driver noise budget, not a cross-driver one.

When the engine supports a second backend (Track B Part 2), this
directory grows a `nvidia/`, `lavapipe/`, etc. subtree.

## References

- [scripts/run_golden.ps1](../../scripts/run_golden.ps1) — Windows
  driver (capture + compare)
- [scripts/run_golden.sh](../../scripts/run_golden.sh) — POSIX driver
- [samples/common/GoldenCapture.hpp](../../samples/common/GoldenCapture.hpp) —
  in-frame readback + post-frame compare helper
- [cmake/CDGolden.cmake](../../cmake/CDGolden.cmake) — wires the
  `smoke-golden-capture` / `smoke-golden-compare` build targets
- [docs/ADR/ADR-20260523-wave133-phase11-v1.0-maturity.md](../../docs/ADR/ADR-20260523-wave133-phase11-v1.0-maturity.md) —
  Track A acceptance criteria
