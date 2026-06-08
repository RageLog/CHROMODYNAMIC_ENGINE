// =============================================================================
// CHROMODYNAMIC — engine/ui/font/src/FreeTypeBackend.cpp
//
// Phase 4 (T2.4) FreeType outline rasterizer + HarfBuzz font handle
// initialisation. Compiled ONLY when CD_UI_FONT_HAVE_FREETYPE is defined.
//
// This TU owns the FT_Library + FT_Face lifetime AND, when HB is
// available, the hb_font_t built on top. Glyph rasterization writes an
// 8-bit alpha bitmap into the caller-supplied vector — atlas packing
// stays in Font.cpp.
// =============================================================================
#include <cd/ui/font/Font.hpp>

#include "FontBackend.hpp"

#if !defined(CD_UI_FONT_HAVE_FREETYPE) || !CD_UI_FONT_HAVE_FREETYPE
// Nothing to compile — Font.cpp provides inline stubs.
#else

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#if defined(CD_UI_FONT_HAVE_HARFBUZZ) && CD_UI_FONT_HAVE_HARFBUZZ
#include <hb.h>
#include <hb-ft.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace cd::ui::font
{

FreeTypeBackend::~FreeTypeBackend()
{
#if defined(CD_UI_FONT_HAVE_HARFBUZZ) && CD_UI_FONT_HAVE_HARFBUZZ
    if (hb_font)
    {
        hb_font_destroy(hb_font);
        hb_font = nullptr;
    }
#endif
    if (face)
    {
        FT_Done_Face(face);
        face = nullptr;
    }
    if (library)
    {
        FT_Done_FreeType(library);
        library = nullptr;
    }
}

namespace ft_backend
{

bool load(FreeTypeBackend& ft, std::span<const std::uint8_t> data, float pixel_size,
          int& out_ascent, int& out_descent, int& out_line_gap, float& out_scale)
{
    if (FT_Init_FreeType(&ft.library) != 0) return false;

    // FT requires the byte buffer to outlive the face. Font::Impl owns
    // ttf_bytes for the Font's lifetime, so passing the raw pointer here
    // is safe.
    if (FT_New_Memory_Face(ft.library,
                           reinterpret_cast<const FT_Byte*>(data.data()),
                           static_cast<FT_Long>(data.size()),
                           0,
                           &ft.face) != 0)
    {
        FT_Done_FreeType(ft.library);
        ft.library = nullptr;
        return false;
    }

    const auto size_26dot6 = static_cast<FT_F26Dot6>(pixel_size * 64.0F);
    if (FT_Set_Char_Size(ft.face, 0, size_26dot6, 96, 96) != 0)
    {
        // Fall back to pixel sizes if metric resolution can't be set.
        if (FT_Set_Pixel_Sizes(ft.face, 0, static_cast<FT_UInt>(pixel_size)) != 0)
        {
            return false;
        }
    }

    ft.pixel_size = pixel_size;

    // FT exposes metrics in 26.6 fixed-point font units that have
    // ALREADY been scaled to the requested pixel size — we report them in
    // FUnits + scale to keep parity with the stb code path (which reports
    // FUnits + a separate scale factor). Compute a consistent scale by
    // dividing the scaled ascender (in pixels) by the unscaled units_per_EM.
    const auto upm = static_cast<float>(ft.face->units_per_EM);
    const float scale = (upm > 0.0F) ? (pixel_size / upm) : 1.0F;

    out_scale    = scale;
    out_ascent   = static_cast<int>(ft.face->ascender);
    out_descent  = static_cast<int>(ft.face->descender);
    out_line_gap = static_cast<int>(ft.face->height - (ft.face->ascender - ft.face->descender));

#if defined(CD_UI_FONT_HAVE_HARFBUZZ) && CD_UI_FONT_HAVE_HARFBUZZ
    ft.hb_font = hb_ft_font_create_referenced(ft.face);
    if (ft.hb_font) hb_ft_font_set_funcs(ft.hb_font);
#endif

    return true;
}

bool rasterize_glyph(FreeTypeBackend& ft, std::uint32_t codepoint,
                     std::vector<std::uint8_t>& dst_alpha,
                     int& w, int& h,
                     float& bearing_x, float& bearing_y, float& advance)
{
    if (!ft.face) return false;
    const FT_UInt gi = FT_Get_Char_Index(ft.face, codepoint);
    if (gi == 0) return false;

    if (FT_Load_Glyph(ft.face, gi, FT_LOAD_DEFAULT) != 0) return false;

    FT_GlyphSlot slot = ft.face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_BITMAP)
    {
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) return false;
    }

    const FT_Bitmap& bmp = slot->bitmap;
    w = static_cast<int>(bmp.width);
    h = static_cast<int>(bmp.rows);
    bearing_x = static_cast<float>(slot->bitmap_left);
    bearing_y = static_cast<float>(slot->bitmap_top);
    // advance.x is in 26.6 fixed-point pixels.
    advance   = static_cast<float>(slot->advance.x) / 64.0F;

    if (w == 0 || h == 0)
    {
        dst_alpha.clear();
        return true;
    }

    dst_alpha.assign(static_cast<std::size_t>(w) * h, std::uint8_t { 0 });

    // FT bitmap pitch can be negative (top-down vs. bottom-up) and the
    // row stride may exceed width. Copy row-by-row to normalise.
    const auto pitch = bmp.pitch;
    const auto absp  = pitch < 0 ? -pitch : pitch;
    const std::uint8_t* src = bmp.buffer;
    for (int row = 0; row < h; ++row)
    {
        const std::uint8_t* src_row =
            (pitch >= 0) ? (src + static_cast<std::size_t>(row) * static_cast<std::size_t>(absp))
                         : (src + static_cast<std::size_t>(h - 1 - row) * static_cast<std::size_t>(absp));
        std::memcpy(dst_alpha.data() + static_cast<std::size_t>(row) * w,
                    src_row,
                    static_cast<std::size_t>(w));
    }
    return true;
}

}  // namespace ft_backend
}  // namespace cd::ui::font

#endif  // CD_UI_FONT_HAVE_FREETYPE
