// =============================================================================
// CHROMODYNAMIC — cd/ui/font/Font.hpp
//
// Phase 1.1 of ADR-20260530-ui-widget-library. Glyph atlas + TTF
// rasterizer for the cd::ui renderer. Foundation: stb_truetype (header-
// only MIT). Phase 4 upgrade swaps in FreeType + HarfBuzz per ADR-009
// for proper BiDi / script shaping; the atlas + sampler API stays.
//
// Scope (Phase 1):
//   * Parse a TTF / OTF byte blob in memory (no on-disk path -- caller
//     loads via cd::vfs / fs::read_file).
//   * Rasterize a specified Unicode codepoint range into an RGBA8 atlas
//     (alpha-only stored in R; renderer uses single-channel sample).
//   * Skyline bin-pack on a fixed 2048x2048 surface (no LRU eviction).
//   * `glyph_uv(codepoint)` returns atlas UV + size + bearing in pixels.
//   * Optional kerning pair lookup via stbtt_GetCodepointKernAdvance.
//
// Out of Phase 1 (Phase 4):
//   * MSDF
//   * Variable fonts
//   * Shaping (BiDi / ligature / Devanagari conjunct)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
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
/// 8-bit alpha (= glyph coverage). The renderer samples this into a
/// single-channel texture and tints in the fragment shader. We do NOT
/// pre-expand to RGBA here; that decision belongs to the renderer.
struct AtlasBitmap
{
    std::vector<std::uint8_t> pixels;   ///< width * height bytes, row-major
    std::uint32_t             width  { 0U };
    std::uint32_t             height { 0U };
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

    /// Parse a TTF / OTF byte blob. The caller retains ownership of `data`
    /// only for the duration of this call -- `Font` does not retain a
    /// pointer into it. Returns false when the blob is unrecognisable
    /// (corrupted header, unsupported variant).
    [[nodiscard]] bool load_ttf_in_memory(std::span<const std::uint8_t> data);

    /// Rasterize codepoints `[first, last]` (inclusive) at `pixel_size`
    /// into a single atlas. The atlas grows on demand up to `max_dim`
    /// per side. Returns false when bin-pack fails (need bigger atlas).
    ///
    /// Typical Phase 1 usage:
    ///   font.rasterize_range(0x0020, 0x00FF, 16.0F, 2048U);  // ASCII + Latin-1
    ///   font.rasterize_range(0x011E, 0x011F, 16.0F, 2048U);  // Türkçe Ğğ
    ///   font.rasterize_range(0x015E, 0x015F, 16.0F, 2048U);  // Türkçe Şş
    ///   font.rasterize_range(0x0130, 0x0131, 16.0F, 2048U);  // Türkçe İı
    [[nodiscard]] bool rasterize_range(std::uint32_t first_codepoint,
                                       std::uint32_t last_codepoint,
                                       float         pixel_size,
                                       std::uint32_t max_dim);

    /// Look up the atlas slot for a codepoint. Returns `nullopt` for
    /// missing glyphs (the renderer should fall back to a `?` or
    /// substitute drawing).
    [[nodiscard]] std::optional<GlyphInfo> glyph_uv(std::uint32_t codepoint) const noexcept;

    /// Read access to the rasterized alpha atlas.
    [[nodiscard]] const AtlasBitmap& atlas() const noexcept { return atlas_; }

    /// Pixel size this font was rasterized at. Useful for layout / line
    /// height computation.
    [[nodiscard]] float pixel_size() const noexcept { return pixel_size_; }

    /// Line height in pixels. After `load_ttf_in_memory + rasterize_range`
    /// completes, this is `(ascent - descent + line_gap) * scale`.
    [[nodiscard]] float line_height() const noexcept { return line_height_; }

    /// Kerning advance between two codepoints (already-rasterized). Zero
    /// when the font has no kerning pair for `(a, b)`.
    [[nodiscard]] float kerning(std::uint32_t a, std::uint32_t b) const noexcept;

    /// True when the font has been parsed (load_ttf_in_memory succeeded).
    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }

private:
    // PIMPL the stb_truetype dependency so the header doesn't drag the
    // 5000-line stb header into every consumer's TU.
    struct Impl;
    std::unique_ptr<Impl>                       impl_;
    AtlasBitmap                                 atlas_ {};
    std::unordered_map<std::uint32_t, GlyphInfo> glyphs_;
    float                                       pixel_size_  { 0.0F };
    float                                       line_height_ { 0.0F };
    bool                                        loaded_      { false };
};

}  // namespace cd::ui::font
