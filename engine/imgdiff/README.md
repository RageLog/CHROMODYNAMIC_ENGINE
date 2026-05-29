# cd::imgdiff

## Purpose
Golden-image pixel-difference utility for visual regression testing. Provides SSIM, PSNR, and per-pixel delta metrics to validate rendering quality across platform/driver combinations without brittle exact-match image comparisons.

## Namespace
`cd::<imgdiff>::`

## Public headers
- `include/cd/imgdiff/Diff.hpp` — SSIM and PSNR metric computation
- `include/cd/imgdiff/Report.hpp` — Delta visualization and summary stats

## Primary types
- `ImgDiff::Diff` — Structural similarity (SSIM) index and peak signal-to-noise ratio (PSNR)
- `ImgDiff::Report` — Per-pixel delta mask, histogram, pass/fail threshold
- `ImgDiff::Options` — Metric selection (SSIM, PSNR, max-delta), tolerance levels

## Usage example
```cpp
#include <cd/imgdiff/Diff.hpp>

// Compare current frame to golden reference.
auto golden = load_image("golden.png");
auto current = load_image("frame_000.png");

cd::imgdiff::Options opts{
  .metric = cd::imgdiff::Metric::SSIM,
  .threshold = 0.98f  // 98% similarity passes
};

auto report = cd::imgdiff::compute(golden, current, opts);
if (report.passed) {
  std::cout << "Rendering test PASSED (SSIM=" << report.ssim << ")\n";
} else {
  report.save_delta_visualization("delta.png");
}
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_imgdiff
ctest --preset ninja-debug -R imgdiff
```

## Dependencies
- `cd::core` — engine types

## References
- **Wang et al. 2004**, "Image Quality Assessment: From Error Visibility to Structural Similarity" (IEEE TIP)
- **Watson et al. 2001**, "What and Where: 3D Object Recognition with an Integrated Computational Model" (perceptual metrics)

## Notes
- Header-only library (minimal deps for test isolation).
- No external image codec dependency; expects pre-loaded pixel buffers.
- SSIM typically more forgiving than pixel-perfect matching for renderers (TAA jitter, compression, etc.).
- Used by Marathon run golden-image test suites (render quality regression checks).
