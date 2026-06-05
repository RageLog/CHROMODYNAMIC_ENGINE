// =============================================================================
// CHROMODYNAMIC — engine/ui/font/src/HarfBuzzShaping.cpp
//
// Phase 4 (T2.4) HarfBuzz BiDi + ligature shaping. Compiled ONLY when
// BOTH CD_UI_FONT_HAVE_FREETYPE and CD_UI_FONT_HAVE_HARFBUZZ are defined.
//
// Exposes one function: hb_shape::shape() — called by Font::shape() to
// turn a UTF-8 string + locale tag into a sequence of ShapedGlyph
// records. Direction is auto-detected from the script (HB_SCRIPT_ARABIC
// / HB_SCRIPT_HEBREW → RTL; everything else → LTR), so RTL strings come
// back with the glyph_id sequence already reversed relative to the
// codepoint order in the input — exactly what the renderer wants for
// left-to-right pen advance.
// =============================================================================
#include <cd/ui/font/Font.hpp>

#include "FontBackend.hpp"

#if defined(CD_UI_FONT_HAVE_FREETYPE) && CD_UI_FONT_HAVE_FREETYPE \
 && defined(CD_UI_FONT_HAVE_HARFBUZZ) && CD_UI_FONT_HAVE_HARFBUZZ

#include <hb.h>
#include <hb-ft.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace cd::ui::font
{
namespace hb_shape
{

std::vector<ShapedGlyph> shape(const FreeTypeBackend& ft,
                               std::string_view       text,
                               std::string_view       locale,
                               float                  /*scale*/)
{
    std::vector<ShapedGlyph> out;
    if (!ft.hb_font || text.empty()) return out;

    hb_buffer_t* buf = hb_buffer_create();
    if (!buf) return out;

    hb_buffer_add_utf8(buf,
                       text.data(),
                       static_cast<int>(text.size()),
                       0,
                       static_cast<int>(text.size()));

    // Let HB guess script + direction from the codepoints. We override
    // language with the caller-supplied locale tag so OpenType features
    // (e.g. Turkish dotless-i shaping) trigger correctly.
    hb_buffer_guess_segment_properties(buf);
    if (!locale.empty())
    {
        hb_buffer_set_language(buf,
                               hb_language_from_string(locale.data(),
                                                       static_cast<int>(locale.size())));
    }

    ::hb_shape(ft.hb_font, buf, nullptr, 0);  // :: qualifies past namespace hb_shape

    unsigned int glyph_count = 0;
    hb_glyph_info_t*     infos = hb_buffer_get_glyph_infos(buf, &glyph_count);
    hb_glyph_position_t* poss  = hb_buffer_get_glyph_positions(buf, &glyph_count);

    out.reserve(glyph_count);
    for (unsigned int i = 0; i < glyph_count; ++i)
    {
        ShapedGlyph g {};
        g.glyph_id  = infos[i].codepoint;             // GID after shaping
        // HB advances are in 26.6 font units. With hb-ft those are
        // already-scaled pixels in 26.6 so dividing by 64 yields pixels
        // at the FT size we configured.
        g.advance_x = static_cast<float>(poss[i].x_advance) / 64.0F;
        g.offset_x  = static_cast<float>(poss[i].x_offset)  / 64.0F;
        g.offset_y  = static_cast<float>(poss[i].y_offset)  / 64.0F;
        out.push_back(g);
    }

    hb_buffer_destroy(buf);
    return out;
}

}  // namespace hb_shape
}  // namespace cd::ui::font

#endif  // CD_UI_FONT_HAVE_FREETYPE && CD_UI_FONT_HAVE_HARFBUZZ
