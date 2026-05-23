# Golden image references (Phase 11 Track A + B)

This directory holds the **per-vendor reference PNGs** the Phase 11
visual-correctness gate compares against. Each subdirectory is a
separate GPU backend; the comparison is always vendor-scoped because
rendered output is not bit-equal across drivers.

> **Do not edit these files by hand.** Re-capture them deliberately by
> running `cmake --build . --target smoke-golden-capture` after a
> visual change has been reviewed.

## Layout

```
tests/golden/
├── nvidia/      ← captured on NVIDIA GeForce RTX 3080 Laptop GPU
│   ├── hello_triangle.png
│   ├── hello_cube.png
│   ├── hello_anim.png
│   ├── hello_pbr.png
│   └── hello_skybox.png
├── intel/       ← captured on Intel Iris Xe Graphics (Tiger Lake iGPU)
│   └── (same 5 sample names)
├── lavapipe/    ← Linux Vulkan software ICD (Phase 13.B; CI runs).
│                  Populated via the `linux-lavapipe-golden-capture`
│                  workflow_dispatch job — see `lavapipe/README.md`.
└── README.md    ← you are here
```

The driver scripts ([scripts/run_golden.ps1](../../scripts/run_golden.ps1),
[scripts/run_golden.sh](../../scripts/run_golden.sh)) accept `-Vendor`
/ `--vendor` to pick the subdirectory. Default is `nvidia`.

## How to consume

```bash
# Compare current build's NVIDIA output against the NVIDIA reference set.
# (CMake target uses -Vendor nvidia by default.)
cmake --build build/ninja-base --target smoke-golden-compare

# Run on a different vendor by setting the device index + vendor slug.
CD_VULKAN_DEVICE_INDEX=0 \
  pwsh -File scripts/run_golden.ps1 -Mode compare -Vendor intel \
       -BuildDir build/ninja-base -Config Debug
```

Tolerance is 8/255 per channel by default — empirically validated
to cover both the single-driver noise budget and the NVIDIA-vs-Intel
cross-vendor delta (which sits at ≤ 1/255 per channel per sample;
see [ARCHITECTURE.md §6](../../docs/ARCHITECTURE.md) for the noise
floor table).

## Capturing a new vendor

```bash
# Find the device index via Vulkan enumeration order.
vulkaninfo --summary | grep -A1 deviceName

# Pick that index, capture under a slug:
CD_VULKAN_DEVICE_INDEX=N \
  pwsh -File scripts/run_golden.ps1 -Mode capture -Vendor <slug>
git add tests/golden/<slug>/
```

Slug convention: lowercase one word — `nvidia`, `intel`, `amd`,
`apple`, `lavapipe`, `swiftshader`.

## Why the cross-vendor noise floor matters

The marathon-era v1.0 tag was rolled back because ctest couldn't see
shader / cull / depth bugs that produced wrong-but-still-zero-exit
output. The golden gate catches that class. Phase 11 Track B Part 2's
question — "is the gate too tight to ship across hardware?" — is
empirically answered: NVIDIA RTX 3080 vs Intel Iris Xe diff is
≤ 1/255 per channel on all 5 wired samples, well under the 8/255
tolerance. The gate is not the cross-vendor blocker some maturity
discussions assume it would be.

## References

- [docs/ARCHITECTURE.md §6 GPU vendor notes](../../docs/ARCHITECTURE.md) —
  cross-vendor noise floor, device-selection env var, capture protocol.
- [scripts/run_golden.ps1](../../scripts/run_golden.ps1) /
  [scripts/run_golden.sh](../../scripts/run_golden.sh) — drivers.
- [samples/common/GoldenCapture.hpp](../../samples/common/GoldenCapture.hpp) —
  in-frame readback + post-frame compare helper.
- [docs/ADR/ADR-20260523-wave133-phase11-v1.0-maturity.md](../../docs/ADR/ADR-20260523-wave133-phase11-v1.0-maturity.md) —
  Track A + B acceptance criteria.
