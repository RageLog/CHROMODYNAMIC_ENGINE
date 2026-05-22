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

// ---- Gaussian-weighted SSIM (FLIP-lite — Wave 61) ---------------------------

namespace ssim_detail
{

[[nodiscard]] inline std::vector<double> build_gauss_2d(double sigma)
{
    if (sigma <= 0.0)
        return { 1.0 };
    const auto r = static_cast<int>(std::ceil(3.0 * sigma));
    const int width = 2 * r + 1;
    std::vector<double> k(static_cast<std::size_t>(width) * static_cast<std::size_t>(width));
    const double inv_2s2 = 1.0 / (2.0 * sigma * sigma);
    double sum = 0.0;
    for (int j = -r; j <= r; ++j)
        for (int i = -r; i <= r; ++i)
        {
            const double v = std::exp(-static_cast<double>(i * i + j * j) * inv_2s2);
            k[static_cast<std::size_t>((j + r) * width + (i + r))] = v;
            sum += v;
        }
    for (auto& v : k)
        v /= sum;
    return k;
}

}  // namespace ssim_detail

/// Gaussian-weighted SSIM (Wang 2004 §III-B reference setup). Slides a
/// 2D Gaussian window (sigma default 1.5, kernel size 2·ceil(3σ)+1 →
/// 11×11 by default) across the image. `stride` controls how often
/// the window is sampled — stride=1 = per-pixel sliding (most
/// accurate, ~window² × per-window cost); stride=window_size/2 is
/// the common "lite" trade-off. Uses BT.601 luminance, same as
/// `compute_ssim_lite`.
///
/// This is the FLIP perceptual-diff foundation; CSF + spatial
/// filtering chain layers on top of this score.
[[nodiscard]] inline cd::core::Result<SsimReport>
compute_ssim_gaussian(ImageView a, ImageView b, double sigma = 1.5,
                      std::uint32_t stride = 1)
{
    if (a.rgba == nullptr || b.rgba == nullptr)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kNullPointer));
    if (a.width != b.width || a.height != b.height)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kDimensionMismatch));
    if (a.width == 0 || a.height == 0)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kEmptyImage));
    if (stride == 0)
        stride = 1;

    constexpr double kL = 255.0;
    constexpr double kK1 = 0.01;
    constexpr double kK2 = 0.03;
    constexpr double kC1 = (kK1 * kL) * (kK1 * kL);
    constexpr double kC2 = (kK2 * kL) * (kK2 * kL);

    const auto kernel = ssim_detail::build_gauss_2d(sigma);
    const int kw = static_cast<int>(std::sqrt(static_cast<double>(kernel.size())));
    const int r = (kw - 1) / 2;
    const int W = static_cast<int>(a.width);
    const int H = static_cast<int>(a.height);

    SsimReport rep;
    rep.min_ssim = 1.0;
    double sum_ssim = 0.0;

    for (int cy = r; cy + r < H; cy += static_cast<int>(stride))
    {
        for (int cx = r; cx + r < W; cx += static_cast<int>(stride))
        {
            double mean_a = 0.0;
            double mean_b = 0.0;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                {
                    const auto i = (static_cast<std::size_t>(cy + dy) * static_cast<std::size_t>(W)
                                    + static_cast<std::size_t>(cx + dx)) * 4U;
                    const double w = kernel[static_cast<std::size_t>((dy + r) * kw + (dx + r))];
                    mean_a += w * ssim_detail::luminance_at(a.rgba, i);
                    mean_b += w * ssim_detail::luminance_at(b.rgba, i);
                }

            double var_a = 0.0;
            double var_b = 0.0;
            double cov_ab = 0.0;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                {
                    const auto i = (static_cast<std::size_t>(cy + dy) * static_cast<std::size_t>(W)
                                    + static_cast<std::size_t>(cx + dx)) * 4U;
                    const double w = kernel[static_cast<std::size_t>((dy + r) * kw + (dx + r))];
                    const double la = ssim_detail::luminance_at(a.rgba, i) - mean_a;
                    const double lb = ssim_detail::luminance_at(b.rgba, i) - mean_b;
                    var_a += w * la * la;
                    var_b += w * lb * lb;
                    cov_ab += w * la * lb;
                }

            const double num = (2.0 * mean_a * mean_b + kC1) * (2.0 * cov_ab + kC2);
            const double den = (mean_a * mean_a + mean_b * mean_b + kC1)
                             * (var_a + var_b + kC2);
            const double ssim = den > 0.0 ? num / den : 1.0;
            sum_ssim += ssim;
            rep.min_ssim = std::min(rep.min_ssim, ssim);
            ++rep.windows;
        }
    }

    if (rep.windows == 0)
    {
        rep.mean_ssim = 1.0;
        rep.min_ssim = 1.0;
        return rep;
    }
    rep.mean_ssim = sum_ssim / static_cast<double>(rep.windows);
    return rep;
}

}  // namespace cd::imgdiff
