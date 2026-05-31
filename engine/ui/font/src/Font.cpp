// =============================================================================
// CHROMODYNAMIC — cd/ui/font/Font.cpp
//
// Phase 1.1 + Phase 4 (T2.4). This TU owns:
//   * stb_truetype backend (default fallback, always compiled).
//   * Shared atlas + skyline bin-pack + MSDF post-processing.
//   * Backend dispatch (calls into FreeTypeBackend.cpp when built with
//     CD_UI_FONT_HAVE_FREETYPE).
//   * Identity Font::shape() fallback (real HB impl lives in
//     HarfBuzzShaping.cpp behind CD_UI_FONT_HAVE_HARFBUZZ).
// =============================================================================
#include <cd/ui/font/Font.hpp>

#include "FontBackend.hpp"

// stb_truetype: enable the implementation in THIS TU only.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert(x) ((void)0)
#include <stb_truetype.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace cd::ui::font
{

// ---- Impl: owns BOTH the stb_truetype side AND (when built) a
//   FreeTypeBackend instance. The active_backend_ field on Font picks
//   which one is consulted per call. We keep both TU-local because PIMPL
//   forward declaration cost is negligible and it lets either backend
//   own resources without leaking types into the header.
struct Font::Impl
{
    // Common to both backends.
    std::vector<std::uint8_t> ttf_bytes;
    float                     scale     { 0.0F };
    int                       ascent    { 0 };
    int                       descent   { 0 };
    int                       line_gap  { 0 };

    // stb_truetype state (always present; fallback path).
    stbtt_fontinfo stb_info {};

    // FreeType + HarfBuzz state, defined in FreeTypeBackend.cpp and
    // referenced via opaque pointer (constructed lazily on first use).
    std::unique_ptr<FreeTypeBackend> ft;

    // Replay-on-grow tracking: every time rasterize_range runs we
    // recompute the skyline from scratch using the previously-packed
    // glyph sizes. To do that we need the SOURCE alpha bitmap of each
    // glyph (so we can re-blit when atlas_mode is kMsdf and we need to
    // rebuild the SDF). Phase 4 stores only the dimensions — full source
    // pixel re-rasterization happens on demand from the backend.
};

namespace
{

// ---------------------------------------------------------------------------
// Skyline bin-pack. Each entry describes one column-skyline segment.
// try_pack returns the (x, y) where a (w, h) box fits, or nullopt when the
// box doesn't fit anywhere within the current bound. This is the SAME
// implementation that shipped with Phase 1.1; behaviour is preserved.
// ---------------------------------------------------------------------------
struct SkylineSegment
{
    std::uint32_t x { 0U };
    std::uint32_t y { 0U };
    std::uint32_t w { 0U };
};

class SkylinePacker
{
public:
    SkylinePacker(std::uint32_t width, std::uint32_t height)
        : width_ { width }, height_ { height }
    {
        skyline_.push_back(SkylineSegment { 0U, 0U, width });
    }

    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>>
    try_pack(std::uint32_t w, std::uint32_t h)
    {
        if (w > width_ || h > height_) return std::nullopt;

        std::uint32_t best_x = 0U, best_y = height_;
        std::size_t   best_idx = skyline_.size();
        for (std::size_t i = 0; i < skyline_.size(); ++i)
        {
            const auto y = peek_y_at_(i, w);
            if (!y.has_value()) continue;
            if (*y + h > height_) continue;
            if (*y < best_y)
            {
                best_y   = *y;
                best_x   = skyline_[i].x;
                best_idx = i;
            }
        }
        if (best_idx == skyline_.size()) return std::nullopt;
        commit_(best_idx, w, h, best_y);
        return std::make_pair(best_x, best_y);
    }

private:
    std::uint32_t              width_;
    std::uint32_t              height_;
    std::vector<SkylineSegment> skyline_;

    [[nodiscard]] std::optional<std::uint32_t>
    peek_y_at_(std::size_t start, std::uint32_t w) const
    {
        if (skyline_[start].x + w > width_) return std::nullopt;
        std::uint32_t remaining = w;
        std::uint32_t max_y     = skyline_[start].y;
        for (std::size_t i = start; i < skyline_.size() && remaining > 0; ++i)
        {
            max_y = std::max(max_y, skyline_[i].y);
            if (skyline_[i].w >= remaining)
            {
                return max_y;
            }
            remaining -= skyline_[i].w;
        }
        if (remaining > 0) return std::nullopt;
        return max_y;
    }

    void commit_(std::size_t start, std::uint32_t w, std::uint32_t h,
                 std::uint32_t box_top_y)
    {
        const std::uint32_t new_top = box_top_y + h;
        const std::uint32_t left_x  = skyline_[start].x;

        std::uint32_t consumed = 0U;
        std::size_t   end      = start;
        std::uint32_t last_old_y = 0U;
        while (end < skyline_.size() && consumed < w)
        {
            consumed   += skyline_[end].w;
            last_old_y = skyline_[end].y;
            ++end;
        }
        const std::uint32_t leftover = consumed - w;

        skyline_.erase(skyline_.begin() + static_cast<std::ptrdiff_t>(start),
                       skyline_.begin() + static_cast<std::ptrdiff_t>(end));

        SkylineSegment top {};
        top.x = left_x;
        top.y = new_top;
        top.w = w;
        skyline_.insert(skyline_.begin() + static_cast<std::ptrdiff_t>(start), top);

        if (leftover > 0U)
        {
            SkylineSegment tail {};
            tail.x = left_x + w;
            tail.y = last_old_y;
            tail.w = leftover;
            skyline_.insert(skyline_.begin() + static_cast<std::ptrdiff_t>(start + 1U), tail);
        }
    }
};

// ---------------------------------------------------------------------------
// MSDF generation — hand-rolled 8-SSED (8-point sequential Euclidean
// distance) approximation. Operates IN-PLACE on a single-channel alpha
// glyph bitmap: input is a coverage map (0 = outside, > 0 = inside);
// output is an unsigned SDF biased so 128 = boundary, > 128 = inside,
// < 128 = outside. The renderer can sample this with bilinear filtering
// and the standard
//
//   alpha = smoothstep(0.5, 0.5 + 1/spread, sample) - smoothstep(0.5 - 1/spread, 0.5, sample)
//
// hint to draw crisp edges at any scale. msdfgen would give multi-
// channel anti-aliasing for sharp corners; for Phase 4 a single-channel
// SDF buys us the scale-invariant rendering benefit without pulling in
// another vendored library.
// ---------------------------------------------------------------------------
constexpr int kMsdfSpread       = 8;     // 8-pixel search radius
constexpr int kMsdfSpreadSquared = kMsdfSpread * kMsdfSpread;

void to_msdf_inplace(std::uint8_t* glyph, std::uint32_t w, std::uint32_t h)
{
    if (w == 0U || h == 0U) return;

    // Snapshot input coverage so the distance walk doesn't read its
    // own writes mid-loop.
    std::vector<std::uint8_t> src(glyph, glyph + static_cast<std::size_t>(w) * h);

    const int iw = static_cast<int>(w);
    const int ih = static_cast<int>(h);
    for (int y = 0; y < ih; ++y)
    {
        for (int x = 0; x < iw; ++x)
        {
            const bool inside = src[static_cast<std::size_t>(y) * iw + x] >= 128U;

            // Search the (2*spread+1)^2 neighbourhood for the closest
            // pixel of the OPPOSITE coverage and record squared
            // Euclidean distance. Bounded loop -> O(spread^2) per pixel
            // is plenty fast for ASCII-sized glyphs.
            int best_sq = std::numeric_limits<int>::max();
            const int y0 = std::max(0, y - kMsdfSpread);
            const int y1 = std::min(ih - 1, y + kMsdfSpread);
            const int x0 = std::max(0, x - kMsdfSpread);
            const int x1 = std::min(iw - 1, x + kMsdfSpread);
            for (int yy = y0; yy <= y1; ++yy)
            {
                for (int xx = x0; xx <= x1; ++xx)
                {
                    const bool here = src[static_cast<std::size_t>(yy) * iw + xx] >= 128U;
                    if (here == inside) continue;
                    const int dx = xx - x;
                    const int dy = yy - y;
                    const int d  = dx * dx + dy * dy;
                    if (d < best_sq) best_sq = d;
                }
            }

            // Map the squared distance to [0, kMsdfSpread^2] and then
            // to the 0..127 half-range. Sign-bias inside = +, outside = -.
            int dist_q = (best_sq >= kMsdfSpreadSquared)
                             ? kMsdfSpreadSquared
                             : best_sq;
            // Linear ramp (cheap; not quite a true distance, but smooth).
            int q = (dist_q * 127) / kMsdfSpreadSquared;
            int signed_q = inside ? (128 + q) : (128 - q);
            if (signed_q < 0)   signed_q = 0;
            if (signed_q > 255) signed_q = 255;
            glyph[static_cast<std::size_t>(y) * iw + x] = static_cast<std::uint8_t>(signed_q);
        }
    }
}

// ---------------------------------------------------------------------------
// Minimal UTF-8 → codepoint decoder. Used by the identity shape() fallback
// AND by the HarfBuzz tests that probe Arabic/Latin/CJK strings.
// Invalid bytes are returned as U+FFFD (replacement character).
// ---------------------------------------------------------------------------
std::vector<std::uint32_t> utf8_to_codepoints(std::string_view s)
{
    std::vector<std::uint32_t> out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size())
    {
        const auto b = static_cast<std::uint8_t>(s[i]);
        std::uint32_t cp = 0xFFFDU;
        int            extra = 0;
        if (b < 0x80U)               { cp = b; extra = 0; }
        else if ((b & 0xE0U) == 0xC0U) { cp = b & 0x1FU; extra = 1; }
        else if ((b & 0xF0U) == 0xE0U) { cp = b & 0x0FU; extra = 2; }
        else if ((b & 0xF8U) == 0xF0U) { cp = b & 0x07U; extra = 3; }
        else                            { cp = 0xFFFDU; extra = 0; }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i)
        {
            const auto c = static_cast<std::uint8_t>(s[i]);
            if ((c & 0xC0U) != 0x80U) { cp = 0xFFFDU; break; }
            cp = (cp << 6U) | (c & 0x3FU);
        }
        out.push_back(cp);
    }
    return out;
}

}  // namespace

