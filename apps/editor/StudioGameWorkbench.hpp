// =============================================================================
// CHROMODYNAMIC — apps/editor/StudioGameWorkbench.hpp
//
// Domestic Commercial-Grade 3D Game Engine & Level Authoring Suite.
// Provides a crystal-clear, high-legibility 5-Zone Studio Workspace with:
//   1. Top Studio Bar: Play/Pause/Stop (PIE), Level Switcher, Gizmo Tools,
//      Save/Load .cdstage, Reset, Help.
//   2. Left Studio Dock: Scene Hierarchy + One-Click 3D Game Object Spawner.
//   3. Center 3D Viewport: True Perspective 3D Shaded Mesh Renderer
//      (6-face oriented 3D boxes, 8-face 3D crystals, articulated 3D Hero,
//      AI patrol bots, 3D rovers), interactive 3D Gizmo dragging, and
//      full Play-In-Editor (WASD + Space Jump + 3D AABB collisions).
//   4. Right Studio Dock: Interactive Property Inspector with draggable
//      sliders, step buttons, role switcher, and 3D PBR Material Sphere.
//   5. Bottom Studio Dock: Playable Game Templates, Quick-Spawn Prefabs,
//      12 Engine Showcases, Live Studio Console, and 19-Pass GPU Profiler.
// =============================================================================
#pragma once

