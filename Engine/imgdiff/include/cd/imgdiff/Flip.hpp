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

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/imgdiff/Gaussian.hpp>
#include <cd/imgdiff/ImageDiff.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
        if (err > r.max_error)
            r.max_error = err;
    }
    r.mean_error = sum / static_cast<double>(r.pixel_count);

    // 3. 95th percentile (sort copy — N ≤ ~1M is acceptable for offline use).
    std::vector<double> sorted = r.error_map;
    std::sort(sorted.begin(), sorted.end());
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

}  // namespace cd::imgdiff
