// =============================================================================
// CHROMODYNAMIC — cd/render2d/Tilemap2D.cpp
// FAZ 34 (ADR-20261111) — Sparse 16x16 Chunked Tilemap, 4-Bit Wang Auto-Tiling
// & Greedy 2D Rectangle Collider Meshing.
// =============================================================================
#include <cd/render2d/Tilemap2D.hpp>

#include <algorithm>
#include <cmath>

namespace cd::render2d
{

namespace
{

[[nodiscard]] int floor_div_int(int a, int b) noexcept
{
    int q = a / b;
    int r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0)))
    {
        --q;
    }
    return q;
}

}  // namespace

Tilemap2D::Tilemap2D()
{
    register_palette_entry(TilePaletteEntry2D {
        .palette_id        = 1U,
        .name              = "Lush Moss Stone",
        .fill_color        = { 54U, 66U, 88U, 255U },
        .top_cap_color     = { 72U, 218U, 136U, 255U },
        .edge_border_color = { 30U, 38U, 54U, 255U },
        .solid             = true,
        .one_way           = false,
        .hazard            = false,
        .emissive          = false,
    });
    register_palette_entry(TilePaletteEntry2D {
        .palette_id        = 2U,
        .name              = "Industrial Steel Deck",
        .fill_color        = { 76U, 88U, 108U, 255U },
        .top_cap_color     = { 242U, 188U, 68U, 255U },
        .edge_border_color = { 40U, 48U, 62U, 255U },
        .solid             = true,
        .one_way           = true,
        .hazard            = false,
        .emissive          = false,
    });
    register_palette_entry(TilePaletteEntry2D {
        .palette_id        = 3U,
        .name              = "Plasma Hazard Spikes",
        .fill_color        = { 196U, 42U, 62U, 255U },
        .top_cap_color     = { 255U, 128U, 96U, 255U },
        .edge_border_color = { 108U, 18U, 32U, 255U },
        .solid             = true,
        .one_way           = false,
        .hazard            = true,
        .emissive          = true,
    });
    register_palette_entry(TilePaletteEntry2D {
        .palette_id        = 4U,
        .name              = "Cyber Neon Conduit",
        .fill_color        = { 38U, 96U, 146U, 255U },
        .top_cap_color     = { 96U, 238U, 255U, 255U },
        .edge_border_color = { 22U, 52U, 88U, 255U },
        .solid             = true,
        .one_way           = false,
        .hazard            = false,
        .emissive          = true,
    });
}

void Tilemap2D::set_tile_size(float units) noexcept
{
    tile_size_ = std::max(0.125F, units);
}

void Tilemap2D::register_palette_entry(TilePaletteEntry2D entry)
{
    if (entry.palette_id == 0U)
    {
        return;
    }
    for (auto& p : palette_)
    {
        if (p.palette_id == entry.palette_id)
        {
            p = std::move(entry);
            return;
        }
    }
    palette_.push_back(std::move(entry));
}

const TilePaletteEntry2D* Tilemap2D::find_palette(std::uint16_t palette_id) const noexcept
{
    for (const auto& p : palette_)
    {
        if (p.palette_id == palette_id)
        {
            return &p;
        }
    }
    return nullptr;
}

Tilemap2D::ChunkCoord Tilemap2D::to_chunk_coord(int gx, int gy, int& lx, int& ly) noexcept
{
    const int cx = floor_div_int(gx, kTileChunkSize);
    const int cy = floor_div_int(gy, kTileChunkSize);
    lx           = gx - cx * kTileChunkSize;
    ly           = gy - cy * kTileChunkSize;
    return { cx, cy };
}

TileCell2D Tilemap2D::get_tile(int gx, int gy) const noexcept
{
    int              lx = 0;
    int              ly = 0;
    const ChunkCoord cc = to_chunk_coord(gx, gy, lx, ly);
    const auto       it = chunks_.find(cc);
    if (it == chunks_.end())
    {
        return {};
    }
    const auto idx = static_cast<std::size_t>(ly * kTileChunkSize + lx);
    return it->second.cells.at(idx);
}

