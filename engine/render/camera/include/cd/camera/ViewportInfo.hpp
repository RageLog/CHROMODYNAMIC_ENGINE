// =============================================================================
// CHROMODYNAMIC — cd/camera/ViewportInfo.hpp
// Phase 61.B / Wave 229 — viewport rect + screen ↔ NDC helpers.
//
// A `ViewportInfo` is the (x, y, width, height) integer pixel rect
// the renderer is drawing into. The accompanying helpers convert
// between screen-space (pixels, top-left origin) and normalized
// device coordinates (NDC, -1 to 1, top-left = (-1, 1) by Vulkan
// convention).
//
//   screen_to_ndc((px, py))  →  (x_ndc, y_ndc)
//   ndc_to_screen((x_ndc, y_ndc)) →  (px, py)
//
// `aspect()` returns width / height (or 1 for zero height).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::camera
{

struct ViewportInfo
{
    std::int32_t x { 0 };
    std::int32_t y { 0 };
    std::uint32_t width  { 0 };
    std::uint32_t height { 0 };
};

[[nodiscard]] inline float aspect(const ViewportInfo& v) noexcept
{
    return (v.height == 0)
        ? 1.0F
        : static_cast<float>(v.width) / static_cast<float>(v.height);
}

/// Screen-space pixel (px, py) → NDC (x_ndc, y_ndc) with NDC top-left
/// at (-1, 1). The viewport's origin (`v.x`, `v.y`) is subtracted first.
inline void screen_to_ndc(const ViewportInfo& v,
                          float px, float py,
                          float& x_ndc, float& y_ndc) noexcept
{
    const float w = (v.width  > 0) ? static_cast<float>(v.width)  : 1.0F;
    const float h = (v.height > 0) ? static_cast<float>(v.height) : 1.0F;
    const float u = (px - static_cast<float>(v.x)) / w;
    const float vv = (py - static_cast<float>(v.y)) / h;
    x_ndc =  (2.0F * u) - 1.0F;
    y_ndc = -(2.0F * vv) + 1.0F;   // y flips: top-left pixel maps to +1
}

inline void ndc_to_screen(const ViewportInfo& v,
                          float x_ndc, float y_ndc,
                          float& px, float& py) noexcept
{
    const float w = static_cast<float>(v.width);
    const float h = static_cast<float>(v.height);
    const float u  = (x_ndc + 1.0F) * 0.5F;
    const float vv = (1.0F - y_ndc) * 0.5F;
    px = static_cast<float>(v.x) + u  * w;
    py = static_cast<float>(v.y) + vv * h;
}

}  // namespace cd::camera
