// =============================================================================
// CHROMODYNAMIC — cd/ui/Theme.hpp
// Phase 40.B / Wave 208 — color palette + spacing tokens.
//
// A central place for non-ImGui UI surfaces (terminal UI, custom HUD,
// dropdown popups built on cd::ui::Anchor) to share a consistent
// visual language without hard-coding hex codes at every widget.
//
// Two built-in themes (Dark / Light) covering background, surface,
// primary, danger, accent, on-* foregrounds. Caller-extension is
// possible by deriving from `Theme` and overriding fields.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::ui
{

struct Color32
{
    std::uint8_t r { 0 };
    std::uint8_t g { 0 };
    std::uint8_t b { 0 };
    std::uint8_t a { 255 };

    friend constexpr bool operator==(Color32, Color32) noexcept = default;
};

struct Theme
{
    Color32 background { 0x1E, 0x1E, 0x1E };
    Color32 surface    { 0x2A, 0x2A, 0x2A };
    Color32 primary    { 0x44, 0x99, 0xFF };
    Color32 danger     { 0xFF, 0x55, 0x55 };
    Color32 accent     { 0xFF, 0xAA, 0x33 };
    Color32 on_background { 0xEE, 0xEE, 0xEE };
    Color32 on_surface    { 0xDD, 0xDD, 0xDD };
    Color32 on_primary    { 0xFF, 0xFF, 0xFF };

    // Spacing tokens (px @ 1.0 DPI scale).
    std::int32_t pad_xs { 2 };
    std::int32_t pad_s  { 4 };
    std::int32_t pad_m  { 8 };
    std::int32_t pad_l  { 16 };
    std::int32_t pad_xl { 32 };
    std::int32_t corner_radius { 4 };
};

[[nodiscard]] inline Theme dark_theme() noexcept
{
    return Theme {};   // defaults are the dark theme
}

[[nodiscard]] inline Theme light_theme() noexcept
{
    Theme t;
    t.background    = { 0xF2, 0xF2, 0xF2 };
    t.surface       = { 0xFF, 0xFF, 0xFF };
    t.primary       = { 0x22, 0x66, 0xCC };
    t.danger        = { 0xCC, 0x33, 0x33 };
    t.accent        = { 0xCC, 0x77, 0x11 };
    t.on_background = { 0x11, 0x11, 0x11 };
    t.on_surface    = { 0x22, 0x22, 0x22 };
    t.on_primary    = { 0xFF, 0xFF, 0xFF };
    return t;
}

}  // namespace cd::ui