// ============================================================================
// Backend dispatch — the FT backend lives in a sibling TU. When
// CD_UI_FONT_HAVE_FREETYPE is undefined, the stubs below short-circuit so
// kFreeType collapses to kStb at runtime.
// ============================================================================
namespace ft_backend
{
#if defined(CD_UI_FONT_HAVE_FREETYPE) && CD_UI_FONT_HAVE_FREETYPE
bool load(FreeTypeBackend& ft, std::span<const std::uint8_t> data, float pixel_size,
          int& out_ascent, int& out_descent, int& out_line_gap, float& out_scale);
bool rasterize_glyph(FreeTypeBackend& ft, std::uint32_t codepoint,
                     std::vector<std::uint8_t>& dst_alpha,
                     int& w, int& h,
                     float& bearing_x, float& bearing_y, float& advance);
#else
inline bool load(FreeTypeBackend&, std::span<const std::uint8_t>, float,
                 int&, int&, int&, float&) { return false; }
inline bool rasterize_glyph(FreeTypeBackend&, std::uint32_t,
                            std::vector<std::uint8_t>&,
                            int&, int&, float&, float&, float&) { return false; }
#endif
}  // namespace ft_backend

namespace hb_shape
{
#if defined(CD_UI_FONT_HAVE_HARFBUZZ) && CD_UI_FONT_HAVE_HARFBUZZ
std::vector<ShapedGlyph> shape(const FreeTypeBackend& ft,
                               std::string_view       text,
                               std::string_view       locale,
                               float                  scale);
#else
inline std::vector<ShapedGlyph> shape(const FreeTypeBackend&,
                                      std::string_view,
                                      std::string_view,
                                      float)
{
    return {};
}
#endif
}  // namespace hb_shape

