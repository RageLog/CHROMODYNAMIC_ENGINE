// =============================================================================
// CHROMODYNAMIC — engine/ui/font/src/FontBackend.hpp (PRIVATE)
//
// Backend-internal handle that owns FreeType + (optionally) HarfBuzz state
// for a single Font instance. Forward declared in Font.cpp so the heavy
// FT / HB headers stay confined to FreeTypeBackend.cpp + HarfBuzzShaping.cpp.
//
// This header is NOT installed and NOT part of the public API.
// =============================================================================
#pragma once

#if defined(CD_UI_FONT_HAVE_FREETYPE) && CD_UI_FONT_HAVE_FREETYPE

// Forward declare the FreeType opaque types so this header doesn't
// transitively include ft2build.h into every TU that pulls FontBackend.hpp.
struct FT_LibraryRec_;
struct FT_FaceRec_;

#if defined(CD_UI_FONT_HAVE_HARFBUZZ) && CD_UI_FONT_HAVE_HARFBUZZ
struct hb_font_t;
#endif

namespace cd::ui::font
{

struct FreeTypeBackend
{
    FT_LibraryRec_*  library { nullptr };
    FT_FaceRec_*     face    { nullptr };
    float            pixel_size { 0.0F };

#if defined(CD_UI_FONT_HAVE_HARFBUZZ) && CD_UI_FONT_HAVE_HARFBUZZ
    hb_font_t*       hb_font { nullptr };
#endif

    FreeTypeBackend() = default;
    ~FreeTypeBackend();

    FreeTypeBackend(const FreeTypeBackend&)            = delete;
    FreeTypeBackend& operator=(const FreeTypeBackend&) = delete;
    FreeTypeBackend(FreeTypeBackend&&)                 = delete;
    FreeTypeBackend& operator=(FreeTypeBackend&&)      = delete;
};

}  // namespace cd::ui::font

#else  // !CD_UI_FONT_HAVE_FREETYPE

// Stub type so Font::Impl can hold a unique_ptr<FreeTypeBackend> without
// pulling in FreeType. unique_ptr<incomplete> works as long as the dtor
// definition sees the complete type — we provide a trivial dtor here.
namespace cd::ui::font
{
struct FreeTypeBackend
{
    FreeTypeBackend() = default;
    ~FreeTypeBackend() = default;
};
}  // namespace cd::ui::font

#endif