#include <cd/editor/FullFledgedEditorSuite.hpp>
#include <cd/element_core/Element.hpp>
#include <cd/gameplay_framework/VesselPilotDirector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cd::editor::studio
{

namespace ur       = cd::ui::renderer;
namespace uw       = cd::ui::widgets;
namespace platform = cd::platform;

enum class GameRole : std::uint8_t
{
    kHero     = 0,
    kPlatform = 1,
    kElevator = 2,
    kCrate    = 3,
    kJumpPad  = 4,
    kEnemy    = 5,
    kCrystal  = 6,
    kVehicle  = 7,
    kLight    = 8,
};

[[nodiscard]] inline const char* role_badge_tag(GameRole r) noexcept
{
    switch (r)
    {
        case GameRole::kHero:     return "HERO";
        case GameRole::kPlatform: return "PLAT";
        case GameRole::kElevator: return "ELEV";
        case GameRole::kCrate:    return "CRATE";
        case GameRole::kJumpPad:  return "JUMP";
        case GameRole::kEnemy:    return "ENEMY";
        case GameRole::kCrystal:  return "COIN";
        case GameRole::kVehicle:  return "ROVER";
        case GameRole::kLight:    return "LIGHT";
    }
    return "OBJ";
}

[[nodiscard]] inline const char* role_full_name(GameRole r) noexcept
{
    switch (r)
    {
        case GameRole::kHero:     return "PLAYABLE 3D HERO (WASD+SPACE)";
        case GameRole::kPlatform: return "SOLID 3D PLATFORM / WALL";
        case GameRole::kElevator: return "MOVING KINEMATIC ELEVATOR";
        case GameRole::kCrate:    return "PUSHABLE PHYSICS CRATE";
        case GameRole::kJumpPad:  return "HIGH-ENERGY BOUNCE PAD";
        case GameRole::kEnemy:    return "AI PATROL / CHASE DRONE";
        case GameRole::kCrystal:  return "COLLECTIBLE GOAL CRYSTAL";
        case GameRole::kVehicle:  return "3D ROVER VEHICLE";
        case GameRole::kLight:    return "DIRECTIONAL / POINT LIGHT";
    }
    return "3D OBJECT";
}

[[nodiscard]] inline ur::Color role_badge_color(GameRole r) noexcept
{
    switch (r)
    {
        case GameRole::kHero:     return ur::Color {  55U, 210U, 255U, 255U };
        case GameRole::kPlatform: return ur::Color {  85U, 135U, 215U, 255U };
        case GameRole::kElevator: return ur::Color {  50U, 205U, 185U, 255U };
        case GameRole::kCrate:    return ur::Color { 235U, 155U,  65U, 255U };
        case GameRole::kJumpPad:  return ur::Color { 175U, 105U, 255U, 255U };
        case GameRole::kEnemy:    return ur::Color { 245U,  75U,  85U, 255U };
        case GameRole::kCrystal:  return ur::Color { 255U, 215U,  55U, 255U };
        case GameRole::kVehicle:  return ur::Color { 110U, 225U,  95U, 255U };
        case GameRole::kLight:    return ur::Color { 255U, 235U, 130U, 255U };
    }
    return ur::Color { 150U, 165U, 190U, 255U };
}

struct StudioGameObject
{
    std::string           name          { "Object" };
    GameRole              role          { GameRole::kPlatform };
    std::array<float, 3>  pos           { 0.0F, 0.0F, 0.0F };
    std::array<float, 3>  spawn_pos     { 0.0F, 0.0F, 0.0F };
    float                 yaw_deg       { 0.0F };
    std::array<float, 3>  scale         { 2.0F, 0.5F, 2.0F };
    ur::Color             color         { 85U, 135U, 215U, 255U };
    float                 metallic      { 0.25F };
    float                 roughness     { 0.45F };
    std::array<float, 3>  vel           { 0.0F, 0.0F, 0.0F };
    float                 move_speed    { 4.5F };
    float                 patrol_range  { 3.5F };
    float                 jump_force    { 10.5F };
    bool                  elev_vertical { false };
    bool                  active        { true };
};

struct ConsoleEntry
{
    std::string text;
    ur::Color   color { 210U, 225U, 245U, 255U };
};

class StudioWorkbench
{
public:
    using TextFn = void (*)(ur::DrawBatcher&, float, float, std::string_view, ur::Color, float, float);
    using LineFn = void (*)(ur::DrawBatcher&, float, float, float, float, float, ur::Color);

    StudioWorkbench()
    {
        load_game_template(0U);
        add_log("[STUDIO] CHROMODYNAMIC 3D Game Engine Studio v1.0 initialized.", ur::Color { 110U, 235U, 155U, 255U });
        add_log("[VULKAN] Swapchain active | 3D Shaded Mesh Viewport + AABB Physics ready.", ur::Color { 125U, 200U, 255U, 255U });
        add_log("[GUIDE]  Select objects in 3D Viewport, drag X/Y/Z Gizmo, or press PLAY (F5)!", ur::Color { 255U, 215U, 100U, 255U });
    }

    void add_log(std::string msg, ur::Color col = ur::Color { 210U, 225U, 245U, 255U })
    {
        if (logs_.size() >= 32U)
        {
            logs_.erase(logs_.begin());
        }
        logs_.push_back(ConsoleEntry { std::move(msg), col });
    }

    [[nodiscard]] bool pie_playing() const noexcept { return is_playing_; }

    void on_event(const platform::OSEvent& e) noexcept
    {
        if (e.kind == platform::OSEventKind::kKeyDown)
        {
            set_key(e.key, true);
            if (e.key == platform::KeyCode::kF5) { request_play_toggle_ = true; }
            if (e.key == platform::KeyCode::kF6) { request_pause_toggle_ = true; }
            if (e.key == platform::KeyCode::kF8) { request_stop_ = true; }
            if (e.key == platform::KeyCode::kEscape && is_playing_) { request_stop_ = true; }
            if (e.key == platform::KeyCode::kF1) { show_help_modal_ = !show_help_modal_; }
            if (e.key == platform::KeyCode::kDelete) { request_delete_ = true; }
            if (e.key == platform::KeyCode::kF && !is_playing_) { focus_selected_object(); }
        }
        else if (e.kind == platform::OSEventKind::kKeyUp)
        {
            set_key(e.key, false);
        }
        else if (e.kind == platform::OSEventKind::kMouseWheel)
        {
            cam_dist_ = std::clamp(cam_dist_ - e.wheel * 1.1F, 3.5F, 36.0F);
        }
    }

    void focus_selected_object() noexcept
    {
        if (selected_idx_ < objects_.size())
        {
            const auto& o = objects_[selected_idx_];
            cam_target_[0] = o.pos[0];
            cam_target_[1] = o.pos[1] + 0.5F;
            cam_target_[2] = o.pos[2];
        }
    }

    void load_game_template(std::size_t idx)
    {
        template_idx_ = idx % 4U;
        objects_.clear();
        is_playing_   = false;
        is_paused_    = false;
        stage_clear_  = false;
        hero_hp_      = 100;
        score_        = 0;
        play_time_s_  = 0.0F;

        auto push_obj = [&](std::string name, GameRole role,
                            float x, float y, float z,
                            float sx, float sy, float sz,
                            ur::Color col, float speed = 4.0F, float range = 3.5F, bool elev_v = false) {
            StudioGameObject o {};
            o.name          = std::move(name);
            o.role          = role;
            o.pos           = { x, y, z };
            o.spawn_pos     = { x, y, z };
            o.scale         = { sx, sy, sz };
            o.color         = col;
            o.move_speed    = speed;
            o.patrol_range  = range;
            o.elev_vertical = elev_v;
            if (role == GameRole::kHero)
            {
                o.move_speed = 6.2F;
                o.jump_force = 11.2F;
                o.metallic   = 0.55F;
                o.roughness  = 0.25F;
            }
            else if (role == GameRole::kCrystal)
            {
                o.metallic  = 0.85F;
                o.roughness = 0.12F;
            }
            else if (role == GameRole::kJumpPad)
            {
                o.jump_force = 15.2F;
            }
            objects_.push_back(std::move(o));
        };

        if (template_idx_ == 0U)
        {
            level_title_ = "01: SKY PLATFORMER ODYSSEY";
            // Main Starting Plaza
            push_obj("Start_Plaza",       GameRole::kPlatform,  0.0F, -0.3F,  0.0F, 5.6F, 0.6F, 5.6F, ur::Color { 62U,  88U, 138U, 255U });
            // Playable 3D Hero
            push_obj("Hero_Player",       GameRole::kHero,      0.0F,  0.9F,  1.2F, 0.8F, 1.5F, 0.8F, ur::Color { 55U, 210U, 255U, 255U });
            // Stepping platforms & elevator
            push_obj("Step_Platform_A",   GameRole::kPlatform,  0.0F,  0.6F, -4.4F, 3.6F, 0.5F, 3.0F, ur::Color { 75U, 115U, 180U, 255U });
            push_obj("Sky_Elevator_01",   GameRole::kElevator,  0.0F,  1.5F, -8.2F, 3.2F, 0.45F, 2.8F, ur::Color { 48U, 195U, 175U, 255U }, 2.6F, 3.2F, false);
            push_obj("High_Sanctuary",    GameRole::kPlatform,  0.0F,  2.3F, -12.4F, 6.2F, 0.6F, 4.6F, ur::Color { 82U, 105U, 168U, 255U });
            push_obj("West_Wing_Deck",    GameRole::kPlatform, -4.8F,  1.6F, -4.4F, 3.4F, 0.5F, 3.8F, ur::Color { 70U, 128U, 175U, 255U });
            push_obj("East_Bastion_Deck", GameRole::kPlatform,  4.8F,  1.2F, -4.4F, 3.4F, 0.5F, 3.8F, ur::Color { 70U, 128U, 175U, 255U });
            // Jump Pad
            push_obj("Bounce_Pad_West",   GameRole::kJumpPad,  -1.8F,  0.15F, -0.8F, 1.4F, 0.3F, 1.4F, ur::Color { 180U, 95U, 255U, 255U });
            // Physics Crates
            push_obj("Physics_Crate_01",  GameRole::kCrate,     1.6F,  0.5F,  0.4F, 0.9F, 0.9F, 0.9F, ur::Color { 230U, 150U, 60U, 255U });
            // Enemy AI Drones
            push_obj("Cyber_Drone_01",    GameRole::kEnemy,     0.0F,  3.1F, -12.2F, 1.0F, 1.1F, 1.0F, ur::Color { 245U, 70U, 80U, 255U }, 2.8F, 2.4F);
            push_obj("Cyber_Drone_02",    GameRole::kEnemy,     4.8F,  2.0F, -4.4F, 1.0F, 1.1F, 1.0F, ur::Color { 245U, 70U, 80U, 255U }, 2.4F, 1.4F);
            // Collectible Goal Crystals
            push_obj("Goal_Crystal_01",   GameRole::kCrystal,   0.0F,  1.5F, -4.4F, 0.7F, 0.9F, 0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_02",   GameRole::kCrystal,  -4.8F,  2.5F, -4.4F, 0.7F, 0.9F, 0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_03",   GameRole::kCrystal,   4.8F,  2.9F, -4.4F, 0.7F, 0.9F, 0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_04",   GameRole::kCrystal,  -1.8F,  3.3F, -12.4F, 0.7F, 0.9F, 0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_05",   GameRole::kCrystal,   1.8F,  3.3F, -12.4F, 0.7F, 0.9F, 0.7F, ur::Color { 255U, 215U, 50U, 255U });
            // Key Sun Light
            push_obj("Solar_KeyLight",    GameRole::kLight,    -5.5F,  6.2F,  2.5F, 0.8F, 0.8F, 0.8F, ur::Color { 255U, 235U, 135U, 255U });
        }
        else if (template_idx_ == 1U)
        {
            level_title_ = "02: CYBER AI STEALTH ARENA";
            push_obj("Arena_Floor",       GameRole::kPlatform,  0.0F, -0.3F, -4.0F, 12.0F, 0.6F, 12.0F, ur::Color { 48U,  64U, 102U, 255U });
            push_obj("Hero_Player",       GameRole::kHero,      0.0F,  0.9F,  0.8F,  0.8F, 1.5F,  0.8F, ur::Color { 55U, 210U, 255U, 255U });
            push_obj("Center_Command",    GameRole::kPlatform,  0.0F,  1.2F, -4.5F,  3.4F, 1.2F,  3.4F, ur::Color { 75U, 110U, 170U, 255U });
            push_obj("North_Wall_Deck",   GameRole::kPlatform,  0.0F,  2.2F, -9.2F,  8.0F, 0.6F,  2.2F, ur::Color { 68U,  98U, 155U, 255U });
            push_obj("Arena_Bounce_L",    GameRole::kJumpPad,  -3.2F,  0.15F, -4.5F, 1.5F, 0.3F,  1.5F, ur::Color { 180U, 95U, 255U, 255U });
            push_obj("Arena_Bounce_R",    GameRole::kJumpPad,   3.2F,  0.15F, -4.5F, 1.5F, 0.3F,  1.5F, ur::Color { 180U, 95U, 255U, 255U });
            push_obj("Patrol_Bot_West",   GameRole::kEnemy,    -3.8F,  0.6F, -2.0F,  1.0F, 1.1F,  1.0F, ur::Color { 245U, 70U, 80U, 255U }, 3.4F, 2.2F);
            push_obj("Patrol_Bot_East",   GameRole::kEnemy,     3.8F,  0.6F, -6.5F,  1.0F, 1.1F,  1.0F, ur::Color { 245U, 70U, 80U, 255U }, 3.4F, 2.2F);
            push_obj("Commander_Bot",     GameRole::kEnemy,     0.0F,  3.0F, -9.2F,  1.2F, 1.3F,  1.2F, ur::Color { 255U, 50U, 95U, 255U }, 3.8F, 3.0F);
            push_obj("Cover_Crate_A",     GameRole::kCrate,    -2.2F,  0.5F, -1.2F,  1.0F, 1.0F,  1.0F, ur::Color { 230U, 150U, 60U, 255U });
            push_obj("Cover_Crate_B",     GameRole::kCrate,     2.2F,  0.5F, -1.2F,  1.0F, 1.0F,  1.0F, ur::Color { 230U, 150U, 60U, 255U });
            push_obj("Goal_Crystal_01",   GameRole::kCrystal,   0.0F,  2.5F, -4.5F,  0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_02",   GameRole::kCrystal,  -4.5F,  0.9F, -4.5F,  0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_03",   GameRole::kCrystal,   4.5F,  0.9F, -4.5F,  0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_04",   GameRole::kCrystal,   0.0F,  3.2F, -9.2F,  0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Arena_Light",       GameRole::kLight,     0.0F,  6.5F, -4.0F,  0.8F, 0.8F,  0.8F, ur::Color { 140U, 220U, 255U, 255U });
        }
        else if (template_idx_ == 2U)
        {
            level_title_ = "03: PHYSICS & ELEVATOR LAB";
            push_obj("Lab_Base_Pad",      GameRole::kPlatform,  0.0F, -0.3F,  0.0F,  6.5F, 0.6F,  5.0F, ur::Color { 58U,  92U, 135U, 255U });
            push_obj("Hero_Player",       GameRole::kHero,     -1.8F,  0.9F,  0.5F,  0.8F, 1.5F,  0.8F, ur::Color { 55U, 210U, 255U, 255U });
            push_obj("Vert_Lift_01",      GameRole::kElevator,  2.2F,  1.2F, -2.8F,  2.6F, 0.45F, 2.6F, ur::Color { 48U, 205U, 185U, 255U }, 2.2F, 2.0F, true);
            push_obj("Mid_Catwalk",       GameRole::kPlatform, -1.5F,  2.6F, -5.8F,  4.8F, 0.5F,  3.2F, ur::Color { 78U, 118U, 175U, 255U });
            push_obj("Horiz_Shuttle_02",  GameRole::kElevator,  0.0F,  3.4F, -9.5F,  3.0F, 0.45F, 2.6F, ur::Color { 48U, 205U, 185U, 255U }, 3.0F, 3.5F, false);
            push_obj("Summit_Tower",      GameRole::kPlatform,  0.0F,  4.2F, -13.2F, 5.2F, 0.6F,  4.0F, ur::Color { 90U, 120U, 195U, 255U });
            push_obj("Heavy_Crate_01",    GameRole::kCrate,     0.4F,  0.5F,  0.2F,  1.0F, 1.0F,  1.0F, ur::Color { 235U, 155U, 60U, 255U });
            push_obj("Heavy_Crate_02",    GameRole::kCrate,     1.6F,  0.5F,  0.2F,  1.0F, 1.0F,  1.0F, ur::Color { 235U, 155U, 60U, 255U });
            push_obj("Heavy_Crate_03",    GameRole::kCrate,    -1.5F,  3.4F, -5.8F,  1.0F, 1.0F,  1.0F, ur::Color { 235U, 155U, 60U, 255U });
            push_obj("Goal_Crystal_01",   GameRole::kCrystal,   2.2F,  3.5F, -2.8F,  0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_02",   GameRole::kCrystal,  -2.8F,  3.5F, -5.8F,  0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_03",   GameRole::kCrystal,   0.0F,  4.4F, -9.5F,  0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_04",   GameRole::kCrystal,   0.0F,  5.2F, -13.2F, 0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Lab_Beacon",        GameRole::kLight,    -4.0F,  6.5F, -4.0F,  0.8F, 0.8F,  0.8F, ur::Color { 255U, 230U, 140U, 255U });
        }
        else
        {
            level_title_ = "04: ROVER CANYON TRIAL";
            push_obj("Canyon_Start_Road", GameRole::kPlatform,  0.0F, -0.3F,  0.0F,  7.0F, 0.6F,  8.0F, ur::Color { 115U, 88U,  68U, 255U });
            push_obj("Hero_Player",       GameRole::kHero,     -1.6F,  0.9F,  1.8F,  0.8F, 1.5F,  0.8F, ur::Color { 55U, 210U, 255U, 255U });
            push_obj("Canyon_Rover_V8",   GameRole::kVehicle,   1.2F,  0.6F,  1.0F,  1.8F, 1.0F,  2.6F, ur::Color { 105U, 225U, 95U, 255U }, 8.0F, 0.0F);
            push_obj("Canyon_Bridge",     GameRole::kPlatform,  0.0F,  0.2F, -7.2F,  4.2F, 0.5F,  6.4F, ur::Color { 95U, 110U, 135U, 255U });
            push_obj("Turbo_JumpPad",     GameRole::kJumpPad,   0.0F,  0.6F, -9.6F,  2.2F, 0.3F,  1.8F, ur::Color { 185U, 95U, 255U, 255U });
            push_obj("Mesa_Finish_Deck",  GameRole::kPlatform,  0.0F,  2.2F, -14.5F, 7.5F, 0.6F,  5.5F, ur::Color { 125U, 95U,  72U, 255U });
            push_obj("Canyon_Drone_01",   GameRole::kEnemy,     0.0F,  3.0F, -14.2F, 1.1F, 1.1F,  1.1F, ur::Color { 245U, 70U,  80U, 255U }, 3.5F, 2.8F);
            push_obj("Goal_Crystal_01",   GameRole::kCrystal,   0.0F,  1.2F, -5.5F,  0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_02",   GameRole::kCrystal,   0.0F,  2.6F, -11.5F, 0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_03",   GameRole::kCrystal,  -2.2F,  3.2F, -14.5F, 0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Goal_Crystal_04",   GameRole::kCrystal,   2.2F,  3.2F, -14.5F, 0.7F, 0.9F,  0.7F, ur::Color { 255U, 215U, 50U, 255U });
            push_obj("Desert_Sun",        GameRole::kLight,     5.5F,  7.0F,  2.0F,  0.8F, 0.8F,  0.8F, ur::Color { 255U, 220U, 120U, 255U });
        }

        selected_idx_  = 1U < objects_.size() ? 1U : 0U;
        cam_target_    = { 0.0F, 1.2F, -3.5F };
        cam_yaw_       = 0.42F;
        cam_pitch_     = 0.38F;
        cam_dist_      = 15.5F;
    }

    void sync_from_showcase_stage(const cd::element_core::Stage& stage)
    {
        load_game_template(template_idx_);
        level_title_ = std::string("SHOWCASE: ") + std::string(stage.name());
        const auto& ch = stage.root().children();
        for (std::size_t i = 0U; i < ch.size() && i < 8U; ++i)
        {
            if (!ch[i]) { continue; }
            StudioGameObject o {};
            o.name      = std::string(ch[i]->name());
            o.role      = (ch[i]->class_name().find("Light") != std::string_view::npos)
                              ? GameRole::kLight
                              : GameRole::kCrate;
            const float angle = static_cast<float>(i) * 0.785F;
            o.pos       = { std::cos(angle) * 2.2F, 0.65F, std::sin(angle) * 2.2F };
            o.spawn_pos = o.pos;
            o.scale     = { 0.85F, 0.85F, 0.85F };
            o.color     = role_badge_color(o.role);
            objects_.push_back(std::move(o));
        }
        add_log("[STAGE] Loaded showcase '" + std::string(stage.name()) + "' into 3D Studio.",
                ur::Color { 125U, 205U, 255U, 255U });
    }

    void spawn_object(GameRole role, cd::element_core::Stage* stage_ptr, cd::editor::AddElementCatalog* catalog_ptr)
    {
        StudioGameObject o {};
        const std::size_t id_num = objects_.size() + 1U;
        o.role  = role;
        o.color = role_badge_color(role);
        o.pos   = {
            cam_target_[0] + std::sin(cam_yaw_) * 1.5F,
            std::max(0.5F, cam_target_[1]),
            cam_target_[2] - std::cos(cam_yaw_) * 1.5F
        };

        switch (role)
        {
            case GameRole::kPlatform:
                o.name  = "Platform_" + std::to_string(id_num);
                o.scale = { 3.2F, 0.5F, 3.2F };
                break;
            case GameRole::kElevator:
                o.name         = "Elevator_" + std::to_string(id_num);
                o.scale        = { 2.8F, 0.45F, 2.8F };
                o.move_speed   = 2.6F;
                o.patrol_range = 3.0F;
                break;
            case GameRole::kCrate:
                o.name  = "Physics_Crate_" + std::to_string(id_num);
                o.scale = { 0.95F, 0.95F, 0.95F };
                break;
            case GameRole::kJumpPad:
                o.name       = "Jump_Pad_" + std::to_string(id_num);
                o.scale      = { 1.5F, 0.35F, 1.5F };
                o.jump_force = 15.0F;
                break;
            case GameRole::kEnemy:
                o.name         = "AI_Drone_" + std::to_string(id_num);
                o.scale        = { 1.0F, 1.1F, 1.0F };
                o.move_speed   = 3.0F;
                o.patrol_range = 2.5F;
                break;
            case GameRole::kCrystal:
                o.name      = "Goal_Crystal_" + std::to_string(id_num);
                o.scale     = { 0.7F, 0.9F, 0.7F };
                o.metallic  = 0.85F;
                o.roughness = 0.12F;
                break;
            case GameRole::kVehicle:
                o.name       = "Rover_V8_" + std::to_string(id_num);
                o.scale      = { 1.8F, 1.0F, 2.6F };
                o.move_speed = 8.0F;
                break;
            case GameRole::kLight:
                o.name    = "Point_Light_" + std::to_string(id_num);
                o.scale   = { 0.75F, 0.75F, 0.75F };
                o.pos[1] += 2.5F;
                break;
            case GameRole::kHero:
                o.name       = "Hero_Player_" + std::to_string(id_num);
                o.scale      = { 0.8F, 1.5F, 0.8F };
                o.move_speed = 6.2F;
                o.jump_force = 11.2F;
                break;
        }
        o.spawn_pos = o.pos;
        objects_.push_back(o);
        selected_idx_ = objects_.size() - 1U;

        if (stage_ptr != nullptr && catalog_ptr != nullptr && !catalog_ptr->items().empty())
        {
            const auto& desc = catalog_ptr->items()[id_num % catalog_ptr->items().size()];
            (void)catalog_ptr->spawn_into_stage(*stage_ptr, desc.class_name, o.name);
        }

        add_log("[SPAWN] Added 3D Object '" + o.name + "' (" + role_badge_tag(role) + ") to level.",
                ur::Color { 115U, 235U, 160U, 255U });
    }

    bool save_level_to_disk(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        std::ofstream out(path, std::ios::trunc);
        if (!out) { return false; }
        out << "# CHROMODYNAMIC 3D GAME STAGE v1.0\n";
        out << "title " << level_title_ << "\n";
        for (const auto& o : objects_)
        {
            out << "obj "
                << o.name << " "
                << static_cast<int>(o.role) << " "
                << o.spawn_pos[0] << " " << o.spawn_pos[1] << " " << o.spawn_pos[2] << " "
                << o.yaw_deg << " "
                << o.scale[0] << " " << o.scale[1] << " " << o.scale[2] << " "
                << static_cast<int>(o.color.r) << " "
                << static_cast<int>(o.color.g) << " "
                << static_cast<int>(o.color.b) << " "
                << o.metallic << " " << o.roughness << " "
                << o.move_speed << " " << o.patrol_range << " " << o.jump_force << " "
                << (o.elev_vertical ? 1 : 0) << "\n";
        }
        add_log("[FILE]  Saved level (" + std::to_string(objects_.size()) + " objects) -> " + path.string(),
                ur::Color { 110U, 240U, 155U, 255U });
        return out.good();
    }

    bool load_level_from_disk(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        if (!in)
        {
            add_log("[WARN]  No saved level at " + path.string() + " (Save first with CTRL+S).",
                    ur::Color { 255U, 195U, 85U, 255U });
            return false;
        }
        std::vector<StudioGameObject> loaded;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#') { continue; }
            std::istringstream iss(line);
            std::string tag;
            iss >> tag;
            if (tag == "obj")
            {
                StudioGameObject o {};
                int role_i = 1, r = 100, g = 150, b = 220, ev = 0;
                if (iss >> o.name >> role_i
                        >> o.pos[0] >> o.pos[1] >> o.pos[2]
                        >> o.yaw_deg
                        >> o.scale[0] >> o.scale[1] >> o.scale[2]
                        >> r >> g >> b
                        >> o.metallic >> o.roughness
                        >> o.move_speed >> o.patrol_range >> o.jump_force >> ev)
                {
                    o.role          = static_cast<GameRole>(std::clamp(role_i, 0, 8));
                    o.spawn_pos     = o.pos;
                    o.color         = ur::Color {
                        static_cast<std::uint8_t>(std::clamp(r, 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(g, 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(b, 0, 255)),
                        255U
                    };
                    o.elev_vertical = (ev != 0);
                    o.active        = true;
                    loaded.push_back(std::move(o));
                }
            }
        }
        if (loaded.empty()) { return false; }
        objects_      = std::move(loaded);
        selected_idx_ = 0U;
        is_playing_   = false;
        add_log("[FILE]  Loaded " + std::to_string(objects_.size()) + " objects from " + path.string(),
                ur::Color { 115U, 225U, 255U, 255U });
        return true;
    }

    void start_play(cd::element_core::Stage& stage, cd::editor::PieWorkbenchController& pie)
    {
        for (auto& o : objects_)
        {
            o.spawn_pos = o.pos;
            o.vel       = { 0.0F, 0.0F, 0.0F };
            o.active    = true;
        }
        is_playing_  = true;
        is_paused_   = false;
        stage_clear_ = false;
        hero_hp_     = 100;
        score_       = 0;
        play_time_s_ = 0.0F;
        (void)pie.play(stage);
        add_log("[PLAY]  PLAY-IN-EDITOR STARTED! Use WASD / Arrows to Move, SPACE to Jump!",
                ur::Color { 95U, 245U, 145U, 255U });
    }

    void stop_play(cd::element_core::Stage& stage, cd::editor::PieWorkbenchController& pie)
    {
        for (auto& o : objects_)
        {
            o.pos    = o.spawn_pos;
            o.vel    = { 0.0F, 0.0F, 0.0F };
            o.active = true;
        }
        is_playing_  = false;
        is_paused_   = false;
        stage_clear_ = false;
        if (pie.state() != cd::editor::PieState::kStopped)
        {
            (void)pie.stop(stage);
        }
        add_log("[STOP]  Returned to 3D Level Authoring Mode (positions restored).",
                ur::Color { 255U, 200U, 95U, 255U });
    }

    void tick_gameplay(float dt)
    {
        const float step_dt = std::clamp(dt > 0.0F ? dt : (1.0F / 60.0F), 0.001F, 0.05F);
        anim_time_ += step_dt;

        if (!is_playing_ || is_paused_) { return; }
        play_time_s_ += step_dt;
        if (invuln_timer_ > 0.0F) { invuln_timer_ = std::max(0.0F, invuln_timer_ - step_dt); }

        // 1. Update Moving Elevators & AI Patrol Bots
        for (auto& o : objects_)
        {
            if (!o.active) { continue; }
            if (o.role == GameRole::kElevator)
            {
                const float phase = play_time_s_ * (o.move_speed * 0.45F);
                const float offset = std::sin(phase) * o.patrol_range;
                const float prev_x = o.pos[0];
                const float prev_y = o.pos[1];
                if (o.elev_vertical)
                {
                    o.pos[1] = o.spawn_pos[1] + offset;
                    o.vel[1] = (o.pos[1] - prev_y) / step_dt;
                }
                else
                {
                    o.pos[0] = o.spawn_pos[0] + offset;
                    o.vel[0] = (o.pos[0] - prev_x) / step_dt;
                }
            }
            else if (o.role == GameRole::kEnemy)
            {
                const float phase = play_time_s_ * (o.move_speed * 0.55F);
                const float old_x = o.pos[0];
                o.pos[0] = o.spawn_pos[0] + std::sin(phase) * o.patrol_range;
                o.yaw_deg = (o.pos[0] >= old_x) ? 90.0F : -90.0F;
            }
        }

        // 2. Find Playable Hero (or Vehicle if no Hero)
        StudioGameObject* hero = nullptr;
        for (auto& o : objects_)
        {
            if (o.active && o.role == GameRole::kHero)
            {
                hero = &o;
                break;
            }
        }
        if (hero == nullptr)
        {
            for (auto& o : objects_)
            {
                if (o.active && o.role == GameRole::kVehicle)
                {
                    hero = &o;
                    break;
                }
            }
        }
        if (hero == nullptr) { return; }

        // 3. Player Input (WASD / Arrow keys relative to camera yaw)
        float move_f = 0.0F;
        float move_r = 0.0F;
        if (key_w_ || key_up_)    { move_f += 1.0F; }
        if (key_s_ || key_down_)  { move_f -= 1.0F; }
        if (key_d_ || key_right_) { move_r += 1.0F; }
        if (key_a_ || key_left_)  { move_r -= 1.0F; }

        // Camera forward/right on XZ plane
        const float fwd_x = -std::sin(cam_yaw_);
        const float fwd_z = -std::cos(cam_yaw_);
        const float rgt_x =  std::cos(cam_yaw_);
        const float rgt_z = -std::sin(cam_yaw_);

        float wish_x = fwd_x * move_f + rgt_x * move_r;
        float wish_z = fwd_z * move_f + rgt_z * move_r;
        const float wish_len = std::sqrt(wish_x * wish_x + wish_z * wish_z);
        if (wish_len > 1e-4F)
        {
            wish_x /= wish_len;
            wish_z /= wish_len;
            hero->yaw_deg = std::atan2(wish_x, wish_z) * (180.0F / 3.14159265F);
            hero_walk_phase_ += step_dt * 11.0F;
        }

        const float spd = hero->move_speed * (key_shift_ ? 1.45F : 1.0F);
        hero->vel[0] = wish_x * spd;
        hero->vel[2] = wish_z * spd;

        // Gravity
        hero->vel[1] = std::max(-22.0F, hero->vel[1] - 24.0F * step_dt);

        // Integrate X & Z with solid platform/crate collision
        hero->pos[0] += hero->vel[0] * step_dt;
        hero->pos[2] += hero->vel[2] * step_dt;

        const float hx = hero->scale[0] * 0.42F;
        const float hy = hero->scale[1] * 0.50F;
        const float hz = hero->scale[2] * 0.42F;

        for (auto& p : objects_)
        {
            if (&p == hero || !p.active) { continue; }
            if (p.role != GameRole::kPlatform && p.role != GameRole::kElevator && p.role != GameRole::kCrate)
            {
                continue;
            }
            const float px = p.scale[0] * 0.5F;
            const float py = p.scale[1] * 0.5F;
            const float pz = p.scale[2] * 0.5F;

            if ( std::abs(hero->pos[1] - p.pos[1]) < (hy + py - 0.12F) &&
                 std::abs(hero->pos[0] - p.pos[0]) < (hx + px) &&
                 std::abs(hero->pos[2] - p.pos[2]) < (hz + pz) )
            {
                if (p.role == GameRole::kCrate)
                {
                    // Push physics crate horizontally!
                    p.pos[0] += hero->vel[0] * step_dt * 0.85F;
                    p.pos[2] += hero->vel[2] * step_dt * 0.85F;
                }
                const float ox = (hx + px) - std::abs(hero->pos[0] - p.pos[0]);
                const float oz = (hz + pz) - std::abs(hero->pos[2] - p.pos[2]);
                if (ox < oz)
                {
                    hero->pos[0] += (hero->pos[0] < p.pos[0]) ? -ox : ox;
                }
                else
                {
                    hero->pos[2] += (hero->pos[2] < p.pos[2]) ? -oz : oz;
                }
            }
        }

        // Integrate Y and check landing on platforms / elevators / crates / jump pads
        hero->pos[1] += hero->vel[1] * step_dt;
        bool on_ground = false;

        for (auto& p : objects_)
        {
            if (&p == hero || !p.active) { continue; }
            if (p.role != GameRole::kPlatform && p.role != GameRole::kElevator &&
                p.role != GameRole::kCrate && p.role != GameRole::kJumpPad)
            {
                continue;
            }
            const float px = p.scale[0] * 0.5F;
            const float py = p.scale[1] * 0.5F;
            const float pz = p.scale[2] * 0.5F;

            if (std::abs(hero->pos[0] - p.pos[0]) < (hx + px - 0.04F) &&
                std::abs(hero->pos[2] - p.pos[2]) < (hz + pz - 0.04F))
            {
                const float hero_bottom = hero->pos[1] - hy;
                const float plat_top    = p.pos[1] + py;
                if (hero->vel[1] <= 0.0F &&
                    hero_bottom <= plat_top &&
                    hero_bottom >= plat_top - 0.65F)
                {
                    hero->pos[1] = plat_top + hy;
                    if (p.role == GameRole::kJumpPad)
                    {
                        hero->vel[1] = p.jump_force;
                        add_log("[JUMP]  Boosted off Jump Pad '" + p.name + "'!",
                                ur::Color { 195U, 130U, 255U, 255U });
                    }
                    else
                    {
                        hero->vel[1] = 0.0F;
                        on_ground    = true;
                        if (p.role == GameRole::kElevator)
                        {
                            hero->pos[0] += p.vel[0] * step_dt;
                        }
                    }
                }
            }
        }

        if (on_ground && key_space_)
        {
            hero->vel[1] = hero->jump_force;
        }

        // Respawn if fallen into the void
        if (hero->pos[1] < -8.5F)
        {
            hero->pos = hero->spawn_pos;
            hero->vel = { 0.0F, 0.0F, 0.0F };
            hero_hp_  = std::max(25, hero_hp_ - 20);
            add_log("[WARN]  Hero fell off the stage! Respawned at start plaza.",
                    ur::Color { 255U, 185U, 80U, 255U });
        }

        // Check Enemy AI & Crystal interactions
        std::size_t crystals_remaining = 0U;
        for (auto& o : objects_)
        {
            if (!o.active) { continue; }
            if (o.role == GameRole::kCrystal)
            {
                const float dx = hero->pos[0] - o.pos[0];
                const float dy = hero->pos[1] - o.pos[1];
                const float dz = hero->pos[2] - o.pos[2];
                if (dx * dx + dy * dy + dz * dz < 1.35F)
                {
                    o.active = false;
                    score_  += 100;
                    add_log("[SCORE] Collected '" + o.name + "'! (+100 PTS, Total: " + std::to_string(score_) + ")",
                            ur::Color { 255U, 225U, 75U, 255U });
                }
                else
                {
                    ++crystals_remaining;
                }
            }
            else if (o.role == GameRole::kEnemy)
            {
                const float dx = hero->pos[0] - o.pos[0];
                const float dy = hero->pos[1] - o.pos[1];
                const float dz = hero->pos[2] - o.pos[2];
                if (std::abs(dx) < 0.95F && std::abs(dz) < 0.95F && std::abs(dy) < 1.25F)
                {
                    if (hero->vel[1] < -1.0F && dy > 0.35F)
                    {
                        o.active     = false;
                        hero->vel[1] = 10.0F;
                        score_      += 200;
                        add_log("[COMBAT] Stomped Enemy Drone '" + o.name + "'! (+200 PTS)",
                                ur::Color { 110U, 245U, 165U, 255U });
                    }
                    else if (invuln_timer_ <= 0.0F)
                    {
                        hero_hp_      = std::max(0, hero_hp_ - 25);
                        invuln_timer_ = 1.1F;
                        hero->vel[1]  = 6.5F;
                        add_log("[DAMAGE] Hit by '" + o.name + "'! (-25 HP)",
                                ur::Color { 255U, 95U, 95U, 255U });
                        if (hero_hp_ == 0)
                        {
                            hero_hp_  = 100;
                            hero->pos = hero->spawn_pos;
                            hero->vel = { 0.0F, 0.0F, 0.0F };
                            add_log("[RESPAWN] Hero HP depleted — respawned at checkpoint!",
                                    ur::Color { 255U, 160U, 75U, 255U });
                        }
                    }
                }
            }
        }

        if (crystals_remaining == 0U && !stage_clear_)
        {
            stage_clear_ = true;
            score_      += 500;
            add_log("[VICTORY] ALL GOAL CRYSTALS COLLECTED! STAGE COMPLETE! (+500 BONUS)",
                    ur::Color { 100U, 255U, 160U, 255U });
        }

        // Smooth camera follow in Play Mode
        if (follow_hero_cam_)
        {
            cam_target_[0] += (hero->pos[0] - cam_target_[0]) * std::min(1.0F, step_dt * 6.5F);
            cam_target_[1] += (hero->pos[1] + 0.6F - cam_target_[1]) * std::min(1.0F, step_dt * 5.0F);
            cam_target_[2] += (hero->pos[2] - cam_target_[2]) * std::min(1.0F, step_dt * 6.5F);
        }
    }

    // =========================================================================
    // MAIN 5-ZONE DOMESTIC STUDIO RENDERER
    // =========================================================================
    void tick_and_draw(ur::DrawBatcher&                    batcher,
                       const uw::Theme&                    /*theme*/,
                       std::uint32_t                       fb_w_u,
                       std::uint32_t                       fb_h_u,
                       const uw::PointerState&             ptr,
                       bool                                right_mouse_down,
                       float                               dt_s,
                       float                               fps,
                       int                                 route_taken,
                       cd::element_core::Stage&            active_stage,
                       cd::editor::PieWorkbenchController& pie_controller,
                       cd::editor::AddElementCatalog&      add_catalog,
                       cd::editor::EditorCockpitShell&     cockpit_shell,
                       int&                                io_gizmo_mode,
                       float&                              io_cam_yaw,
                       float&                              io_cam_pitch,
                       TextFn                              draw_text,
                       LineFn                              draw_line)
    {
        // Sync external camera orbit deltas if right mouse is held
        if (right_mouse_down)
        {
            cam_yaw_   = io_cam_yaw;
            cam_pitch_ = std::clamp(io_cam_pitch, 0.08F, 1.35F);
            io_cam_pitch = cam_pitch_;

            // RMB + WASD/QE flies camera in Edit Mode
            if (!is_playing_)
            {
                const float fly_spd = 7.5F * std::clamp(dt_s > 0.0F ? dt_s : 0.016F, 0.001F, 0.05F);
                const float fx = -std::sin(cam_yaw_);
                const float fz = -std::cos(cam_yaw_);
                const float rx =  std::cos(cam_yaw_);
                const float rz = -std::sin(cam_yaw_);
                if (key_w_) { cam_target_[0] += fx * fly_spd; cam_target_[2] += fz * fly_spd; }
                if (key_s_) { cam_target_[0] -= fx * fly_spd; cam_target_[2] -= fz * fly_spd; }
                if (key_d_) { cam_target_[0] += rx * fly_spd; cam_target_[2] += rz * fly_spd; }
                if (key_a_) { cam_target_[0] -= rx * fly_spd; cam_target_[2] -= rz * fly_spd; }
            }
        }
        else
        {
            io_cam_yaw   = cam_yaw_;
            io_cam_pitch = cam_pitch_;
        }

        // Sync external PIE start (e.g., --pie-play CLI flag)
        if (!is_playing_ && pie_controller.state() == cd::editor::PieState::kPlaying)
        {
            start_play(active_stage, pie_controller);
        }

        if (request_play_toggle_)
        {
            request_play_toggle_ = false;
            if (!is_playing_) { start_play(active_stage, pie_controller); }
            else              { stop_play(active_stage, pie_controller); }
        }
        if (request_pause_toggle_)
        {
            request_pause_toggle_ = false;
            if (is_playing_) { is_paused_ = !is_paused_; }
        }
        if (request_stop_)
        {
            request_stop_ = false;
            if (is_playing_) { stop_play(active_stage, pie_controller); }
        }
        if (request_delete_)
        {
            request_delete_ = false;
            delete_selected();
        }

        tick_gameplay(dt_s);

        const float fb_w = static_cast<float>(fb_w_u);
        const float fb_h = static_cast<float>(fb_h_u);

        // Studio Layout Metrics (clean, spacious 5-zone partition)
        constexpr float kTopBarH    = 50.0F;
        constexpr float kStatusBarH = 26.0F;
        constexpr float kBottomH    = 186.0F;
        const float     left_w      = std::clamp(fb_w * 0.21F, 265.0F, 315.0F);
        const float     right_w     = std::clamp(fb_w * 0.24F, 305.0F, 360.0F);
        const float     center_w    = std::max(320.0F, fb_w - left_w - right_w);
        const float     main_h      = std::max(240.0F, fb_h - kTopBarH - kBottomH - kStatusBarH);

        const uw::Rect top_rect    { 0.0F,              0.0F,                  fb_w,     kTopBarH };
        const uw::Rect left_rect   { 0.0F,              kTopBarH,              left_w,   main_h };
        const uw::Rect vp_rect     { left_w,            kTopBarH,              center_w, main_h };
        const uw::Rect right_rect  { left_w + center_w, kTopBarH,              right_w,  main_h };
        const uw::Rect bottom_rect { 0.0F,              kTopBarH + main_h,     fb_w,     kBottomH };
        const uw::Rect status_rect { 0.0F,              fb_h - kStatusBarH,    fb_w,     kStatusBarH };

        // Full studio background
        batcher.quad(0.0F, 0.0F, fb_w, fb_h, ur::Color { 14U, 17U, 24U, 255U });

        // 1. Draw Center 3D Shaded Viewport
        draw_3d_viewport(batcher, vp_rect, ptr, io_gizmo_mode, draw_text, draw_line);

        // 2. Draw Left Hierarchy + Object Spawner Dock
        draw_left_dock(batcher, left_rect, ptr, active_stage, add_catalog, draw_text);

        // 3. Draw Right Interactive Property Inspector Dock
        draw_right_inspector(batcher, right_rect, ptr, active_stage, pie_controller, draw_text);

        // 4. Draw Bottom Game Templates / Showcases / Console / Profiler Dock
        draw_bottom_dock(batcher, bottom_rect, ptr, active_stage, add_catalog, cockpit_shell, fps, draw_text);

        // 5. Draw Top Studio Command Bar
        draw_top_bar(batcher, top_rect, ptr, active_stage, pie_controller, io_gizmo_mode, draw_text);

        // 6. Draw Bottom Status Bar
        draw_status_bar(batcher, status_rect, fps, route_taken, draw_text);

        // 7. Optional Help Modal (F1 or [? HELP] button)
        if (show_help_modal_)
        {
            draw_help_modal(batcher, fb_w, fb_h, ptr, draw_text);
        }
    }

private:
    void set_key(platform::KeyCode k, bool down) noexcept
    {
        switch (k)
        {
            case platform::KeyCode::kW:      key_w_     = down; break;
            case platform::KeyCode::kA:      key_a_     = down; break;
            case platform::KeyCode::kS:      key_s_     = down; break;
            case platform::KeyCode::kD:      key_d_     = down; break;
            case platform::KeyCode::kUp:     key_up_    = down; break;
            case platform::KeyCode::kDown:   key_down_  = down; break;
            case platform::KeyCode::kLeft:   key_left_  = down; break;
            case platform::KeyCode::kRight:  key_right_ = down; break;
            case platform::KeyCode::kSpace:  key_space_ = down; break;
            case platform::KeyCode::kLShift:
            case platform::KeyCode::kRShift: key_shift_ = down; break;
            default: break;
        }
    }

    void duplicate_selected()
    {
        if (selected_idx_ >= objects_.size()) { return; }
        StudioGameObject copy = objects_[selected_idx_];
        copy.name         += "_Copy";
        copy.pos[0]       += 1.2F;
        copy.pos[2]       += 0.8F;
        copy.spawn_pos     = copy.pos;
        objects_.push_back(copy);
        selected_idx_ = objects_.size() - 1U;
        add_log("[EDIT]  Duplicated object -> '" + copy.name + "'.", ur::Color { 130U, 215U, 255U, 255U });
    }

    void delete_selected()
    {
        if (objects_.size() <= 1U || selected_idx_ >= objects_.size()) { return; }
        const std::string del_name = objects_[selected_idx_].name;
        objects_.erase(objects_.begin() + static_cast<std::ptrdiff_t>(selected_idx_));
        if (selected_idx_ >= objects_.size())
        {
            selected_idx_ = objects_.size() - 1U;
        }
        add_log("[EDIT]  Deleted object '" + del_name + "'.", ur::Color { 255U, 155U, 95U, 255U });
    }

    // ---- UI Button Helper ---------------------------------------------------
    [[nodiscard]] bool draw_button(ur::DrawBatcher&        batcher,
                                   const uw::Rect&         r,
                                   std::string_view        label,
                                   const uw::PointerState& ptr,
                                   ur::Color               base_bg,
                                   ur::Color               active_bg,
                                   bool                    is_active,
                                   TextFn                  draw_text,
                                   float                   text_scale = 1.0F)
    {
        const bool hov = r.contains(ptr.mouse_x, ptr.mouse_y);
        ur::Color bg = is_active ? active_bg : base_bg;
        if (hov && !is_active)
        {
            bg.r = static_cast<std::uint8_t>(std::min(255, static_cast<int>(bg.r) + 26));
            bg.g = static_cast<std::uint8_t>(std::min(255, static_cast<int>(bg.g) + 28));
            bg.b = static_cast<std::uint8_t>(std::min(255, static_cast<int>(bg.b) + 34));
        }
        batcher.quad(r.x, r.y, r.w, r.h, bg);
        const ur::Color border = is_active
            ? ur::Color { 255U, 255U, 255U, 235U }
            : (hov ? ur::Color { 130U, 175U, 245U, 220U }
                   : ur::Color {  68U,  84U, 118U, 190U });
        batcher.quad(r.x, r.y, r.w, 1.0F, border);
        batcher.quad(r.x, r.y + r.h - 1.0F, r.w, 1.0F, ur::Color { 12U, 15U, 22U, 220U });

        const float px = std::max(1.0F, std::round(text_scale));
        const float text_w = static_cast<float>(label.size()) * 6.0F * px;
        const float tx = std::max(r.x + 5.0F, r.x + (r.w - text_w) * 0.5F);
        const float ty = r.y + (r.h - 7.0F * px) * 0.5F;
        const ur::Color tc = is_active
            ? ur::Color { 12U, 16U, 24U, 255U }
            : ur::Color { 238U, 244U, 255U, 255U };
        draw_text(batcher, tx, ty, label, tc, text_scale, r.x + r.w - 3.0F);
        return hov && ptr.left_pressed;
    }

    // ---- 1. TOP STUDIO COMMAND BAR ------------------------------------------
    void draw_top_bar(ur::DrawBatcher&                    batcher,
                      const uw::Rect&                     r,
                      const uw::PointerState&             ptr,
                      cd::element_core::Stage&            active_stage,
                      cd::editor::PieWorkbenchController& pie_controller,
                      int&                                io_gizmo_mode,
                      TextFn                              draw_text)
    {
        batcher.quad(r.x, r.y, r.w, r.h, ur::Color { 19U, 24U, 36U, 255U });
        batcher.quad(r.x, r.y + r.h - 2.0F, r.w, 2.0F,
                     is_playing_ ? ur::Color { 65U, 225U, 125U, 255U }
                                 : ur::Color { 75U, 140U, 245U, 255U });

        float cx = 8.0F;
        const float by = 7.0F;
        const float bh = r.h - 14.0F;

        // Brand Badge
        batcher.quad(cx, by, 182.0F, bh, ur::Color { 78U, 58U, 155U, 255U });
        batcher.quad(cx, by, 182.0F, 2.0F, ur::Color { 165U, 140U, 255U, 255U });
        draw_text(batcher, cx + 8.0F, by + 6.0F, "CHROMODYNAMIC",
                  ur::Color { 255U, 255U, 255U, 255U }, 1.0F, cx + 178.0F);
        draw_text(batcher, cx + 8.0F, by + 19.0F, "3D GAME STUDIO v1.0",
                  ur::Color { 205U, 225U, 255U, 255U }, 1.0F, cx + 178.0F);
        cx += 190.0F;

        // Level Template Switcher: [<] [LEVEL 01/04: ...] [>]
        if (draw_button(batcher, uw::Rect { cx, by, 26.0F, bh }, "<", ptr,
                        ur::Color { 34U, 42U, 62U, 255U }, ur::Color { 90U, 160U, 245U, 255U }, false, draw_text))
        {
            load_game_template((template_idx_ + 3U) % 4U);
        }
        cx += 30.0F;

        if (draw_button(batcher, uw::Rect { cx, by, 238.0F, bh }, level_title_, ptr,
                        ur::Color { 30U, 38U, 58U, 255U }, ur::Color { 75U, 135U, 220U, 255U }, false, draw_text))
        {
            load_game_template((template_idx_ + 1U) % 4U);
        }
        cx += 242.0F;

        if (draw_button(batcher, uw::Rect { cx, by, 26.0F, bh }, ">", ptr,
                        ur::Color { 34U, 42U, 62U, 255U }, ur::Color { 90U, 160U, 245U, 255U }, false, draw_text))
        {
            load_game_template((template_idx_ + 1U) % 4U);
        }
        cx += 36.0F;

        // PLAY / PAUSE / STOP Transport Controls (prominent center bar)
        if (draw_button(batcher, uw::Rect { cx, by, 118.0F, bh },
                        is_playing_ ? "PLAYING (F5)" : "PLAY GAME (F5)", ptr,
                        ur::Color { 32U, 88U, 54U, 255U },
                        ur::Color { 75U, 230U, 125U, 255U },
                        is_playing_ && !is_paused_, draw_text))
        {
            if (!is_playing_) { start_play(active_stage, pie_controller); }
            else if (is_paused_) { is_paused_ = false; }
        }
        cx += 123.0F;

        if (draw_button(batcher, uw::Rect { cx, by, 86.0F, bh }, "PAUSE (F6)", ptr,
                        ur::Color { 42U, 48U, 66U, 255U },
                        ur::Color { 245U, 195U, 70U, 255U },
                        is_playing_ && is_paused_, draw_text))
        {
            if (is_playing_) { is_paused_ = !is_paused_; }
        }
        cx += 91.0F;

        if (draw_button(batcher, uw::Rect { cx, by, 102.0F, bh }, "STOP/EDIT (F8)", ptr,
                        is_playing_ ? ur::Color { 135U, 42U, 48U, 255U } : ur::Color { 38U, 44U, 60U, 255U },
                        ur::Color { 245U, 90U, 95U, 255U },
                        false, draw_text))
        {
            if (is_playing_) { stop_play(active_stage, pie_controller); }
        }
        cx += 112.0F;

        // 3D Transform Gizmo Mode Selector: [W: MOVE] [E: ROTATE] [R: SCALE]
        if (draw_button(batcher, uw::Rect { cx, by, 72.0F, bh }, "W: MOVE", ptr,
                        ur::Color { 34U, 42U, 62U, 255U },
                        ur::Color { 95U, 185U, 255U, 255U },
                        io_gizmo_mode == 0, draw_text))
        {
            io_gizmo_mode = 0;
        }
        cx += 76.0F;

        if (draw_button(batcher, uw::Rect { cx, by, 76.0F, bh }, "E: ROTATE", ptr,
                        ur::Color { 34U, 42U, 62U, 255U },
                        ur::Color { 95U, 185U, 255U, 255U },
                        io_gizmo_mode == 1, draw_text))
        {
            io_gizmo_mode = 1;
        }
        cx += 80.0F;

        if (draw_button(batcher, uw::Rect { cx, by, 74.0F, bh }, "R: SCALE", ptr,
                        ur::Color { 34U, 42U, 62U, 255U },
                        ur::Color { 95U, 185U, 255U, 255U },
                        io_gizmo_mode == 2, draw_text))
        {
            io_gizmo_mode = 2;
        }
        cx += 84.0F;

        // Save / Load / Reset / Help on the right
        if (cx + 260.0F < r.w)
        {
            cx = r.w - 266.0F;
        }
        if (draw_button(batcher, uw::Rect { cx, by, 68.0F, bh }, "SAVE LVL", ptr,
                        ur::Color { 32U, 64U, 92U, 255U },
                        ur::Color { 95U, 200U, 255U, 255U }, false, draw_text))
        {
            (void)save_level_to_disk("levels/custom_game.cdstage");
        }
        cx += 72.0F;

        if (draw_button(batcher, uw::Rect { cx, by, 68.0F, bh }, "LOAD LVL", ptr,
                        ur::Color { 34U, 52U, 82U, 255U },
                        ur::Color { 95U, 200U, 255U, 255U }, false, draw_text))
        {
            (void)load_level_from_disk("levels/custom_game.cdstage");
        }
        cx += 72.0F;

        if (draw_button(batcher, uw::Rect { cx, by, 56.0F, bh }, "RESET", ptr,
                        ur::Color { 56U, 42U, 58U, 255U },
                        ur::Color { 235U, 125U, 95U, 255U }, false, draw_text))
        {
            load_game_template(template_idx_);
            add_log("[RESET] Reloaded clean template '" + level_title_ + "'.", ur::Color { 255U, 205U, 95U, 255U });
        }
        cx += 60.0F;

        if (draw_button(batcher, uw::Rect { cx, by, 54.0F, bh }, "HELP F1", ptr,
                        ur::Color { 48U, 42U, 82U, 255U },
                        ur::Color { 185U, 145U, 255U, 255U }, show_help_modal_, draw_text))
        {
            show_help_modal_ = !show_help_modal_;
        }
    }

    // ---- 2. LEFT DOCK: SCENE HIERARCHY + GAME OBJECT SPAWNER ----------------
    void draw_left_dock(ur::DrawBatcher&                batcher,
                        const uw::Rect&                 r,
                        const uw::PointerState&         ptr,
                        cd::element_core::Stage&        active_stage,
                        cd::editor::AddElementCatalog&  add_catalog,
                        TextFn                          draw_text)
    {
        batcher.quad(r.x, r.y, r.w, r.h, ur::Color { 18U, 22U, 32U, 255U });
        batcher.quad(r.x + r.w - 1.0F, r.y, 1.0F, r.h, ur::Color { 52U, 64U, 92U, 255U });

        // Spawner section height at the bottom of Left Dock
        constexpr float kSpawnerH = 192.0F;
        const float     hier_h    = std::max(120.0F, r.h - kSpawnerH);

        // --- A. SCENE HIERARCHY HEADER ---
        batcher.quad(r.x, r.y, r.w - 1.0F, 28.0F, ur::Color { 26U, 33U, 48U, 255U });
        batcher.quad(r.x, r.y + 27.0F, r.w - 1.0F, 1.0F, ur::Color { 75U, 130U, 225U, 220U });
        const std::string hdr = "SCENE HIERARCHY (" + std::to_string(objects_.size()) + " OBJECTS)";
        draw_text(batcher, r.x + 10.0F, r.y + 10.0F, hdr,
                  ur::Color { 235U, 242U, 255U, 255U }, 1.0F, r.x + r.w - 8.0F);

        // Object list rows
        float ry = r.y + 32.0F;
        const float list_bottom = r.y + hier_h - 34.0F;
        constexpr float kRowH = 22.0F;

        if (selected_idx_ >= objects_.size() && !objects_.empty())
        {
            selected_idx_ = 0U;
        }

        const std::size_t max_vis = static_cast<std::size_t>(std::max(1.0F, (list_bottom - ry) / kRowH));
        std::size_t start_i = 0U;
        if (selected_idx_ >= max_vis)
        {
            start_i = selected_idx_ - max_vis + 1U;
        }

        for (std::size_t i = start_i; i < objects_.size(); ++i)
        {
            if (ry + kRowH > list_bottom) { break; }
            const auto& o = objects_[i];
            const uw::Rect row_r { r.x + 6.0F, ry, r.w - 13.0F, kRowH - 2.0F };
            const bool hov = row_r.contains(ptr.mouse_x, ptr.mouse_y);
            if (hov && ptr.left_pressed)
            {
                selected_idx_ = i;
            }
            const bool sel = (i == selected_idx_);

            const ur::Color bg =
                sel ? ur::Color { 48U,  82U, 152U, 255U } :
                hov ? ur::Color { 34U,  44U,  66U, 255U } :
                      ur::Color { 23U,  28U,  40U, 255U };
            batcher.quad(row_r.x, row_r.y, row_r.w, row_r.h, bg);
            if (sel)
            {
                batcher.quad(row_r.x, row_r.y, 3.0F, row_r.h, ur::Color { 100U, 210U, 255U, 255U });
            }

            // Role badge pill
            const ur::Color bc = role_badge_color(o.role);
            batcher.quad(row_r.x + 6.0F, row_r.y + 3.0F, 38.0F, row_r.h - 6.0F, bc);
            draw_text(batcher, row_r.x + 9.0F, row_r.y + 6.0F, role_badge_tag(o.role),
                      ur::Color { 12U, 15U, 22U, 255U }, 1.0F, row_r.x + 43.0F);

            // Object name
            draw_text(batcher, row_r.x + 50.0F, row_r.y + 6.0F, o.name,
                      sel ? ur::Color { 255U, 255U, 255U, 255U }
                          : (o.active ? ur::Color { 215U, 226U, 245U, 255U }
                                      : ur::Color { 120U, 130U, 150U, 255U }),
                      1.0F, row_r.x + row_r.w - 58.0F);

            // Y height badge on right of row
            char pos_b[24];
            std::snprintf(pos_b, sizeof(pos_b), "Y:%.1f", static_cast<double>(o.pos[1]));
            draw_text(batcher, row_r.x + row_r.w - 48.0F, row_r.y + 6.0F, pos_b,
                      ur::Color { 150U, 175U, 210U, 255U }, 1.0F, row_r.x + row_r.w - 4.0F);

            ry += kRowH;
        }

        // Hierarchy Action Buttons: [+ DUPLICATE] [FOCUS (F)] [DELETE]
        const float act_y = r.y + hier_h - 30.0F;
        const float bw3   = (r.w - 20.0F) / 3.0F;
        if (draw_button(batcher, uw::Rect { r.x + 6.0F, act_y, bw3, 24.0F }, "+ DUPLICATE", ptr,
                        ur::Color { 32U, 52U, 82U, 255U }, ur::Color { 90U, 180U, 255U, 255U }, false, draw_text))
        {
            duplicate_selected();
        }
        if (draw_button(batcher, uw::Rect { r.x + 9.0F + bw3, act_y, bw3, 24.0F }, "FOCUS (F)", ptr,
                        ur::Color { 32U, 52U, 82U, 255U }, ur::Color { 90U, 180U, 255U, 255U }, false, draw_text))
        {
            focus_selected_object();
        }
        if (draw_button(batcher, uw::Rect { r.x + 12.0F + bw3 * 2.0F, act_y, bw3, 24.0F }, "DELETE", ptr,
                        ur::Color { 78U, 34U, 42U, 255U }, ur::Color { 245U, 90U, 95U, 255U }, false, draw_text))
        {
            delete_selected();
        }

        // --- B. ADD 3D GAME OBJECT SPAWNER ---
        const float sp_y = r.y + hier_h;
        batcher.quad(r.x, sp_y, r.w - 1.0F, 28.0F, ur::Color { 26U, 33U, 48U, 255U });
        batcher.quad(r.x, sp_y, r.w - 1.0F, 1.0F, ur::Color { 65U, 82U, 115U, 255U });
        batcher.quad(r.x, sp_y + 27.0F, r.w - 1.0F, 1.0F, ur::Color { 75U, 210U, 145U, 220U });
        draw_text(batcher, r.x + 10.0F, sp_y + 10.0F, "CREATE 3D GAME OBJECT (CLICK TO ADD)",
                  ur::Color { 130U, 245U, 175U, 255U }, 1.0F, r.x + r.w - 8.0F);

        struct SpawnBtnSpec
        {
            const char* label;
            GameRole    role;
            ur::Color   bg;
        };
        static constexpr std::array<SpawnBtnSpec, 8> kSpawnBtns {{
            { "+ PLATFORM",    GameRole::kPlatform, ur::Color { 34U, 56U, 92U, 255U } },
            { "+ ELEVATOR",    GameRole::kElevator, ur::Color { 26U, 72U, 74U, 255U } },
            { "+ PHYS CRATE",  GameRole::kCrate,    ur::Color { 82U, 56U, 28U, 255U } },
            { "+ BOUNCE PAD",  GameRole::kJumpPad,  ur::Color { 62U, 38U, 96U, 255U } },
            { "+ ENEMY DRONE", GameRole::kEnemy,    ur::Color { 88U, 32U, 40U, 255U } },
            { "+ GOAL COIN",   GameRole::kCrystal,  ur::Color { 84U, 72U, 24U, 255U } },
            { "+ 3D ROVER",    GameRole::kVehicle,  ur::Color { 34U, 74U, 42U, 255U } },
            { "+ POINT LIGHT", GameRole::kLight,    ur::Color { 78U, 72U, 34U, 255U } },
        }};

        const float col_w = (r.w - 18.0F) * 0.5F;
        for (std::size_t b = 0U; b < kSpawnBtns.size(); ++b)
        {
            const float bx = r.x + 6.0F + static_cast<float>(b % 2U) * (col_w + 5.0F);
            const float by = sp_y + 34.0F + static_cast<float>(b / 2U) * 37.0F;
            if (draw_button(batcher, uw::Rect { bx, by, col_w, 32.0F },
                            kSpawnBtns[b].label, ptr,
                            kSpawnBtns[b].bg, ur::Color { 95U, 225U, 155U, 255U },
                            false, draw_text))
            {
                spawn_object(kSpawnBtns[b].role, &active_stage, &add_catalog);
            }
        }
    }

    // ---- 3. CENTER 3D VIEWPORT (TRUE PERSPECTIVE 3D MESHES + GIZMO + PIE) ---
    void draw_3d_viewport(ur::DrawBatcher&        batcher,
                          const uw::Rect&         r,
                          const uw::PointerState& ptr,
                          int&                    io_gizmo_mode,
                          TextFn                  draw_text,
                          LineFn                  draw_line)
    {
        constexpr float kHdrH  = 28.0F;
        constexpr float kFootH = 26.0F;
        const uw::Rect canvas {
            r.x + 2.0F,
            r.y + kHdrH,
            std::max(64.0F, r.w - 4.0F),
            std::max(64.0F, r.h - kHdrH - kFootH)
        };

        // Scissor clip to viewport canvas so 3D polygons never bleed into docks
        batcher.push_scissor(ur::ScissorRect {
            static_cast<std::int32_t>(canvas.x),
            static_cast<std::int32_t>(canvas.y),
            static_cast<std::uint32_t>(canvas.w),
            static_cast<std::uint32_t>(canvas.h)
        });

        // 3.1 Sky & Horizon Gradient
        const float horizon_ratio = std::clamp(0.46F - (cam_pitch_ - 0.35F) * 0.45F, 0.18F, 0.78F);
        const float horizon_y     = canvas.y + canvas.h * horizon_ratio;
        for (int b = 0; b < 8; ++b)
        {
            const float t0 = static_cast<float>(b) / 8.0F;
            const float t1 = static_cast<float>(b + 1) / 8.0F;
            const float y0 = canvas.y + (horizon_y - canvas.y) * t0;
            const float y1 = canvas.y + (horizon_y - canvas.y) * t1;
            batcher.quad(canvas.x, y0, canvas.w, y1 - y0 + 1.0F,
                         ur::Color {
                             static_cast<std::uint8_t>(14.0F + t0 * 36.0F),
                             static_cast<std::uint8_t>(22.0F + t0 * 54.0F),
                             static_cast<std::uint8_t>(44.0F + t0 * 82.0F),
                             255U
                         });
        }
        batcher.quad(canvas.x, horizon_y - 2.0F, canvas.w, 4.0F, ur::Color { 95U, 165U, 235U, 195U });

        for (int b = 0; b < 8; ++b)
        {
            const float t0 = static_cast<float>(b) / 8.0F;
            const float t1 = static_cast<float>(b + 1) / 8.0F;
            const float y0 = horizon_y + (canvas.y + canvas.h - horizon_y) * t0;
            const float y1 = horizon_y + (canvas.y + canvas.h - horizon_y) * t1;
            batcher.quad(canvas.x, y0, canvas.w, y1 - y0 + 1.0F,
                         ur::Color {
                             static_cast<std::uint8_t>(28.0F - t0 * 11.0F),
                             static_cast<std::uint8_t>(35.0F - t0 * 13.0F),
                             static_cast<std::uint8_t>(48.0F - t0 * 16.0F),
                             255U
                         });
        }

        // 3.2 3D Camera Basis Vectors
        const float cy_yaw = std::cos(cam_yaw_);
        const float sy_yaw = std::sin(cam_yaw_);
        const float cp_pit = std::cos(cam_pitch_);
        const float sp_pit = std::sin(cam_pitch_);

        const float cam_x = cam_target_[0] + cam_dist_ * cp_pit * sy_yaw;
        const float cam_y = cam_target_[1] + cam_dist_ * sp_pit;
        const float cam_z = cam_target_[2] + cam_dist_ * cp_pit * cy_yaw;

        float fx = cam_target_[0] - cam_x;
        float fy = cam_target_[1] - cam_y;
        float fz = cam_target_[2] - cam_z;
        const float flen = std::max(1e-4F, std::sqrt(fx * fx + fy * fy + fz * fz));
        fx /= flen; fy /= flen; fz /= flen;

        float rx = -fz;
        float ry = 0.0F;
        float rz = fx;
        const float rlen = std::max(1e-4F, std::sqrt(rx * rx + rz * rz));
        rx /= rlen; rz /= rlen;

        const float ux = ry * fz - rz * fy;
        const float uy = rz * fx - rx * fz;
        const float uz = rx * fy - ry * fx;

        const float fov_scale = canvas.h * 0.90F;
        const float cx_scr    = canvas.x + canvas.w * 0.5F;
        const float cy_scr    = canvas.y + canvas.h * 0.5F;

        auto project_3d = [&](float wx, float wy, float wz,
                              float& out_sx, float& out_sy, float& out_vz) noexcept -> bool {
            const float dx = wx - cam_x;
            const float dy = wy - cam_y;
            const float dz = wz - cam_z;
            const float vz = dx * fx + dy * fy + dz * fz;
            if (vz <= 0.35F) { return false; }
            const float vx = dx * rx + dy * ry + dz * rz;
            const float vy = dx * ux + dy * uy + dz * uz;
            out_sx = cx_scr + (vx / vz) * fov_scale;
            out_sy = cy_scr - (vy / vz) * fov_scale;
            out_vz = vz;
            return true;
        };

        // 3.3 Perspective 3D Ground Grid (y = 0)
        if (show_grid_)
        {
            for (int g = -14; g <= 14; ++g)
            {
                const float coord = static_cast<float>(g) * 1.0F;
                float x0 = 0.0F, y0 = 0.0F, z0 = 0.0F;
                float x1 = 0.0F, y1 = 0.0F, z1 = 0.0F;
                if (project_3d(coord, 0.0F, -16.0F, x0, y0, z0) &&
                    project_3d(coord, 0.0F,  10.0F, x1, y1, z1))
                {
                    const ur::Color c = (g == 0)
                        ? ur::Color { 75U, 150U, 255U, 210U }
                        : ur::Color { 52U,  64U,  86U, 115U };
                    draw_line(batcher, x0, y0, x1, y1, (g == 0) ? 2.0F : 1.0F, c);
                }
                if (project_3d(-14.0F, 0.0F, coord - 3.0F, x0, y0, z0) &&
                    project_3d( 14.0F, 0.0F, coord - 3.0F, x1, y1, z1))
                {
                    const ur::Color c = (g == 3)
                        ? ur::Color { 240U, 85U,  85U, 210U }
                        : ur::Color { 52U,  64U,  86U, 115U };
                    draw_line(batcher, x0, y0, x1, y1, (g == 3) ? 2.0F : 1.0F, c);
                }
            }
        }

        // Helper: Draw a true 6-face perspective-projected 3D oriented box
        auto draw_3d_box = [&](float bx, float by, float bz,
                               float sx, float sy, float sz,
                               float yaw_deg,
                               ur::Color base_col,
                               float metallic,
                               float roughness,
                               bool highlight_edges) {
            const float hx = sx * 0.5F;
            const float hy = sy * 0.5F;
            const float hz = sz * 0.5F;
            const float rad = yaw_deg * (3.14159265F / 180.0F);
            const float cy  = std::cos(rad);
            const float syw = std::sin(rad);

            auto to_world = [&](float lx, float ly, float lz) noexcept -> std::array<float, 3> {
                return {
                    bx + lx * cy + lz * syw,
                    by + ly,
                    bz - lx * syw + lz * cy
                };
            };

            const std::array<std::array<float, 3>, 8> wv {{
                to_world(-hx, -hy, -hz), // 0: L B K
                to_world( hx, -hy, -hz), // 1: R B K
                to_world( hx, -hy,  hz), // 2: R B F
                to_world(-hx, -hy,  hz), // 3: L B F
                to_world(-hx,  hy, -hz), // 4: L T K
                to_world( hx,  hy, -hz), // 5: R T K
                to_world( hx,  hy,  hz), // 6: R T F
                to_world(-hx,  hy,  hz), // 7: L T F
            }};

            std::array<float, 8> px {}, py {}, pz {};
            for (std::size_t k = 0U; k < 8U; ++k)
            {
                if (!project_3d(wv[k][0], wv[k][1], wv[k][2], px[k], py[k], pz[k]))
                {
                    return;
                }
            }

            struct FaceDef
            {
                std::array<std::size_t, 4> idx;
                float                      nx, ny, nz;
                float                      dist;
            };
            std::array<FaceDef, 6> faces {{
                { { 4, 5, 6, 7 },  0.0F,  1.0F,  0.0F, 0.0F }, // Top (+Y)
                { { 0, 3, 2, 1 },  0.0F, -1.0F,  0.0F, 0.0F }, // Bottom (-Y)
                { { 7, 6, 2, 3 },  syw,   0.0F,  cy,   0.0F }, // Front (+Z)
                { { 5, 4, 0, 1 }, -syw,   0.0F, -cy,   0.0F }, // Back (-Z)
                { { 6, 5, 1, 2 },  cy,    0.0F, -syw,  0.0F }, // Right (+X)
                { { 4, 7, 3, 0 }, -cy,    0.0F,  syw,  0.0F }, // Left (-X)
            }};

            for (auto& f : faces)
            {
                const float fcx = 0.25F * (wv[f.idx[0]][0] + wv[f.idx[1]][0] + wv[f.idx[2]][0] + wv[f.idx[3]][0]);
                const float fcy = 0.25F * (wv[f.idx[0]][1] + wv[f.idx[1]][1] + wv[f.idx[2]][1] + wv[f.idx[3]][1]);
                const float fcz = 0.25F * (wv[f.idx[0]][2] + wv[f.idx[1]][2] + wv[f.idx[2]][2] + wv[f.idx[3]][2]);
                const float dx = fcx - cam_x;
                const float dy = fcy - cam_y;
                const float dz = fcz - cam_z;
                f.dist = dx * dx + dy * dy + dz * dz;
            }

            std::sort(faces.begin(), faces.end(), [](const FaceDef& a, const FaceDef& b) {
                return a.dist > b.dist;
            });

            // Sun direction normalized: (-0.45, 0.80, 0.40)
            for (const auto& f : faces)
            {
                const float fcx = 0.25F * (wv[f.idx[0]][0] + wv[f.idx[1]][0] + wv[f.idx[2]][0] + wv[f.idx[3]][0]);
                const float fcy = 0.25F * (wv[f.idx[0]][1] + wv[f.idx[1]][1] + wv[f.idx[2]][1] + wv[f.idx[3]][1]);
                const float fcz = 0.25F * (wv[f.idx[0]][2] + wv[f.idx[1]][2] + wv[f.idx[2]][2] + wv[f.idx[3]][2]);
                // Backface culling against camera ray
                const float vdot = (fcx - cam_x) * f.nx + (fcy - cam_y) * f.ny + (fcz - cam_z) * f.nz;
                if (vdot >= 0.0F) { continue; }

                const float ndotl = std::max(0.0F, f.nx * -0.45F + f.ny * 0.80F + f.nz * 0.40F);
                const float sky   = 0.38F + 0.18F * (f.ny * 0.5F + 0.5F);
                const float spec  = (f.ny > 0.5F) ? ((1.0F - roughness) * (0.20F + 0.25F * metallic)) : 0.0F;
                const float lit   = std::clamp(sky + ndotl * 0.65F, 0.28F, 1.35F);

                const ur::Color fc {
                    static_cast<std::uint8_t>(std::clamp(base_col.r * lit + spec * 220.0F, 0.0F, 255.0F)),
                    static_cast<std::uint8_t>(std::clamp(base_col.g * lit + spec * 230.0F, 0.0F, 255.0F)),
                    static_cast<std::uint8_t>(std::clamp(base_col.b * lit + spec * 255.0F, 0.0F, 255.0F)),
                    base_col.a
                };

                batcher.quad_poly(px[f.idx[0]], py[f.idx[0]],
                                  px[f.idx[1]], py[f.idx[1]],
                                  px[f.idx[2]], py[f.idx[2]],
                                  px[f.idx[3]], py[f.idx[3]], fc);

                const ur::Color edge_c = highlight_edges
                    ? ur::Color { 255U, 215U, 70U, 255U }
                    : ur::Color {
                          static_cast<std::uint8_t>(fc.r / 2U),
                          static_cast<std::uint8_t>(fc.g / 2U),
                          static_cast<std::uint8_t>(fc.b / 2U),
                          190U
                      };
                const float edge_w = highlight_edges ? 2.0F : 1.0F;
                for (std::size_t e = 0U; e < 4U; ++e)
                {
                    const std::size_t a = f.idx[e];
                    const std::size_t b = f.idx[(e + 1U) % 4U];
                    draw_line(batcher, px[a], py[a], px[b], py[b], edge_w, edge_c);
                }
            }
        };

        // Helper: Draw a 3D Spinning Octahedron (for Crystals & Lights)
        auto draw_3d_octahedron = [&](float ox, float oy, float oz,
                                      float rx_s, float ry_s, float spin_rad,
                                      ur::Color col, bool sel) {
            float tx = 0.0F, ty = 0.0F, tz = 0.0F;
            float bx = 0.0F, by = 0.0F, bz = 0.0F;
            if (!project_3d(ox, oy + ry_s, oz, tx, ty, tz) ||
                !project_3d(ox, oy - ry_s, oz, bx, by, bz))
            {
                return;
            }
            std::array<float, 4> ex {}, ey {}, ez {};
            for (std::size_t k = 0U; k < 4U; ++k)
            {
                const float a = spin_rad + static_cast<float>(k) * 1.5707963F;
                if (!project_3d(ox + std::cos(a) * rx_s, oy, oz + std::sin(a) * rx_s, ex[k], ey[k], ez[k]))
                {
                    return;
                }
            }
            for (std::size_t k = 0U; k < 4U; ++k)
            {
                const std::size_t nk = (k + 1U) % 4U;
                const float shade = (k % 2U == 0U) ? 1.15F : 0.82F;
                const ur::Color top_c {
                    static_cast<std::uint8_t>(std::clamp(col.r * shade + 25.0F, 0.0F, 255.0F)),
                    static_cast<std::uint8_t>(std::clamp(col.g * shade + 25.0F, 0.0F, 255.0F)),
                    static_cast<std::uint8_t>(std::clamp(col.b * shade + 25.0F, 0.0F, 255.0F)),
                    245U
                };
                const ur::Color bot_c {
                    static_cast<std::uint8_t>(col.r * 0.65F),
                    static_cast<std::uint8_t>(col.g * 0.65F),
                    static_cast<std::uint8_t>(col.b * 0.65F),
                    245U
                };
                batcher.triangle(tx, ty, ex[k], ey[k], ex[nk], ey[nk], top_c);
                batcher.triangle(bx, by, ex[nk], ey[nk], ex[k], ey[k], bot_c);
                if (sel)
                {
                    draw_line(batcher, tx, ty, ex[k], ey[k], 2.0F, ur::Color { 255U, 235U, 95U, 255U });
                    draw_line(batcher, bx, by, ex[k], ey[k], 2.0F, ur::Color { 255U, 235U, 95U, 255U });
                }
            }
        };

        // 3.4 Sort all active objects back-to-front (Painter's Algorithm)
        struct DrawEntry
        {
            std::size_t idx;
            float       dist_sq;
        };
        std::vector<DrawEntry> order;
        order.reserve(objects_.size());
        for (std::size_t i = 0U; i < objects_.size(); ++i)
        {
            if (!objects_[i].active) { continue; }
            const float dx = objects_[i].pos[0] - cam_x;
            const float dy = objects_[i].pos[1] - cam_y;
            const float dz = objects_[i].pos[2] - cam_z;
            order.push_back(DrawEntry { i, dx * dx + dy * dy + dz * dz });
        }
        std::sort(order.begin(), order.end(), [](const DrawEntry& a, const DrawEntry& b) {
            return a.dist_sq > b.dist_sq;
        });

        // Check if mouse is clicking a Gizmo axis before object picking
        bool gizmo_consumed_click = false;
        if (!is_playing_ && selected_idx_ < objects_.size())
        {
            const auto& sel_o = objects_[selected_idx_];
            float ox = 0.0F, oy = 0.0F, oz = 0.0F;
            if (project_3d(sel_o.pos[0], sel_o.pos[1], sel_o.pos[2], ox, oy, oz))
            {
                const float giz_len = std::max(1.4F, std::max({ sel_o.scale[0], sel_o.scale[1], sel_o.scale[2] }) * 0.55F + 0.85F);
                const std::array<std::array<float, 3>, 3> axes {{
                    { sel_o.pos[0] + giz_len, sel_o.pos[1],           sel_o.pos[2] },
                    { sel_o.pos[0],           sel_o.pos[1] + giz_len, sel_o.pos[2] },
                    { sel_o.pos[0],           sel_o.pos[1],           sel_o.pos[2] + giz_len },
                }};

                auto dist_to_seg = [](float px, float py, float x0, float y0, float x1, float y1) noexcept -> float {
                    const float vx = x1 - x0;
                    const float vy = y1 - y0;
                    const float l2 = vx * vx + vy * vy;
                    if (l2 < 1.0F) { return std::hypot(px - x0, py - y0); }
                    const float t = std::clamp(((px - x0) * vx + (py - y0) * vy) / l2, 0.0F, 1.0F);
                    return std::hypot(px - (x0 + t * vx), py - (y0 + t * vy));
                };

                hovered_gizmo_axis_ = -1;
                float best_d = 14.0F;
                std::array<float, 3> ax_scr_x {}, ax_scr_y {};
                for (std::size_t a = 0U; a < 3U; ++a)
                {
                    float az = 0.0F;
                    if (project_3d(axes[a][0], axes[a][1], axes[a][2], ax_scr_x[a], ax_scr_y[a], az))
                    {
                        const float d = dist_to_seg(ptr.mouse_x, ptr.mouse_y, ox, oy, ax_scr_x[a], ax_scr_y[a]);
                        if (d < best_d)
                        {
                            best_d = d;
                            hovered_gizmo_axis_ = static_cast<int>(a);
                        }
                    }
                }

                if (ptr.left_pressed && hovered_gizmo_axis_ != -1 && canvas.contains(ptr.mouse_x, ptr.mouse_y))
                {
                    dragging_gizmo_axis_ = hovered_gizmo_axis_;
                    drag_start_mouse_x_  = ptr.mouse_x;
                    drag_start_mouse_y_  = ptr.mouse_y;
                    const auto dg        = static_cast<std::size_t>(dragging_gizmo_axis_);
                    if (io_gizmo_mode == 0)      { drag_start_val_ = objects_[selected_idx_].pos[dg]; }
                    else if (io_gizmo_mode == 1) { drag_start_val_ = objects_[selected_idx_].yaw_deg; }
                    else                         { drag_start_val_ = objects_[selected_idx_].scale[dg]; }
                    gizmo_consumed_click = true;
                }

                if (!ptr.left_down)
                {
                    dragging_gizmo_axis_ = -1;
                }
                else if (dragging_gizmo_axis_ != -1)
                {
                    const auto a = static_cast<std::size_t>(dragging_gizmo_axis_);
                    float dir_x = ax_scr_x[a] - ox;
                    float dir_y = ax_scr_y[a] - oy;
                    const float dlen = std::max(1.0F, std::hypot(dir_x, dir_y));
                    dir_x /= dlen;
                    dir_y /= dlen;
                    const float proj_px = (ptr.mouse_x - drag_start_mouse_x_) * dir_x +
                                          (ptr.mouse_y - drag_start_mouse_y_) * dir_y;
                    auto& target_o = objects_[selected_idx_];
                    if (io_gizmo_mode == 0)
                    {
                        target_o.pos[a]       = drag_start_val_ + proj_px * 0.025F;
                        target_o.spawn_pos[a] = target_o.pos[a];
                    }
                    else if (io_gizmo_mode == 1)
                    {
                        target_o.yaw_deg = drag_start_val_ + (ptr.mouse_x - drag_start_mouse_x_) * 0.65F;
                    }
                    else
                    {
                        target_o.scale[a] = std::clamp(drag_start_val_ + proj_px * 0.025F, 0.25F, 24.0F);
                    }
                    gizmo_consumed_click = true;
                }
            }
        }

        // 3.5 Draw each 3D Object in Depth-Sorted Order
        std::optional<std::size_t> clicked_obj_idx;
        for (const auto& entry : order)
        {
            const std::size_t i = entry.idx;
            const auto& o       = objects_[i];
            const bool is_sel   = (!is_playing_ && i == selected_idx_);

            // Ground drop shadow under elevated objects
            if (o.pos[1] - o.scale[1] * 0.5F > 0.05F && o.role != GameRole::kPlatform)
            {
                draw_3d_box(o.pos[0], 0.02F, o.pos[2],
                            o.scale[0] * 0.85F, 0.02F, o.scale[2] * 0.85F,
                            o.yaw_deg, ur::Color { 8U, 10U, 16U, 150U }, 0.0F, 1.0F, false);
            }

            if (o.role == GameRole::kHero)
            {
                // Glowing ground landing ring under Hero
                draw_3d_box(o.pos[0], 0.03F, o.pos[2],
                            0.95F, 0.03F, 0.95F, 0.0F,
                            ur::Color { 55U, 220U, 255U, 180U }, 0.8F, 0.2F, false);

                const float swing = is_playing_ ? std::sin(hero_walk_phase_) * 0.16F : 0.0F;
                const float rad   = o.yaw_deg * (3.14159265F / 180.0F);
                const float cy    = std::cos(rad);
                const float syw   = std::sin(rad);
                const float base_y = o.pos[1] - o.scale[1] * 0.5F;

                // Left & Right 3D Legs
                draw_3d_box(o.pos[0] - 0.16F * cy + swing * syw, base_y + 0.28F, o.pos[2] + 0.16F * syw + swing * cy,
                            0.22F, 0.52F, 0.24F, o.yaw_deg, ur::Color { 38U, 115U, 185U, 255U }, 0.4F, 0.4F, is_sel);
                draw_3d_box(o.pos[0] + 0.16F * cy - swing * syw, base_y + 0.28F, o.pos[2] - 0.16F * syw - swing * cy,
                            0.22F, 0.52F, 0.24F, o.yaw_deg, ur::Color { 38U, 115U, 185U, 255U }, 0.4F, 0.4F, is_sel);
                // 3D Torso Armor
                draw_3d_box(o.pos[0], base_y + 0.82F, o.pos[2],
                            0.52F, 0.58F, 0.34F, o.yaw_deg, o.color, o.metallic, o.roughness, is_sel);
                // 3D Head + Glowing Cyan Visor
                draw_3d_box(o.pos[0], base_y + 1.28F, o.pos[2],
                            0.38F, 0.36F, 0.38F, o.yaw_deg, ur::Color { 230U, 240U, 255U, 255U }, 0.6F, 0.2F, is_sel);
                draw_3d_box(o.pos[0] + 0.16F * syw, base_y + 1.30F, o.pos[2] + 0.16F * cy,
                            0.30F, 0.14F, 0.12F, o.yaw_deg, ur::Color { 50U, 245U, 255U, 255U }, 0.9F, 0.1F, false);
            }
            else if (o.role == GameRole::kCrystal)
            {
                const float bob  = std::sin(anim_time_ * 3.2F + static_cast<float>(i)) * 0.14F;
                const float spin = anim_time_ * 2.2F + static_cast<float>(i);
                draw_3d_octahedron(o.pos[0], o.pos[1] + bob, o.pos[2],
                                   o.scale[0] * 0.55F, o.scale[1] * 0.60F, spin, o.color, is_sel);
            }
            else if (o.role == GameRole::kLight)
            {
                draw_3d_octahedron(o.pos[0], o.pos[1], o.pos[2],
                                   0.45F, 0.45F, anim_time_ * 0.8F, o.color, is_sel);
            }
            else if (o.role == GameRole::kEnemy)
            {
                // Patrol range line on ground
                float px0 = 0.0F, py0 = 0.0F, pz0 = 0.0F;
                float px1 = 0.0F, py1 = 0.0F, pz1 = 0.0F;
                if (project_3d(o.spawn_pos[0] - o.patrol_range, o.pos[1] - 0.4F, o.pos[2], px0, py0, pz0) &&
                    project_3d(o.spawn_pos[0] + o.patrol_range, o.pos[1] - 0.4F, o.pos[2], px1, py1, pz1))
                {
                    draw_line(batcher, px0, py0, px1, py1, 1.5F, ur::Color { 245U, 75U, 85U, 130U });
                }
                draw_3d_box(o.pos[0], o.pos[1], o.pos[2],
                            o.scale[0], o.scale[1] * 0.75F, o.scale[2],
                            o.yaw_deg, o.color, 0.65F, 0.25F, is_sel);
                // Red glowing scanner visor on top
                draw_3d_box(o.pos[0], o.pos[1] + o.scale[1] * 0.45F, o.pos[2],
                            o.scale[0] * 0.65F, 0.22F, o.scale[2] * 0.65F,
                            anim_time_ * 120.0F, ur::Color { 255U, 215U, 70U, 255U }, 0.9F, 0.1F, false);
            }
            else if (o.role == GameRole::kVehicle)
            {
                // Chassis + Cabin + Wheels
                draw_3d_box(o.pos[0], o.pos[1] - 0.1F, o.pos[2],
                            o.scale[0], o.scale[1] * 0.55F, o.scale[2],
                            o.yaw_deg, o.color, 0.7F, 0.25F, is_sel);
                draw_3d_box(o.pos[0], o.pos[1] + 0.32F, o.pos[2] - 0.15F,
                            o.scale[0] * 0.72F, o.scale[1] * 0.45F, o.scale[2] * 0.52F,
                            o.yaw_deg, ur::Color { 75U, 195U, 255U, 255U }, 0.9F, 0.1F, is_sel);
            }
            else
            {
                // Platform / Elevator / Crate / JumpPad
                draw_3d_box(o.pos[0], o.pos[1], o.pos[2],
                            o.scale[0], o.scale[1], o.scale[2],
                            o.yaw_deg, o.color, o.metallic, o.roughness, is_sel);
                if (o.role == GameRole::kJumpPad)
                {
                    const float pulse = 0.08F * std::sin(anim_time_ * 6.0F);
                    draw_3d_box(o.pos[0], o.pos[1] + o.scale[1] * 0.55F + pulse, o.pos[2],
                                o.scale[0] * 0.70F, 0.12F, o.scale[2] * 0.70F,
                                anim_time_ * 45.0F, ur::Color { 235U, 165U, 255U, 255U }, 0.9F, 0.1F, false);
                }
            }

            // Screen-space nameplate & click-to-select hit test
            float sx = 0.0F, sy = 0.0F, sz = 0.0F;
            if (project_3d(o.pos[0], o.pos[1] + o.scale[1] * 0.5F + 0.35F, o.pos[2], sx, sy, sz))
            {
                float cx_obj = 0.0F, cy_obj = 0.0F, cz_obj = 0.0F;
                (void)project_3d(o.pos[0], o.pos[1], o.pos[2], cx_obj, cy_obj, cz_obj);
                const float hit_rad = std::clamp((fov_scale / std::max(1.0F, cz_obj)) *
                                                 std::max({ o.scale[0], o.scale[1], o.scale[2] }) * 0.45F,
                                                 16.0F, 95.0F);
                if (!is_playing_ && !gizmo_consumed_click && ptr.left_pressed &&
                    canvas.contains(ptr.mouse_x, ptr.mouse_y) &&
                    std::hypot(ptr.mouse_x - cx_obj, ptr.mouse_y - cy_obj) <= hit_rad)
                {
                    clicked_obj_idx = i;
                }

                // Draw clean floating nameplate for Hero, Enemies, Crystals, Elevators, and Selected object
                if (is_sel || o.role == GameRole::kHero || o.role == GameRole::kEnemy || o.role == GameRole::kElevator)
                {
                    const float lbl_w = static_cast<float>(o.name.size()) * 6.0F + 12.0F;
                    const float lx    = sx - lbl_w * 0.5F;
                    const float ly    = sy - 14.0F;
                    batcher.quad(lx, ly, lbl_w, 14.0F,
                                 is_sel ? ur::Color { 42U, 78U, 155U, 240U }
                                        : ur::Color { 14U, 18U,  28U, 210U });
                    batcher.quad(lx, ly, lbl_w, 1.0F,
                                 is_sel ? ur::Color { 255U, 220U, 85U, 255U }
                                        : role_badge_color(o.role));
                    draw_text(batcher, lx + 6.0F, ly + 4.0F, o.name,
                              is_sel ? ur::Color { 255U, 245U, 145U, 255U }
                                     : ur::Color { 225U, 235U, 255U, 255U },
                              1.0F, lx + lbl_w - 2.0F);
                }
            }
        }

        if (clicked_obj_idx.has_value())
        {
            selected_idx_ = *clicked_obj_idx;
        }

        // 3.6 Draw 3D Transform Gizmo on Selected Object (in EDIT mode)
        if (!is_playing_ && selected_idx_ < objects_.size())
        {
            const auto& sel_o = objects_[selected_idx_];
            float ox = 0.0F, oy = 0.0F, oz = 0.0F;
            if (project_3d(sel_o.pos[0], sel_o.pos[1], sel_o.pos[2], ox, oy, oz))
            {
                const float giz_len = std::max(1.4F, std::max({ sel_o.scale[0], sel_o.scale[1], sel_o.scale[2] }) * 0.55F + 0.85F);
                struct AxisDraw
                {
                    int         axis;
                    float       wx, wy, wz;
                    const char* label;
                    ur::Color   col;
                };
                const std::array<AxisDraw, 3> axes {{
                    { 0, sel_o.pos[0] + giz_len, sel_o.pos[1],           sel_o.pos[2],           "X", ur::Color { 255U,  75U,  75U, 255U } },
                    { 1, sel_o.pos[0],           sel_o.pos[1] + giz_len, sel_o.pos[2],           "Y", ur::Color {  75U, 245U, 110U, 255U } },
                    { 2, sel_o.pos[0],           sel_o.pos[1],           sel_o.pos[2] + giz_len, "Z", ur::Color {  80U, 165U, 255U, 255U } },
                }};

                for (const auto& ad : axes)
                {
                    float ax = 0.0F, ay = 0.0F, az = 0.0F;
                    if (project_3d(ad.wx, ad.wy, ad.wz, ax, ay, az))
                    {
                        const bool active_ax = (dragging_gizmo_axis_ == ad.axis || hovered_gizmo_axis_ == ad.axis);
                        const ur::Color c = active_ax ? ur::Color { 255U, 240U, 80U, 255U } : ad.col;
                        draw_line(batcher, ox, oy, ax, ay, active_ax ? 4.0F : 3.0F, c);

                        // 3D Arrowhead triangle at tip
                        const float dx = ax - ox;
                        const float dy = ay - oy;
                        const float dl = std::max(1.0F, std::hypot(dx, dy));
                        const float ux_a = dx / dl;
                        const float uy_a = dy / dl;
                        const float px_a = -uy_a;
                        const float py_a =  ux_a;
                        batcher.triangle(ax + ux_a * 8.0F, ay + uy_a * 8.0F,
                                         ax - ux_a * 6.0F + px_a * 6.0F, ay - uy_a * 6.0F + py_a * 6.0F,
                                         ax - ux_a * 6.0F - px_a * 6.0F, ay - uy_a * 6.0F - py_a * 6.0F, c);
                        draw_text(batcher, ax + ux_a * 11.0F - 3.0F, ay + uy_a * 11.0F - 3.0F,
                                  ad.label, c, 1.0F, canvas.x + canvas.w);
                    }
                }
                batcher.quad(ox - 5.0F, oy - 5.0F, 10.0F, 10.0F, ur::Color { 255U, 245U, 150U, 255U });
            }
        }

        // 3.7 Play-In-Editor (PIE) Live Game HUD Overlay
        if (is_playing_)
        {
            std::size_t total_crystals = 0U;
            std::size_t collected      = 0U;
            for (const auto& o : objects_)
            {
                if (o.role == GameRole::kCrystal)
                {
                    ++total_crystals;
                    if (!o.active) { ++collected; }
                }
            }

            // Top-Left Gameplay HUD Card
            const float hx = canvas.x + 12.0F;
            const float hy = canvas.y + 12.0F;
            batcher.quad(hx, hy, 310.0F, 64.0F, ur::Color { 12U, 16U, 26U, 225U });
            batcher.quad(hx, hy, 310.0F, 2.0F, ur::Color { 75U, 235U, 135U, 255U });

            draw_text(batcher, hx + 10.0F, hy + 8.0F, "LIVE PLAY MODE (PIE)",
                      ur::Color { 95U, 245U, 145U, 255U }, 1.0F, hx + 300.0F);

            // Health bar
            draw_text(batcher, hx + 10.0F, hy + 24.0F, "HERO HP:",
                      ur::Color { 230U, 240U, 255U, 255U }, 1.0F, hx + 80.0F);
            batcher.quad(hx + 66.0F, hy + 23.0F, 120.0F, 10.0F, ur::Color { 35U, 42U, 58U, 255U });
            const float hp_w = 120.0F * (static_cast<float>(hero_hp_) / 100.0F);
            batcher.quad(hx + 66.0F, hy + 23.0F, hp_w, 10.0F,
                         hero_hp_ > 40 ? ur::Color { 65U, 225U, 120U, 255U }
                                       : ur::Color { 245U, 80U, 80U, 255U });

            char score_b[96];
            std::snprintf(score_b, sizeof(score_b), "SCORE: %05d   |   CRYSTALS: %zu / %zu",
                          score_, collected, total_crystals);
            draw_text(batcher, hx + 10.0F, hy + 42.0F, score_b,
                      ur::Color { 255U, 225U, 85U, 255U }, 1.0F, hx + 302.0F);

            // Stage Clear Celebration Banner!
            if (stage_clear_)
            {
                const float bw = 380.0F;
                const float bh = 88.0F;
                const float bx = canvas.x + (canvas.w - bw) * 0.5F;
                const float by = canvas.y + canvas.h * 0.22F;
                batcher.quad(bx, by, bw, bh, ur::Color { 16U, 36U, 28U, 245U });
                batcher.quad(bx, by, bw, 3.0F, ur::Color { 95U, 255U, 150U, 255U });
                draw_text(batcher, bx + 42.0F, by + 14.0F, "STAGE COMPLETE! VICTORY!",
                          ur::Color { 110U, 255U, 165U, 255U }, 2.0F, bx + bw - 10.0F);
                if (draw_button(batcher, uw::Rect { bx + 95.0F, by + 48.0F, 190.0F, 28.0F },
                                "PLAY NEXT LEVEL ->", ptr,
                                ur::Color { 38U, 110U, 72U, 255U },
                                ur::Color { 95U, 245U, 150U, 255U }, false, draw_text))
                {
                    load_game_template((template_idx_ + 1U) % 4U);
                }
            }
        }

        batcher.pop_scissor();

        // 3.8 Viewport Top Header Bar (Camera Presets & View Toggles)
        batcher.quad(r.x, r.y, r.w, kHdrH, ur::Color { 22U, 28U, 42U, 255U });
        batcher.quad(r.x, r.y + kHdrH - 1.0F, r.w, 1.0F, ur::Color { 65U, 84U, 122U, 255U });
        draw_text(batcher, r.x + 10.0F, r.y + 10.0F,
                  is_playing_ ? "3D GAME VIEWPORT [LIVE PLAY-IN-EDITOR]"
                              : "3D LEVEL VIEWPORT [PERSPECTIVE SHADED]",
                  is_playing_ ? ur::Color { 100U, 245U, 150U, 255U }
                              : ur::Color { 225U, 236U, 255U, 255U },
                  1.0F, r.x + 280.0F);

        float vbx = r.x + r.w - 352.0F;
        if (vbx > r.x + 240.0F)
        {
            if (draw_button(batcher, uw::Rect { vbx, r.y + 4.0F, 56.0F, 20.0F }, "PERSP", ptr,
                            ur::Color { 34U, 44U, 66U, 255U }, ur::Color { 90U, 175U, 250U, 255U }, false, draw_text))
            {
                cam_yaw_ = 0.42F; cam_pitch_ = 0.38F; cam_dist_ = 15.5F;
            }
            vbx += 60.0F;
            if (draw_button(batcher, uw::Rect { vbx, r.y + 4.0F, 48.0F, 20.0F }, "TOP", ptr,
                            ur::Color { 34U, 44U, 66U, 255U }, ur::Color { 90U, 175U, 250U, 255U }, false, draw_text))
            {
                cam_yaw_ = 0.0F; cam_pitch_ = 1.28F; cam_dist_ = 18.0F;
            }
            vbx += 52.0F;
            if (draw_button(batcher, uw::Rect { vbx, r.y + 4.0F, 52.0F, 20.0F }, "SIDE", ptr,
                            ur::Color { 34U, 44U, 66U, 255U }, ur::Color { 90U, 175U, 250U, 255U }, false, draw_text))
            {
                cam_yaw_ = 1.57F; cam_pitch_ = 0.18F; cam_dist_ = 16.0F;
            }
            vbx += 56.0F;
            if (draw_button(batcher, uw::Rect { vbx, r.y + 4.0F, 52.0F, 20.0F }, "ZOOM+", ptr,
                            ur::Color { 34U, 44U, 66U, 255U }, ur::Color { 90U, 175U, 250U, 255U }, false, draw_text))
            {
                cam_dist_ = std::max(4.0F, cam_dist_ - 2.2F);
            }
            vbx += 56.0F;
            if (draw_button(batcher, uw::Rect { vbx, r.y + 4.0F, 52.0F, 20.0F }, "ZOOM-", ptr,
                            ur::Color { 34U, 44U, 66U, 255U }, ur::Color { 90U, 175U, 250U, 255U }, false, draw_text))
            {
                cam_dist_ = std::min(34.0F, cam_dist_ + 2.2F);
            }
            vbx += 56.0F;
            if (draw_button(batcher, uw::Rect { vbx, r.y + 4.0F, 64.0F, 20.0F },
                            show_grid_ ? "GRID: ON" : "GRID: OFF", ptr,
                            ur::Color { 34U, 44U, 66U, 255U }, ur::Color { 90U, 175U, 250U, 255U }, show_grid_, draw_text))
            {
                show_grid_ = !show_grid_;
            }
        }

        // 3.9 Viewport Bottom Control Prompt Bar
        const float fy_bar = r.y + r.h - kFootH;
        batcher.quad(r.x, fy_bar, r.w, kFootH, ur::Color { 18U, 22U, 34U, 255U });
        batcher.quad(r.x, fy_bar, r.w, 1.0F, ur::Color { 52U, 66U, 96U, 255U });
        const std::string prompt = is_playing_
            ? "PLAY CONTROLS: [WASD / ARROWS] MOVE HERO  |  [SPACE] JUMP  |  [SHIFT] SPRINT  |  [RMB DRAG] ORBIT CAM  |  [F8] STOP"
            : "EDIT CONTROLS: [LMB CLICK] SELECT  |  [DRAG X/Y/Z GIZMO] MOVE/ROT/SCALE  |  [RMB DRAG] ORBIT 3D CAM  |  [SCROLL] ZOOM";
        draw_text(batcher, r.x + 10.0F, fy_bar + 9.0F, prompt,
                  is_playing_ ? ur::Color { 115U, 245U, 160U, 255U }
                              : ur::Color { 195U, 212U, 240U, 255U },
                  1.0F, r.x + r.w - 8.0F);
    }

    // ---- 4. RIGHT DOCK: INTERACTIVE PROPERTY INSPECTOR ----------------------
    void draw_right_inspector(ur::DrawBatcher&                    batcher,
                              const uw::Rect&                     r,
                              const uw::PointerState&             ptr,
                              cd::element_core::Stage&            active_stage,
                              cd::editor::PieWorkbenchController& pie_controller,
                              TextFn                              draw_text)
    {
        batcher.quad(r.x, r.y, r.w, r.h, ur::Color { 18U, 22U, 32U, 255U });
        batcher.quad(r.x, r.y, 1.0F, r.h, ur::Color { 52U, 64U, 92U, 255U });

        // Header
        batcher.quad(r.x + 1.0F, r.y, r.w - 1.0F, 28.0F, ur::Color { 26U, 33U, 48U, 255U });
        batcher.quad(r.x + 1.0F, r.y + 27.0F, r.w - 1.0F, 1.0F, ur::Color { 75U, 130U, 225U, 220U });
        draw_text(batcher, r.x + 10.0F, r.y + 10.0F, "OBJECT INSPECTOR & PROPERTIES",
                  ur::Color { 235U, 242U, 255U, 255U }, 1.0F, r.x + r.w - 8.0F);

        if (selected_idx_ >= objects_.size()) { return; }
        auto& o = objects_[selected_idx_];

        float iy = r.y + 34.0F;
        const float ix = r.x + 8.0F;
        const float iw = r.w - 16.0F;

        // Selected Object Name Card
        batcher.quad(ix, iy, iw, 42.0F, ur::Color { 25U, 32U, 48U, 255U });
        batcher.quad(ix, iy, 4.0F, 42.0F, role_badge_color(o.role));
        draw_text(batcher, ix + 10.0F, iy + 7.0F, "SELECTED: " + o.name,
                  ur::Color { 255U, 255U, 255U, 255U }, 1.0F, ix + iw - 6.0F);
        draw_text(batcher, ix + 10.0F, iy + 23.0F, role_full_name(o.role),
                  role_badge_color(o.role), 1.0F, ix + iw - 6.0F);
        iy += 48.0F;

        // Role Switcher Row: [< ROLE] [ROLE TAG] [ROLE >]
        if (draw_button(batcher, uw::Rect { ix, iy, 28.0F, 22.0F }, "<", ptr,
                        ur::Color { 34U, 44U, 64U, 255U }, ur::Color { 90U, 175U, 250U, 255U }, false, draw_text))
        {
            const int prev_r = (static_cast<int>(o.role) + 8) % 9;
            o.role  = static_cast<GameRole>(prev_r);
            o.color = role_badge_color(o.role);
        }
        batcher.quad(ix + 32.0F, iy, iw - 64.0F, 22.0F, ur::Color { 28U, 36U, 52U, 255U });
        draw_text(batcher, ix + 40.0F, iy + 7.0F,
                  std::string("ROLE: ") + role_badge_tag(o.role),
                  ur::Color { 225U, 236U, 255U, 255U }, 1.0F, ix + iw - 36.0F);
        if (draw_button(batcher, uw::Rect { ix + iw - 28.0F, iy, 28.0F, 22.0F }, ">", ptr,
                        ur::Color { 34U, 44U, 64U, 255U }, ur::Color { 90U, 175U, 250U, 255U }, false, draw_text))
        {
            const int next_r = (static_cast<int>(o.role) + 1) % 9;
            o.role  = static_cast<GameRole>(next_r);
            o.color = role_badge_color(o.role);
        }
        iy += 28.0F;

        // Interactive Slider Row Helper: [Label] [-] [Slider Bar] [+] [Value]
        auto draw_slider_row = [&](const char* label, float& val, float min_v, float max_v,
                                   float step, ur::Color bar_col, bool sync_spawn = false) {
            if (iy + 22.0F > r.y + r.h - 36.0F) { return; }
            draw_text(batcher, ix + 2.0F, iy + 6.0F, label,
                      ur::Color { 205U, 218U, 242U, 255U }, 1.0F, ix + 86.0F);

            const float btn_minus_x = ix + 88.0F;
            const float bar_x       = btn_minus_x + 22.0F;
            const float bar_w       = std::max(40.0F, iw - 182.0F);
            const float btn_plus_x  = bar_x + bar_w + 3.0F;
            const float val_x       = btn_plus_x + 24.0F;

            if (draw_button(batcher, uw::Rect { btn_minus_x, iy + 1.0F, 19.0F, 18.0F }, "-", ptr,
                            ur::Color { 34U, 42U, 62U, 255U }, bar_col, false, draw_text))
            {
                val = std::clamp(val - step, min_v, max_v);
                if (sync_spawn) { o.spawn_pos = o.pos; }
            }

            const uw::Rect bar_r { bar_x, iy + 2.0F, bar_w, 16.0F };
            batcher.quad(bar_r.x, bar_r.y, bar_r.w, bar_r.h, ur::Color { 14U, 18U, 28U, 255U });
            const float norm = std::clamp((val - min_v) / std::max(1e-4F, max_v - min_v), 0.0F, 1.0F);
            batcher.quad(bar_r.x + 1.0F, bar_r.y + 1.0F, (bar_r.w - 2.0F) * norm, bar_r.h - 2.0F, bar_col);

            if (ptr.left_down && bar_r.contains(ptr.mouse_x, ptr.mouse_y))
            {
                const float t = std::clamp((ptr.mouse_x - bar_r.x) / bar_r.w, 0.0F, 1.0F);
                val = min_v + t * (max_v - min_v);
                if (sync_spawn) { o.spawn_pos = o.pos; }
            }

            if (draw_button(batcher, uw::Rect { btn_plus_x, iy + 1.0F, 19.0F, 18.0F }, "+", ptr,
                            ur::Color { 34U, 42U, 62U, 255U }, bar_col, false, draw_text))
            {
                val = std::clamp(val + step, min_v, max_v);
                if (sync_spawn) { o.spawn_pos = o.pos; }
            }

            char vb[32];
            std::snprintf(vb, sizeof(vb), "%.2f", static_cast<double>(val));
            draw_text(batcher, val_x, iy + 6.0F, vb,
                      ur::Color { 245U, 250U, 255U, 255U }, 1.0F, ix + iw);
            iy += 21.0F;
        };

        // Section 1: 3D Transform
        batcher.quad(ix, iy, iw, 18.0F, ur::Color { 26U, 34U, 52U, 255U });
        draw_text(batcher, ix + 6.0F, iy + 5.0F, "1. 3D TRANSFORM (POS / ROT / SCALE)",
                  ur::Color { 125U, 200U, 255U, 255U }, 1.0F, ix + iw);
        iy += 21.0F;

        draw_slider_row("POS X (m)",   o.pos[0],   -16.0F, 16.0F,  0.5F, ur::Color { 225U,  75U,  75U, 255U }, true);
        draw_slider_row("POS Y (m)",   o.pos[1],    -2.0F, 14.0F,  0.5F, ur::Color {  70U, 215U, 105U, 255U }, true);
        draw_slider_row("POS Z (m)",   o.pos[2],   -20.0F, 10.0F,  0.5F, ur::Color {  75U, 150U, 245U, 255U }, true);
        draw_slider_row("YAW (deg)",   o.yaw_deg, -180.0F, 180.0F, 15.0F, ur::Color { 185U, 140U, 250U, 255U });
        draw_slider_row("WIDTH X",     o.scale[0],   0.3F, 16.0F,  0.4F, ur::Color { 210U,  95U,  95U, 255U });
        draw_slider_row("HEIGHT Y",    o.scale[1],   0.2F, 10.0F,  0.25F, ur::Color {  85U, 205U, 120U, 255U });
        draw_slider_row("DEPTH Z",     o.scale[2],   0.3F, 16.0F,  0.4F, ur::Color {  95U, 160U, 235U, 255U });

        iy += 4.0F;

        // Section 2: PBR Material & Color Swatches
        batcher.quad(ix, iy, iw, 18.0F, ur::Color { 26U, 34U, 52U, 255U });
        draw_text(batcher, ix + 6.0F, iy + 5.0F, "2. PBR MATERIAL & COLOR SWATCHES",
                  ur::Color { 125U, 200U, 255U, 255U }, 1.0F, ix + iw);
        iy += 22.0F;

        // 6 Quick Color Preset Swatches + Live 3D Shaded Material Sphere
        static constexpr std::array<ur::Color, 6> kSwatches {{
            {  55U, 210U, 255U, 255U },
            {  75U, 225U, 130U, 255U },
            { 255U, 210U,  55U, 255U },
            { 245U,  75U,  85U, 255U },
            {  85U, 135U, 225U, 255U },
            { 185U, 105U, 255U, 255U },
        }};
        const float sw_w = (iw - 78.0F) / 6.0F;
        for (std::size_t s = 0U; s < kSwatches.size(); ++s)
        {
            const uw::Rect sr { ix + static_cast<float>(s) * (sw_w + 3.0F), iy, sw_w, 22.0F };
            batcher.quad(sr.x, sr.y, sr.w, sr.h, kSwatches[s]);
            batcher.quad(sr.x, sr.y, sr.w, 1.0F, ur::Color { 255U, 255U, 255U, 200U });
            if (sr.contains(ptr.mouse_x, ptr.mouse_y) && ptr.left_pressed)
            {
                o.color = kSwatches[s];
            }
        }
        // Shaded PBR sphere preview on right of swatch strip
        const float sp_x = ix + iw - 52.0F;
        batcher.quad(sp_x, iy - 2.0F, 48.0F, 26.0F, ur::Color { 14U, 18U, 26U, 255U });
        batcher.quad(sp_x + 12.0F, iy + 1.0F, 22.0F, 20.0F, o.color);
        batcher.quad(sp_x + 16.0F, iy + 4.0F, 8.0F, 6.0F, ur::Color { 255U, 255U, 255U, 220U });
        iy += 26.0F;

        draw_slider_row("METALLIC",    o.metallic,  0.0F, 1.0F, 0.1F, ur::Color { 215U, 185U,  85U, 255U });
        draw_slider_row("ROUGHNESS",   o.roughness, 0.05F, 1.0F, 0.1F, ur::Color { 130U, 195U, 235U, 255U });

        iy += 4.0F;

        // Section 3: Gameplay & AI Behavior
        batcher.quad(ix, iy, iw, 18.0F, ur::Color { 26U, 34U, 52U, 255U });
        draw_text(batcher, ix + 6.0F, iy + 5.0F, "3. GAMEPLAY & PHYSICS BEHAVIOR",
                  ur::Color { 125U, 245U, 175U, 255U }, 1.0F, ix + iw);
        iy += 21.0F;

        draw_slider_row("SPEED (m/s)", o.move_speed,   0.0F, 16.0F, 0.5F, ur::Color {  80U, 220U, 165U, 255U });
        draw_slider_row("PATROL (m)",  o.patrol_range, 0.0F, 12.0F, 0.5F, ur::Color { 245U, 165U,  75U, 255U });
        draw_slider_row("JUMP IMPULSE", o.jump_force,  4.0F, 22.0F, 1.0F, ur::Color { 185U, 115U, 255U, 255U });

        // Bottom CTA in Inspector: [SNAP TO GROUND] [TEST IN PLAY MODE (F5)]
        const float bot_y = r.y + r.h - 32.0F;
        if (bot_y > iy)
        {
            const float half_w = (iw - 6.0F) * 0.5F;
            if (draw_button(batcher, uw::Rect { ix, bot_y, half_w, 26.0F }, "SNAP GROUND", ptr,
                            ur::Color { 34U, 48U, 72U, 255U }, ur::Color { 95U, 185U, 255U, 255U }, false, draw_text))
            {
                o.pos[1]       = o.scale[1] * 0.5F;
                o.spawn_pos[1] = o.pos[1];
            }
            if (draw_button(batcher, uw::Rect { ix + half_w + 6.0F, bot_y, half_w, 26.0F },
                            is_playing_ ? "STOP PLAY (F8)" : "PLAY TEST (F5)", ptr,
                            is_playing_ ? ur::Color { 135U, 42U, 48U, 255U } : ur::Color { 32U, 92U, 56U, 255U },
                            ur::Color { 75U, 235U, 130U, 255U }, false, draw_text))
            {
                if (!is_playing_) { start_play(active_stage, pie_controller); }
                else              { stop_play(active_stage, pie_controller); }
            }
        }
    }

    // ---- 5. BOTTOM DOCK: TEMPLATES / SHOWCASES / CONSOLE / PROFILER ---------
    void draw_bottom_dock(ur::DrawBatcher&                batcher,
                          const uw::Rect&                 r,
                          const uw::PointerState&         ptr,
                          cd::element_core::Stage&        active_stage,
                          cd::editor::AddElementCatalog&  add_catalog,
                          cd::editor::EditorCockpitShell& cockpit_shell,
                          float                           fps,
                          TextFn                          draw_text)
    {
        batcher.quad(r.x, r.y, r.w, r.h, ur::Color { 16U, 20U, 30U, 255U });
        batcher.quad(r.x, r.y, r.w, 1.0F, ur::Color { 62U, 78U, 112U, 255U });

        // Tab Strip Bar (28px tall)
        batcher.quad(r.x, r.y + 1.0F, r.w, 27.0F, ur::Color { 22U, 28U, 42U, 255U });
        static constexpr std::array<const char*, 4> kTabs {{
            "1. PLAYABLE GAME TEMPLATES & PREFABS",
            "2. 12 CHROMODYNAMIC SHOWCASES",
            "3. STUDIO CONSOLE & EVENT LOG",
            "4. 19-PASS FRAMEGRAPH GPU PROFILER",
        }};
        float tx = r.x + 8.0F;
        for (std::size_t t = 0U; t < 4U; ++t)
        {
            const float tw = 248.0F;
            if (draw_button(batcher, uw::Rect { tx, r.y + 4.0F, tw, 22.0F }, kTabs[t], ptr,
                            ur::Color { 30U, 38U, 56U, 255U },
                            ur::Color { 85U, 165U, 250U, 255U },
                            bottom_tab_ == static_cast<int>(t), draw_text))
            {
                bottom_tab_ = static_cast<int>(t);
            }
            tx += tw + 6.0F;
        }

        const float body_y = r.y + 32.0F;
        const float body_h = r.h - 36.0F;

        if (bottom_tab_ == 0)
        {
            // 4 Playable 3D Game Template Cards
            struct TmplCard
            {
                const char* title;
                const char* subtitle;
                const char* features;
                ur::Color   accent;
            };
            static constexpr std::array<TmplCard, 4> kCards {{
                { "GAME 01: SKY PLATFORMER",  "Hero + Moving Elevator + 5 Crystals", "WASD + Space Jump | AABB Platforming", ur::Color { 65U, 185U, 255U, 255U } },
                { "GAME 02: CYBER AI ARENA",  "Stealth Arena + 3 Patrol Drones",     "Stomp AI Bots | Bounce Pads | Cover",  ur::Color { 245U, 85U,  95U, 255U } },
                { "GAME 03: ELEVATOR LAB",    "Vertical + Horizontal Lifts & Crates","Push Physics Crates | Multi-Story",    ur::Color { 65U, 225U, 175U, 255U } },
                { "GAME 04: ROVER CANYON",    "Driveable 3D Rover + Canyon Ramps",   "High-Speed Canyon Trial + Checkpoints",ur::Color { 255U, 200U, 70U, 255U } },
            }};

            const float card_w = (r.w - 36.0F) * 0.25F;
            for (std::size_t c = 0U; c < 4U; ++c)
            {
                const float cx = r.x + 8.0F + static_cast<float>(c) * (card_w + 6.0F);
                const uw::Rect cr { cx, body_y, card_w, body_h - 4.0F };
                const bool hov = cr.contains(ptr.mouse_x, ptr.mouse_y);
                const bool act = (template_idx_ == c);

                batcher.quad(cr.x, cr.y, cr.w, cr.h,
                             act ? ur::Color { 32U, 48U, 78U, 255U }
                                 : (hov ? ur::Color { 28U, 36U, 54U, 255U }
                                        : ur::Color { 22U, 27U, 40U, 255U }));
                batcher.quad(cr.x, cr.y, cr.w, 3.0F, kCards[c].accent);

                draw_text(batcher, cr.x + 10.0F, cr.y + 10.0F, kCards[c].title,
                          ur::Color { 255U, 255U, 255U, 255U }, 1.0F, cr.x + cr.w - 8.0F);
                draw_text(batcher, cr.x + 10.0F, cr.y + 28.0F, kCards[c].subtitle,
                          kCards[c].accent, 1.0F, cr.x + cr.w - 8.0F);
                draw_text(batcher, cr.x + 10.0F, cr.y + 46.0F, kCards[c].features,
                          ur::Color { 185U, 200U, 225U, 255U }, 1.0F, cr.x + cr.w - 8.0F);

                if (draw_button(batcher, uw::Rect { cr.x + 10.0F, cr.y + cr.h - 32.0F, cr.w - 20.0F, 24.0F },
                                act ? "ACTIVE GAME LEVEL (RELOAD)" : "LOAD THIS 3D GAME LEVEL", ptr,
                                ur::Color { 34U, 52U, 82U, 255U }, kCards[c].accent, act, draw_text))
                {
                    load_game_template(c);
                    add_log(std::string("[TEMPLATE] Loaded playable game level: ") + kCards[c].title,
                            kCards[c].accent);
                }
            }
        }
        else if (bottom_tab_ == 1)
        {
            // 12 Engine Showcases Grid (2 rows x 6 cols)
            const auto& sc_list = cockpit_shell.project_hub_showcases();
            const float sw = (r.w - 46.0F) / 6.0F;
            const float sh = (body_h - 10.0F) * 0.5F;
            for (std::size_t s = 0U; s < sc_list.size() && s < 12U; ++s)
            {
                const float sx = r.x + 8.0F + static_cast<float>(s % 6U) * (sw + 6.0F);
                const float sy = body_y + static_cast<float>(s / 6U) * (sh + 4.0F);
                const std::string lbl = std::to_string(s + 1U) + ". " + std::string(sc_list[s].slug);
                if (draw_button(batcher, uw::Rect { sx, sy, sw, sh }, lbl, ptr,
                                ur::Color { 26U, 34U, 52U, 255U },
                                ur::Color { 85U, 175U, 250U, 255U }, false, draw_text))
                {
                    active_stage = cd::gameplay_director::build_showcase_stage_by_slug(sc_list[s].slug);
                    sync_from_showcase_stage(active_stage);
                }
            }
            (void)add_catalog;
        }
        else if (bottom_tab_ == 2)
        {
            // Live Studio Console Log
            float ly = body_y + 4.0F;
            const std::size_t n = logs_.size();
            const std::size_t start_l = n > 8U ? (n - 8U) : 0U;
            for (std::size_t i = start_l; i < n; ++i)
            {
                draw_text(batcher, r.x + 12.0F, ly, logs_[i].text, logs_[i].color, 1.0F, r.x + r.w - 12.0F);
                ly += 16.0F;
            }
        }
        else
        {
            // 19-Pass FrameGraph GPU Profiler Bars
            static constexpr std::array<std::pair<const char*, float>, 7> kPasses {{
                { "1. ShadowCSM (4 Cascades)", 1.45F },
                { "2. GBuffer Opaque + Masked", 2.10F },
                { "3. DDGI + ReSTIR GI Probe",  1.85F },
                { "4. PBR Direct + IBL Shade",  2.40F },
                { "5. Volumetric Fog + Sky",    0.95F },
                { "6. TAA + ACES Tonemap",      0.80F },
                { "7. Studio 3D UI Batcher",    0.65F },
            }};
            float py = body_y + 4.0F;
            for (const auto& [pname, pms] : kPasses)
            {
                draw_text(batcher, r.x + 14.0F, py + 2.0F, pname,
                          ur::Color { 215U, 228U, 250U, 255U }, 1.0F, r.x + 220.0F);
                batcher.quad(r.x + 230.0F, py, 320.0F, 12.0F, ur::Color { 24U, 30U, 44U, 255U });
                batcher.quad(r.x + 230.0F, py, std::min(320.0F, pms * 95.0F), 12.0F,
                             ur::Color { 75U, 200U, 255U, 255U });
                char mb[32];
                std::snprintf(mb, sizeof(mb), "%.2f ms  (%.0f FPS)", static_cast<double>(pms), static_cast<double>(fps));
                draw_text(batcher, r.x + 560.0F, py + 2.0F, mb,
                          ur::Color { 120U, 235U, 165U, 255U }, 1.0F, r.x + r.w - 12.0F);
                py += 18.0F;
            }
        }
    }

    // ---- 6. BOTTOM STATUS BAR -----------------------------------------------
    void draw_status_bar(ur::DrawBatcher& batcher,
                         const uw::Rect&  r,
                         float            fps,
                         int              route_taken,
                         TextFn           draw_text)
    {
        batcher.quad(r.x, r.y, r.w, r.h, ur::Color { 14U, 18U, 28U, 255U });
        batcher.quad(r.x, r.y, r.w, 1.0F, ur::Color { 55U, 72U, 108U, 255U });

        char sb[256];
        std::snprintf(
            sb, sizeof(sb),
            "VULKAN 1.3 RHI (%s)   |   FPS: %.1f (%.2f ms)   |   VERTS: %zu   |   3D OBJECTS: %zu   |   STATE: %s   |   SCORE: %d",
            route_taken == 1 ? "ROUTE A: cd::material::UiVariant" : "ROUTE B: INLINE GLSL",
            static_cast<double>(fps),
            static_cast<double>(fps > 0.1F ? (1000.0F / fps) : 16.6F),
            batcher.vertex_count(),
            objects_.size(),
            is_playing_ ? (is_paused_ ? "PAUSED (F6)" : "PLAYING GAME (F5)") : "LEVEL AUTHORING (EDIT)",
            score_);

        draw_text(batcher, r.x + 10.0F, r.y + 9.0F, sb,
                  is_playing_ ? ur::Color { 105U, 245U, 155U, 255U }
                              : ur::Color { 205U, 220U, 245U, 255U },
                  1.0F, r.x + r.w - 10.0F);
    }

    // ---- 7. HELP MODAL (F1) -------------------------------------------------
    void draw_help_modal(ur::DrawBatcher&        batcher,
                         float                   fb_w,
                         float                   fb_h,
                         const uw::PointerState& ptr,
                         TextFn                  draw_text)
    {
        batcher.quad(0.0F, 0.0F, fb_w, fb_h, ur::Color { 5U, 8U, 14U, 195U });
        const float mw = 640.0F;
        const float mh = 340.0F;
        const float mx = (fb_w - mw) * 0.5F;
        const float my = (fb_h - mh) * 0.5F;
        batcher.quad(mx, my, mw, mh, ur::Color { 22U, 28U, 42U, 255U });
        batcher.quad(mx, my, mw, 3.0F, ur::Color { 95U, 185U, 255U, 255U });

        draw_text(batcher, mx + 18.0F, my + 16.0F, "CHROMODYNAMIC 3D GAME STUDIO — QUICK GUIDE",
                  ur::Color { 255U, 255U, 255U, 255U }, 1.5F, mx + mw - 18.0F);

        static constexpr std::array<const char*, 10> kHelpLines {{
            "1. PLAY 3D GAME (F5): Click [PLAY GAME (F5)] in the Top Bar to play your level!",
            "2. PLAYER CONTROLS:   [WASD] or [ARROW KEYS] to Move, [SPACE] to Jump, [SHIFT] to Sprint.",
            "3. STOP / EDIT (F8):  Click [STOP/EDIT (F8)] to return to authoring and restore positions.",
            "4. ADD 3D OBJECTS:    Use the green [+ PLATFORM], [+ ELEVATOR], [+ ENEMY], [+ COIN] buttons.",
            "5. 3D GIZMO DRAG:     Select any object and Left-Click Drag its Red(X), Green(Y), Blue(Z) arrows.",
            "6. GIZMO MODES:       Press [W] for Move, [E] for Rotate, [R] for Scale.",
            "7. 3D CAMERA:         Hold [RIGHT MOUSE BUTTON + DRAG] to Orbit; [MOUSE WHEEL] to Zoom.",
            "8. INSPECTOR:         Drag sliders or click [-] / [+] in the Right Dock to tune properties.",
            "9. GAME TEMPLATES:    Click any of the 4 Game Template cards in the Bottom Dock to switch games.",
            "10. SAVE / LOAD:      Click [SAVE LVL] / [LOAD LVL] to persist your custom .cdstage level!",
        }};

        float hy = my + 52.0F;
        for (const char* ln : kHelpLines)
        {
            draw_text(batcher, mx + 20.0F, hy, ln, ur::Color { 215U, 228U, 250U, 255U }, 1.0F, mx + mw - 20.0F);
            hy += 22.0F;
        }

        if (draw_button(batcher, uw::Rect { mx + mw - 140.0F, my + mh - 40.0F, 120.0F, 28.0F },
                        "CLOSE (F1)", ptr,
                        ur::Color { 45U, 85U, 155U, 255U }, ur::Color { 95U, 195U, 255U, 255U }, false, draw_text))
        {
            show_help_modal_ = false;
        }
    }

    std::vector<StudioGameObject> objects_              {};
    std::vector<ConsoleEntry>     logs_                 {};
    std::string                   level_title_          { "01: SKY PLATFORMER ODYSSEY" };
    std::size_t                   template_idx_         { 0U };
    std::size_t                   selected_idx_         { 1U };
    std::array<float, 3>          cam_target_           { 0.0F, 1.2F, -3.5F };
    float                         cam_yaw_              { 0.42F };
    float                         cam_pitch_            { 0.38F };
    float                         cam_dist_             { 15.5F };
    float                         anim_time_            { 0.0F };
    float                         play_time_s_          { 0.0F };
    float                         hero_walk_phase_      { 0.0F };
    float                         invuln_timer_         { 0.0F };
    int                           hero_hp_              { 100 };
    int                           score_                { 0 };
    int                           bottom_tab_           { 0 };
    int                           hovered_gizmo_axis_   { -1 };
    int                           dragging_gizmo_axis_  { -1 };
    float                         drag_start_mouse_x_   { 0.0F };
    float                         drag_start_mouse_y_   { 0.0F };
    float                         drag_start_val_       { 0.0F };
    bool                          is_playing_           { false };
    bool                          is_paused_            { false };
    bool                          stage_clear_          { false };
    bool                          show_grid_            { true };
    bool                          follow_hero_cam_      { true };
    bool                          show_help_modal_      { false };
    bool                          request_play_toggle_  { false };
    bool                          request_pause_toggle_ { false };
    bool                          request_stop_         { false };
    bool                          request_delete_       { false };
    bool                          key_w_                { false };
    bool                          key_a_                { false };
    bool                          key_s_                { false };
    bool                          key_d_                { false };
    bool                          key_up_               { false };
    bool                          key_down_             { false };
    bool                          key_left_             { false };
    bool                          key_right_            { false };
    bool                          key_space_            { false };
    bool                          key_shift_            { false };
};

}  // namespace cd::editor::studio
