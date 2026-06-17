// =============================================================================
// CHROMODYNAMIC — cd/ui/Anchor.hpp
// Phase 31.B / Wave 199 — anchor + margin layout primitive.
//
// Pixel-precise layout helper for non-ImGui UI surfaces (terminal UI,
// HUD overlays, custom inspector panels). Given a parent rect and a
// child anchor descriptor, returns the child's solved rect.
//
//   Anchor.min = (mx, my) ∈ [0, 1] — point in parent space (normalized)
//   Anchor.max = (Mx, My) ∈ [0, 1] — second point (Mx >= mx, My >= my)
//   Anchor.offset_min / offset_max — pixel offsets added to each anchor
//
// When min == max the child has a fixed pixel size (offset_max -
// offset_min); when min != max the child stretches with the parent
// (Godot Control / production IntRectTransform semantics).
//
// All math is integer pixel — no float drift across resolution changes.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::ui
{

struct IntRect
{
    std::int32_t x { 0 };
    std::int32_t y { 0 };
    std::int32_t w { 0 };
    std::int32_t h { 0 };
};

struct Anchor
{
    float        min_x { 0.0F };
    float        min_y { 0.0F };
    float        max_x { 0.0F };
    float        max_y { 0.0F };
    std::int32_t offset_min_x { 0 };
    std::int32_t offset_min_y { 0 };
    std::int32_t offset_max_x { 0 };
    std::int32_t offset_max_y { 0 };
};

[[nodiscard]] inline IntRect resolve(const IntRect& parent, const Anchor& a) noexcept
{
    const auto pwf = static_cast<float>(parent.w);
    const auto phf = static_cast<float>(parent.h);
    const auto x0 = parent.x + static_cast<std::int32_t>(a.min_x * pwf) + a.offset_min_x;
    const auto y0 = parent.y + static_cast<std::int32_t>(a.min_y * phf) + a.offset_min_y;
    const auto x1 = parent.x + static_cast<std::int32_t>(a.max_x * pwf) + a.offset_max_x;
    const auto y1 = parent.y + static_cast<std::int32_t>(a.max_y * phf) + a.offset_max_y;
    return IntRect { x0, y0, x1 - x0, y1 - y0 };
}

/// Convenience: full-bleed (child fills parent).
[[nodiscard]] inline Anchor stretch() noexcept
{
    return Anchor { 0.0F, 0.0F, 1.0F, 1.0F, 0, 0, 0, 0 };
}

/// Convenience: centered fixed-size child.
[[nodiscard]] inline Anchor center(std::int32_t w, std::int32_t h) noexcept
{
    return Anchor {
        0.5F, 0.5F, 0.5F, 0.5F,
        -w / 2, -h / 2, w / 2, h / 2,
    };
}

}  // namespace cd::ui