std::uint8_t Tilemap2D::compute_autotile_mask(int gx, int gy) const noexcept
{
    const std::uint16_t self_id = get_tile(gx, gy).palette_id;
    if (self_id == 0U)
    {
        return 0U;
    }
    std::uint8_t mask = 0U;
    if (get_tile(gx, gy + 1).palette_id == self_id) mask |= autotile_bits::kNorth;
    if (get_tile(gx + 1, gy).palette_id == self_id) mask |= autotile_bits::kEast;
    if (get_tile(gx, gy - 1).palette_id == self_id) mask |= autotile_bits::kSouth;
    if (get_tile(gx - 1, gy).palette_id == self_id) mask |= autotile_bits::kWest;
    return mask;
}

void Tilemap2D::refresh_autotile_at(int gx, int gy) noexcept
{
    int              lx = 0;
    int              ly = 0;
    const ChunkCoord cc = to_chunk_coord(gx, gy, lx, ly);
    auto             it = chunks_.find(cc);
    if (it == chunks_.end())
    {
        return;
    }
    const auto idx    = static_cast<std::size_t>(ly * kTileChunkSize + lx);
    auto&      cell   = it->second.cells.at(idx);
    cell.autotile_mask = (cell.palette_id != 0U) ? compute_autotile_mask(gx, gy) : 0U;
}

void Tilemap2D::set_tile(int gx, int gy, std::uint16_t palette_id, bool update_autotile)
{
    int              lx = 0;
    int              ly = 0;
    const ChunkCoord cc = to_chunk_coord(gx, gy, lx, ly);

    if (palette_id == 0U)
    {
        auto it = chunks_.find(cc);
        if (it == chunks_.end())
        {
            return;
        }
        const auto idx            = static_cast<std::size_t>(ly * kTileChunkSize + lx);
        it->second.cells.at(idx)  = TileCell2D {};
        bool any_remaining        = false;
        for (const auto& c : it->second.cells)
        {
            if (c.palette_id != 0U)
            {
                any_remaining = true;
                break;
            }
        }
        if (!any_remaining)
        {
            chunks_.erase(it);
        }
    }
    else
    {
        auto&      chunk     = chunks_[cc];
        const auto idx       = static_cast<std::size_t>(ly * kTileChunkSize + lx);
        auto&      cell      = chunk.cells.at(idx);
        cell.palette_id      = palette_id;
        cell.variation       = static_cast<std::uint8_t>(((gx * 73856093) ^ (gy * 19349663)) & 3);
    }

    if (update_autotile)
    {
        refresh_autotile_at(gx, gy);
        refresh_autotile_at(gx, gy + 1);
        refresh_autotile_at(gx + 1, gy);
        refresh_autotile_at(gx, gy - 1);
        refresh_autotile_at(gx - 1, gy);
    }
}

void Tilemap2D::paint_rect(int x0, int y0, int x1, int y1, std::uint16_t palette_id, bool update_autotile)
{
    const int min_x = std::min(x0, x1);
    const int max_x = std::max(x0, x1);
    const int min_y = std::min(y0, y1);
    const int max_y = std::max(y0, y1);

    for (int gy = min_y; gy <= max_y; ++gy)
    {
        for (int gx = min_x; gx <= max_x; ++gx)
        {
            set_tile(gx, gy, palette_id, false);
        }
    }

    if (update_autotile)
    {
        for (int gy = min_y - 1; gy <= max_y + 1; ++gy)
        {
            for (int gx = min_x - 1; gx <= max_x + 1; ++gx)
            {
                refresh_autotile_at(gx, gy);
            }
        }
    }
}

void Tilemap2D::rebuild_all_autotile_masks() noexcept
{
    for (auto& [cc, chunk] : chunks_)
    {
        for (int ly = 0; ly < kTileChunkSize; ++ly)
        {
            for (int lx = 0; lx < kTileChunkSize; ++lx)
            {
                const auto idx  = static_cast<std::size_t>(ly * kTileChunkSize + lx);
                auto&      cell = chunk.cells.at(idx);
                if (cell.palette_id != 0U)
                {
                    const int gx       = cc.cx * kTileChunkSize + lx;
                    const int gy       = cc.cy * kTileChunkSize + ly;
                    cell.autotile_mask = compute_autotile_mask(gx, gy);
                }
                else
                {
                    cell.autotile_mask = 0U;
                }
            }
        }
    }
}

