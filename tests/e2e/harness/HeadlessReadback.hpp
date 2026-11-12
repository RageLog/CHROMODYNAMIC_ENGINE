// =============================================================================
// CHROMODYNAMIC — tests/e2e/harness/HeadlessReadback.hpp
//
// Headless frame analysis, RGBA8 pixel buffer statistics, and color entropy /
// variance validation to verify textured rendering over flat fallback rectangles.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::e2e
{

struct FrameColorStats
{
    float mean_r { 0.0F };
    float mean_g { 0.0F };
    float mean_b { 0.0F };
    float variance_r { 0.0F };
    float variance_g { 0.0F };
    float variance_b { 0.0F };
    float total_variance { 0.0F };
    std::size_t unique_colors { 0U };
    bool is_flat_monochrome { true };
};

class HeadlessReadback final
{
public:
    [[nodiscard]] static FrameColorStats analyze_rgba8(
        std::span<const std::uint8_t> rgba_bytes,
        std::uint32_t width,
        std::uint32_t height)
    {
        FrameColorStats stats {};
        const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
        if (pixel_count == 0 || rgba_bytes.size() < pixel_count * 4U)
        {
            return stats;
        }

        double sum_r = 0.0;
        double sum_g = 0.0;
        double sum_b = 0.0;

        for (std::size_t i = 0; i < pixel_count; ++i)
        {
            sum_r += rgba_bytes[i * 4U + 0U];
            sum_g += rgba_bytes[i * 4U + 1U];
            sum_b += rgba_bytes[i * 4U + 2U];
        }

        const double inv_n = 1.0 / static_cast<double>(pixel_count);
        const double mean_r = sum_r * inv_n;
        const double mean_g = sum_g * inv_n;
        const double mean_b = sum_b * inv_n;
        stats.mean_r = static_cast<float>(mean_r);
        stats.mean_g = static_cast<float>(mean_g);
        stats.mean_b = static_cast<float>(mean_b);

        double var_r = 0.0;
        double var_g = 0.0;
        double var_b = 0.0;

        for (std::size_t i = 0; i < pixel_count; ++i)
        {
            const double dr = static_cast<double>(rgba_bytes[i * 4U + 0U]) - mean_r;
            const double dg = static_cast<double>(rgba_bytes[i * 4U + 1U]) - mean_g;
            const double db = static_cast<double>(rgba_bytes[i * 4U + 2U]) - mean_b;
            var_r += dr * dr;
            var_g += dg * dg;
            var_b += db * db;
        }

        stats.variance_r = static_cast<float>(var_r * inv_n);
        stats.variance_g = static_cast<float>(var_g * inv_n);
        stats.variance_b = static_cast<float>(var_b * inv_n);
        stats.total_variance = (stats.variance_r + stats.variance_g + stats.variance_b) / 3.0F;

        // A frame is considered flat monochrome if normalized variance is essentially zero
        stats.is_flat_monochrome = (stats.total_variance < 0.01F);

        return stats;
    }

    [[nodiscard]] static bool verify_texture_quality(
        std::span<const std::uint8_t> rgba_bytes,
        std::uint32_t width,
        std::uint32_t height,
        float min_variance_threshold = 5.0F)
    {
        const auto stats = analyze_rgba8(rgba_bytes, width, height);
        return !stats.is_flat_monochrome && stats.total_variance >= min_variance_threshold;
    }
};

}  // namespace cd::e2e
