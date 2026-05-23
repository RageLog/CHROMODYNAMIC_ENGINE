// =============================================================================
// CHROMODYNAMIC — cd/imgdiff/Gaussian.hpp
// Phase 6 / Wave 54 — separable Gaussian blur on RGBA8 images.
//
// Foundation primitive for SSIM Gaussian-windowed scoring and the
// future FLIP perceptual diff (low-pass + CSF roll-off). Operates on
// the existing `cd::imgdiff::ImageView` so it composes cleanly with
// `compare()`, `compute_ssim_lite()`, and `highlight()`.
//
// Implementation:
//   * Separable: horizontal pass produces an RGBA8 intermediate, then
//     vertical pass produces the final output. Two O(W·H·R) passes
//     instead of one O(W·H·R²) — for sigma=2 (radius=6) that's 5×
//     fewer multiplies than a non-separable kernel.
//   * Border policy: CLAMP TO EDGE (extends pixel rows / columns past
//     the image). Wrap and zero-pad are not yet needed.
//   * Sigma → radius: r = ceil(3 * sigma). 99.7 % of a Gaussian's mass
//     lives in ±3σ so further radius gives < 1 LSB contribution per
//     tap on 8-bit data.
//
// Header-only; cd::core only. Output is a fresh std::vector<uint8_t>
// of `width * height * 4` bytes; the caller owns it.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/imgdiff/ImageDiff.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cd::imgdiff
{

namespace gauss_detail
{

[[nodiscard]] inline std::vector<double> build_kernel(double sigma)
{
    if (sigma <= 0.0)
        return { 1.0 };
    const auto r = static_cast<int>(std::ceil(3.0 * sigma));
    const int width = 2 * r + 1;
    std::vector<double> k(static_cast<std::size_t>(width));
    const double inv_sqrt = 1.0 / (std::sqrt(2.0 * 3.14159265358979323846) * sigma);
    const double inv_2s2 = 1.0 / (2.0 * sigma * sigma);
    double sum = 0.0;
    for (int i = -r; i <= r; ++i)
    {
        const double v = inv_sqrt * std::exp(-static_cast<double>(i * i) * inv_2s2);
        k[static_cast<std::size_t>(i + r)] = v;
        sum += v;
    }
    for (auto& v : k)
        v /= sum;
    return k;
}

}  // namespace gauss_detail

/// In-place-like Gaussian blur. Returns a fresh RGBA8 buffer of the
/// same dimensions as `src`. Alpha is blurred alongside RGB so the
/// callsite gets a fully-resampled image; if you want to preserve the
/// source alpha exactly, copy it back after this call.
[[nodiscard]] inline cd::core::Result<std::vector<std::uint8_t>>
gaussian_blur(ImageView src, double sigma)
{
    if (src.rgba == nullptr)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kNullPointer));
    if (src.width == 0 || src.height == 0)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kEmptyImage));

    const auto kernel = gauss_detail::build_kernel(sigma);
    const auto kw = static_cast<int>(kernel.size());
    const int r = (kw - 1) / 2;
    const auto W = static_cast<int>(src.width);
    const auto H = static_cast<int>(src.height);
    const auto N = static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4U;

    std::vector<std::uint8_t> mid(N);
    std::vector<std::uint8_t> out(N);

    auto idx = [&](int x, int y, int c) {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
                + static_cast<std::size_t>(x)) * 4U + static_cast<std::size_t>(c);
    };

    // Horizontal pass: src → mid.
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            double acc[4] = { 0.0, 0.0, 0.0, 0.0 };
            for (int t = -r; t <= r; ++t)
            {
                const int sx = std::clamp(x + t, 0, W - 1);
                const double w = kernel[static_cast<std::size_t>(t + r)];
                for (int c = 0; c < 4; ++c)
                    acc[c] += w * static_cast<double>(src.rgba[idx(sx, y, c)]);
            }
            for (int c = 0; c < 4; ++c)
                mid[idx(x, y, c)] = static_cast<std::uint8_t>(
                    std::clamp(acc[c] + 0.5, 0.0, 255.0));
        }
    }
    // Vertical pass: mid → out.
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            double acc[4] = { 0.0, 0.0, 0.0, 0.0 };
            for (int t = -r; t <= r; ++t)
            {
                const int sy = std::clamp(y + t, 0, H - 1);
                const double w = kernel[static_cast<std::size_t>(t + r)];
                for (int c = 0; c < 4; ++c)
                    acc[c] += w * static_cast<double>(mid[idx(x, sy, c)]);
            }
            for (int c = 0; c < 4; ++c)
                out[idx(x, y, c)] = static_cast<std::uint8_t>(
                    std::clamp(acc[c] + 0.5, 0.0, 255.0));
        }
    }
    return out;
}

}  // namespace cd::imgdiff