std::size_t Tilemap2D::non_empty_tile_count() const noexcept
{
    std::size_t count = 0U;
    for (const auto& [cc, chunk] : chunks_)
    {
        (void)cc;
        for (const auto& cell : chunk.cells)
        {
            if (cell.palette_id != 0U)
            {
                ++count;
            }
        }
    }
    return count;
}

std::vector<Tilemap2D::PlacedTileRecord> Tilemap2D::export_tiles() const
{
    std::vector<PlacedTileRecord> out;
    out.reserve(non_empty_tile_count());
    for (const auto& [cc, chunk] : chunks_)
    {
        for (int ly = 0; ly < kTileChunkSize; ++ly)
        {
            for (int lx = 0; lx < kTileChunkSize; ++lx)
            {
                const auto& cell = chunk.cells.at(static_cast<std::size_t>(ly * kTileChunkSize + lx));
                if (cell.palette_id != 0U)
                {
                    out.push_back(PlacedTileRecord {
                        .gx            = cc.cx * kTileChunkSize + lx,
                        .gy            = cc.cy * kTileChunkSize + ly,
                        .palette_id    = cell.palette_id,
                        .autotile_mask = cell.autotile_mask,
                    });
                }
            }
        }
    }
    return out;
}

std::vector<CollisionRect2D> Tilemap2D::bake_greedy_collision_rects() const
{
    if (chunks_.empty())
    {
        return {};
    }

    // 1. Collect horizontal runs per row (gy) with identical collision traits.
    struct HorizRun
    {
        int           gy { 0 };
        int           x0 { 0 };
        int           x1 { 0 };  // inclusive
        std::uint16_t kind_key { 0U };
        bool          one_way { false };
        bool          hazard { false };
        std::uint16_t palette_id { 1U };
        bool          merged { false };
    };

    int min_gx = 0;
    int max_gx = -1;
    int min_gy = 0;
    int max_gy = -1;
    bool first = true;
    for (const auto& [cc, chunk] : chunks_)
    {
        (void)chunk;
        const int gx0 = cc.cx * kTileChunkSize;
        const int gx1 = gx0 + kTileChunkSize - 1;
        const int gy0 = cc.cy * kTileChunkSize;
        const int gy1 = gy0 + kTileChunkSize - 1;
        if (first)
        {
            min_gx = gx0;
            max_gx = gx1;
            min_gy = gy0;
            max_gy = gy1;
            first  = false;
        }
        else
        {
            min_gx = std::min(min_gx, gx0);
            max_gx = std::max(max_gx, gx1);
            min_gy = std::min(min_gy, gy0);
            max_gy = std::max(max_gy, gy1);
        }
    }

    if (first)
    {
        return {};
    }

    std::vector<HorizRun> runs;
    for (int gy = min_gy; gy <= max_gy; ++gy)
    {
        int  gx = min_gx;
        while (gx <= max_gx)
        {
            const TileCell2D          c   = get_tile(gx, gy);
            const TilePaletteEntry2D* pal = (c.palette_id != 0U) ? find_palette(c.palette_id) : nullptr;
            if (pal == nullptr || !pal->solid)
            {
                ++gx;
                continue;
            }
            const std::uint16_t key = static_cast<std::uint16_t>(
                1U + (pal->one_way ? 2U : 0U) + (pal->hazard ? 4U : 0U));
            const int start_x = gx;
            ++gx;
            while (gx <= max_gx)
            {
                const TileCell2D          nc   = get_tile(gx, gy);
                const TilePaletteEntry2D* npal = (nc.palette_id != 0U) ? find_palette(nc.palette_id) : nullptr;
                if (npal == nullptr || !npal->solid)
                {
                    break;
                }
                const std::uint16_t nkey = static_cast<std::uint16_t>(
                    1U + (npal->one_way ? 2U : 0U) + (npal->hazard ? 4U : 0U));
                if (nkey != key)
                {
                    break;
                }
                ++gx;
            }
            runs.push_back(HorizRun {
                .gy         = gy,
                .x0         = start_x,
                .x1         = gx - 1,
                .kind_key   = key,
                .one_way    = pal->one_way,
                .hazard     = pal->hazard,
                .palette_id = pal->palette_id,
                .merged     = false,
            });
        }
    }

    // 2. Vertically merge matching [x0, x1, kind_key] runs across consecutive gy rows.
    std::vector<CollisionRect2D> rects;
    rects.reserve(runs.size());

    for (std::size_t i = 0U; i < runs.size(); ++i)
    {
        if (runs.at(i).merged)
        {
            continue;
        }
        auto& base  = runs.at(i);
        base.merged = true;
        int top_gy  = base.gy;

        if (!base.one_way)
        {
            for (std::size_t j = i + 1U; j < runs.size(); ++j)
            {
                auto& cand = runs.at(j);
                if (cand.merged)
                {
                    continue;
                }
                if (cand.gy > top_gy + 1)
                {
                    break;
                }
                if (cand.gy == top_gy + 1 &&
                    cand.x0 == base.x0 &&
                    cand.x1 == base.x1 &&
                    cand.kind_key == base.kind_key)
                {
                    cand.merged = true;
                    top_gy      = cand.gy;
                }
            }
        }

        const float min_wx = static_cast<float>(base.x0) * tile_size_;
        const float max_wx = static_cast<float>(base.x1 + 1) * tile_size_;
        const float min_wy = static_cast<float>(base.gy) * tile_size_;
        const float max_wy = base.one_way
            ? (min_wy + tile_size_ * 0.35F + (static_cast<float>(base.gy == top_gy ? 1 : 1) * tile_size_ * 0.65F))
            : (static_cast<float>(top_gy + 1) * tile_size_);

        rects.push_back(CollisionRect2D {
            .center       = { 0.5F * (min_wx + max_wx), 0.5F * (min_wy + max_wy) },
            .half_extents = { 0.5F * (max_wx - min_wx), 0.5F * (max_wy - min_wy) },
            .one_way      = base.one_way,
            .hazard       = base.hazard,
            .palette_id   = base.palette_id,
        });
    }

    return rects;
}

