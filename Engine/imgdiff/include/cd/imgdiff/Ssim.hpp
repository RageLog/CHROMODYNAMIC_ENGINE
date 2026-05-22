// =============================================================================
// CHROMODYNAMIC — cd/imgdiff/Ssim.hpp
// Phase 6 / Wave 39 — perceptual diff layer (SSIM-lite).
//
// Structural Similarity Index — Wang et al., "Image Quality Assessment:
// From Error Visibility to Structural Similarity" (IEEE TIP, 2004).
// Captures luminance/contrast/structure agreement instead of raw pixel
// distance, so it correlates with human perception better than RMSE.
//
// This is the LITE variant:
//   * Luminance-only (Y = 0.299R + 0.587G + 0.114B). No per-channel
//     SSIM, no SSIM in CIE Lab. Keeps the runtime cheap and avoids
//     extra math deps.
//   * Non-overlapping NxN box windows (default 8x8) instead of an
//     11x11 Gaussian window. Slightly noisier per-window score but
//     5-10x cheaper, and the aggregate is still useful for golden-image
//     regression detection.
//   * Reports mean_ssim (aggregate quality), min_ssim (worst window —
//     useful for "where did the renderer regress?" queries), and the
//     window count.
//
// Header-only. cd::core + standard math; the bigger imgdiff library
// already exists alongside this header.
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

struct SsimReport
{
    double mean_ssim { 0.0 };   ///< Average over every window. 1.0 = identical, ~0 = orthogonal.
    double min_ssim { 0.0 };    ///< Worst-case window. Useful for "where is the regression?"
    std::uint32_t windows { 0 };
};

namespace ssim_detail
{

[[nodiscard]] inline double luminance_at(const std::uint8_t* rgba, std::size_t i) noexcept
{
    // ITU-R BT.601 weights — same as the JPEG / classic Y/Cb/Cr split.
    return 0.299 * static_cast<double>(rgba[i + 0])
         + 0.587 * static_cast<double>(rgba[i + 1])
         + 0.114 * static_cast<double>(rgba[i + 2]);
}

}  // namespace ssim_detail

/// Compute SSIM-lite on two RGBA8 images. `window_size` is the side of
/// the non-overlapping square box used to estimate local statistics
/// (default 8; the original SSIM paper uses 11 with a Gaussian weight).
/// Returns kDimensionMismatch / kEmptyImage / kNullPointer on the usual
/// boundary failures.
[[nodiscard]] inline cd::core::Result<SsimReport>
compute_ssim_lite(ImageView a, ImageView b, std::uint32_t window_size = 8)
{
    if (a.rgba == nullptr || b.rgba == nullptr)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kNullPointer));
    if (a.width != b.width || a.height != b.height)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kDimensionMismatch));
    if (a.width == 0 || a.height == 0 || window_size == 0)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kEmptyImage));

    // SSIM stabiliser constants (Wang 2004): k1=0.01, k2=0.03, L=255.
    constexpr double kL = 255.0;
    constexpr double kK1 = 0.01;
    constexpr double kK2 = 0.03;
    constexpr double kC1 = (kK1 * kL) * (kK1 * kL);
    constexpr double kC2 = (kK2 * kL) * (kK2 * kL);

    const auto win = window_size;
    SsimReport r;
    r.min_ssim = 1.0;
    double sum_ssim = 0.0;

    for (std::uint32_t y0 = 0; y0 + win <= a.height; y0 += win)
    {
        for (std::uint32_t x0 = 0; x0 + win <= a.width; x0 += win)
        {
            double mean_a = 0.0;
            double mean_b = 0.0;
            const auto inv_n = 1.0 / static_cast<double>(win * win);
            for (std::uint32_t dy = 0; dy < win; ++dy)
            {
                for (std::uint32_t dx = 0; dx < win; ++dx)
                {
                    const std::size_t i =
                        (static_cast<std::size_t>(y0 + dy) * a.width + (x0 + dx)) * 4U;
                    mean_a += ssim_detail::luminance_at(a.rgba, i);
                    mean_b += ssim_detail::luminance_at(b.rgba, i);
                }
            }
            mean_a *= inv_n;
            mean_b *= inv_n;

            double var_a = 0.0;
            double var_b = 0.0;
            double cov_ab = 0.0;
            for (std::uint32_t dy = 0; dy < win; ++dy)
            {
                for (std::uint32_t dx = 0; dx < win; ++dx)
                {
                    const std::size_t i =
                        (static_cast<std::size_t>(y0 + dy) * a.width + (x0 + dx)) * 4U;
                    const double la = ssim_detail::luminance_at(a.rgba, i) - mean_a;
                    const double lb = ssim_detail::luminance_at(b.rgba, i) - mean_b;
                    var_a += la * la;
                    var_b += lb * lb;
                    cov_ab += la * lb;
                }
            }
            var_a *= inv_n;
            var_b *= inv_n;
            cov_ab *= inv_n;

            const double num = (2.0 * mean_a * mean_b + kC1) * (2.0 * cov_ab + kC2);
            const double den = (mean_a * mean_a + mean_b * mean_b + kC1)
                             * (var_a + var_b + kC2);
            const double ssim = den > 0.0 ? num / den : 1.0;
            sum_ssim += ssim;
            r.min_ssim = std::min(r.min_ssim, ssim);
            ++r.windows;
        }
    }

    if (r.windows == 0)
    {
        // Image smaller than one window — return identity for an empty
        // grid so callers don't divide by zero. The caller can detect
        // this by inspecting `windows`.
        r.mean_ssim = 1.0;
        r.min_ssim = 1.0;
        return r;
    }
    r.mean_ssim = sum_ssim / static_cast<double>(r.windows);
    return r;
}

/// Convenience: returns true if `report.mean_ssim >= threshold`. The
/// default 0.99 corresponds to a near-perceptually-identical match for
/// 8x8 windows; production renderer regression tests typically use
/// 0.995 - 0.998 depending on jitter tolerance.
[[nodiscard]] inline bool ssim_passes(const SsimReport& report,
                                      double threshold = 0.99) noexcept
{
    return report.mean_ssim >= threshold;
}

}  // namespace cd::imgdiff
