// =============================================================================
// CHROMODYNAMIC — cd/imgdiff/ImageDiff.hpp
// Phase 5 / S4.x — golden image diff utility.
//
// Compares two RGBA8 buffers of equal dimensions and produces:
//   * `DiffReport` — count of differing pixels (above a tolerance),
//                   per-channel max absolute delta, mean abs delta,
//                   RMSE, and PSNR (dB).
//   * `highlight()` — an RGBA8 image where matching pixels are kept and
//                   differing pixels are painted bright red. Useful as a
//                   CI artifact when a golden test fails so a human can
//                   eyeball where the regression lives.
//
// Why not a full FLIP / SSIM here? Those need perceptual models that
// pull in non-trivial deps and tuning. This library is the minimum
// useful baseline — pixel-exact + tolerance-banded — that every render
// test can lean on right now. A perceptual layer can wrap this later
// without changing the API.
//
// Header-only. Depends only on cd::core (Result, ErrorCode).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cd::imgdiff
{

namespace imgdiff_errors
{
inline constexpr std::uint32_t kDomain = 0x0019;

enum class Code : std::uint32_t
{
    kOk = 0,
    kDimensionMismatch = 1,
    kEmptyImage = 2,
    kNullPointer = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace imgdiff_errors

/// Non-owning view over an RGBA8 image buffer. The buffer must be at
/// least `width * height * 4` bytes; row-major, top-left origin.
struct ImageView
{
    const std::uint8_t* rgba { nullptr };
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
};

struct DiffReport
{
    std::uint32_t pixel_count { 0 };
    std::uint32_t different_pixels { 0 };  ///< Above the tolerance band.
    std::uint8_t max_abs_delta_r { 0 };
    std::uint8_t max_abs_delta_g { 0 };
    std::uint8_t max_abs_delta_b { 0 };
    std::uint8_t max_abs_delta_a { 0 };
    double mean_abs_delta { 0.0 };  ///< Average over all 4 channels.
    double rmse { 0.0 };            ///< In 0-255 units.
    double psnr_db { 0.0 };         ///< +∞ when images are identical; clamped to 999 in that case.
};

/// Compare two RGBA8 images. `tolerance` is the max absolute per-channel
/// delta that still counts as "the same pixel" (so the test can ignore
/// 1-LSB float-roundoff noise from the renderer). Returns
/// kDimensionMismatch when the views' sizes disagree.
[[nodiscard]] inline cd::core::Result<DiffReport>
compare(ImageView a, ImageView b, std::uint8_t tolerance = 0)
{
    if (a.rgba == nullptr || b.rgba == nullptr)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kNullPointer));
    if (a.width != b.width || a.height != b.height)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kDimensionMismatch));
    if (a.width == 0 || a.height == 0)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kEmptyImage));

    DiffReport r;
    r.pixel_count = a.width * a.height;
    double sum_abs = 0.0;
    double sum_sq = 0.0;
    const std::size_t n = static_cast<std::size_t>(r.pixel_count) * 4U;
    for (std::size_t i = 0; i < n; i += 4)
    {
        const auto dr = static_cast<int>(a.rgba[i + 0]) - static_cast<int>(b.rgba[i + 0]);
        const auto dg = static_cast<int>(a.rgba[i + 1]) - static_cast<int>(b.rgba[i + 1]);
        const auto db = static_cast<int>(a.rgba[i + 2]) - static_cast<int>(b.rgba[i + 2]);
        const auto da = static_cast<int>(a.rgba[i + 3]) - static_cast<int>(b.rgba[i + 3]);
        const auto adr = static_cast<std::uint8_t>(dr < 0 ? -dr : dr);
        const auto adg = static_cast<std::uint8_t>(dg < 0 ? -dg : dg);
        const auto adb = static_cast<std::uint8_t>(db < 0 ? -db : db);
        const auto ada = static_cast<std::uint8_t>(da < 0 ? -da : da);
        r.max_abs_delta_r = std::max(r.max_abs_delta_r, adr);
        r.max_abs_delta_g = std::max(r.max_abs_delta_g, adg);
        r.max_abs_delta_b = std::max(r.max_abs_delta_b, adb);
        r.max_abs_delta_a = std::max(r.max_abs_delta_a, ada);
        if (adr > tolerance || adg > tolerance || adb > tolerance || ada > tolerance)
            ++r.different_pixels;
        sum_abs += static_cast<double>(adr) + static_cast<double>(adg)
                 + static_cast<double>(adb) + static_cast<double>(ada);
        sum_sq += static_cast<double>(dr * dr) + static_cast<double>(dg * dg)
                + static_cast<double>(db * db) + static_cast<double>(da * da);
    }
    const auto channel_count = static_cast<double>(n);
    r.mean_abs_delta = sum_abs / channel_count;
    const double mse = sum_sq / channel_count;
    r.rmse = std::sqrt(mse);
    constexpr double kPsnrSentinelMax = 999.0;
    if (mse <= 0.0)
        r.psnr_db = kPsnrSentinelMax;
    else
        r.psnr_db = 20.0 * std::log10(255.0 / r.rmse);
    return r;
}

/// Build an RGBA8 highlight image: pixels within `tolerance` are kept
/// from `a` (slightly darkened so the eye can pick out the overlay);
/// pixels above the tolerance are painted opaque red. Returns the same
/// errors as `compare`.
[[nodiscard]] inline cd::core::Result<std::vector<std::uint8_t>>
highlight(ImageView a, ImageView b, std::uint8_t tolerance = 0)
{
    if (a.rgba == nullptr || b.rgba == nullptr)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kNullPointer));
    if (a.width != b.width || a.height != b.height)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kDimensionMismatch));
    if (a.width == 0 || a.height == 0)
        return std::unexpected(imgdiff_errors::make(imgdiff_errors::Code::kEmptyImage));

    std::vector<std::uint8_t> out(static_cast<std::size_t>(a.width) * a.height * 4U);
    const std::size_t n = out.size();
    for (std::size_t i = 0; i < n; i += 4)
    {
        const auto dr = std::abs(static_cast<int>(a.rgba[i + 0]) - static_cast<int>(b.rgba[i + 0]));
        const auto dg = std::abs(static_cast<int>(a.rgba[i + 1]) - static_cast<int>(b.rgba[i + 1]));
        const auto db = std::abs(static_cast<int>(a.rgba[i + 2]) - static_cast<int>(b.rgba[i + 2]));
        const auto da = std::abs(static_cast<int>(a.rgba[i + 3]) - static_cast<int>(b.rgba[i + 3]));
        const bool differs = (dr > tolerance) || (dg > tolerance) || (db > tolerance) || (da > tolerance);
        if (differs)
        {
            out[i + 0] = 255;
            out[i + 1] = 0;
            out[i + 2] = 0;
            out[i + 3] = 255;
        }
        else
        {
            // Darken matching pixels to ~50% so the red overlay pops.
            out[i + 0] = static_cast<std::uint8_t>(a.rgba[i + 0] / 2);
            out[i + 1] = static_cast<std::uint8_t>(a.rgba[i + 1] / 2);
            out[i + 2] = static_cast<std::uint8_t>(a.rgba[i + 2] / 2);
            out[i + 3] = a.rgba[i + 3];
        }
    }
    return out;
}

/// Convenience: returns true if `report.different_pixels <= max_failures`.
/// Folds the threshold into one boolean for test fixtures.
[[nodiscard]] inline bool passes(const DiffReport& report, std::uint32_t max_failures = 0) noexcept
{
    return report.different_pixels <= max_failures;
}

}  // namespace cd::imgdiff
