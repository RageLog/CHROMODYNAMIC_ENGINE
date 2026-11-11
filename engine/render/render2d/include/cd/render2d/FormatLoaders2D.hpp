// =============================================================================
// CHROMODYNAMIC — cd/render2d/FormatLoaders2D.hpp
// FAZ 34 (ADR-20261111) — Zero-Dependency Direct `.ase` / `.aseprite` Header &
// Frame-Tag Reader + `.ldtk` Level/Tile/Entity Importer.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/math/Vector.hpp>
#include <cd/render2d/SpriteBatch2D.hpp>
#include <cd/render2d/Tilemap2D.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::render2d
{

struct AsepriteTag2D
{
    std::string   name {};
    std::uint16_t from_frame { 0U };
    std::uint16_t to_frame { 0U };
    std::uint8_t  direction { 0U };  ///< 0=Forward, 1=Reverse, 2=Ping-pong
};

struct AsepriteDocumentInfo2D
{
    std::uint16_t              width { 0U };
    std::uint16_t              height { 0U };
    std::uint16_t              frame_count { 0U };
    std::uint16_t              color_depth_bpp { 32U };
    std::vector<std::uint16_t> frame_durations_ms {};
    std::vector<AsepriteTag2D> tags {};

    /// Builds horizontal sprite-strip `FlipbookClip2D` definitions directly from Aseprite Frame Tags.
    [[nodiscard]] std::vector<FlipbookClip2D> build_horizontal_strip_clips() const;
};

/// Parses an `.ase` / `.aseprite` binary file (magic `0xA5E0` header, `0xF1FA` frames,
/// and `0x2018` Frame Tags chunks) with zero third-party dependencies.
[[nodiscard]] cd::core::Result<AsepriteDocumentInfo2D>
parse_aseprite_binary(std::span<const std::uint8_t> bytes);

/// Builds a valid `.aseprite` binary buffer in memory (for tests & starter asset generation).
[[nodiscard]] std::vector<std::uint8_t> build_aseprite_header_and_tags_blob(
    std::uint16_t                  width,
    std::uint16_t                  height,
    std::span<const std::uint16_t> frame_durations_ms,
    std::span<const AsepriteTag2D> tags);

struct LdtkEntity2D
{
    std::string     identifier {};
    cd::math::Vec2f world_pos { 0.0F, 0.0F };
    cd::math::Vec2f size_units { 1.0F, 1.0F };
};

struct LdtkImportedLevel2D
{
    std::string               level_identifier {};
    int                       grid_width { 0 };
    int                       grid_height { 0 };
    int                       tile_px { 16 };
    std::size_t               imported_tiles { 0U };
    std::vector<LdtkEntity2D> entities {};
};

/// Imports an LDtk (`.ldtk`) JSON document (IntGrid `intGridCsv`, `gridTiles`, and `entityInstances`)
/// directly into `Tilemap2D` and returns imported entities in Y-up engine world coordinates.
[[nodiscard]] cd::core::Result<LdtkImportedLevel2D>
import_ldtk_level_json(std::string_view json_text, Tilemap2D& out_tilemap);

}  // namespace cd::render2d
