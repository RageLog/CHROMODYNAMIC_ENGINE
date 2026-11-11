// =============================================================================
// CHROMODYNAMIC — cd/world2d/PlaytestRunner2D.cpp
// FAZ 35 (ADR-20261111) — Deterministic 60 Hz 2D Playtest & Telemetry Oracle.
// =============================================================================
#include <cd/world2d/PlaytestRunner2D.hpp>

#include <cd/schema/Schema.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace cd::world2d
{

std::string PlaytestReport2D::to_json_string() const
{
    std::ostringstream os;
    os << "{\n"
       << "  \"stage_name\": \"" << cd::schema::escape_json_string(stage_name) << "\",\n"
       << "  \"frames_simulated\": " << frames_simulated << ",\n"
       << "  \"simulated_seconds\": " << simulated_seconds << ",\n"
       << "  \"initial_player_pos\": [" << initial_player_pos.x << ", " << initial_player_pos.y << "],\n"
       << "  \"final_player_pos\": [" << final_player_pos.x << ", " << final_player_pos.y << "],\n"
       << "  \"max_player_y\": " << max_player_y << ",\n"
       << "  \"horizontal_distance_covered\": " << horizontal_distance_covered << ",\n"
       << "  \"player_hp\": " << player_hp << ",\n"
       << "  \"score\": " << score << ",\n"
       << "  \"gems_collected\": " << gems_collected << ",\n"
       << "  \"total_gems\": " << total_gems << ",\n"
       << "  \"enemies_defeated\": " << enemies_defeated << ",\n"
       << "  \"victory\": " << (victory ? "true" : "false") << ",\n"
       << "  \"game_over\": " << (game_over ? "true" : "false") << ",\n"
       << "  \"active_lua_vms\": " << active_lua_vms << ",\n"
       << "  \"lua_error_count\": " << lua_error_count << ",\n"
       << "  \"trajectory_samples\": [";

    for (std::size_t i = 0U; i < trajectory_samples.size(); ++i)
    {
        if (i > 0U) os << ", ";
        os << "[" << trajectory_samples.at(i).x << ", " << trajectory_samples.at(i).y << "]";
    }
    os << "]\n}\n";
    return os.str();
}

PlayerInput2D compute_autopilot_input_2d(const Stage2D& stage) noexcept
{
    PlayerInput2D in {};
    const Entity2D* hero = nullptr;
    const Entity2D* goal = nullptr;

    for (const auto& e : stage.entities())
    {
        if (!e.visible || e.collected_or_defeated) continue;
        if (e.role == EntityRole2D::kPlayerHero && hero == nullptr) hero = &e;
        if (e.role == EntityRole2D::kGoalPortal && goal == nullptr) goal = &e;
    }
    if (hero == nullptr)
    {
        return in;
    }

    float target_x = (goal != nullptr) ? goal->position.x : (hero->position.x + 10.0F);
    float target_y = (goal != nullptr) ? goal->position.y : hero->position.y;

    const float dx = target_x - hero->position.x;
    if (std::fabs(dx) > 0.25F)
    {
        in.move_x = (dx >= 0.0F) ? 1.0F : -1.0F;
    }

    // Look ahead 1 tile for hazards, gaps, walls, or elevated goals
    const int ahead_gx = static_cast<int>(std::floor(hero->position.x + in.move_x * 1.1F));
    const int foot_gy  = static_cast<int>(std::floor(hero->position.y - hero->size.y * 0.5F - 0.15F));
    const int wall_gy  = foot_gy + 1;

    const auto ahead_floor = stage.tilemap().get_tile(ahead_gx, foot_gy);
    const auto ahead_wall  = stage.tilemap().get_tile(ahead_gx, wall_gy);

    bool enemy_ahead = false;
    for (const auto& e : stage.entities())
    {
        if (e.visible && !e.collected_or_defeated && e.role == EntityRole2D::kPatrolEnemy)
        {
            if (std::fabs(e.position.x - hero->position.x) < 2.2F)
            {
                enemy_ahead = true;
                break;
            }
        }
    }

    const bool gap_or_hazard_ahead = (ahead_floor.palette_id == 0U || ahead_floor.palette_id == 3U);
    const bool wall_ahead          = (ahead_wall.palette_id != 0U);
    const bool climb_needed        = (target_y > hero->position.y + 1.2F && std::fabs(dx) < 6.0F);

    if (gap_or_hazard_ahead || wall_ahead || enemy_ahead || climb_needed)
    {
        in.jump_pressed = true;
        in.jump_held    = true;
    }
    else if (hero->runtime_velocity.y > 0.5F)
    {
        in.jump_held = true;
    }

    return in;
}

PlaytestReport2D run_playtest_2d(
    Stage2D&      stage,
    std::uint32_t frame_count,
    bool          use_autopilot,
    bool          restore_stage_after)
{
    PlaytestReport2D report {};
    report.stage_name = std::string { stage.name() };

    stage.enter_play();

    const auto find_hero_pos = [&stage]() noexcept -> cd::math::Vec2f {
        for (const auto& e : stage.entities())
        {
            if (e.visible && e.role == EntityRole2D::kPlayerHero)
            {
                return e.position;
            }
        }
        return { 0.0F, 0.0F };
    };

    report.initial_player_pos = find_hero_pos();
    report.max_player_y       = report.initial_player_pos.y;
    report.trajectory_samples.push_back(report.initial_player_pos);

    constexpr float kFixedDt = 1.0F / 60.0F;
    for (std::uint32_t f = 0U; f < frame_count; ++f)
    {
        const PlayerInput2D input = use_autopilot ? compute_autopilot_input_2d(stage) : PlayerInput2D {};
        stage.step_play(kFixedDt, input);
        ++report.frames_simulated;

        const cd::math::Vec2f cur_pos = find_hero_pos();
        report.max_player_y           = std::max(report.max_player_y, cur_pos.y);
        if ((f + 1U) % 15U == 0U || f + 1U == frame_count)
        {
            report.trajectory_samples.push_back(cur_pos);
        }

        if (stage.hud().victory || stage.hud().game_over)
        {
            break;
        }
    }

    report.final_player_pos            = find_hero_pos();
    report.simulated_seconds           = static_cast<float>(report.frames_simulated) * kFixedDt;
    report.horizontal_distance_covered = std::fabs(report.final_player_pos.x - report.initial_player_pos.x);
    report.player_hp                   = stage.hud().player_hp;
    report.score                       = stage.hud().score;
    report.gems_collected              = stage.hud().gems_collected;
    report.total_gems                  = stage.hud().total_gems;
    report.enemies_defeated            = stage.hud().enemies_defeated;
    report.victory                     = stage.hud().victory;
    report.game_over                   = stage.hud().game_over;
    report.active_lua_vms              = stage.hud().active_lua_vms;
    report.lua_error_count             = stage.hud().lua_error_count;

    if (restore_stage_after)
    {
        stage.exit_play();
    }

    return report;
}

}  // namespace cd::world2d
