// =============================================================================
// CHROMODYNAMIC — cd/imgdiff/Flip.hpp
// Phase 7 / Wave 68 — FLIP-lite perceptual difference (Andersson 2020).
//
// FLIP — "A Difference Evaluator for Alternating Images" (Andersson, Nilsson,
// Akenine-Möller; HPG 2020). Models human contrast sensitivity (CSF) and
// produces a per-pixel error in [0, 1] that aligns with the perceived
// magnitude of an artifact when the viewer alternates between reference
// and test images.
//
// LITE variant (this header) implements:
//   * Luminance-only path (BT.601 Y). Full FLIP runs CSF separately on
//     achromatic + red-green + blue-yellow channels; lite skips chroma.
//   * Spatial-domain CSF approximation: a single Gaussian blur with σ
//     derived from `pixels_per_degree` (PPD). PPD models how much of the
//     visual field one screen pixel covers — higher PPD → finer spatial
//     frequencies visible → smaller σ.
//   * Pixel-wise |ΔY| / 255 difference after CSF filtering, mapped through
//     a softened contrast curve.
//   * Aggregates: mean_error, max_error, p95_error, window count.
//
// Full FLIP (CIE Lab + frequency-domain CSF + edge / point detection)
// stays as a Phase 7 follow-up; the API here is the "good enough for
// renderer regression triage" baseline.
//
// References:
//   * Andersson et al. 2020 — https://research.nvidia.com/publication/2020-07_FLIP
//   * Wang 2004 SSIM (Wave 39, Wave 61) — the structural counterpart.
//
// Header-only. Depends on cd::core + cd::imgdiff Gaussian helper.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/imgdiff/Gaussian.hpp>
#include <cd/imgdiff/ImageDiff.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace cd::imgdiff
{

struct FlipReport
{
    double mean_error { 0.0 };  ///< Average per-pixel error in [0, 1].
    double max_error { 0.0 };   ///< Worst per-pixel error.
    double p95_error { 0.0 };   ///< 95th percentile (robust "worst case").
    std::uint32_t pixel_count { 0 };
    /// Per-pixel error map (row-major, single channel float ∈ [0, 1]).
    /// Caller can colour-map it into a heatmap for visual inspection.
    std::vector<double> error_map;
};

namespace flip_detail
{

/// Translate pixels-per-degree to a CSF cutoff sigma in PIXELS. Higher
/// PPD → less blurring (we see finer detail when the display is far).
/// Constants tuned to match the Andersson 2020 paper §3.1 ballpark; the
/// "lite" name documents that this is an approximation of the full
/// frequency-domain CSF.
[[nodiscard]] inline double csf_sigma_pixels(double pixels_per_degree) noexcept
{
    if (pixels_per_degree <= 0.0)
        return 0.0;
    // Empirical: σ ≈ 0.16 * PPD gives a noticeable CSF rolloff at
    // typical desktop viewing distances (PPD ~ 60-80). Floors at 0.5
    // so we never collapse to a 1-tap kernel (which would defeat the
    // CSF model entirely).
    const double s = 0.16 * pixels_per_degree;
    return std::max(0.5, std::min(s, 8.0));
}

/// Softened contrast curve: maps raw |ΔY| ∈ [0, 255] to perceptual
/// error ∈ [0, 1]. Below a JND-class threshold the error is muted
/// (humans rarely notice 1-2 LSB shifts); above the threshold it ramps
/// roughly quadratically to 1.0 at half-range.
[[nodiscard]] inline double perceptual_map(double delta_abs) noexcept
{
    constexpr double kJndFloor = 2.0;     // ~2 LSB tolerance before any error
    constexpr double kSaturate = 96.0;    // ΔY = 96 → ≈ 1.0
    if (delta_abs <= kJndFloor)
        return 0.0;
    const double t = std::min((delta_abs - kJndFloor) / (kSaturate - kJndFloor), 1.0);
    return t * t;  // quadratic ramp
}

}  // namespace flip_detail

/// FLIP-lite perceptual error map + scalar aggregates.
///
/// `pixels_per_degree` defaults to 67 (a typical 27" 1440p monitor at
/// ~60 cm viewing distance). For headless / regression testing on
/// arbitrary images, leaving the default is fine — the score still
/// orders matching → mismatching images correctly.
[[nodiscard]] inline cd::core::Result<FlipReport>
compute_flip_lite(ImageView a, ImageView b, double pixels_per_degree = 67.0)
{
    if (a.rgba == nullptr || b.rgba == nullptr)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kNullPointer));
    if (a.width != b.width || a.height != b.height)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kDimensionMismatch));
    if (a.width == 0 || a.height == 0)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kEmptyImage));

    const double sigma = flip_detail::csf_sigma_pixels(pixels_per_degree);

    // 1. CSF approximation: blur both images.
    auto blurred_a = gaussian_blur(a, sigma);
    if (!blurred_a.has_value())
        return std::unexpected(blurred_a.error());
    auto blurred_b = gaussian_blur(b, sigma);
    if (!blurred_b.has_value())
        return std::unexpected(blurred_b.error());

    // 2. Per-pixel luminance ΔE → perceptual map.
    FlipReport r;
    r.pixel_count = a.width * a.height;
    r.error_map.resize(r.pixel_count);

    double sum = 0.0;
    for (std::uint32_t i = 0; i < r.pixel_count; ++i)
    {
        const std::size_t k = static_cast<std::size_t>(i) * 4U;
        const double ya = 0.299 * static_cast<double>((*blurred_a)[k + 0])
                        + 0.587 * static_cast<double>((*blurred_a)[k + 1])
                        + 0.114 * static_cast<double>((*blurred_a)[k + 2]);
        const double yb = 0.299 * static_cast<double>((*blurred_b)[k + 0])
                        + 0.587 * static_cast<double>((*blurred_b)[k + 1])
                        + 0.114 * static_cast<double>((*blurred_b)[k + 2]);
        const double delta = std::abs(ya - yb);
        const double err = flip_detail::perceptual_map(delta);
        r.error_map[i] = err;
        sum += err;
        r.max_error = std::max(err, r.max_error);
    }
    r.mean_error = sum / static_cast<double>(r.pixel_count);

    // 3. 95th percentile (sort copy — N ≤ ~1M is acceptable for offline use).
    std::vector<double> sorted = r.error_map;
    std::ranges::sort(sorted);
    const auto p95_idx = static_cast<std::size_t>(
        std::min(static_cast<double>(sorted.size() - 1),
                 static_cast<double>(sorted.size()) * 0.95));
    r.p95_error = sorted[p95_idx];
    return r;
}

