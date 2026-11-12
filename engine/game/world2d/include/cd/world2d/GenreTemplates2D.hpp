// =============================================================================
// CHROMODYNAMIC: cd/world2d/GenreTemplates2D.hpp
// Milestone 4: Commercial Switchable 2D Genre Templates:
// Action Platformer, Top-Down Y-Sorted RPG Dungeon, and Twin-Stick Arena.
// =============================================================================
#pragma once

#include <cd/world2d/Stage2D.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace cd::world2d
{

enum class GenreTemplateKind2D : std::uint8_t
{
    kActionPlatformer  = 0,
    kTopDownRpgDungeon = 1,
    kTwinStickArena    = 2,
    kEmpty2D           = 3,
    kPongArcade        = 4,
    kBreakTheBridges   = 5,
};

[[nodiscard]] constexpr std::string_view genre_template_name_2d(GenreTemplateKind2D kind) noexcept
{
    switch (kind)
    {
        case GenreTemplateKind2D::kActionPlatformer:  return "ActionPlatformer";
        case GenreTemplateKind2D::kTopDownRpgDungeon: return "TopDownRpgDungeon";
        case GenreTemplateKind2D::kTwinStickArena:    return "TwinStickArena";
        case GenreTemplateKind2D::kEmpty2D:           return "Empty2D";
        case GenreTemplateKind2D::kPongArcade:        return "PongArcade";
        case GenreTemplateKind2D::kBreakTheBridges:   return "BreakTheBridges";
    }
    return "ActionPlatformer";
}

[[nodiscard]] constexpr std::optional<GenreTemplateKind2D> parse_genre_template_kind_2d(std::string_view str) noexcept
{
    if (str == "platformer" || str == "action_platformer" || str == "ActionPlatformer" || str == "showcase")
    {
        return GenreTemplateKind2D::kActionPlatformer;
    }
    if (str == "rpg" || str == "top_down_rpg" || str == "TopDownRpgDungeon" || str == "dungeon")
    {
        return GenreTemplateKind2D::kTopDownRpgDungeon;
    }
    if (str == "arena" || str == "twin_stick_arena" || str == "TwinStickArena" || str == "bullet_heaven")
    {
        return GenreTemplateKind2D::kTwinStickArena;
    }
    if (str == "empty" || str == "empty_2d" || str == "Empty2D" || str == "blank")
    {
        return GenreTemplateKind2D::kEmpty2D;
    }
    if (str == "pong" || str == "pong_arcade" || str == "PongArcade" || str == "Pong")
    {
        return GenreTemplateKind2D::kPongArcade;
    }
    if (str == "break_the_bridges" || str == "bridges" || str == "BreakTheBridges" || str == "breakout_bridges")
    {
        return GenreTemplateKind2D::kBreakTheBridges;
    }
    return std::nullopt;
}

class GenreTemplates2D
{
public:
    /// Applies the specified genre template to stage, configuring physics,
    /// character controller, tilemaps, dynamic lights, occluders, parallax layers,
    /// and gameplay entities.
    static void apply_genre_template(Stage2D& stage, GenreTemplateKind2D kind);

    /// 0. Clean Empty 2D Project Stage: Zero demo clutter, centered camera,
    /// clean tilemap and lighting ready for authoring from scratch.
    static void build_empty_stage(Stage2D& stage);

    /// 1. Action Platformer: Side-scrolling, platforms, character controller with
    /// coyote-time jump and wall-jump, jump pads, collectible gems, patrol enemies,
    /// and multi-tier parallax skyline.
    static void build_action_platformer(Stage2D& stage);

    /// 2. Top-Down RPG Dungeon: Top-down dungeon, gravity 0, 4/8-way movement,
    /// dungeon walls, torches with 2D point lights and soft occluders, chests,
    /// keys, and dynamic Y-sorting by entity foot Y.
    static void build_top_down_rpg(Stage2D& stage);

    /// 3. Twin-Stick Arena: Top-down arena, gravity 0, 360-degree aiming, pooled
    /// projectiles via ObjectPool2D, enemy swarm spawner, SpatialHash2D broadphase
    /// collisions, XP gem drops, and survival timer HUD.
    static void build_twin_stick_arena(Stage2D& stage);

    /// 4. Neon Cyber Pong: Classic 1P vs CPU arcade court with spin/angle paddle
    /// deflection, accelerating plasma ball rallies, spark bursts, and point scoring.
    static void build_pong_arcade(Stage2D& stage);

    /// 5. Break the Bridges: Tactical arcade bridge-demolition & ricochet game where
    /// multi-span bridges cross a plasma canyon while enemy raiders march across;
    /// shattering bridge planks & support piers collapses enemies into the chasm!
    static void build_break_the_bridges(Stage2D& stage);
};

}  // namespace cd::world2d
