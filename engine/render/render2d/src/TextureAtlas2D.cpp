// =============================================================================
// CHROMODYNAMIC — cd/render2d/TextureAtlas2D.cpp
// Milestone 3 — Real RGBA8 GPU Texture Atlas Container Implementation.
// =============================================================================
#include <cd/render2d/TextureAtlas2D.hpp>

#include <cd/asset/image/Image.hpp>
#include <cd/asset/json/Json.hpp>

#include <cmath>
#include <cstring>

namespace cd::render2d
{

namespace
{

[[nodiscard]] std::uint32_t scale_px(std::uint32_t v16, std::uint32_t ts) noexcept
{
    return (v16 * ts) / 16U;
}

void fill_subrect(
    TextureAtlas2D& atlas,
    std::uint32_t   ox,
    std::uint32_t   oy,
    std::uint32_t   ts,
    std::uint32_t   x16,
    std::uint32_t   y16,
    std::uint32_t   w16,
    std::uint32_t   h16,
    ColorRgba8      col) noexcept
{
    const std::uint32_t x0 = scale_px(x16, ts);
    const std::uint32_t y0 = scale_px(y16, ts);
    const std::uint32_t x1 = std::max(x0 + 1U, scale_px(x16 + w16, ts));
    const std::uint32_t y1 = std::max(y0 + 1U, scale_px(y16 + h16, ts));
    for (std::uint32_t y = y0; y < std::min(ts, y1); ++y)
    {
        for (std::uint32_t x = x0; x < std::min(ts, x1); ++x)
        {
            atlas.set_pixel(ox + x, oy + y, col);
        }
    }
}

void add_dark_pixel_outline(
    TextureAtlas2D& atlas,
    std::uint32_t   ox,
    std::uint32_t   oy,
    std::uint32_t   ts,
    ColorRgba8      outline = { 18U, 22U, 34U, 255U })
{
    std::vector<std::pair<std::uint32_t, std::uint32_t>> border_pts;
    border_pts.reserve(static_cast<std::size_t>(ts * ts / 2U));
    for (std::uint32_t y = 0U; y < ts; ++y)
    {
        for (std::uint32_t x = 0U; x < ts; ++x)
        {
            if (atlas.get_pixel(ox + x, oy + y).a != 0U)
            {
                continue;
            }
            const bool adj_solid =
                (x > 0U && atlas.get_pixel(ox + x - 1U, oy + y).a > 128U) ||
                (x + 1U < ts && atlas.get_pixel(ox + x + 1U, oy + y).a > 128U) ||
                (y > 0U && atlas.get_pixel(ox + x, oy + y - 1U).a > 128U) ||
                (y + 1U < ts && atlas.get_pixel(ox + x, oy + y + 1U).a > 128U);
            if (adj_solid)
            {
                border_pts.emplace_back(ox + x, oy + y);
            }
        }
    }
    for (const auto& [px, py] : border_pts)
    {
        atlas.set_pixel(px, py, outline);
    }
}

void draw_circle_pattern(
    TextureAtlas2D& atlas,
    std::uint32_t   cx,
    std::uint32_t   cy,
    std::uint32_t   radius,
    ColorRgba8      fill,
    ColorRgba8      border)
{
    const int r = static_cast<int>(radius);
    for (int dy = -r; dy <= r; ++dy)
    {
        for (int dx = -r; dx <= r; ++dx)
        {
            const int dist_sq = dx * dx + dy * dy;
            if (dist_sq <= r * r)
            {
                const bool is_edge = (dist_sq >= (r - 1) * (r - 1));
                atlas.set_pixel(
                    static_cast<std::uint32_t>(static_cast<int>(cx) + dx),
                    static_cast<std::uint32_t>(static_cast<int>(cy) + dy),
                    is_edge ? border : fill);
            }
        }
    }
}

void draw_ashlar_masonry(
    TextureAtlas2D& atlas,
    std::uint32_t   ox,
    std::uint32_t   oy,
    std::uint32_t   ts,
    ColorRgba8      stone_mid,
    ColorRgba8      stone_hi,
    ColorRgba8      stone_lo,
    ColorRgba8      mortar)
{
    const std::uint32_t row_h = std::max(4U, ts / 4U);
    const std::uint32_t brk_w = std::max(4U, ts / 2U);
    for (std::uint32_t y = 0U; y < ts; ++y)
    {
        const std::uint32_t row   = y / row_h;
        const std::uint32_t ry    = y % row_h;
        const std::uint32_t shift = (row & 1U) != 0U ? (brk_w / 2U) : 0U;
        for (std::uint32_t x = 0U; x < ts; ++x)
        {
            const std::uint32_t rx = (x + shift) % brk_w;
            if (ry == 0U || rx == 0U)
            {
                atlas.set_pixel(ox + x, oy + y, mortar);
            }
            else if (ry == 1U || rx == 1U)
            {
                atlas.set_pixel(ox + x, oy + y, stone_hi);
            }
            else if (ry + 1U == row_h || rx + 1U == brk_w)
            {
                atlas.set_pixel(ox + x, oy + y, stone_lo);
            }
            else
            {
                const std::uint32_t h = (x * 17U + y * 31U + row * 13U) & 7U;
                atlas.set_pixel(ox + x, oy + y, (h == 0U) ? stone_lo : ((h == 7U) ? stone_hi : stone_mid));
            }
        }
    }
}

void draw_grass_cap_over_stone(
    TextureAtlas2D& atlas,
    std::uint32_t   ox,
    std::uint32_t   oy,
    std::uint32_t   ts,
    bool            round_left,
    bool            round_right)
{
    draw_ashlar_masonry(
        atlas, ox, oy, ts,
        { 76U, 88U, 114U, 255U },
        { 102U, 118U, 148U, 255U },
        { 54U, 64U, 86U, 255U },
        { 34U, 40U, 56U, 255U });

    const ColorRgba8 turf_hi  { 116U, 248U, 154U, 255U };
    const ColorRgba8 turf_mid { 64U, 212U, 124U, 255U };
    const ColorRgba8 turf_lo  { 36U, 148U, 88U, 255U };
    const ColorRgba8 soil_rim { 92U, 64U, 48U, 255U };
    const ColorRgba8 bdr_dark { 22U, 30U, 42U, 255U };

    for (std::uint32_t x = 0U; x < ts; ++x)
    {
        const std::uint32_t overhang = (ts / 4U) + (((x * 5U + 3U) % 4U) * std::max(1U, ts / 16U));
        for (std::uint32_t y = 0U; y < std::min(ts, overhang + 2U); ++y)
        {
            if ((round_left && x == 0U && y == 0U) || (round_right && x + 1U == ts && y == 0U))
            {
                atlas.set_pixel(ox + x, oy + y, { 0U, 0U, 0U, 0U });
                continue;
            }
            if (y == 0U)
            {
                atlas.set_pixel(ox + x, oy + y, bdr_dark);
            }
            else if (y <= 2U)
            {
                atlas.set_pixel(ox + x, oy + y, ((x + y) & 1U) == 0U ? turf_hi : turf_mid);
            }
            else if (y < overhang)
            {
                atlas.set_pixel(ox + x, oy + y, (y + 1U == overhang) ? turf_lo : turf_mid);
            }
            else
            {
                atlas.set_pixel(ox + x, oy + y, soil_rim);
            }
        }
    }
    if (round_left)
    {
        for (std::uint32_t y = 1U; y < ts; ++y)
        {
            atlas.set_pixel(ox, oy + y, bdr_dark);
        }
    }
    if (round_right)
    {
        for (std::uint32_t y = 1U; y < ts; ++y)
        {
            atlas.set_pixel(ox + ts - 1U, oy + y, bdr_dark);
        }
    }
}

void draw_cyber_knight_frame(
    TextureAtlas2D& atlas,
    std::uint32_t   ox,
    std::uint32_t   oy,
    std::uint32_t   ts,
    int             pose)
{
    const ColorRgba8 armor_hi  { 112U, 178U, 245U, 255U };
    const ColorRgba8 armor_mid { 56U, 114U, 198U, 255U };
    const ColorRgba8 armor_lo  { 34U, 68U, 128U, 255U };
    const ColorRgba8 visor_hi  { 185U, 255U, 255U, 255U };
    const ColorRgba8 visor_mid { 68U, 232U, 255U, 255U };
    const ColorRgba8 cape_red  { 218U, 52U, 76U, 255U };
    const ColorRgba8 cape_lo   { 142U, 28U, 48U, 255U };
    const ColorRgba8 gold_trim { 250U, 204U, 78U, 255U };
    const ColorRgba8 blade_col { 165U, 248U, 255U, 255U };
    const ColorRgba8 boot_col  { 44U, 54U, 78U, 255U };

    const std::uint32_t bob = (pose == 1 || pose == 2) ? 1U : 0U;
    const std::uint32_t lift = (pose == 3) ? 1U : 0U;

    // Flowing crimson scarf / cape behind shoulder
    fill_subrect(atlas, ox, oy, ts, 2U, 6U + bob - lift, 4U, 6U, cape_red);
    fill_subrect(atlas, ox, oy, ts, 2U, 9U + bob - lift, 3U, 3U, cape_lo);
    if (pose == 1 || pose == 3)
    {
        fill_subrect(atlas, ox, oy, ts, 1U, 7U, 2U, 3U, cape_red);
    }

    // Helmet + crest + glowing cyan visor
    fill_subrect(atlas, ox, oy, ts, 5U, 2U + bob - lift, 6U, 5U, armor_mid);
    fill_subrect(atlas, ox, oy, ts, 6U, 1U + bob - lift, 4U, 2U, armor_hi);
    fill_subrect(atlas, ox, oy, ts, 7U, 1U + bob - lift, 2U, 1U, gold_trim);
    fill_subrect(atlas, ox, oy, ts, 7U, 3U + bob - lift, 4U, 2U, visor_mid);
    fill_subrect(atlas, ox, oy, ts, 8U, 3U + bob - lift, 2U, 1U, visor_hi);

    // Torso chestplate + pauldrons + gold energy core
    fill_subrect(atlas, ox, oy, ts, 4U, 7U + bob - lift, 8U, 5U, armor_mid);
    fill_subrect(atlas, ox, oy, ts, 4U, 7U + bob - lift, 2U, 2U, armor_hi);
    fill_subrect(atlas, ox, oy, ts, 10U, 7U + bob - lift, 2U, 2U, armor_hi);
    fill_subrect(atlas, ox, oy, ts, 5U, 10U + bob - lift, 6U, 2U, armor_lo);
    fill_subrect(atlas, ox, oy, ts, 7U, 8U + bob - lift, 2U, 2U, gold_trim);

    // Energy blade held at right side
    if (pose == 3)
    {
        fill_subrect(atlas, ox, oy, ts, 12U, 2U, 2U, 6U, blade_col);
        fill_subrect(atlas, ox, oy, ts, 11U, 7U, 3U, 1U, gold_trim);
    }
    else
    {
        fill_subrect(atlas, ox, oy, ts, 12U, 5U + bob, 2U, 5U, blade_col);
        fill_subrect(atlas, ox, oy, ts, 11U, 9U + bob, 3U, 1U, gold_trim);
    }

    // Boots / stride animation
    if (pose == 0)
    {
        fill_subrect(atlas, ox, oy, ts, 5U, 12U, 2U, 3U, boot_col);
        fill_subrect(atlas, ox, oy, ts, 9U, 12U, 2U, 3U, boot_col);
    }
    else if (pose == 1)
    {
        fill_subrect(atlas, ox, oy, ts, 4U, 12U, 2U, 3U, boot_col);
        fill_subrect(atlas, ox, oy, ts, 10U, 11U, 2U, 3U, boot_col);
    }
    else if (pose == 2)
    {
        fill_subrect(atlas, ox, oy, ts, 6U, 11U, 2U, 3U, boot_col);
        fill_subrect(atlas, ox, oy, ts, 8U, 12U, 2U, 3U, boot_col);
    }
    else
    {
        fill_subrect(atlas, ox, oy, ts, 5U, 11U, 2U, 2U, boot_col);
        fill_subrect(atlas, ox, oy, ts, 9U, 11U, 2U, 2U, boot_col);
        fill_subrect(atlas, ox, oy, ts, 5U, 13U, 2U, 2U, { 255U, 210U, 64U, 255U });
        fill_subrect(atlas, ox, oy, ts, 9U, 13U, 2U, 2U, { 120U, 248U, 255U, 255U });
    }

    add_dark_pixel_outline(atlas, ox, oy, ts);
}

void draw_faceted_gem(
    TextureAtlas2D& atlas,
    std::uint32_t   ox,
    std::uint32_t   oy,
    std::uint32_t   ts,
    ColorRgba8      core_hi,
    ColorRgba8      mid_col,
    ColorRgba8      deep_col)
{
    const int half = static_cast<int>(ts / 2U);
    const int rad  = std::max(3, half - static_cast<int>(std::max(2U, ts / 8U)));
    for (int dy = -rad; dy <= rad; ++dy)
    {
        for (int dx = -rad; dx <= rad; ++dx)
        {
            const int manhattan = std::abs(dx) * 5 + std::abs(dy) * 4;
            if (manhattan <= rad * 4)
            {
                ColorRgba8 c = mid_col;
                if (dx < 0 && dy < 0)
                {
                    c = core_hi;
                }
                else if (dx > 0 && dy > 0)
                {
                    c = deep_col;
                }
                if (std::abs(dx) <= 1 && std::abs(dy) <= 1)
                {
                    c = { 255U, 255U, 255U, 255U };
                }
                atlas.set_pixel(
                    ox + static_cast<std::uint32_t>(half + dx),
                    oy + static_cast<std::uint32_t>(half + dy),
                    c);
            }
        }
    }
    add_dark_pixel_outline(atlas, ox, oy, ts);
}

}  // namespace

TextureAtlas2D::TextureAtlas2D(
    std::uint32_t       width,
    std::uint32_t       height,
    TextureFilterMode2D filter)
    : width_(std::max(1U, width))
    , height_(std::max(1U, height))
    , filter_mode_(filter)
    , pixels_(static_cast<std::size_t>(width_ * height_ * 4U), 0U)
{
}

ColorRgba8 TextureAtlas2D::get_pixel(std::uint32_t x, std::uint32_t y) const noexcept
{
    if (x >= width_ || y >= height_ || pixels_.empty())
    {
        return { 0U, 0U, 0U, 0U };
    }
    const std::size_t idx = static_cast<std::size_t>((y * width_ + x) * 4U);
    return {
        pixels_[idx],
        pixels_[idx + 1U],
        pixels_[idx + 2U],
        pixels_[idx + 3U],
    };
}

void TextureAtlas2D::set_pixel(std::uint32_t x, std::uint32_t y, ColorRgba8 c) noexcept
{
    if (x >= width_ || y >= height_ || pixels_.empty())
    {
        return;
    }
    const std::size_t idx = static_cast<std::size_t>((y * width_ + x) * 4U);
    pixels_[idx]      = c.r;
    pixels_[idx + 1U] = c.g;
    pixels_[idx + 2U] = c.b;
    pixels_[idx + 3U] = c.a;
}

UvRect2D TextureAtlas2D::compute_uv(
    std::uint32_t px,
    std::uint32_t py,
    std::uint32_t pw,
    std::uint32_t ph) const noexcept
{
    if (width_ == 0U || height_ == 0U)
    {
        return { 0.0F, 0.0F, 1.0F, 1.0F };
    }
    const float inv_w = 1.0F / static_cast<float>(width_);
    const float inv_h = 1.0F / static_cast<float>(height_);
    const std::uint32_t clamped_x = std::min(px, width_);
    const std::uint32_t clamped_y = std::min(py, height_);
    const std::uint32_t clamped_w = std::min(pw, width_ - clamped_x);
    const std::uint32_t clamped_h = std::min(ph, height_ - clamped_y);

    return {
        static_cast<float>(clamped_x) * inv_w,
        static_cast<float>(clamped_y) * inv_h,
        static_cast<float>(clamped_x + clamped_w) * inv_w,
        static_cast<float>(clamped_y + clamped_h) * inv_h,
    };
}

UvRect2D TextureAtlas2D::add_region(
    std::string_view name,
    std::uint32_t    x,
    std::uint32_t    y,
    std::uint32_t    w,
    std::uint32_t    h)
{
    const UvRect2D uv = compute_uv(x, y, w, h);
    for (auto& r : regions_)
    {
        if (r.name == name)
        {
            r.x       = x;
            r.y       = y;
            r.width   = w;
            r.height  = h;
            r.pixel_w = w;
            r.pixel_h = h;
            r.uv      = uv;
            return uv;
        }
    }

    AtlasRegion2D reg {};
    reg.name    = std::string(name);
    reg.x       = x;
    reg.y       = y;
    reg.width   = w;
    reg.height  = h;
    reg.pixel_w = w;
    reg.pixel_h = h;
    reg.uv      = uv;
    regions_.push_back(std::move(reg));
    return uv;
}

const AtlasRegion2D* TextureAtlas2D::find_region(std::string_view name) const noexcept
{
    for (const auto& r : regions_)
    {
        if (r.name == name)
        {
            return &r;
        }
    }
    return nullptr;
}

void TextureAtlas2D::slice_grid(std::string_view prefix, std::uint32_t tile_w, std::uint32_t tile_h)
{
    if (tile_w == 0U || tile_h == 0U || width_ == 0U || height_ == 0U)
    {
        return;
    }
    const std::uint32_t cols = width_ / tile_w;
    const std::uint32_t rows = height_ / tile_h;
    for (std::uint32_t r = 0U; r < rows; ++r)
    {
        for (std::uint32_t c = 0U; c < cols; ++c)
        {
            const std::string name = std::string(prefix) + "_" + std::to_string(r) + "_" + std::to_string(c);
            add_region(name, c * tile_w, r * tile_h, tile_w, tile_h);
        }
    }
}

TextureAtlas2D TextureAtlas2D::generate_procedural_starter_atlas(std::uint32_t tile_size)
{
    const std::uint32_t ts        = (tile_size < 8U) ? 16U : tile_size;
    const std::uint32_t atlas_dim = ts * 8U; // 128x128 for 16px, 256x256 for 32px
    TextureAtlas2D atlas { atlas_dim, atlas_dim, TextureFilterMode2D::kNearest };

    // =========================================================================
    // Row 0: Core Terrain, Gems, Coins, Patrol Drone & Radiant Spark
    // =========================================================================

    // (0,0) tile_grass: Lush moss-capped stone masonry
    atlas.add_region("tile_grass", 0U * ts, 0U * ts, ts, ts);
    draw_grass_cap_over_stone(atlas, 0U * ts, 0U * ts, ts, false, false);

    // (1,0) tile_dirt: Subterranean cavern earth with embedded pebbles
    atlas.add_region("tile_dirt", 1U * ts, 0U * ts, ts, ts);
    for (std::uint32_t y = 0U; y < ts; ++y)
    {
        for (std::uint32_t x = 0U; x < ts; ++x)
        {
            const std::uint32_t h = (x * 19U + y * 37U + ((x ^ y) * 7U)) & 15U;
            const ColorRgba8 c = (h < 3U) ? ColorRgba8 { 68U, 46U, 36U, 255U }
                               : (h < 6U) ? ColorRgba8 { 92U, 64U, 48U, 255U }
                               : (h < 13U) ? ColorRgba8 { 114U, 82U, 62U, 255U }
                                           : ColorRgba8 { 138U, 106U, 82U, 255U };
            atlas.set_pixel(1U * ts + x, 0U * ts + y, c);
        }
    }

    // (2,0) tile_stone: Carved ashlar dungeon/fortress stone blocks
    atlas.add_region("tile_stone", 2U * ts, 0U * ts, ts, ts);
    draw_ashlar_masonry(
        atlas, 2U * ts, 0U * ts, ts,
        { 86U, 98U, 124U, 255U },
        { 118U, 134U, 166U, 255U },
        { 58U, 68U, 90U, 255U },
        { 32U, 38U, 52U, 255U });

    // (3,0) gem_ruby: Faceted crimson-rose crystal with pixel outline
    atlas.add_region("gem_ruby", 3U * ts, 0U * ts, ts, ts);
    draw_faceted_gem(
        atlas, 3U * ts, 0U * ts, ts,
        { 255U, 152U, 178U, 255U },
        { 232U, 44U, 82U, 255U },
        { 148U, 18U, 48U, 255U });

    // (4,0) coin_gold: Embossed gold medallion with specular rim
    atlas.add_region("coin_gold", 4U * ts, 0U * ts, ts, ts);
    {
        const std::uint32_t ox = 4U * ts;
        const std::uint32_t oy = 0U * ts;
        draw_circle_pattern(
            atlas, ox + ts / 2U, oy + ts / 2U, std::max(3U, ts / 2U - scale_px(3U, ts)),
            { 255U, 214U, 48U, 255U }, { 176U, 122U, 18U, 255U });
        fill_subrect(atlas, ox, oy, ts, 7U, 5U, 2U, 6U, { 255U, 248U, 176U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (5,0) enemy_drone: Armored hover sentinel with twin rotor pods & crimson visor
    atlas.add_region("enemy_drone", 5U * ts, 0U * ts, ts, ts);
    {
        const std::uint32_t ox = 5U * ts;
        const std::uint32_t oy = 0U * ts;
        fill_subrect(atlas, ox, oy, ts, 1U, 3U, 4U, 1U, { 160U, 238U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 11U, 3U, 4U, 1U, { 160U, 238U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 4U, 2U, 3U, { 72U, 82U, 104U, 255U });
        fill_subrect(atlas, ox, oy, ts, 12U, 4U, 2U, 3U, { 72U, 82U, 104U, 255U });
        fill_subrect(atlas, ox, oy, ts, 4U, 4U, 8U, 7U, { 88U, 98U, 122U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 4U, 6U, 2U, { 132U, 146U, 176U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 6U, 6U, 3U, { 38U, 22U, 32U, 255U });
        fill_subrect(atlas, ox, oy, ts, 6U, 7U, 4U, 2U, { 255U, 46U, 68U, 255U });
        fill_subrect(atlas, ox, oy, ts, 7U, 7U, 2U, 1U, { 255U, 196U, 204U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 11U, 2U, 2U, { 255U, 140U, 48U, 255U });
        fill_subrect(atlas, ox, oy, ts, 9U, 11U, 2U, 2U, { 255U, 140U, 48U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (6,0) particle_spark: Radiant 4-point starflare with soft alpha halo
    atlas.add_region("particle_spark", 6U * ts, 0U * ts, ts, ts);
    {
        const std::uint32_t ox  = 6U * ts;
        const std::uint32_t oy  = 0U * ts;
        const int           mid = static_cast<int>(ts / 2U);
        const int           rad = std::max(3, mid - 1);
        for (int dy = -rad; dy <= rad; ++dy)
        {
            for (int dx = -rad; dx <= rad; ++dx)
            {
                const float ax   = static_cast<float>(std::abs(dx)) / static_cast<float>(rad);
                const float ay   = static_cast<float>(std::abs(dy)) / static_cast<float>(rad);
                const float star = std::min(ax + ay * 0.35F, ay + ax * 0.35F);
                if (star <= 0.85F)
                {
                    const float core = std::clamp(1.0F - star / 0.85F, 0.0F, 1.0F);
                    const auto  a    = static_cast<std::uint8_t>(std::lround(core * core * 255.0F));
                    const auto  g    = static_cast<std::uint8_t>(std::clamp(185.0F + core * 70.0F, 0.0F, 255.0F));
                    const auto  b    = static_cast<std::uint8_t>(std::clamp(80.0F + core * 175.0F, 0.0F, 255.0F));
                    atlas.set_pixel(
                        ox + static_cast<std::uint32_t>(mid + dx),
                        oy + static_cast<std::uint32_t>(mid + dy),
                        { 255U, g, b, a });
                }
            }
        }
    }

    // (7,0) gem_emerald: Faceted cyan-emerald energy crystal
    atlas.add_region("gem_emerald", 7U * ts, 0U * ts, ts, ts);
    draw_faceted_gem(
        atlas, 7U * ts, 0U * ts, ts,
        { 168U, 255U, 232U, 255U },
        { 46U, 226U, 168U, 255U },
        { 20U, 134U, 108U, 255U });

    // =========================================================================
    // Row 1: Cyber-Knight 4-Frame Flipbook + Interactive Props & Portal
    // =========================================================================
    atlas.add_region("hero_idle", 0U * ts, 1U * ts, ts, ts);
    draw_cyber_knight_frame(atlas, 0U * ts, 1U * ts, ts, 0);

    atlas.add_region("hero_walk_0", 1U * ts, 1U * ts, ts, ts);
    draw_cyber_knight_frame(atlas, 1U * ts, 1U * ts, ts, 1);

    atlas.add_region("hero_walk_1", 2U * ts, 1U * ts, ts, ts);
    draw_cyber_knight_frame(atlas, 2U * ts, 1U * ts, ts, 2);

    atlas.add_region("hero_jump", 3U * ts, 1U * ts, ts, ts);
    draw_cyber_knight_frame(atlas, 3U * ts, 1U * ts, ts, 3);

    // (4,1) crate_wood: Reinforced supply crate with iron corner brackets & timber planks
    atlas.add_region("crate_wood", 4U * ts, 1U * ts, ts, ts);
    {
        const std::uint32_t ox = 4U * ts;
        const std::uint32_t oy = 1U * ts;
        fill_subrect(atlas, ox, oy, ts, 1U, 1U, 14U, 14U, { 166U, 112U, 58U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 2U, 12U, 12U, { 196U, 138U, 74U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 5U, 12U, 1U, { 124U, 78U, 38U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 10U, 12U, 1U, { 124U, 78U, 38U, 255U });
        for (std::uint32_t d = 2U; d < 14U; ++d)
        {
            fill_subrect(atlas, ox, oy, ts, d, d, 2U, 1U, { 146U, 96U, 48U, 255U });
        }
        const ColorRgba8 iron { 86U, 98U, 118U, 255U };
        fill_subrect(atlas, ox, oy, ts, 1U, 1U, 3U, 3U, iron);
        fill_subrect(atlas, ox, oy, ts, 12U, 1U, 3U, 3U, iron);
        fill_subrect(atlas, ox, oy, ts, 1U, 12U, 3U, 3U, iron);
        fill_subrect(atlas, ox, oy, ts, 12U, 12U, 3U, 3U, iron);
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (5,1) jumppad_coil: Sci-fi bounce launcher pad with glowing cyan-emerald coils
    atlas.add_region("jumppad_coil", 5U * ts, 1U * ts, ts, ts);
    {
        const std::uint32_t ox = 5U * ts;
        const std::uint32_t oy = 1U * ts;
        fill_subrect(atlas, ox, oy, ts, 1U, 12U, 14U, 3U, { 68U, 78U, 98U, 255U });
        fill_subrect(atlas, ox, oy, ts, 4U, 8U, 8U, 4U, { 42U, 198U, 172U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 5U, 12U, 3U, { 96U, 252U, 218U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 5U, 6U, 1U, { 225U, 255U, 248U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (6,1) portal_swirl: Dimensional stone archway & glowing cyan-violet vortex
    atlas.add_region("portal_swirl", 6U * ts, 1U * ts, ts, ts);
    {
        const std::uint32_t ox  = 6U * ts;
        const std::uint32_t oy  = 1U * ts;
        const float         cx  = static_cast<float>(ts) * 0.5F;
        const float         cy  = static_cast<float>(ts) * 0.52F;
        const float         rx  = static_cast<float>(ts) * 0.40F;
        const float         ry  = static_cast<float>(ts) * 0.46F;
        for (std::uint32_t y = 0U; y < ts; ++y)
        {
            for (std::uint32_t x = 0U; x < ts; ++x)
            {
                const float dx = (static_cast<float>(x) + 0.5F - cx) / rx;
                const float dy = (static_cast<float>(y) + 0.5F - cy) / ry;
                const float r2 = dx * dx + dy * dy;
                if (r2 <= 1.0F && r2 >= 0.68F)
                {
                    atlas.set_pixel(ox + x, oy + y, { 92U, 106U, 138U, 255U });
                }
                else if (r2 < 0.68F)
                {
                    const float ang   = std::atan2(dy, dx);
                    const float swirl = 0.5F + 0.5F * std::sin(ang * 3.0F + std::sqrt(r2) * 9.0F);
                    const auto  r_c   = static_cast<std::uint8_t>(std::clamp(58.0F + (1.0F - r2) * 140.0F, 0.0F, 255.0F));
                    const auto  g_c   = static_cast<std::uint8_t>(std::clamp(165.0F + swirl * 88.0F, 0.0F, 255.0F));
                    atlas.set_pixel(ox + x, oy + y, { r_c, g_c, 255U, 255U });
                }
            }
        }
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (7,1) platform_hover: Industrial hover bridge with hazard stripes & thrusters
    atlas.add_region("platform_hover", 7U * ts, 1U * ts, ts, ts);
    {
        const std::uint32_t ox = 7U * ts;
        const std::uint32_t oy = 1U * ts;
        fill_subrect(atlas, ox, oy, ts, 1U, 4U, 14U, 6U, { 74U, 88U, 114U, 255U });
        for (std::uint32_t sx = 2U; sx < 14U; sx += 3U)
        {
            fill_subrect(atlas, ox, oy, ts, sx, 4U, 2U, 2U, { 248U, 194U, 58U, 255U });
        }
        fill_subrect(atlas, ox, oy, ts, 3U, 10U, 3U, 2U, { 88U, 238U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 10U, 10U, 3U, 2U, { 88U, 238U, 255U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // =========================================================================
    // Row 2: Specialized Tiles (One-Way, Spikes, Neon, Dungeon, Arena) & RPG Sprites
    // =========================================================================

    // (0,2) tile_platform_oneway: Steel catwalk girder with hazard top & open lattice
    atlas.add_region("tile_platform_oneway", 0U * ts, 2U * ts, ts, ts);
    {
        const std::uint32_t ox = 0U * ts;
        const std::uint32_t oy = 2U * ts;
        fill_subrect(atlas, ox, oy, ts, 0U, 0U, 16U, 6U, { 78U, 90U, 114U, 255U });
        fill_subrect(atlas, ox, oy, ts, 0U, 0U, 16U, 1U, { 28U, 34U, 48U, 255U });
        for (std::uint32_t sx = 1U; sx < 15U; sx += 4U)
        {
            fill_subrect(atlas, ox, oy, ts, sx, 1U, 2U, 2U, { 246U, 192U, 62U, 255U });
        }
        fill_subrect(atlas, ox, oy, ts, 0U, 5U, 16U, 1U, { 36U, 44U, 60U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 6U, 2U, 3U, { 52U, 62U, 82U, 255U });
        fill_subrect(atlas, ox, oy, ts, 12U, 6U, 2U, 3U, { 52U, 62U, 82U, 255U });
    }

    // (1,2) tile_spikes_plasma: Metallic sawtooth spikes with glowing plasma tips
    atlas.add_region("tile_spikes_plasma", 1U * ts, 2U * ts, ts, ts);
    {
        const std::uint32_t ox = 1U * ts;
        const std::uint32_t oy = 2U * ts;
        fill_subrect(atlas, ox, oy, ts, 0U, 12U, 16U, 4U, { 78U, 24U, 40U, 255U });
        const std::uint32_t half_tooth = std::max(2U, ts / 4U);
        for (std::uint32_t x = 0U; x < ts; ++x)
        {
            const std::uint32_t local_x = x % (half_tooth * 2U);
            const std::uint32_t dist    = (local_x < half_tooth) ? (half_tooth - 1U - local_x) : (local_x - half_tooth);
            const std::uint32_t tip_y   = scale_px(3U, ts) + (dist * scale_px(9U, ts)) / std::max(1U, half_tooth);
            for (std::uint32_t y = tip_y; y < scale_px(13U, ts); ++y)
            {
                const ColorRgba8 c = (y <= tip_y + 1U)
                    ? ColorRgba8 { 255U, 182U, 120U, 255U }
                    : ((y <= tip_y + scale_px(3U, ts))
                           ? ColorRgba8 { 255U, 64U, 92U, 255U }
                           : ColorRgba8 { 164U, 32U, 56U, 255U });
                atlas.set_pixel(ox + x, oy + y, c);
            }
        }
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (2,2) tile_neon_conduit: Titanium wall block with glowing cyan circuit traces
    atlas.add_region("tile_neon_conduit", 2U * ts, 2U * ts, ts, ts);
    {
        const std::uint32_t ox = 2U * ts;
        const std::uint32_t oy = 2U * ts;
        fill_subrect(atlas, ox, oy, ts, 0U, 0U, 16U, 16U, { 34U, 54U, 84U, 255U });
        fill_subrect(atlas, ox, oy, ts, 1U, 1U, 14U, 14U, { 46U, 74U, 112U, 255U });
        fill_subrect(atlas, ox, oy, ts, 0U, 1U, 16U, 2U, { 88U, 238U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 3U, 7U, 10U, 2U, { 72U, 212U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 7U, 3U, 2U, 10U, { 72U, 212U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 6U, 6U, 4U, 4U, { 188U, 252U, 255U, 255U });
    }

    // (3,2) tile_dungeon_floor: Chiseled RPG dungeon cobblestone flagstones
    atlas.add_region("tile_dungeon_floor", 3U * ts, 2U * ts, ts, ts);
    draw_ashlar_masonry(
        atlas, 3U * ts, 2U * ts, ts,
        { 46U, 52U, 70U, 255U },
        { 60U, 68U, 90U, 255U },
        { 34U, 40U, 54U, 255U },
        { 24U, 28U, 38U, 255U });

    // (4,2) tile_arena_floor: Sci-fi arena deck plating with recessed neon grid
    atlas.add_region("tile_arena_floor", 4U * ts, 2U * ts, ts, ts);
    {
        const std::uint32_t ox = 4U * ts;
        const std::uint32_t oy = 2U * ts;
        fill_subrect(atlas, ox, oy, ts, 0U, 0U, 16U, 16U, { 26U, 34U, 52U, 255U });
        fill_subrect(atlas, ox, oy, ts, 1U, 1U, 14U, 14U, { 34U, 46U, 68U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 2U, 5U, 5U, { 42U, 56U, 82U, 255U });
        fill_subrect(atlas, ox, oy, ts, 9U, 2U, 5U, 5U, { 42U, 56U, 82U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 9U, 5U, 5U, { 42U, 56U, 82U, 255U });
        fill_subrect(atlas, ox, oy, ts, 9U, 9U, 5U, 5U, { 42U, 56U, 82U, 255U });
        fill_subrect(atlas, ox, oy, ts, 7U, 7U, 2U, 2U, { 54U, 148U, 204U, 255U });
    }

    // (5,2) chest_gold: Ornate oak & gold RPG treasure chest
    atlas.add_region("chest_gold", 5U * ts, 2U * ts, ts, ts);
    {
        const std::uint32_t ox = 5U * ts;
        const std::uint32_t oy = 2U * ts;
        fill_subrect(atlas, ox, oy, ts, 2U, 4U, 12U, 10U, { 154U, 92U, 42U, 255U });
        fill_subrect(atlas, ox, oy, ts, 3U, 3U, 10U, 4U, { 188U, 118U, 56U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 7U, 12U, 2U, { 248U, 202U, 64U, 255U });
        fill_subrect(atlas, ox, oy, ts, 4U, 3U, 2U, 11U, { 242U, 192U, 54U, 255U });
        fill_subrect(atlas, ox, oy, ts, 10U, 3U, 2U, 11U, { 242U, 192U, 54U, 255U });
        fill_subrect(atlas, ox, oy, ts, 7U, 6U, 2U, 4U, { 255U, 240U, 148U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (6,2) skeleton_guard: Armored dungeon sentinel with horned helm, shield & halberd
    atlas.add_region("skeleton_guard", 6U * ts, 2U * ts, ts, ts);
    {
        const std::uint32_t ox = 6U * ts;
        const std::uint32_t oy = 2U * ts;
        fill_subrect(atlas, ox, oy, ts, 5U, 2U, 6U, 5U, { 218U, 222U, 232U, 255U });
        fill_subrect(atlas, ox, oy, ts, 3U, 1U, 2U, 2U, { 196U, 184U, 152U, 255U });
        fill_subrect(atlas, ox, oy, ts, 11U, 1U, 2U, 2U, { 196U, 184U, 152U, 255U });
        fill_subrect(atlas, ox, oy, ts, 6U, 4U, 1U, 2U, { 255U, 48U, 68U, 255U });
        fill_subrect(atlas, ox, oy, ts, 9U, 4U, 1U, 2U, { 255U, 48U, 68U, 255U });
        fill_subrect(atlas, ox, oy, ts, 4U, 7U, 8U, 5U, { 122U, 44U, 62U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 7U, 3U, 5U, { 112U, 126U, 154U, 255U });
        fill_subrect(atlas, ox, oy, ts, 13U, 2U, 1U, 12U, { 148U, 116U, 74U, 255U });
        fill_subrect(atlas, ox, oy, ts, 12U, 2U, 3U, 3U, { 196U, 212U, 236U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 12U, 2U, 3U, { 92U, 102U, 124U, 255U });
        fill_subrect(atlas, ox, oy, ts, 9U, 12U, 2U, 3U, { 92U, 102U, 124U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (7,2) torch_brazier: Wrought-iron brazier with layered pixel flame
    atlas.add_region("torch_brazier", 7U * ts, 2U * ts, ts, ts);
    {
        const std::uint32_t ox = 7U * ts;
        const std::uint32_t oy = 2U * ts;
        fill_subrect(atlas, ox, oy, ts, 5U, 9U, 6U, 3U, { 72U, 80U, 98U, 255U });
        fill_subrect(atlas, ox, oy, ts, 6U, 12U, 4U, 3U, { 52U, 58U, 74U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 4U, 6U, 5U, { 255U, 118U, 32U, 255U });
        fill_subrect(atlas, ox, oy, ts, 6U, 2U, 4U, 6U, { 255U, 196U, 54U, 255U });
        fill_subrect(atlas, ox, oy, ts, 7U, 4U, 2U, 4U, { 255U, 250U, 196U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // =========================================================================
    // Row 3: Wang Edge Variants + Pillars + Projectiles + HD-2D Party & Trees
    // =========================================================================
    atlas.add_region("tile_grass_left", 0U * ts, 3U * ts, ts, ts);
    draw_grass_cap_over_stone(atlas, 0U * ts, 3U * ts, ts, true, false);

    atlas.add_region("tile_grass_right", 1U * ts, 3U * ts, ts, ts);
    draw_grass_cap_over_stone(atlas, 1U * ts, 3U * ts, ts, false, true);

    atlas.add_region("tile_grass_island", 2U * ts, 3U * ts, ts, ts);
    draw_grass_cap_over_stone(atlas, 2U * ts, 3U * ts, ts, true, true);

    // (3,3) stone_pillar: Fluted architectural stone column with capital & plinth
    atlas.add_region("stone_pillar", 3U * ts, 3U * ts, ts, ts);
    {
        const std::uint32_t ox = 3U * ts;
        const std::uint32_t oy = 3U * ts;
        fill_subrect(atlas, ox, oy, ts, 1U, 1U, 14U, 3U, { 118U, 132U, 162U, 255U });
        fill_subrect(atlas, ox, oy, ts, 3U, 4U, 10U, 9U, { 92U, 106U, 134U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 4U, 2U, 9U, { 66U, 78U, 102U, 255U });
        fill_subrect(atlas, ox, oy, ts, 9U, 4U, 2U, 9U, { 66U, 78U, 102U, 255U });
        fill_subrect(atlas, ox, oy, ts, 1U, 13U, 14U, 2U, { 118U, 132U, 162U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (4,3) projectile_plasma: High-energy cyan plasma bolt
    atlas.add_region("projectile_plasma", 4U * ts, 3U * ts, ts, ts);
    {
        const std::uint32_t ox = 4U * ts;
        const std::uint32_t oy = 3U * ts;
        draw_circle_pattern(
            atlas, ox + ts / 2U, oy + ts / 2U, std::max(3U, ts / 3U),
            { 92U, 244U, 255U, 255U }, { 32U, 168U, 238U, 255U });
        draw_circle_pattern(
            atlas, ox + ts / 2U, oy + ts / 2U, std::max(1U, ts / 6U),
            { 255U, 255U, 255U, 255U }, { 210U, 255U, 255U, 255U });
    }

    // (5,3) mage_companion: HD-2D / RPG Arcane Mage companion with pointed hat & glowing staff
    atlas.add_region("mage_companion", 5U * ts, 3U * ts, ts, ts);
    {
        const std::uint32_t ox = 5U * ts;
        const std::uint32_t oy = 3U * ts;
        fill_subrect(atlas, ox, oy, ts, 6U, 1U, 4U, 2U, { 72U, 54U, 164U, 255U });
        fill_subrect(atlas, ox, oy, ts, 3U, 3U, 10U, 2U, { 92U, 70U, 196U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 5U, 6U, 3U, { 246U, 212U, 178U, 255U });
        fill_subrect(atlas, ox, oy, ts, 6U, 6U, 1U, 1U, { 32U, 42U, 68U, 255U });
        fill_subrect(atlas, ox, oy, ts, 9U, 6U, 1U, 1U, { 32U, 42U, 68U, 255U });
        fill_subrect(atlas, ox, oy, ts, 4U, 8U, 8U, 6U, { 68U, 96U, 198U, 255U });
        fill_subrect(atlas, ox, oy, ts, 7U, 8U, 2U, 6U, { 246U, 202U, 72U, 255U });
        fill_subrect(atlas, ox, oy, ts, 13U, 4U, 1U, 10U, { 156U, 112U, 64U, 255U });
        fill_subrect(atlas, ox, oy, ts, 12U, 2U, 3U, 3U, { 118U, 248U, 255U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (6,3) key_golden: Ornate RPG dungeon key
    atlas.add_region("key_golden", 6U * ts, 3U * ts, ts, ts);
    {
        const std::uint32_t ox = 6U * ts;
        const std::uint32_t oy = 3U * ts;
        fill_subrect(atlas, ox, oy, ts, 4U, 3U, 5U, 5U, { 255U, 214U, 56U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 4U, 3U, 3U, { 0U, 0U, 0U, 0U });
        fill_subrect(atlas, ox, oy, ts, 6U, 8U, 2U, 6U, { 255U, 214U, 56U, 255U });
        fill_subrect(atlas, ox, oy, ts, 8U, 10U, 3U, 1U, { 255U, 236U, 128U, 255U });
        fill_subrect(atlas, ox, oy, ts, 8U, 12U, 3U, 1U, { 255U, 236U, 128U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (7,3) tree_pine: Layered emerald-cypress pixel-art tree for HD-2D & overworld
    atlas.add_region("tree_pine", 7U * ts, 3U * ts, ts, ts);
    {
        const std::uint32_t ox = 7U * ts;
        const std::uint32_t oy = 3U * ts;
        fill_subrect(atlas, ox, oy, ts, 6U, 12U, 4U, 3U, { 118U, 78U, 48U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 9U, 12U, 3U, { 36U, 136U, 86U, 255U });
        fill_subrect(atlas, ox, oy, ts, 3U, 6U, 10U, 3U, { 52U, 178U, 108U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 3U, 6U, 3U, { 76U, 214U, 132U, 255U });
        fill_subrect(atlas, ox, oy, ts, 7U, 1U, 2U, 2U, { 118U, 242U, 162U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // =========================================================================
    // Row 4: Arcade Paddles, Plasma Ball & Structural Destructible Bridges
    // =========================================================================
    // (0,4) paddle_cyan: Vertical player cyber-paddle with glowing cyan bumper
    atlas.add_region("paddle_cyan", 0U * ts, 4U * ts, ts, ts);
    {
        const std::uint32_t ox = 0U * ts;
        const std::uint32_t oy = 4U * ts;
        fill_subrect(atlas, ox, oy, ts, 3U, 1U, 10U, 14U, { 42U, 62U, 92U, 255U });
        fill_subrect(atlas, ox, oy, ts, 4U, 2U, 8U, 12U, { 68U, 108U, 156U, 255U });
        fill_subrect(atlas, ox, oy, ts, 10U, 2U, 3U, 12U, { 56U, 238U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 11U, 4U, 1U, 8U, { 228U, 255U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 3U, 3U, 2U, { 142U, 198U, 242U, 255U });
        fill_subrect(atlas, ox, oy, ts, 5U, 11U, 3U, 2U, { 142U, 198U, 242U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (1,4) paddle_magenta: Vertical AI opponent paddle with glowing crimson-magenta bumper
    atlas.add_region("paddle_magenta", 1U * ts, 4U * ts, ts, ts);
    {
        const std::uint32_t ox = 1U * ts;
        const std::uint32_t oy = 4U * ts;
        fill_subrect(atlas, ox, oy, ts, 3U, 1U, 10U, 14U, { 84U, 36U, 68U, 255U });
        fill_subrect(atlas, ox, oy, ts, 4U, 2U, 8U, 12U, { 138U, 58U, 108U, 255U });
        fill_subrect(atlas, ox, oy, ts, 3U, 2U, 3U, 12U, { 255U, 68U, 148U, 255U });
        fill_subrect(atlas, ox, oy, ts, 4U, 4U, 1U, 8U, { 255U, 224U, 242U, 255U });
        fill_subrect(atlas, ox, oy, ts, 8U, 3U, 3U, 2U, { 224U, 128U, 178U, 255U });
        fill_subrect(atlas, ox, oy, ts, 8U, 11U, 3U, 2U, { 224U, 128U, 178U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (2,4) paddle_striker: Horizontal armored bridge-demolition striker & plasma launcher
    atlas.add_region("paddle_striker", 2U * ts, 4U * ts, ts, ts);
    {
        const std::uint32_t ox = 2U * ts;
        const std::uint32_t oy = 4U * ts;
        fill_subrect(atlas, ox, oy, ts, 1U, 4U, 14U, 8U, { 48U, 64U, 90U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 3U, 12U, 3U, { 58U, 236U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 4U, 4U, 8U, 1U, { 232U, 255U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 6U, 1U, 4U, 3U, { 255U, 196U, 64U, 255U });
        fill_subrect(atlas, ox, oy, ts, 3U, 8U, 10U, 3U, { 92U, 116U, 152U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (3,4) ball_energy: High-contrast radiant arcade energy sphere
    atlas.add_region("ball_energy", 3U * ts, 4U * ts, ts, ts);
    {
        const std::uint32_t ox = 3U * ts;
        const std::uint32_t oy = 4U * ts;
        draw_circle_pattern(
            atlas, ox + ts / 2U, oy + ts / 2U, std::max(4U, ts / 2U - scale_px(2U, ts)),
            { 255U, 252U, 224U, 255U }, { 64U, 224U, 255U, 255U });
        fill_subrect(atlas, ox, oy, ts, 6U, 5U, 3U, 3U, { 255U, 255U, 255U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (4,4) bridge_plank: Riveted iron-and-oak suspension bridge span segment
    atlas.add_region("bridge_plank", 4U * ts, 4U * ts, ts, ts);
    {
        const std::uint32_t ox = 4U * ts;
        const std::uint32_t oy = 4U * ts;
        fill_subrect(atlas, ox, oy, ts, 0U, 2U, 16U, 3U, { 112U, 128U, 152U, 255U }); // Top steel rail
        fill_subrect(atlas, ox, oy, ts, 0U, 5U, 16U, 7U, { 168U, 112U, 56U, 255U });  // Heavy timber deck
        fill_subrect(atlas, ox, oy, ts, 1U, 6U, 6U, 5U, { 196U, 136U, 72U, 255U });
        fill_subrect(atlas, ox, oy, ts, 9U, 6U, 6U, 5U, { 196U, 136U, 72U, 255U });
        fill_subrect(atlas, ox, oy, ts, 0U, 12U, 16U, 2U, { 74U, 86U, 108U, 255U });  // Under-girder truss
        fill_subrect(atlas, ox, oy, ts, 2U, 3U, 2U, 1U, { 255U, 214U, 104U, 255U });  // Rivets
        fill_subrect(atlas, ox, oy, ts, 12U, 3U, 2U, 1U, { 255U, 214U, 104U, 255U });
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    // (5,4) bridge_pillar: Load-bearing keystone masonry & steel bridge pier
    atlas.add_region("bridge_pillar", 5U * ts, 4U * ts, ts, ts);
    {
        const std::uint32_t ox = 5U * ts;
        const std::uint32_t oy = 4U * ts;
        fill_subrect(atlas, ox, oy, ts, 2U, 1U, 12U, 3U, { 214U, 164U, 76U, 255U });  // Golden keystone cap
        fill_subrect(atlas, ox, oy, ts, 3U, 4U, 10U, 9U, { 108U, 118U, 138U, 255U }); // Stone shaft
        fill_subrect(atlas, ox, oy, ts, 5U, 5U, 2U, 7U, { 72U, 80U, 96U, 255U });     // Fluting
        fill_subrect(atlas, ox, oy, ts, 9U, 5U, 2U, 7U, { 72U, 80U, 96U, 255U });
        fill_subrect(atlas, ox, oy, ts, 2U, 13U, 12U, 2U, { 84U, 92U, 112U, 255U });  // Pier footing
        add_dark_pixel_outline(atlas, ox, oy, ts);
    }

    return atlas;
}

TextureAtlas2D TextureAtlas2D::generate_starter_normal_atlas(std::uint32_t tile_size)
{
    const std::uint32_t ts = (tile_size < 8U) ? 16U : tile_size;
    const std::uint32_t atlas_dim = ts * 8U;
    TextureAtlas2D normal_atlas { atlas_dim, atlas_dim, TextureFilterMode2D::kLinear };

    // Fill completely with flat tangent normal (0, 0, 1) -> RGB (128, 128, 255, 255)
    const ColorRgba8 flat_norm { 128U, 128U, 255U, 255U };
    for (std::uint32_t y = 0; y < atlas_dim; ++y)
    {
        for (std::uint32_t x = 0; x < atlas_dim; ++x)
        {
            normal_atlas.set_pixel(x, y, flat_norm);
        }
    }

    // Mirror same regions as albedo atlas
    const TextureAtlas2D starter = generate_procedural_starter_atlas(tile_size);
    for (const auto& reg : starter.regions())
    {
        normal_atlas.add_region(reg.name, reg.x, reg.y, reg.width, reg.height);

        // Spherical normals for circles / gems
        if (reg.name == "gem_ruby" || reg.name == "coin_gold" || reg.name == "enemy_drone")
        {
            const float r = static_cast<float>(reg.width) * 0.5F;
            const float cx = static_cast<float>(reg.x) + r;
            const float cy = static_cast<float>(reg.y) + r;
            for (std::uint32_t py = reg.y; py < reg.y + reg.height; ++py)
            {
                for (std::uint32_t px = reg.x; px < reg.x + reg.width; ++px)
                {
                    const float dx = (static_cast<float>(px) + 0.5F - cx) / r;
                    const float dy = (static_cast<float>(py) + 0.5F - cy) / r;
                    const float d_sq = dx * dx + dy * dy;
                    if (d_sq <= 1.0F)
                    {
                        const float dz = std::sqrt(std::max(0.0F, 1.0F - d_sq));
                        const auto nr = static_cast<std::uint8_t>(std::clamp((dx * 0.5F + 0.5F) * 255.0F, 0.0F, 255.0F));
                        const auto ng = static_cast<std::uint8_t>(std::clamp((dy * 0.5F + 0.5F) * 255.0F, 0.0F, 255.0F));
                        const auto nb = static_cast<std::uint8_t>(std::clamp((dz * 0.5F + 0.5F) * 255.0F, 0.0F, 255.0F));
                        normal_atlas.set_pixel(px, py, { nr, ng, nb, 255U });
                    }
                }
            }
        }
        else if (reg.name.starts_with("tile_"))
        {
            // Top bevel: points upward (+Y)
            for (std::uint32_t px = reg.x; px < reg.x + reg.width; ++px)
            {
                normal_atlas.set_pixel(px, reg.y, { 128U, 230U, 180U, 255U });
                normal_atlas.set_pixel(px, reg.y + reg.height - 1U, { 128U, 40U, 180U, 255U });
            }
            for (std::uint32_t py = reg.y; py < reg.y + reg.height; ++py)
            {
                normal_atlas.set_pixel(reg.x, py, { 40U, 128U, 180U, 255U });
                normal_atlas.set_pixel(reg.x + reg.width - 1U, py, { 230U, 128U, 180U, 255U });
            }
        }
    }

    return normal_atlas;
}

TextureAtlas2D TextureAtlas2D::generate_starter_emissive_atlas(std::uint32_t tile_size)
{
    const std::uint32_t ts = (tile_size < 8U) ? 16U : tile_size;
    const std::uint32_t atlas_dim = ts * 8U;
    TextureAtlas2D emissive_atlas { atlas_dim, atlas_dim, TextureFilterMode2D::kLinear };

    // Default: completely dark / non-emissive
    const TextureAtlas2D starter = generate_procedural_starter_atlas(tile_size);
    for (const auto& reg : starter.regions())
    {
        emissive_atlas.add_region(reg.name, reg.x, reg.y, reg.width, reg.height);
        if (reg.name.starts_with("hero_"))
        {
            // Glowing visor
            for (std::uint32_t vx = 4U; vx < reg.width - 4U; ++vx)
            {
                emissive_atlas.set_pixel(reg.x + vx, reg.y + 4U, { 96U, 238U, 255U, 255U });
            }
        }
        else if (reg.name == "gem_ruby")
        {
            // Gem core glow
            draw_circle_pattern(
                emissive_atlas,
                reg.x + reg.width / 2U,
                reg.y + reg.height / 2U,
                reg.width / 4U,
                { 255U, 40U, 80U, 255U },
                { 180U, 20U, 40U, 255U });
        }
        else if (reg.name == "enemy_drone")
        {
            // Pulsing sensor eye
            draw_circle_pattern(
                emissive_atlas,
                reg.x + reg.width / 2U,
                reg.y + reg.height / 2U,
                reg.width / 6U,
                { 255U, 30U, 40U, 255U },
                { 255U, 30U, 40U, 255U });
        }
        else if (reg.name == "particle_spark")
        {
            const std::uint32_t scx = reg.x + reg.width / 2U;
            const std::uint32_t scy = reg.y + reg.height / 2U;
            emissive_atlas.set_pixel(scx, scy, { 255U, 255U, 240U, 255U });
            emissive_atlas.set_pixel(scx - 1U, scy, { 255U, 200U, 50U, 255U });
            emissive_atlas.set_pixel(scx + 1U, scy, { 255U, 200U, 50U, 255U });
            emissive_atlas.set_pixel(scx, scy - 1U, { 255U, 200U, 50U, 255U });
            emissive_atlas.set_pixel(scx, scy + 1U, { 255U, 200U, 50U, 255U });
        }
    }

    return emissive_atlas;
}

StarterAtlasBundle2D TextureAtlas2D::generate_starter_bundle(std::uint32_t tile_size)
{
    return StarterAtlasBundle2D {
        .albedo   = generate_procedural_starter_atlas(tile_size),
        .normal   = generate_starter_normal_atlas(tile_size),
        .emissive = generate_starter_emissive_atlas(tile_size),
    };
}

cd::core::Result<TextureAtlas2D> TextureAtlas2D::load_from_file(
    std::string_view    path,
    TextureFilterMode2D filter)
{
    const auto img_res = cd::asset::image::load_image(path);
    if (!img_res.has_value())
    {
        return std::unexpected(atlas_errors::make(
            atlas_errors::Code::kFileNotFound,
            "TextureAtlas2D: failed to load image from file"));
    }
    const auto& img = img_res.value();
    TextureAtlas2D atlas { img.width, img.height, filter };
    if (!img.rgba.empty())
    {
        std::memcpy(atlas.mutable_pixels().data(), img.rgba.data(), img.rgba.size());
    }
    return atlas;
}

cd::core::Result<TextureAtlas2D> TextureAtlas2D::load_from_memory(
    std::span<const std::uint8_t> png_bytes,
    TextureFilterMode2D           filter)
{
    const auto img_res = cd::asset::image::load_image_from_memory(png_bytes);
    if (!img_res.has_value())
    {
        return std::unexpected(atlas_errors::make(
            atlas_errors::Code::kDecodeFailed,
            "TextureAtlas2D: failed to decode PNG memory buffer"));
    }
    const auto& img = img_res.value();
    TextureAtlas2D atlas { img.width, img.height, filter };
    if (!img.rgba.empty())
    {
        std::memcpy(atlas.mutable_pixels().data(), img.rgba.data(), img.rgba.size());
    }
    return atlas;
}

cd::core::Result<void> TextureAtlas2D::load_regions_from_aseprite_json(std::string_view json_text)
{
    const auto doc_res = cd::asset::json::parse(json_text);
    if (!doc_res.has_value())
    {
        return std::unexpected(atlas_errors::make(atlas_errors::Code::kDecodeFailed, "Invalid JSON"));
    }
    const auto& root = doc_res.value();
    if (!root.is_object())
    {
        return std::unexpected(atlas_errors::make(atlas_errors::Code::kInvalidData, "Expected JSON object"));
    }
    const auto frames_res = root.at("frames");
    if (!frames_res.has_value())
    {
        return {};
    }
    const auto* frames = *frames_res;

    if (frames->is_object())
    {
        for (const auto& [name, fv] : frames->as_object())
        {
            if (const auto f_res = fv.at("frame"); f_res.has_value() && (*f_res)->is_object())
            {
                const auto* f = *f_res;
                const auto x_res = f->at("x");
                const auto y_res = f->at("y");
                const auto w_res = f->at("w");
                const auto h_res = f->at("h");
                const auto x = (x_res.has_value() && (*x_res)->is_number()) ? static_cast<std::uint32_t>((*x_res)->as_number()) : 0U;
                const auto y = (y_res.has_value() && (*y_res)->is_number()) ? static_cast<std::uint32_t>((*y_res)->as_number()) : 0U;
                const auto w = (w_res.has_value() && (*w_res)->is_number()) ? static_cast<std::uint32_t>((*w_res)->as_number()) : 0U;
                const auto h = (h_res.has_value() && (*h_res)->is_number()) ? static_cast<std::uint32_t>((*h_res)->as_number()) : 0U;
                add_region(name, x, y, w, h);
            }
        }
    }
    else if (frames->is_array())
    {
        std::size_t idx = 0;
        for (const auto& item : frames->as_array())
        {
            std::string name = "frame_" + std::to_string(idx);
            if (const auto fn_res = item.at("filename"); fn_res.has_value() && (*fn_res)->is_string())
            {
                name = (*fn_res)->as_string();
            }
            if (const auto f_res = item.at("frame"); f_res.has_value() && (*f_res)->is_object())
            {
                const auto* f = *f_res;
                const auto x_res = f->at("x");
                const auto y_res = f->at("y");
                const auto w_res = f->at("w");
                const auto h_res = f->at("h");
                const auto x = (x_res.has_value() && (*x_res)->is_number()) ? static_cast<std::uint32_t>((*x_res)->as_number()) : 0U;
                const auto y = (y_res.has_value() && (*y_res)->is_number()) ? static_cast<std::uint32_t>((*y_res)->as_number()) : 0U;
                const auto w = (w_res.has_value() && (*w_res)->is_number()) ? static_cast<std::uint32_t>((*w_res)->as_number()) : 0U;
                const auto h = (h_res.has_value() && (*h_res)->is_number()) ? static_cast<std::uint32_t>((*h_res)->as_number()) : 0U;
                add_region(name, x, y, w, h);
            }
            ++idx;
        }
    }

    return {};
}

cd::core::Result<void> TextureAtlas2D::load_regions_from_ldtk_json(std::string_view json_text)
{
    const auto doc_res = cd::asset::json::parse(json_text);
    if (!doc_res.has_value())
    {
        return std::unexpected(atlas_errors::make(atlas_errors::Code::kDecodeFailed, "Invalid LDtk JSON"));
    }
    const auto& root = doc_res.value();
    const auto defs_res = root.at("defs");
    if (!defs_res.has_value() || !(*defs_res)->is_object())
    {
        return {};
    }
    const auto tilesets_res = (*defs_res)->at("tilesets");
    if (!tilesets_res.has_value() || !(*tilesets_res)->is_array())
    {
        return {};
    }

    for (const auto& ts : (*tilesets_res)->as_array())
    {
        std::string id = "Tileset";
        if (const auto id_res = ts.at("identifier"); id_res.has_value() && (*id_res)->is_string())
        {
            id = (*id_res)->as_string();
        }
        std::uint32_t tile_w = 16U;
        if (const auto gs_res = ts.at("tileGridSize"); gs_res.has_value() && (*gs_res)->is_number())
        {
            tile_w = static_cast<std::uint32_t>((*gs_res)->as_number());
        }
        const std::uint32_t tile_h = tile_w;
        std::uint32_t px_w = width_;
        if (const auto pw_res = ts.at("pxWid"); pw_res.has_value() && (*pw_res)->is_number())
        {
            px_w = static_cast<std::uint32_t>((*pw_res)->as_number());
        }
        std::uint32_t px_h = height_;
        if (const auto ph_res = ts.at("pxHei"); ph_res.has_value() && (*ph_res)->is_number())
        {
            px_h = static_cast<std::uint32_t>((*ph_res)->as_number());
        }

        const std::uint32_t cols = (tile_w > 0U) ? (px_w / tile_w) : 1U;
        const std::uint32_t rows = (tile_h > 0U) ? (px_h / tile_h) : 1U;
        for (std::uint32_t r = 0U; r < rows; ++r)
        {
            for (std::uint32_t c = 0U; c < cols; ++c)
            {
                const std::string name = id + "_" + std::to_string(r * cols + c);
                add_region(name, c * tile_w, r * tile_h, tile_w, tile_h);
            }
        }
    }

    return {};
}

cd::core::Result<TextureAtlas2D> load_texture_atlas_png(
    std::string_view    path,
    TextureFilterMode2D filter)
{
    return TextureAtlas2D::load_from_file(path, filter);
}

cd::core::Result<TextureAtlas2D> load_texture_atlas_png_memory(
    std::span<const std::uint8_t> bytes,
    std::uint32_t                 width,
    std::uint32_t                 height,
    TextureFilterMode2D           filter)
{
    if (width > 0U && height > 0U)
    {
        const std::size_t expected_size = static_cast<std::size_t>(width * height * 4U);
        if (bytes.size() < expected_size)
        {
            return std::unexpected(atlas_errors::make(
                atlas_errors::Code::kInvalidData,
                "Buffer size smaller than width * height * 4"));
        }
        TextureAtlas2D atlas { width, height, filter };
        std::memcpy(atlas.mutable_pixels().data(), bytes.data(), expected_size);
        return atlas;
    }

    return TextureAtlas2D::load_from_memory(bytes, filter);
}

}  // namespace cd::render2d
