// =============================================================================
// CHROMODYNAMIC — cd/render/ClearColorPreset.hpp
// Phase 86.B / Wave 254 — named clear-color choices.
//
// Common scene clear colors: cornflower blue (XNA / production legacy),
// black, magenta debug, soft gray editor. Returns a 4-component
// linear RGBA in [0, 1] — caller applies sRGB conversion if the
// target framebuffer is sRGB.
//
// Magenta is intentionally garish — when you see it, the renderer
// failed to clear properly and the previous frame is showing through.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>

namespace cd::render
{

using ClearColor = std::array<float, 4>;

[[nodiscard]] constexpr ClearColor clear_black() noexcept
{
    return { 0.0F, 0.0F, 0.0F, 1.0F };
}

[[nodiscard]] constexpr ClearColor clear_white() noexcept
{
    return { 1.0F, 1.0F, 1.0F, 1.0F };
}

[[nodiscard]] constexpr ClearColor clear_cornflower_blue() noexcept
{
    return { 0.392F, 0.584F, 0.929F, 1.0F };
}

[[nodiscard]] constexpr ClearColor clear_editor_gray() noexcept
{
    return { 0.118F, 0.118F, 0.118F, 1.0F };
}

[[nodiscard]] constexpr ClearColor clear_debug_magenta() noexcept
{
    return { 1.0F, 0.0F, 1.0F, 1.0F };
}

}  // namespace cd::render