// ============================================================================
Font::Font() = default;
Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

Backend Font::select_backend(Backend desired) noexcept
{
    Backend resolved = desired;
    if (resolved == Backend::kAuto)
    {
#if defined(CD_UI_FONT_HAVE_FREETYPE) && CD_UI_FONT_HAVE_FREETYPE
        resolved = Backend::kFreeType;
#else
        resolved = Backend::kStb;
#endif
    }
    if (resolved == Backend::kFreeType && !has_freetype())
    {
        resolved = Backend::kStb;
    }
    active_backend_ = resolved;
    return resolved;
}

AtlasMode Font::select_atlas_mode(AtlasMode desired) noexcept
{
    // Mode is only honoured when no glyphs have been packed yet — once a
    // glyph is rasterized the bitmap format is locked.
    if (glyphs_.empty())
    {
        atlas_mode_ = desired;
    }
    return atlas_mode_;
}

bool Font::load_ttf_in_memory(std::span<const std::uint8_t> data)
{
    constexpr std::size_t kMinTtfHeaderBytes = 256U;
    if (data.size() < kMinTtfHeaderBytes) return false;

    // Resolve backend on first load if caller never called select_backend().
    if (active_backend_ == Backend::kAuto)
    {
        (void)select_backend(Backend::kAuto);
    }

    impl_ = std::make_unique<Impl>();
    impl_->ttf_bytes.assign(data.begin(), data.end());

    // ---- stb_truetype init (also used by the FT path as a metric cross-
    //      check + kerning fallback for fonts without GPOS).
    const int offset = stbtt_GetFontOffsetForIndex(impl_->ttf_bytes.data(), 0);
    if (offset < 0)
    {
        impl_.reset();
        return false;
    }
    if (stbtt_InitFont(&impl_->stb_info, impl_->ttf_bytes.data(), offset) == 0)
    {
        impl_.reset();
        return false;
    }

    loaded_ = true;
    return true;
}

