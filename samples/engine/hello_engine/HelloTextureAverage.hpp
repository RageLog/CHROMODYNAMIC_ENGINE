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

// ---------------------------------------------------------------------------
// fold_texture_avg_into_factor
//
// Combine the glTF material's `base_color_factor` (RGBA) with the
// texture-average colour computed by
// compute_texture_average_alpha_weighted. The glTF spec composes
// material factor and texture as a per-channel **product** at shade
// time; the SSBO path mirrors that — a (1,1,1) factor times a red
// texture average yields red, while a (0.5, 0.5, 0.5) factor halves
// the texture-derived colour.
//
// Behavior:
//   - When `texture_avg` has a value, returns
//     `{ factor.r*avg.r, factor.g*avg.g, factor.b*avg.b, factor.a }`.
//   - When `texture_avg` is std::nullopt (no valid texture average),
//     returns the factor unchanged. Callers that branched on
//     "did the avg succeed?" can drop the branch and just call this.
// ---------------------------------------------------------------------------
[[nodiscard]] inline std::array<float, 4>
fold_texture_avg_into_factor(
    const std::array<float, 4>&                factor,
    const std::optional<std::array<float, 4>>& texture_avg) noexcept
{
    if (!texture_avg.has_value())
        return factor;
    return {
        factor[0] * (*texture_avg)[0],
        factor[1] * (*texture_avg)[1],
        factor[2] * (*texture_avg)[2],
        factor[3],
    };
}

}  // namespace cd_sample
