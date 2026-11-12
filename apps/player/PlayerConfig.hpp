// =============================================================================
// CHROMODYNAMIC - apps/player/PlayerConfig.hpp
//
// Command-line configuration and portable zero-argument bundle auto-discovery
// for the standalone cd_player runtime executable (ADR-20261112).
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace cd::player
{

inline constexpr std::array<std::string_view, 12> kAllShowcaseSlugs {
    "01_pbr_materials_sss_hair",
    "02_celestial_aether_fog_clouds",
    "03_sponza_clustered_ddgi_restir",
    "04_optics_cinematic_studio",
    "05_avatar_locomotion_anim_ik",
    "06_jolt_dynamics_rover_shatter",
    "07_terra_ocean_nature_world",
    "08_synth_pilot_squad_narrative",
    "09_acoustics_hrtf_echo_chamber",
    "10_spark_swarm_and_decals",
    "11_custom_ui_hud_and_menus",
    "12_planar_2d_platformer_net",
};

struct PlayerConfig
{
    std::string   showcase_slug { "01_pbr_materials_sss_hair" };
    std::string   pak_path {};
    std::string   stage2d_path {};
    std::string   capture_png_path {};
    std::uint32_t frames_to_run { 0U };
    std::string   stage_mode { "Pure3D" };
    std::string   genre { "showcase" };
    bool          run_all_showcases { false };
    bool          force_headless { false };
    bool          force_windowed { false };
    bool          autopilot_2d { false };
    bool          show_help { false };
    bool          has_error { false };
    int           exit_code { 0 };

    [[nodiscard]] bool is_2d_genre() const noexcept
    {
        return genre == "empty" || genre == "empty_2d" ||
               genre == "platformer" || genre == "action_platformer" ||
               genre == "rpg" || genre == "top_down_rpg" ||
               genre == "arena" || genre == "twin_stick_arena" ||
               genre == "pong" || genre == "pong_arcade" ||
               genre == "break_the_bridges" || genre == "bridges";
    }

    [[nodiscard]] bool uses_stage2d() const noexcept
    {
        return !pak_path.empty() || !stage2d_path.empty() || is_2d_genre() || stage_mode == "Pure2D";
    }
};

[[nodiscard]] PlayerConfig parse_player_cli(int argc, const char* const* argv);

}  // namespace cd::player
