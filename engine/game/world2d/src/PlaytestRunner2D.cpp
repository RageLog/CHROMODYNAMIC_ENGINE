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
    const Entity2D* hero   = nullptr;
    const Entity2D* goal   = nullptr;
    const Entity2D* paddle = nullptr;
    const Entity2D* ball   = nullptr;

    for (const auto& e : stage.entities())
    {
        if (!e.visible || e.collected_or_defeated) continue;
        if (e.role == EntityRole2D::kPlayerHero && hero == nullptr) hero = &e;
        if (e.role == EntityRole2D::kGoalPortal && goal == nullptr) goal = &e;
        if (e.role == EntityRole2D::kKinematicPaddle && paddle == nullptr) paddle = &e;
        if (e.role == EntityRole2D::kBouncingBall && ball == nullptr) ball = &e;
    }

    // Arcade Paddle Autopilot (Pong & Break the Bridges)
    if (hero == nullptr && paddle != nullptr)
    {
        const float t = stage.hud().sim_time_sec;
        if (paddle->size.y >= paddle->size.x)
        {
            // Vertical Pong Paddle: track incoming ball with spin angle offset
            if (ball != nullptr)
            {
                const float spin_bias = std::sin(t * 2.4F) * 0.65F;
                const float dy        = (ball->position.y + spin_bias) - paddle->position.y;
                in.move_y             = std::clamp(dy * 1.8F, -1.0F, 1.0F);
            }
        }
        else
        {
            // Horizontal Break the Bridges Striker: intercept falling sphere & fire artillery at intact bridges
            float target_x = paddle->position.x;
            if (ball != nullptr && ball->runtime_velocity.y <= 2.5F)
            {
                const float spin_bias = std::sin(t * 3.1F) * 0.55F;
                target_x              = ball->position.x + spin_bias;
            }
            else
            {
                float best_d = 1.0e9F;
                for (const auto& blk : stage.entities())
                {
                    if (blk.visible && !blk.collected_or_defeated && blk.role == EntityRole2D::kDestructibleBlock)
                    {
                        const float d = std::fabs(blk.position.x - paddle->position.x) -
                                        ((blk.size.y > blk.size.x * 1.1F) ? 3.0F : 0.0F); // prioritize keystone piers!
                        if (d < best_d)
                        {
                            best_d   = d;
                            target_x = blk.position.x;
                        }
                    }
                }
            }
            in.move_x       = std::clamp((target_x - paddle->position.x) * 1.6F, -1.0F, 1.0F);
            in.fire_held    = true;
            in.fire_pressed = true;
        }
        return in;
    }

    if (hero == nullptr)
    {
        return in;
    }

    // Top-Down RPG & Twin-Stick Arena (zero-gravity 2D navigation + omni-aim combat)
    if (std::fabs(stage.gravity_y()) < 0.01F)
    {
        const Entity2D* nearest_enemy = nullptr;
        float           best_enemy_d2 = 1.0e9F;
        const Entity2D* nearest_gem   = nullptr;
        float           best_gem_d2   = 1.0e9F;

        for (const auto& e : stage.entities())
        {
            if (!e.visible || e.collected_or_defeated)
            {
                continue;
            }
            const float dx = e.position.x - hero->position.x;
            const float dy = e.position.y - hero->position.y;
            const float d2 = dx * dx + dy * dy;
            if (e.role == EntityRole2D::kPatrolEnemy && d2 < best_enemy_d2)
            {
                best_enemy_d2 = d2;
                nearest_enemy = &e;
            }
            else if (e.role == EntityRole2D::kCollectibleGem && d2 < best_gem_d2)
            {
                best_gem_d2 = d2;
                nearest_gem = &e;
            }
        }

        if (nearest_enemy != nullptr)
        {
            const float ex   = nearest_enemy->position.x - hero->position.x;
            const float ey   = nearest_enemy->position.y - hero->position.y;
            in.aim_angle_rad = std::atan2(ey, ex);
            in.fire_held     = true;
            in.fire_pressed  = true;
        }

        cd::math::Vec2f desired_dir { 0.0F, 0.0F };
        if (nearest_enemy != nullptr && best_enemy_d2 < (3.4F * 3.4F))
        {
            // Kite / strafe orbit around close enemy
            const float d  = std::max(0.1F, std::sqrt(best_enemy_d2));
            const float nx = (hero->position.x - nearest_enemy->position.x) / d;
            const float ny = (hero->position.y - nearest_enemy->position.y) / d;
            desired_dir.x  = nx * 0.65F - ny * 0.75F;
            desired_dir.y  = ny * 0.65F + nx * 0.75F;
        }
        else if (nearest_gem != nullptr)
        {
            desired_dir = nearest_gem->position - hero->position;
        }
        else if (goal != nullptr)
        {
            desired_dir = goal->position - hero->position;
        }
        else
        {
            const float t = stage.hud().sim_time_sec;
            desired_dir   = { std::cos(t * 1.1F), std::sin(t * 1.1F) };
        }

        const float len = std::sqrt(desired_dir.x * desired_dir.x + desired_dir.y * desired_dir.y);
        if (len > 0.05F)
        {
            in.move_x = desired_dir.x / len;
            in.move_y = desired_dir.y / len;
            if (nearest_enemy == nullptr)
            {
                in.aim_angle_rad = std::atan2(in.move_y, in.move_x);
            }
        }
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
        for (const auto& e : stage.entities())
        {
            if (e.visible && e.role == EntityRole2D::kKinematicPaddle)
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
