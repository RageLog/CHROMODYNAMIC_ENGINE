// =============================================================================
// CHROMODYNAMIC — cd/render2d/Tilemap2D.hpp
// FAZ 34 (ADR-20261111) — Sparse Chunked (16x16) 2D Tilemap with 4-Bit Cardinal
// (16-Variant Wang) Auto-Tiling & Greedy Strip/Rect Physics Collider Meshing.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>
#include <cd/render2d/Camera2D.hpp>
#include <cd/render2d/Lighting2D.hpp>
#include <cd/render2d/SpriteBatch2D.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cd::render2d
{

inline constexpr int kTileChunkSize = 16;

namespace autotile_bits
{
inline constexpr std::uint8_t kNorth = 1U << 0U;  // +Y neighbor
inline constexpr std::uint8_t kEast  = 1U << 1U;  // +X neighbor
inline constexpr std::uint8_t kSouth = 1U << 2U;  // -Y neighbor
inline constexpr std::uint8_t kWest  = 1U << 3U;  // -X neighbor
}  // namespace autotile_bits

struct TilePaletteEntry2D
{
    std::uint16_t            palette_id { 1U };
    std::string              name { "Solid Stone" };
    ColorRgba8               fill_color { 56U, 68U, 92U, 255U };
    ColorRgba8               top_cap_color { 76U, 214U, 138U, 255U };
    ColorRgba8               edge_border_color { 34U, 42U, 60U, 255U };
    bool                     solid { true };
    bool                     one_way { false };
    bool                     hazard { false };
    bool                     emissive { false };
    std::uint32_t            texture_slot { 0xFFFFFFFFU };
    UvRect2D                 uv_rect {};
    std::array<UvRect2D, 16> autotile_uvs {};
    bool                     has_autotile_uvs { false };
    std::uint32_t            normal_texture_slot { 0xFFFFFFFFU };
    std::uint32_t            emissive_texture_slot { 0xFFFFFFFFU };
    float                    emissive_intensity { 0.0F };
};

struct TileCell2D
{
    std::uint16_t palette_id { 0U };  ///< 0 = empty air
    std::uint8_t  autotile_mask { 0U }; ///< 0..15 cardinal neighbor bitmask
    std::uint8_t  variation { 0U };
};

struct CollisionRect2D
{
    cd::math::Vec2f center { 0.0F, 0.0F };
    cd::math::Vec2f half_extents { 0.5F, 0.5F };
    bool            one_way { false };
    bool            hazard { false };
    std::uint16_t   palette_id { 1U };
};

class Tilemap2D
{
public:
    Tilemap2D();

    [[nodiscard]] constexpr float tile_size() const noexcept { return tile_size_; }
    void set_tile_size(float units) noexcept;

    void register_palette_entry(TilePaletteEntry2D entry);
    [[nodiscard]] const TilePaletteEntry2D* find_palette(std::uint16_t palette_id) const noexcept;
    [[nodiscard]] std::span<const TilePaletteEntry2D> palette() const noexcept { return palette_; }

    /// Set a single cell and optionally recompute 4-bit Wang auto-tile masks on `(gx, gy)` and its 4 neighbors.
    void set_tile(int gx, int gy, std::uint16_t palette_id, bool update_autotile = true);
    void erase_tile(int gx, int gy, bool update_autotile = true) { set_tile(gx, gy, 0U, update_autotile); }

    /// Paint an inclusive grid rectangle `[x0..x1] x [y0..y1]` and refresh auto-tile borders.
    void paint_rect(int x0, int y0, int x1, int y1, std::uint16_t palette_id, bool update_autotile = true);

    /// 4-way breadth-first search flood fill replacing contiguous matching tiles.
    /// Returns total number of cells modified.
    std::size_t flood_fill(
        int           start_x,
        int           start_y,
        std::uint16_t replacement_id,
        std::size_t   max_cells = 4096);

    /// Paints a 2D rectangular pattern array into the tilemap at (start_x, start_y).
    /// Returns total number of cells stamped.
    std::size_t stamp(
        int                            start_x,
        int                            start_y,
        int                            pattern_width,
        int                            pattern_height,
        std::span<const std::uint16_t> pattern,
        bool                           update_autotile = true);

    std::size_t stamp(
        int                            start_x,
        int                            start_y,
        std::span<const std::uint16_t> pattern,
        int                            pattern_width,
        int                            pattern_height,
        bool                           update_autotile = true)
    {
        return stamp(start_x, start_y, pattern_width, pattern_height, pattern, update_autotile);
    }

    void clear() noexcept { chunks_.clear(); }

    [[nodiscard]] TileCell2D get_tile(int gx, int gy) const noexcept;
    [[nodiscard]] std::size_t non_empty_tile_count() const noexcept;
    [[nodiscard]] std::size_t active_chunk_count() const noexcept { return chunks_.size(); }

    /// Computes 4-bit cardinal auto-tile mask (0..15) for `(gx, gy)`.
    [[nodiscard]] std::uint8_t compute_autotile_mask(int gx, int gy) const noexcept;

    void rebuild_all_autotile_masks() noexcept;

    /// Greedy 2D meshing: merges contiguous solid/one-way/hazard tiles across chunk boundaries
    /// into the minimal set of axis-aligned `CollisionRect2D` boxes for `PhysicsWorld2D`.
    [[nodiscard]] std::vector<CollisionRect2D> bake_greedy_collision_rects() const;

    /// Export non-empty `(gx, gy, palette_id)` tuples in deterministic sorted order for serialization.
    struct PlacedTileRecord
    {
        int           gx { 0 };
        int           gy { 0 };
        std::uint16_t palette_id { 0U };
        std::uint8_t  autotile_mask { 0U };
    };
    [[nodiscard]] std::vector<PlacedTileRecord> export_tiles() const;

    /// Frustum-culled emission of visible chunks & Wang-styled tiles into `Canvas2D`.
    void emit_to_canvas(
        Canvas2D&                    canvas,
        const Camera2D&              camera,
        const LightingEnvironment2D& lighting,
        std::uint8_t                 layer = 60U) const;

private:
    struct ChunkCoord
    {
        int cx { 0 };
        int cy { 0 };
        friend constexpr auto operator<=>(const ChunkCoord&, const ChunkCoord&) noexcept = default;
    };

    struct Chunk
    {
        std::array<TileCell2D, static_cast<std::size_t>(kTileChunkSize * kTileChunkSize)> cells {};
    };

    [[nodiscard]] static ChunkCoord to_chunk_coord(int gx, int gy, int& lx, int& ly) noexcept;
    void refresh_autotile_at(int gx, int gy) noexcept;

    float                           tile_size_ { 1.0F };
    std::vector<TilePaletteEntry2D> palette_ {};
    std::map<ChunkCoord, Chunk>     chunks_ {};
};

}  // namespace cd::render2d
