// =============================================================================
// CHROMODYNAMIC — cd/world2d/PlaytestRunner2D.hpp
// FAZ 35 (ADR-20261111) — Deterministic Headless & Live 2D Playtest Oracle for
// Automated QA and AI Co-Pilot Verification (`--playtest-2d` / `--telemetry-out`).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace cd::world2d
{

struct PlaytestReport2D
{
    std::string                  stage_name {};
    std::uint32_t                frames_simulated { 0U };
    float                        simulated_seconds { 0.0F };
    cd::math::Vec2f              initial_player_pos { 0.0F, 0.0F };
    cd::math::Vec2f              final_player_pos { 0.0F, 0.0F };
    float                        max_player_y { 0.0F };
    float                        horizontal_distance_covered { 0.0F };
    int                          player_hp { 100 };
    int                          score { 0 };
    int                          gems_collected { 0 };
    int                          total_gems { 0 };
    int                          enemies_defeated { 0 };
    bool                         victory { false };
    bool                         game_over { false };
    std::uint32_t                active_lua_vms { 0U };
    std::uint32_t                lua_error_count { 0U };
    std::vector<cd::math::Vec2f> trajectory_samples {};

    [[nodiscard]] std::string to_json_string() const;
};

/// Computes one frame of intelligent 2D platformer autopilot input (navigates toward
/// uncollected gems / goal portal, leaps across pits, hazards & ledges).
[[nodiscard]] PlayerInput2D compute_autopilot_input_2d(const Stage2D& stage) noexcept;

/// Steps `stage` deterministically at 60 Hz for `frame_count` frames and returns
/// a complete machine-readable `PlaytestReport2D`. When `restore_stage_after` is
/// `true`, calls `stage.exit_play()` after capturing telemetry; when `false`, leaves
/// the stage at the final simulated frame (ideal for `--capture-png` inspection!).
[[nodiscard]] PlaytestReport2D run_playtest_2d(
    Stage2D&      stage,
    std::uint32_t frame_count,
    bool          use_autopilot       = true,
    bool          restore_stage_after = true);

}  // namespace cd::world2d
