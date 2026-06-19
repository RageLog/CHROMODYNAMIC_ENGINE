// =============================================================================
// CHROMODYNAMIC — cd/math/Color.hpp
// Phase 23.B / Wave 188 — HSV ↔ RGB color-space conversion.
//
// Used by editor color pickers, particle gradient lookups, and any
// shader-side ColorRamp / heatmap visualization. All channels are
// normalized floats in [0, 1].
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>

namespace cd::math
{

[[nodiscard]] inline Vec3f hsv_to_rgb(float h, float s, float v) noexcept
{
    if (s <= 0.0F) return Vec3f { v, v, v };
    // Wrap h into [0, 1).
    h -= std::floor(h);
    const float h6 = h * 6.0F;
    const int  sector = static_cast<int>(h6);
    const float f = h6 - static_cast<float>(sector);
    const float p = v * (1.0F - s);
    const float q = v * (1.0F - s * f);
    const float t = v * (1.0F - s * (1.0F - f));
    switch (sector)
    {
        case 0: return { v, t, p };
        case 1: return { q, v, p };
        case 2: return { p, v, t };
        case 3: return { p, q, v };
        case 4: return { t, p, v };
        default: return { v, p, q };
    }
}

[[nodiscard]] inline Vec3f rgb_to_hsv(float r, float g, float b) noexcept
{
    const float mx = std::max({ r, g, b });
    const float mn = std::min({ r, g, b });
    const float d  = mx - mn;
    const float v = mx;
    const float s = mx > 0.0F ? d / mx : 0.0F;
    if (d <= 0.0F) return Vec3f { 0.0F, 0.0F, v };
    float h = 0.0F;
    if (mx == r)      h = (g - b) / d + (g < b ? 6.0F : 0.0F);
    else if (mx == g) h = (b - r) / d + 2.0F;
    else              h = (r - g) / d + 4.0F;
    h /= 6.0F;
    return Vec3f { h, s, v };
}

// Phase 25.B — additional color helpers.

[[nodiscard]] inline Vec3f lerp_rgb(const Vec3f& a, const Vec3f& b, float t) noexcept
{
    t = std::max(t, 0.0F);
    t = std::min(t, 1.0F);
    return Vec3f {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

[[nodiscard]] inline float linear_to_srgb(float linear) noexcept
{
    if (linear <= 0.0031308F) return 12.92F * linear;
    return 1.055F * std::pow(linear, 1.0F / 2.4F) - 0.055F;
}

[[nodiscard]] inline float srgb_to_linear(float srgb) noexcept
{
    if (srgb <= 0.04045F) return srgb / 12.92F;
    return std::pow((srgb + 0.055F) / 1.055F, 2.4F);
}

}  // namespace cd::math