bool Font::rasterize_range(std::uint32_t first_codepoint,
                           std::uint32_t last_codepoint,
                           float         pixel_size,
                           std::uint32_t max_dim)
{
    if (!loaded_ || pixel_size <= 0.0F || max_dim == 0U) return false;
    if (last_codepoint < first_codepoint) return false;

    // Lazy-init atlas + metrics on first call.
    if (atlas_.width == 0U)
    {
        const std::uint32_t initial = std::min<std::uint32_t>(1024U, max_dim);
        atlas_.width  = initial;
        atlas_.height = initial;
        atlas_.pixels.assign(static_cast<std::size_t>(initial) * initial, std::uint8_t { 0 });

        // Resolve metrics via the active backend. FT path tries first, then
        // falls back to stb if FT init fails (e.g. unsupported font).
        bool got_metrics = false;
        if (active_backend_ == Backend::kFreeType)
        {
#if defined(CD_UI_FONT_HAVE_FREETYPE) && CD_UI_FONT_HAVE_FREETYPE
            if (!impl_->ft) impl_->ft = std::make_unique<FreeTypeBackend>();
            got_metrics = ft_backend::load(*impl_->ft,
                                           std::span<const std::uint8_t>(impl_->ttf_bytes),
                                           pixel_size,
                                           impl_->ascent, impl_->descent,
                                           impl_->line_gap, impl_->scale);
#endif
            if (!got_metrics) active_backend_ = Backend::kStb;
        }
        if (!got_metrics)
        {
            impl_->scale = stbtt_ScaleForPixelHeight(&impl_->stb_info, pixel_size);
            stbtt_GetFontVMetrics(&impl_->stb_info, &impl_->ascent, &impl_->descent, &impl_->line_gap);
        }
        pixel_size_  = pixel_size;
        line_height_ = static_cast<float>(impl_->ascent - impl_->descent + impl_->line_gap) * impl_->scale;
    }

    SkylinePacker packer(atlas_.width, atlas_.height);
    for (const auto& [_, g] : glyphs_)
    {
        const auto w = static_cast<std::uint32_t>(g.width);
        const auto h = static_cast<std::uint32_t>(g.height);
        if (w == 0U || h == 0U) continue;
        (void)packer.try_pack(w, h);
    }

    const float scale = impl_->scale;
    for (std::uint32_t cp = first_codepoint; cp <= last_codepoint; ++cp)
    {
        if (glyphs_.find(cp) != glyphs_.end()) continue;

        // ---- Per-backend rasterization into a TEMP alpha bitmap.
        std::vector<std::uint8_t> tmp;
        int                       w = 0, h = 0;
        float                     bx = 0.0F, by = 0.0F, adv = 0.0F;
        bool                      have_glyph = false;

        if (active_backend_ == Backend::kFreeType && impl_->ft)
        {
            have_glyph = ft_backend::rasterize_glyph(*impl_->ft, cp, tmp,
                                                     w, h, bx, by, adv);
        }
        if (!have_glyph)
        {
            // stb path — also the FT fallback when FT can't load this glyph.
            const int gi = stbtt_FindGlyphIndex(&impl_->stb_info, static_cast<int>(cp));
            if (gi == 0) continue;  // not in font

            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetGlyphBitmapBox(&impl_->stb_info, gi, scale, scale, &x0, &y0, &x1, &y1);
            w = std::max(0, x1 - x0);
            h = std::max(0, y1 - y0);

            int advance_i = 0, lsb = 0;
            stbtt_GetGlyphHMetrics(&impl_->stb_info, gi, &advance_i, &lsb);
            bx  = static_cast<float>(x0);
            by  = static_cast<float>(-y0);
            adv = static_cast<float>(advance_i) * scale;

            if (w > 0 && h > 0)
            {
                tmp.assign(static_cast<std::size_t>(w) * h, std::uint8_t { 0 });
                stbtt_MakeGlyphBitmap(&impl_->stb_info,
                                      tmp.data(),
                                      w, h, w,
                                      scale, scale, gi);
            }
            have_glyph = true;
        }
        if (!have_glyph) continue;

        GlyphInfo info {};
        info.width     = static_cast<float>(w);
        info.height    = static_cast<float>(h);
        info.bearing_x = bx;
        info.bearing_y = by;
        info.advance   = adv;

        if (w == 0 || h == 0)
        {
            glyphs_.emplace(cp, info);
            continue;
        }

        const auto packed = packer.try_pack(static_cast<std::uint32_t>(w),
                                            static_cast<std::uint32_t>(h));
        if (!packed.has_value()) return false;
        const auto [px, py] = *packed;

        // Optional MSDF post-process happens on the SOURCE bitmap before
        // we blit into the atlas, so the SDF respects glyph-local coords
        // (not atlas-page coords; bleed across glyphs is impossible).
        if (atlas_mode_ == AtlasMode::kMsdf)
        {
            to_msdf_inplace(tmp.data(),
                            static_cast<std::uint32_t>(w),
                            static_cast<std::uint32_t>(h));
        }

        // Blit tmp -> atlas at (px, py).
        for (int row = 0; row < h; ++row)
        {
            std::memcpy(atlas_.pixels.data() +
                            (static_cast<std::size_t>(py + row) * atlas_.width + px),
                        tmp.data() + static_cast<std::size_t>(row) * w,
                        static_cast<std::size_t>(w));
        }

        info.u0 = static_cast<float>(px)     / static_cast<float>(atlas_.width);
        info.v0 = static_cast<float>(py)     / static_cast<float>(atlas_.height);
        info.u1 = static_cast<float>(px + w) / static_cast<float>(atlas_.width);
        info.v1 = static_cast<float>(py + h) / static_cast<float>(atlas_.height);
        glyphs_.emplace(cp, info);
    }
    return true;
}

