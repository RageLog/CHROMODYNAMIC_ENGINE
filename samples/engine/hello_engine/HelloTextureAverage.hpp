// =============================================================================
// HelloTextureAverage.hpp
// -----------------------------------------------------------------------------
// phase822-rt-chrome-sponza-tex-avg-extract: stdlib-only header that
// provides the texture-average colour helper used by HelloGltf.hpp.
// Kept dependency-free (no cd::rhi / no Vulkan headers) so a unit
// test can include it without dragging in the entire RHI stack —
// the same isolation pattern HelloRayQuery.hpp's static_assert
// avoids by not being includable from a test TU.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace cd_sample
{

// ---------------------------------------------------------------------------
// compute_texture_average_alpha_weighted
//
// Returns the **alpha-weighted average colour** of an RGBA8 texture,
// or std::nullopt when the sum of alpha weights is zero (every
// sampled pixel was fully transparent).
//
// `stride` (default 16) is the sparse-sample step in both axes —
// sub-millisecond for a 4 K texture; the chrome reflection use case
// is "good enough to pick a dominant colour", not "pixel-accurate".
//
// `rgba.size()` is expected to be `width * height * 4` but we
// tolerate undersized buffers (the off + 3 bounds check guards).
// ---------------------------------------------------------------------------
[[nodiscard]] inline std::optional<std::array<float, 4>>
compute_texture_average_alpha_weighted(
    std::span<const std::uint8_t> rgba,
    std::uint32_t                 width,
    std::uint32_t                 height,
    std::uint32_t                 stride = 16U) noexcept
{
    if (width == 0 || height == 0 || stride == 0 || rgba.empty())
        return std::nullopt;
    double sum_r = 0.0;
    double sum_g = 0.0;
    double sum_b = 0.0;
    double sum_a = 0.0;
    for (std::uint32_t y = 0; y < height; y += stride)
    {
        for (std::uint32_t x = 0; x < width; x += stride)
        {
            const std::size_t off =
                (static_cast<std::size_t>(y) * width + x) * 4U;
            if (off + 3U >= rgba.size()) continue;
            const double w =
                static_cast<double>(rgba[off + 3U]) / 255.0;
            sum_r += static_cast<double>(rgba[off + 0U]) * w;
            sum_g += static_cast<double>(rgba[off + 1U]) * w;
            sum_b += static_cast<double>(rgba[off + 2U]) * w;
            sum_a += w;
        }
    }
    if (sum_a <= 0.0)
        return std::nullopt;
    return std::array<float, 4> {
        static_cast<float>(sum_r / sum_a / 255.0),
        static_cast<float>(sum_g / sum_a / 255.0),
        static_cast<float>(sum_b / sum_a / 255.0),
        1.0F
    };
}

}  // namespace cd_sample