void Tilemap2D::emit_to_canvas(
    Canvas2D&                    canvas,
    const Camera2D&              camera,
    const LightingEnvironment2D& lighting,
    std::uint8_t                 layer) const
{
    if (chunks_.empty())
    {
        return;
    }

    const Aabb2D cam_bounds = camera.visible_world_bounds(canvas.viewport(), tile_size_);
    const int    vis_cx0    = floor_div_int(static_cast<int>(std::floor(cam_bounds.min.x / tile_size_)), kTileChunkSize);
    const int    vis_cx1    = floor_div_int(static_cast<int>(std::ceil(cam_bounds.max.x / tile_size_)), kTileChunkSize);
    const int    vis_cy0    = floor_div_int(static_cast<int>(std::floor(cam_bounds.min.y / tile_size_)), kTileChunkSize);
    const int    vis_cy1    = floor_div_int(static_cast<int>(std::ceil(cam_bounds.max.y / tile_size_)), kTileChunkSize);

    for (const auto& [cc, chunk] : chunks_)
    {
        if (cc.cx < vis_cx0 || cc.cx > vis_cx1 || cc.cy < vis_cy0 || cc.cy > vis_cy1)
        {
            continue;
        }

        for (int ly = 0; ly < kTileChunkSize; ++ly)
        {
            for (int lx = 0; lx < kTileChunkSize; ++lx)
            {
                const TileCell2D& cell = chunk.cells.at(static_cast<std::size_t>(ly * kTileChunkSize + lx));
                if (cell.palette_id == 0U)
                {
                    continue;
                }
                const TilePaletteEntry2D* pal = find_palette(cell.palette_id);
                if (pal == nullptr)
                {
                    continue;
                }

                const int   gx     = cc.cx * kTileChunkSize + lx;
                const int   gy     = cc.cy * kTileChunkSize + ly;
                const float wx0    = static_cast<float>(gx) * tile_size_;
                const float wy0    = static_cast<float>(gy) * tile_size_;
                const float wx1    = wx0 + tile_size_;
                const float wy1    = wy0 + tile_size_;
                const Aabb2D tb    { .min = { wx0, wy0 }, .max = { wx1, wy1 } };
                if (!tb.overlaps(cam_bounds))
                {
                    continue;
                }

                const cd::math::Vec2f tl = camera.world_to_screen({ wx0, wy1 }, canvas.viewport());
                const cd::math::Vec2f br = camera.world_to_screen({ wx1, wy0 }, canvas.viewport());
                const float           sx = std::floor(tl.x);
                const float           sy = std::floor(tl.y);
                const float           sw = std::max(1.0F, std::ceil(br.x) - sx);
                const float           sh = std::max(1.0F, std::ceil(br.y) - sy);

                const cd::math::Vec2f center { 0.5F * (wx0 + wx1), 0.5F * (wy0 + wy1) };
                const std::uint64_t   key = make_sort_key_2d(layer, 0, wy0, false, cell.palette_id);

                // Emissive tiles stay bright; regular tiles respond to 2D lights & soft shadows
                const ColorRgba8 base_fill = pal->emissive
                    ? pal->fill_color
                    : evaluate_lighting_2d(lighting, center, { 0.0F, 0.25F, 0.96F }, pal->fill_color);
                const ColorRgba8 top_cap = pal->emissive
                    ? pal->top_cap_color
                    : evaluate_lighting_2d(lighting, { center.x, wy1 }, { 0.0F, 0.65F, 0.76F }, pal->top_cap_color);
                const ColorRgba8 border = evaluate_lighting_2d(
                    lighting, center, { 0.0F, 0.0F, 1.0F }, pal->edge_border_color);

                if (pal->hazard)
                {
                    // Sawtooth spike silhouette
                    canvas.add_solid_rect(sx, sy + sh * 0.68F, sw, sh * 0.32F, border, key);
                    CanvasCmd2D spike1 {};
                    spike1.kind     = CanvasCmdKind2D::kTriangleGrad;
                    spike1.sort_key = key + 1ULL;
                    spike1.x0       = sx + sw * 0.25F;
                    spike1.y0       = sy + sh * 0.08F;
                    spike1.x1       = sx;
                    spike1.y1       = sy + sh * 0.72F;
                    spike1.x2       = sx + sw * 0.50F;
                    spike1.y2       = sy + sh * 0.72F;
                    spike1.c0       = top_cap;
                    spike1.c1       = base_fill;
                    spike1.c2       = base_fill;
                    CanvasCmd2D spike2 = spike1;
                    spike2.x0          = sx + sw * 0.75F;
                    spike2.x1          = sx + sw * 0.50F;
                    spike2.x2          = sx + sw;
                    canvas.add_solid_rect(sx + sw * 0.14F, sy + sh * 0.24F, sw * 0.22F, sh * 0.50F, top_cap, key + 1ULL);
                    canvas.add_solid_rect(sx + sw * 0.64F, sy + sh * 0.24F, sw * 0.22F, sh * 0.50F, top_cap, key + 1ULL);
                    continue;
                }

                const float draw_h = pal->one_way ? std::max(4.0F, sh * 0.42F) : sh;
                canvas.add_solid_rect(sx, sy, sw, draw_h, base_fill, key);

                // Wang 4-bit auto-tile exposed borders:
                const float rim = std::clamp(sh * 0.22F, 2.0F, 8.0F);
                const float bdr = std::clamp(sh * 0.08F, 1.0F, 3.0F);
                if ((cell.autotile_mask & autotile_bits::kNorth) == 0U)
                {
                    canvas.add_solid_rect(sx, sy, sw, rim, top_cap, key + 2ULL);
                }
                if ((cell.autotile_mask & autotile_bits::kSouth) == 0U && !pal->one_way)
                {
                    canvas.add_solid_rect(sx, sy + draw_h - bdr, sw, bdr, border, key + 1ULL);
                }
                if ((cell.autotile_mask & autotile_bits::kWest) == 0U)
                {
                    canvas.add_solid_rect(sx, sy, bdr, draw_h, border, key + 1ULL);
                }
                if ((cell.autotile_mask & autotile_bits::kEast) == 0U)
                {
                    canvas.add_solid_rect(sx + sw - bdr, sy, bdr, draw_h, border, key + 1ULL);
                }
            }
        }
    }
}

}  // namespace cd::render2d