/// Convenience: pass-fail gate. Default 0.05 = "5 % of pixels differ
/// noticeably on average" is a forgiving renderer-regression threshold;
/// production goldens typically use 0.005 - 0.01.
[[nodiscard]] inline bool flip_passes(const FlipReport& r,
                                      double mean_threshold = 0.05) noexcept
{
    return r.mean_error <= mean_threshold;
}

/// Build an RGBA8 heatmap of the FLIP error map: green = 0 error,
/// yellow = mid error, red = max. Dimensions match the input image.
/// Useful as a CI artifact when a FLIP gate fails so a human can
/// eyeball where the regression lives.
[[nodiscard]] inline std::vector<std::uint8_t>
flip_heatmap(const FlipReport& r, std::uint32_t width, std::uint32_t height)
{
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4U);
    if (r.error_map.size() != static_cast<std::size_t>(width) * height)
        return out;  // dimensions disagree → empty zero-init heatmap
    for (std::size_t i = 0; i < r.error_map.size(); ++i)
    {
        const double e = std::clamp(r.error_map[i], 0.0, 1.0);
        // Green → Yellow → Red ramp: R rises from 0 to 255 as e ∈ [0, 0.5];
        // G stays high until e > 0.5 then falls.
        const std::uint8_t R = static_cast<std::uint8_t>(std::min(1.0, e * 2.0) * 255.0);
        const std::uint8_t G = static_cast<std::uint8_t>(std::max(0.0, 1.0 - std::max(0.0, e - 0.5) * 2.0) * 255.0);
        const std::uint8_t B = 0;
        const std::size_t k = i * 4U;
        out[k + 0] = R;
        out[k + 1] = G;
        out[k + 2] = B;
        out[k + 3] = 255;
    }
    return out;
}

