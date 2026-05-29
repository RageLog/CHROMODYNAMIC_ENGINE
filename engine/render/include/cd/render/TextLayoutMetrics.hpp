// =============================================================================
// CHROMODYNAMIC — cd/render/TextLayoutMetrics.hpp
// Phase 76.A / Wave 244 — text bounding-box + line metrics.
//
// Coarse text layout estimation for editor panels — actual glyph
// rendering is the font engine's job. This struct describes what the
// rendered string occupies on screen given a font size:
//
//   width  — sum of advance widths (or wrap-line longest line).
//   height — line_count * line_height.
//
// `measure_simple(text, font_size_px, char_advance_px)` is the
// monospace approximation used until a real font metrics backend
// is wired (FreeType / DirectWrite).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string_view>

namespace cd::render
{

struct TextLayoutMetrics
{
    float width        { 0.0F };
    float height       { 0.0F };
    std::uint32_t line_count { 1 };
};

[[nodiscard]] inline TextLayoutMetrics measure_simple(
    std::string_view text,
    float font_size_px = 14.0F,
    float char_advance_px = 7.0F) noexcept
{
    TextLayoutMetrics m;
    if (text.empty()) return m;

    std::uint32_t lines = 1;
    float longest = 0.0F;
    float running = 0.0F;
    for (char c : text)
    {
        if (c == '\n')
        {
            if (running > longest) longest = running;
            running = 0.0F;
            ++lines;
        }
        else
        {
            running += char_advance_px;
        }
    }
    if (running > longest) longest = running;

    m.width      = longest;
    m.line_count = lines;
    m.height     = static_cast<float>(lines) * font_size_px;
    return m;
}

}  // namespace cd::render
