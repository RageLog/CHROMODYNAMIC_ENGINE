// =============================================================================
// CHROMODYNAMIC — cd/ui/font/Font.hpp
//
// Phase 1.1 + Phase 4 (T2.4) of ADR-20260530-ui-widget-library.
//
//   Phase 1.1 surface (PRESERVED)
//     load_ttf_in_memory / rasterize_range / glyph_uv / kerning / atlas /
//     pixel_size / line_height / is_loaded
//
//   Phase 4 additions (T2.4)
//     ShapedGlyph + Font::shape() — HarfBuzz BiDi + ligature shaping when
//       built with CD_UI_FONT_HAVE_HARFBUZZ; identity fallback otherwise.
//     AtlasMode::kAlpha (Phase 1, default) | kMsdf (Phase 4) — when kMsdf is
//       selected before rasterize_range(), the atlas pixels store an unsigned
//       8-SSED Euclidean SDF approximation derived from the alpha bitmap.
//       atlas_mode() / sdf_zero() expose what the renderer needs to sample
//       the SDF correctly (the zero level lives at 128 by convention; pixels
//       > 128 are INSIDE the glyph, < 128 are OUTSIDE).
//     load_ttf / select_backend — runtime backend probe + selection wrapper
//       around load_ttf_in_memory. Default backend is FreeType when built
//       with CD_UI_FONT_HAVE_FREETYPE, else stb_truetype.
//
// Backend matrix (compile-time, set by engine/ui/font/CMakeLists.txt):
//   * CD_UI_FONT_HAVE_FREETYPE = 1  → FreeType outline rasterizer available
//   * CD_UI_FONT_HAVE_HARFBUZZ = 1  → HarfBuzz shape() available
//   * Neither defined            → stb_truetype-only fallback (existing
//                                   behaviour, 100% source-compatible).
//
// All public types and the existing Phase 1.1 public methods keep their
// exact pre-T2.4 signatures so cd::ui_widgets (T2.1–T2.3) keeps compiling
// untouched.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::ui::font
{

/// One glyph's atlas slot and font-metric data.
struct GlyphInfo
{
    float u0 { 0.0F }, v0 { 0.0F };   ///< top-left atlas UV (0..1)
    float u1 { 0.0F }, v1 { 0.0F };   ///< bottom-right atlas UV (0..1)
    float width  { 0.0F };            ///< rendered width in pixels
    float height { 0.0F };            ///< rendered height in pixels
    float bearing_x { 0.0F };         ///< horizontal offset to draw origin
    float bearing_y { 0.0F };         ///< baseline-to-top distance (positive up)
    float advance   { 0.0F };         ///< horizontal pen advance after this glyph
};

/// Atlas pixel buffer + dimensions. The pixel format is single-channel
/// 8-bit — coverage when atlas_mode() == kAlpha, signed-distance-field
/// (biased to unsigned with zero at 128) when atlas_mode() == kMsdf.
struct AtlasBitmap
{
    std::vector<std::uint8_t> pixels;   ///< width * height bytes, row-major
    std::uint32_t             width  { 0U };
    std::uint32_t             height { 0U };
};

/// One shaped glyph produced by Font::shape(). With HarfBuzz available
/// the glyph_id corresponds to a real GID inside the font; without it
/// the field is set to the Unicode codepoint and the shaper is identity.
struct ShapedGlyph
{
    std::uint32_t glyph_id { 0U };   ///< font glyph index (HB) or codepoint (fallback)
    float         advance_x { 0.0F };
    float         offset_x  { 0.0F };
    float         offset_y  { 0.0F };
};

/// Selects how `rasterize_range` populates the atlas bitmap. Switch
/// before the first rasterize_range() call. Switching after rasterization
/// has begun is a no-op.
enum class AtlasMode : std::uint8_t
{
    kAlpha = 0,   ///< 8-bit coverage (Phase 1 default).
    kMsdf  = 1,   ///< 8-bit unsigned SDF (Phase 4); zero level at value 128.
};

/// Which backend should rasterize outlines. Default == kAuto picks
/// FreeType when CD_UI_FONT_HAVE_FREETYPE, stb_truetype otherwise. The
/// switch is per-Font (so a renderer can A/B compare backends).
enum class Backend : std::uint8_t
{
    kAuto     = 0,
    kStb      = 1,
    kFreeType = 2,
};

/// One rasterized font face at one pixel size. Multiple sizes / faces
/// of the same TTF live in separate `Font` instances by design (atlas
/// pages don't share between sizes; renderer batches per-page).
class Font
{
public:
    Font();
    ~Font();

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;

    /// Select rasterizer backend BEFORE load_ttf_in_memory(). When
    /// CD_UI_FONT_HAVE_FREETYPE is not defined, kFreeType silently falls
    /// back to kStb. Returns the backend that will actually be used.
    Backend select_backend(Backend desired) noexcept;

    /// Which backend is currently active. kAuto resolves at load time.
    [[nodiscard]] Backend backend() const noexcept { return active_backend_; }

    /// Select atlas storage format BEFORE the first rasterize_range().
    /// Returns the mode that will actually be used (kMsdf falls back to
    /// kAlpha when no rasterized glyph exists to seed the SDF).
    AtlasMode select_atlas_mode(AtlasMode desired) noexcept;

    /// Current atlas mode (kAlpha by default).
    [[nodiscard]] AtlasMode atlas_mode() const noexcept { return atlas_mode_; }

    /// The pixel value that marks the zero level of an MSDF atlas. By
    /// convention 128 — pixels strictly greater than this are INSIDE the
    /// glyph, strictly less are OUTSIDE. Always 128 for kMsdf, undefined
    /// (returns 0) for kAlpha.
    [[nodiscard]] std::uint8_t sdf_zero() const noexcept
    {
        return atlas_mode_ == AtlasMode::kMsdf ? std::uint8_t { 128U } : std::uint8_t { 0U };
    }

    /// Parse a TTF / OTF byte blob. The caller retains ownership of `data`
    /// only for the duration of this call -- `Font` does not retain a
    /// pointer into it. Returns false when the blob is unrecognisable
    /// (corrupted header, unsupported variant).
    [[nodiscard]] bool load_ttf_in_memory(std::span<const std::uint8_t> data);

    /// Convenience alias matching the T2.4 brief naming.
    [[nodiscard]] bool load_ttf(std::span<const std::uint8_t> data)
    {
        return load_ttf_in_memory(data);
    }

    /// Rasterize codepoints `[first, last]` (inclusive) at `pixel_size`
    /// into a single atlas. The atlas grows on demand up to `max_dim`
    /// per side. Returns false when bin-pack fails (need bigger atlas).
    [[nodiscard]] bool rasterize_range(std::uint32_t first_codepoint,
                                       std::uint32_t last_codepoint,
                                       float         pixel_size,
                                       std::uint32_t max_dim);

    /// Look up the atlas slot for a codepoint. Returns `nullopt` for
    /// missing glyphs (the renderer should fall back to a `?` or
    /// substitute drawing).
    [[nodiscard]] std::optional<GlyphInfo> glyph_uv(std::uint32_t codepoint) const noexcept;

    /// Read access to the rasterized atlas (alpha when kAlpha, SDF when kMsdf).
    [[nodiscard]] const AtlasBitmap& atlas() const noexcept { return atlas_; }

    /// Pixel size this font was rasterized at.
    [[nodiscard]] float pixel_size() const noexcept { return pixel_size_; }

    /// Line height in pixels.
    [[nodiscard]] float line_height() const noexcept { return line_height_; }

    /// Kerning advance between two codepoints (already-rasterized). Zero
    /// when the font has no kerning pair for `(a, b)`.
    [[nodiscard]] float kerning(std::uint32_t a, std::uint32_t b) const noexcept;

    /// True when the font has been parsed (load_ttf_in_memory succeeded).
    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }

    /// HarfBuzz-driven shaping (BiDi reorder + ligature substitution).
    /// `text` is interpreted as UTF-8. `locale` selects language tags HB
    /// uses for script-specific OpenType features ("en", "ar", "he",
    /// "tr-TR", ...). When CD_UI_FONT_HAVE_HARFBUZZ is not defined OR
    /// the active backend is stb_truetype, a 1:1 identity shaping is
    /// returned (one ShapedGlyph per Unicode codepoint, monotone advance,
    /// no reorder, no ligature merge) so the public surface keeps
    /// compiling on minimal builds.
    [[nodiscard]] std::vector<ShapedGlyph> shape(std::string_view text,
                                                 std::string_view locale = "en") const;

    /// Compile-time feature probe — useful for tests + selecting code
    /// paths that assume HarfBuzz exists.
    [[nodiscard]] static constexpr bool has_freetype() noexcept
    {
#if defined(CD_UI_FONT_HAVE_FREETYPE) && CD_UI_FONT_HAVE_FREETYPE
        return true;
#else
        return false;
#endif
    }
    [[nodiscard]] static constexpr bool has_harfbuzz() noexcept
    {
#if defined(CD_UI_FONT_HAVE_HARFBUZZ) && CD_UI_FONT_HAVE_HARFBUZZ
        return true;
#else
        return false;
#endif
    }

private:
    // PIMPL the backend-specific font data (stb_truetype OR FreeType +
    // HarfBuzz handles). Both backends populate the same `info` pointer
    // and `scale` indirectly via thin C++ wrappers in the .cpp files.
    struct Impl;
    std::unique_ptr<Impl>                        impl_;
    AtlasBitmap                                  atlas_ {};
    std::unordered_map<std::uint32_t, GlyphInfo> glyphs_;
    float                                        pixel_size_     { 0.0F };
    float                                        line_height_    { 0.0F };
    bool                                         loaded_         { false };
    Backend                                      active_backend_ { Backend::kAuto };
    AtlasMode                                    atlas_mode_     { AtlasMode::kAlpha };
};

}  // namespace cd::ui::font
