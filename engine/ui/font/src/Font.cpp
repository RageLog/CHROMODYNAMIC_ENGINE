// =============================================================================
// CHROMODYNAMIC — cd/ui/font/Font.cpp
//
// Phase 1.1 impl. Uses stb_truetype (single-header MIT, vendored via the
// asset_image FetchContent of nothings/stb). The skyline bin-pack is a
// minimal in-house implementation -- stbtt_BakeFontBitmap does its own
// bin-packing but its API rasterizes a CONTIGUOUS codepoint range into
// a single bake call. We need to support incremental ranges (Türkçe
// diakritik are scattered across Latin Extended-A), so we drive the
// rasterizer per-glyph and pack ourselves.
// =============================================================================
#include <cd/ui/font/Font.hpp>

// stb_truetype: enable the implementation in THIS TU only.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert(x) ((void)0)
#include <stb_truetype.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace cd::ui::font
{

// ---- Impl: holds the stb_truetype font info + a copy of the TTF bytes
//   so the stbtt_fontinfo's pointer into the font data stays valid for
//   the lifetime of the Font.
struct Font::Impl
{
    std::vector<std::uint8_t> ttf_bytes;
    stbtt_fontinfo            info {};
    float                     scale { 0.0F };
    int                       ascent { 0 };
    int                       descent { 0 };
    int                       line_gap { 0 };
};

namespace
{

/// Minimal skyline bin-pack. Each entry describes one column-skyline
/// segment. `try_pack` returns the (x, y) where a (w, h) box fits, or
/// nullopt when the box doesn't fit anywhere within the current bound.
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
        // The placed box sits in [x_left, x_left + w) × [box_top_y, box_top_y + h).
        // Replace every old skyline segment covered by w with a single new
        // segment at top_y = box_top_y + h. Carry the unconsumed remainder
        // of the LAST overlapped segment forward at its ORIGINAL y so the
        // skyline stays a valid 1-D step function.
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

}  // namespace

Font::Font() = default;
Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

bool Font::load_ttf_in_memory(std::span<const std::uint8_t> data)
{
    // stb_truetype does no input validation — it trusts the SFNT header
    // and walks the table directory blindly. Reject obviously-too-small
    // / malformed blobs ourselves before reaching for stbtt_InitFont,
    // which would otherwise dereference garbage offsets and AV-crash.
    constexpr std::size_t kMinTtfHeaderBytes = 256U;
    if (data.size() < kMinTtfHeaderBytes) return false;

    impl_ = std::make_unique<Impl>();
    impl_->ttf_bytes.assign(data.begin(), data.end());

    // GetFontOffsetForIndex returns -1 when the SFNT magic isn't one of
    // the known values ('1\0\0\0' / 'OTTO' / 'true' / 'typ1' / 'ttcf').
    const int offset = stbtt_GetFontOffsetForIndex(impl_->ttf_bytes.data(), 0);
    if (offset < 0)
    {
        impl_.reset();
        return false;
    }
    if (stbtt_InitFont(&impl_->info, impl_->ttf_bytes.data(), offset) == 0)
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

    // Lazy-init the atlas on first rasterize_range call. We pick a 1024
    // starting size and double-up to `max_dim` on overflow. For Phase 1
    // we keep a single page (no eviction).
    if (atlas_.width == 0U)
    {
        const std::uint32_t initial = std::min<std::uint32_t>(1024U, max_dim);
        atlas_.width  = initial;
        atlas_.height = initial;
        atlas_.pixels.assign(static_cast<std::size_t>(initial) * initial, std::uint8_t { 0 });
        impl_->scale = stbtt_ScaleForPixelHeight(&impl_->info, pixel_size);
        stbtt_GetFontVMetrics(&impl_->info, &impl_->ascent, &impl_->descent, &impl_->line_gap);
        pixel_size_  = pixel_size;
        line_height_ = static_cast<float>(impl_->ascent - impl_->descent + impl_->line_gap) * impl_->scale;
    }

    SkylinePacker packer(atlas_.width, atlas_.height);
    // Replay all previously-packed glyphs into the packer so we keep
    // their reservations.
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
        const int gi = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp));
        if (gi == 0) continue;  // not in font

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&impl_->info, gi, scale, scale, &x0, &y0, &x1, &y1);
        const std::uint32_t w = static_cast<std::uint32_t>(std::max(0, x1 - x0));
        const std::uint32_t h = static_cast<std::uint32_t>(std::max(0, y1 - y0));

        int advance_i = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&impl_->info, gi, &advance_i, &lsb);

        GlyphInfo info {};
        info.width     = static_cast<float>(w);
        info.height    = static_cast<float>(h);
        info.bearing_x = static_cast<float>(x0);
        info.bearing_y = static_cast<float>(-y0);  // y0 is top above baseline (negative)
        info.advance   = static_cast<float>(advance_i) * scale;

        if (w == 0U || h == 0U)
        {
            // Whitespace glyph (e.g. space) -- no atlas slot needed.
            glyphs_.emplace(cp, info);
            continue;
        }

        const auto packed = packer.try_pack(w, h);
        if (!packed.has_value())
        {
            return false;  // ran out of atlas space
        }
        const auto [px, py] = *packed;

        // Rasterize directly into the atlas at (px, py).
        stbtt_MakeGlyphBitmap(
            &impl_->info,
            atlas_.pixels.data() + static_cast<std::size_t>(py) * atlas_.width + px,
            static_cast<int>(w), static_cast<int>(h),
            static_cast<int>(atlas_.width),
            scale, scale, gi);

        info.u0 = static_cast<float>(px)      / static_cast<float>(atlas_.width);
        info.v0 = static_cast<float>(py)      / static_cast<float>(atlas_.height);
        info.u1 = static_cast<float>(px + w)  / static_cast<float>(atlas_.width);
        info.v1 = static_cast<float>(py + h)  / static_cast<float>(atlas_.height);
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
    const int adv = stbtt_GetCodepointKernAdvance(&impl_->info, static_cast<int>(a), static_cast<int>(b));
    return static_cast<float>(adv) * impl_->scale;
}

}  // namespace cd::ui::font