// ---- Full FLIP (Wave 94) ---------------------------------------------------
//
// Adds the chroma-channel + per-channel CSF treatment from the Andersson
// 2020 paper that the LITE variant skipped:
//   * RGB → Y + Cx + Cz (BT.709 luminance + opponent chroma channels).
//   * Per-channel CSF approximation: each channel gets its own σ derived
//     from `pixels_per_degree`. Chroma channels are blurred MORE than
//     luminance (humans see colour detail at lower spatial frequencies
//     than brightness detail).
//   * Per-channel |Δ| → softened contrast map (same JND-floor+quadratic
//     curve as the LITE variant, scaled per channel).
//   * Final error = sqrt(0.6·err_Y² + 0.2·err_Cx² + 0.2·err_Cz²).
//
// Still spatial-domain (no FFT) and still "lite"-flavoured — full FLIP
// adds explicit feature detectors (edges + points) on top of this. The
// chroma extension closes most of the perceptual gap for the
// renderer-regression workload.

namespace flip_detail
{

/// Build a separable 1D Gaussian kernel for the given sigma. Mirrors
/// the cd::imgdiff::Gaussian helper but kept local so this header can
/// stay self-contained.
[[nodiscard]] inline std::vector<double> build_gauss_kernel(double sigma) noexcept
{
    if (sigma <= 0.0)
        return { 1.0 };
    const auto r = static_cast<int>(std::ceil(3.0 * sigma));
    const int width = 2 * r + 1;
    std::vector<double> k(static_cast<std::size_t>(width));
    const double inv_sqrt = 1.0 / (std::sqrt(2.0 * std::numbers::pi) * sigma);
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

/// Blur a single channel of doubles, separable, clamp-to-edge.
inline void blur_channel(const std::vector<double>& src, std::vector<double>& dst,
                         std::uint32_t W, std::uint32_t H, double sigma)
{
    const auto k = build_gauss_kernel(sigma);
    const int kw = static_cast<int>(k.size());
    const int r = (kw - 1) / 2;
    std::vector<double> tmp(src.size(), 0.0);
    // Horizontal pass: src → tmp.
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
        {
            double acc = 0.0;
            for (int t = -r; t <= r; ++t)
            {
                const int sx = std::clamp(static_cast<int>(x) + t,
                                          0, static_cast<int>(W) - 1);
                acc += k[static_cast<std::size_t>(t + r)]
                     * src[static_cast<std::size_t>(y) * W + static_cast<std::size_t>(sx)];
            }
            tmp[static_cast<std::size_t>(y) * W + x] = acc;
        }
    // Vertical pass: tmp → dst.
    dst.assign(src.size(), 0.0);
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
        {
            double acc = 0.0;
            for (int t = -r; t <= r; ++t)
            {
                const int sy = std::clamp(static_cast<int>(y) + t,
                                          0, static_cast<int>(H) - 1);
                acc += k[static_cast<std::size_t>(t + r)]
                     * tmp[static_cast<std::size_t>(sy) * W + static_cast<std::size_t>(x)];
            }
            dst[static_cast<std::size_t>(y) * W + x] = acc;
        }
}

/// RGB (0..255) → BT.709 Y + opponent Cx + opponent Cz (each ~[-128, 128]).
inline void rgb_to_ycxcz(const std::uint8_t* rgba, std::uint32_t W, std::uint32_t H,
                         std::vector<double>& Y, std::vector<double>& Cx,
                         std::vector<double>& Cz)
{
    const std::size_t n = static_cast<std::size_t>(W) * H;
    Y.resize(n);
    Cx.resize(n);
    Cz.resize(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto r = static_cast<double>(rgba[i * 4 + 0]);
        const auto g = static_cast<double>(rgba[i * 4 + 1]);
        const auto b = static_cast<double>(rgba[i * 4 + 2]);
        Y[i]  = 0.2126 * r + 0.7152 * g + 0.0722 * b;       // luminance
        Cx[i] = (r - g) * 0.5;                              // red-green opponent
        Cz[i] = b - 0.5 * (r + g);                          // blue-yellow opponent
    }
}

}  // namespace flip_detail

/// Full FLIP-with-chroma perceptual error map. Aggregates and report
/// shape are identical to `compute_flip_lite`; the heatmap helper
/// works on both.
[[nodiscard]] inline cd::core::Result<FlipReport>
compute_flip_full(ImageView a, ImageView b, double pixels_per_degree = 67.0)
{
    if (a.rgba == nullptr || b.rgba == nullptr)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kNullPointer));
    if (a.width != b.width || a.height != b.height)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kDimensionMismatch));
    if (a.width == 0 || a.height == 0)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kEmptyImage));

    // Per-channel CSF sigmas. Luminance gets the finest σ (humans see
    // finest detail in brightness); chroma gets 1.5x σ (coarser
    // colour detail per Mullen 1985 chromatic CSF measurements).
    const double sigma_y = flip_detail::csf_sigma_pixels(pixels_per_degree);
    const double sigma_c = sigma_y * 1.5;
    const std::uint32_t W = a.width;
    const std::uint32_t H = a.height;

    std::vector<double> Y_a;
    std::vector<double> Cx_a;
    std::vector<double> Cz_a;
    std::vector<double> Y_b;
    std::vector<double> Cx_b;
    std::vector<double> Cz_b;
    flip_detail::rgb_to_ycxcz(a.rgba, W, H, Y_a, Cx_a, Cz_a);
    flip_detail::rgb_to_ycxcz(b.rgba, W, H, Y_b, Cx_b, Cz_b);

    std::vector<double> Y_a_b;
    std::vector<double> Cx_a_b;
    std::vector<double> Cz_a_b;
    std::vector<double> Y_b_b;
    std::vector<double> Cx_b_b;
    std::vector<double> Cz_b_b;
    flip_detail::blur_channel(Y_a, Y_a_b, W, H, sigma_y);
    flip_detail::blur_channel(Y_b, Y_b_b, W, H, sigma_y);
    flip_detail::blur_channel(Cx_a, Cx_a_b, W, H, sigma_c);
    flip_detail::blur_channel(Cx_b, Cx_b_b, W, H, sigma_c);
    flip_detail::blur_channel(Cz_a, Cz_a_b, W, H, sigma_c);
    flip_detail::blur_channel(Cz_b, Cz_b_b, W, H, sigma_c);

    FlipReport r;
    r.pixel_count = W * H;
    r.error_map.resize(r.pixel_count);
    double sum = 0.0;
    for (std::uint32_t i = 0; i < r.pixel_count; ++i)
    {
        const double dy = std::abs(Y_a_b[i] - Y_b_b[i]);
        const double dcx = std::abs(Cx_a_b[i] - Cx_b_b[i]);
        const double dcz = std::abs(Cz_a_b[i] - Cz_b_b[i]);

        const double e_y = flip_detail::perceptual_map(dy);
        const double e_cx = flip_detail::perceptual_map(dcx);
        const double e_cz = flip_detail::perceptual_map(dcz);
        // Weighted root: luminance dominates (0.6) over each chroma (0.2).
        const double err = std::sqrt(0.6 * e_y * e_y
                                   + 0.2 * e_cx * e_cx
                                   + 0.2 * e_cz * e_cz);
        const double err_clamped = std::min(err, 1.0);
        r.error_map[i] = err_clamped;
        sum += err_clamped;
        r.max_error = std::max(err_clamped, r.max_error);
    }
    r.mean_error = sum / static_cast<double>(r.pixel_count);

    std::vector<double> sorted = r.error_map;
    std::ranges::sort(sorted);
    const auto p95_idx = static_cast<std::size_t>(
        std::min(static_cast<double>(sorted.size() - 1),
                 static_cast<double>(sorted.size()) * 0.95));
    r.p95_error = sorted[p95_idx];
    return r;
}

}  // namespace cd::imgdiff