std::optional<GlyphInfo> Font::glyph_uv(std::uint32_t codepoint) const noexcept
{
    auto it = glyphs_.find(codepoint);
    if (it == glyphs_.end()) return std::nullopt;
    return it->second;
}

float Font::kerning(std::uint32_t a, std::uint32_t b) const noexcept
{
    if (!loaded_) return 0.0F;
    const int adv = stbtt_GetCodepointKernAdvance(&impl_->stb_info,
                                                  static_cast<int>(a),
                                                  static_cast<int>(b));
    return static_cast<float>(adv) * impl_->scale;
}

std::vector<ShapedGlyph> Font::shape(std::string_view text,
                                     std::string_view locale) const
{
    if (!loaded_ || text.empty()) return {};

#if defined(CD_UI_FONT_HAVE_HARFBUZZ) && CD_UI_FONT_HAVE_HARFBUZZ
    if (active_backend_ == Backend::kFreeType && impl_->ft)
    {
        auto out = hb_shape::shape(*impl_->ft, text, locale, impl_->scale);
        if (!out.empty()) return out;
    }
#else
    (void)locale;
#endif

    // ---- Identity shaper fallback (stb path or HB unavailable).
    // Decode UTF-8, emit one ShapedGlyph per codepoint with the codepoint
    // itself in glyph_id and the stb-reported advance.
    std::vector<ShapedGlyph> out;
    const auto cps = utf8_to_codepoints(text);
    out.reserve(cps.size());
    for (std::uint32_t cp : cps)
    {
        ShapedGlyph g {};
        g.glyph_id = cp;
        int adv_i = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&impl_->stb_info, static_cast<int>(cp),
                                   &adv_i, &lsb);
        g.advance_x = static_cast<float>(adv_i) * impl_->scale;
        out.push_back(g);
    }
    return out;
}

}  // namespace cd::ui::font
