// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/Studio2DWorkspace.hpp
//
// FAZ 35.2 (ADR-20261111) -- Commercial 2D Engine Studio Workspace (`[2D Studio]`)
//
// Bridges:
//   * `cd::schema`   (L0) -- Auto-generated Schema Inspector & Draft 2020-12 JSON Schema
//   * `cd::render2d` (L3) -- 64-bit Z/Y-Sort, Subpixel Camera2D, Wang Auto-Tile,
//                            Greedy Collider Meshing, Soft-Shadow Lighting2D,
//                            `.aseprite` & `.ldtk` asset pipelines
//   * `cd::world2d`  (L4) -- Stage2D, PhysicsWorld2D + CharacterController2D,
//                            Sandboxed Lua 5.4 Live VM, Symmetric Human/AI CommandBus2D,
//                            Deterministic PlaytestRunner2D, `.cdpak` packager
// =============================================================================
#pragma once

#include <cd/app_common/FileTextIo.hpp>
#include <cd/app_common/PackageExporter.hpp>
#include <cd/asset/pak/Pak.hpp>
#include <cd/render2d/FormatLoaders2D.hpp>
#include <cd/render2d/Renderer2D.hpp>
#include <cd/schema/Schema.hpp>
#include <cd/ui/ChromaUi.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/PlaytestRunner2D.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cd::studio
{

enum class Tool2DKind : std::uint8_t
{
    kSelectMove = 0U,
    kTileBrush  = 1U,
    kRectFill   = 2U,
    kEraser     = 3U,
    kStamp      = 4U,
    kFloodFill  = 5U,
};

struct StampBrushConfig
{
    int                        pattern_width { 2 };
    int                        pattern_height { 2 };
    std::vector<std::uint16_t> pattern { 1U, 1U, 1U, 1U };
    int                        preset_index { 0 }; // 0: 2x2 Block, 1: 3x1 Platform, 2: 1x3 Pillar, 3: 3x3 Arena Block
};

struct Studio2DState
{
    bool initialized { false };

    cd::world2d::Stage2D              stage {};
    cd::world2d::CommandBus2D         command_bus {};
    cd::schema::SchemaRegistry        schema_registry {};
    cd::schema::TypeSchema            entity_schema { cd::world2d::make_entity2d_type_schema() };
    cd::render2d::Canvas2D            canvas {};
    cd::render2d::Render2DDebugOptions debug_opts {};

    Tool2DKind       active_tool { Tool2DKind::kTileBrush };
    std::uint16_t    selected_tile_id { 1U };
    bool             auto_wang_tiling { true };
    std::uint32_t    selected_entity_id { 1U };
    StampBrushConfig stamp_cfg {};
    std::uint32_t    locked_layers_mask { 0U };
    float            anim_preview_fps { 10.0F };
    bool             anim_preview_playing { true };
    bool             anim_preview_loop { true };
    int              anim_preview_clip_idx { 0 };
    float            anim_preview_timer { 0.0F };
    int              anim_preview_frame { 0 };
    int              active_genre_template_idx { 0 };

    // Viewport mouse interaction state
    bool   pan_dragging { false };
    ImVec2 prev_mouse_pos { 0.0F, 0.0F };
    bool   brush_stroke_open { false };
    int    last_painted_gx { -99999 };
    int    last_painted_gy { -99999 };
    bool   rect_drag_active { false };
    int    rect_start_gx { 0 };
    int    rect_start_gy { 0 };
    int    rect_cur_gx { 0 };
    int    rect_cur_gy { 0 };
    bool   entity_drag_active { false };
    float  entity_drag_offset_x { 0.0F };
    float  entity_drag_offset_y { 0.0F };

    // Live Play-In-Editor (2D PIE) & Verification state
    bool  autopilot_in_pie { false };
    bool  has_playtest_report { false };
    cd::world2d::PlaytestReport2D last_playtest {};

    // Editable text buffers for ChromaUi InputText
    std::array<char, 2048> lua_script_buf {};
    std::uint32_t          lua_synced_entity_id { 0U };
    std::array<char, 2048> ai_json_cmd_buf {};
    std::array<char, 260>  stage_file_buf {};
    std::array<char, 260>  import_file_buf {};
    std::string            last_status_msg { "Ready - Commercial 2D Studio initialized." };

    void sync_lua_buffer_from_selected() noexcept
    {
        if (const auto* ent = stage.find_entity(selected_entity_id))
        {
            std::snprintf(lua_script_buf.data(), lua_script_buf.size(), "%s", ent->lua_script.c_str());
            lua_synced_entity_id = selected_entity_id;
        }
        else
        {
            lua_script_buf[0]    = '\0';
            lua_synced_entity_id = 0U;
        }
    }

    void ensure_initialized()
    {
        if (initialized)
        {
            return;
        }
        initialized = true;
        cd::world2d::register_world2d_schemas(schema_registry);
        entity_schema = cd::world2d::make_entity2d_type_schema();
        stage.build_starter_illuminated_platformer();
        debug_opts.show_grid             = true;
        debug_opts.show_camera_frame     = true;
        debug_opts.show_greedy_colliders = false;
        debug_opts.show_light_gizmos     = true;

        if (!stage.entities().empty())
        {
            selected_entity_id = stage.entities().front().id;
        }
        sync_lua_buffer_from_selected();

        std::snprintf(
            ai_json_cmd_buf.data(),
            ai_json_cmd_buf.size(),
            "{\"commands\":[{\"op\":\"paint_rect_2d\",\"x0\":7,\"y0\":6,\"x1\":11,\"y1\":6,\"palette_id\":2},"
            "{\"op\":\"spawn_entity_2d\",\"name\":\"AI_Sky_Gem\",\"role\":3,\"pos\":[9.0,7.4],\"size\":[0.65,0.65]}]}");
        std::snprintf(stage_file_buf.data(), stage_file_buf.size(), "%s", "studio_stage.cd2d.json");
        std::snprintf(import_file_buf.data(), import_file_buf.size(), "%s", "level_01.ldtk");
    }

    void reset_to_starter_stage()
    {
        initialized = false;
        command_bus = cd::world2d::CommandBus2D {};
        ensure_initialized();
    }
};

[[nodiscard]] inline Studio2DState& studio_2d()
{
    static Studio2DState state {};
    state.ensure_initialized();
    return state;
}

[[nodiscard]] inline std::uint32_t pick_entity2d_at_world(
    const cd::world2d::Stage2D& stage,
    cd::math::Vec2f             world_pt) noexcept
{
    std::uint32_t best_id   = 0U;
    std::int32_t  best_prio = -999999;
    for (const auto& ent : stage.entities())
    {
        if (!ent.visible)
        {
            continue;
        }
        const float hx = std::max(0.35F, ent.size.x * 0.5F);
        const float hy = std::max(0.35F, ent.size.y * 0.5F);
        if (world_pt.x >= ent.position.x - hx && world_pt.x <= ent.position.x + hx &&
            world_pt.y >= ent.position.y - hy && world_pt.y <= ent.position.y + hy)
        {
            const std::int32_t prio = ent.layer * 1000 + ent.z_index;
            if (best_id == 0U || prio >= best_prio)
            {
                best_id   = ent.id;
                best_prio = prio;
            }
        }
    }
    return best_id;
}

template <typename LogFn>
[[nodiscard]] inline bool export_2d_and_3d_standalone_bundle(
    const char*      export_dir,
    LogFn&&          log_push,
    std::string_view exe_name     = {},
    std::string_view project_json = {})
{
    auto& s2d = studio_2d();
    std::string scene3d_json {};
    const std::string scene3d_path = std::string(export_dir) + "/game_world.cdscene.json";
    if (auto s3d_res = cd::app_common::read_text_file_utf8(scene3d_path); s3d_res.has_value())
    {
        scene3d_json = std::move(*s3d_res);
    }

    cd::app_common::PackageExportRequest req {};
    req.output_dir         = export_dir;
    req.mode               = scene3d_json.empty() ? "Pure2D" : "Hybrid2D3D";
    req.game_title         = std::string(s2d.stage.name());
    req.executable_name    = std::string(exe_name);
    req.stage2d_json       = s2d.stage.save_to_json_string(true);
    req.scene3d_json       = std::move(scene3d_json);
    req.project_json       = std::string(project_json);
    req.copy_player_binary = true;

    const auto exp_res = cd::app_common::export_standalone_cdpak_bundle(req);
    if (!exp_res.has_value())
    {
        s2d.last_status_msg = "Export failed: " + std::string(exp_res.error().message);
        log_push("[export] ERROR writing standalone .cdpak bundle to " + std::string(export_dir));
        return false;
    }

    const std::string bundled_label = exp_res->bundled_player_exe
        ? (", +" + std::filesystem::path(exp_res->exe_path).filename().string() + " bundled)")
        : std::string(", launcher ready)");
    s2d.last_status_msg = "Packaged " + exp_res->pak_path + " (" +
                          std::to_string(exp_res->pak_size_bytes) + " bytes" + bundled_label;
    log_push("[export] Standalone .cdpak bundle -> " + std::string(export_dir) +
             (exp_res->exe_path.empty() ? std::string {} : (" (" + exp_res->exe_path + ")")));
    return true;
}

[[nodiscard]] inline bool load_2d_project_or_stage_from_disk(
    const std::string& path_str,
    std::string*       out_project_json = nullptr)
{
    if (path_str.empty())
    {
        return false;
    }

    std::error_code ec {};
    const std::filesystem::path in_path(path_str);
    std::filesystem::path stage_path {};
    std::filesystem::path proj_file {};

    if (path_str.ends_with(".cd2d.json"))
    {
        stage_path = in_path;
        const auto cand_proj = in_path.parent_path().parent_path() / "project.cdproj";
        if (std::filesystem::exists(cand_proj, ec))
        {
            proj_file = cand_proj;
        }
    }
    else
    {
        const std::filesystem::path base_dir = std::filesystem::is_directory(in_path, ec)
            ? in_path
            : in_path.parent_path();
        if (std::filesystem::is_regular_file(in_path, ec) &&
            (path_str.ends_with(".cdproj") || path_str.ends_with(".cdproject")))
        {
            proj_file = in_path;
        }
        else if (std::filesystem::exists(base_dir / "project.cdproj", ec))
        {
            proj_file = base_dir / "project.cdproj";
        }

        if (std::filesystem::exists(base_dir / "stages" / "main.cd2d.json", ec))
        {
            stage_path = base_dir / "stages" / "main.cd2d.json";
        }
        else if (std::filesystem::exists(base_dir / "game_world.cd2d.json", ec))
        {
            stage_path = base_dir / "game_world.cd2d.json";
        }
    }

    if (stage_path.empty() || !std::filesystem::exists(stage_path, ec))
    {
        return false;
    }

    auto& s2d = studio_2d();
    const auto lr = s2d.stage.load_from_file(stage_path.string());
    if (!lr.has_value())
    {
        s2d.last_status_msg = "Failed to load 2D stage: " + std::string(lr.error().message);
        return false;
    }

    if (out_project_json != nullptr && !proj_file.empty())
    {
        if (auto pr = cd::app_common::read_text_file_utf8(proj_file.string()); pr.has_value())
        {
            *out_project_json = std::move(*pr);
        }
    }

    s2d.command_bus.clear_history();
    if (s2d.stage.arcade_ruleset() == cd::world2d::ArcadeRuleset2D::kPongCourt)
    {
        s2d.active_genre_template_idx = 4;
    }
    else if (s2d.stage.arcade_ruleset() == cd::world2d::ArcadeRuleset2D::kBridgeDemolition)
    {
        s2d.active_genre_template_idx = 5;
    }
    else if (s2d.stage.entities().empty() && s2d.stage.tilemap().non_empty_tile_count() == 0U)
    {
        s2d.active_genre_template_idx = 3;
    }
    else if (s2d.stage.name().find("Dungeon") != std::string_view::npos ||
             s2d.stage.name().find("RPG") != std::string_view::npos)
    {
        s2d.active_genre_template_idx = 1;
    }
    else if (s2d.stage.name().find("Arena") != std::string_view::npos ||
             s2d.stage.name().find("Twin") != std::string_view::npos)
    {
        s2d.active_genre_template_idx = 2;
    }
    else
    {
        s2d.active_genre_template_idx = 0;
    }
    s2d.selected_entity_id = s2d.stage.entities().empty() ? 0U : s2d.stage.entities().front().id;
    s2d.sync_lua_buffer_from_selected();
    std::snprintf(s2d.stage_file_buf.data(), s2d.stage_file_buf.size(), "%s", stage_path.string().c_str());
    s2d.last_status_msg = "Loaded 2D project stage: " + stage_path.string();
    return true;
}

[[nodiscard]] inline bool save_2d_project_folder_to_disk(const std::string& project_dir)
{
    if (project_dir.empty())
    {
        return false;
    }
    std::error_code ec {};
    const std::filesystem::path dir(project_dir);
    const std::filesystem::path stages_dir = dir / "stages";
    std::filesystem::create_directories(stages_dir, ec);

    auto& s2d = studio_2d();
    const std::string stage_file = (stages_dir / "main.cd2d.json").string();
    if (!s2d.stage.save_to_file(stage_file).has_value())
    {
        return false;
    }

    const std::string stage_name(s2d.stage.name());
    const std::string proj_json =
        "{\n"
        "  \"engine\": \"CHROMODYNAMIC\",\n"
        "  \"version\": \"1.0.0\",\n"
        "  \"name\": \"" + stage_name + "\",\n"
        "  \"mode\": \"Pure2D\",\n"
        "  \"entry_stage2d\": \"stages/main.cd2d.json\",\n"
        "  \"arcade_ruleset\": " + std::to_string(static_cast<int>(s2d.stage.arcade_ruleset())) + "\n"
        "}\n";
    return cd::app_common::write_text_file_utf8((dir / "project.cdproj").string(), proj_json).has_value();
}

inline void spawn_2d_prefab_from_catalog(int prefab_kind)
{
    auto& s2d = studio_2d();
    const auto cam_pos = s2d.stage.camera().position();

    cd::world2d::Entity2D proto {};
    proto.position      = { std::round(cam_pos.x * 2.0F) * 0.5F, std::round(cam_pos.y * 2.0F) * 0.5F };
    proto.home_position = proto.position;

    switch (prefab_kind)
    {
        case 0: // Crystal Gem (+Lua Hover)
            proto.name       = "Crystal_Gem_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role       = cd::world2d::EntityRole2D::kCollectibleGem;
            proto.size       = { 0.65F, 0.65F };
            proto.color_rgba = { 0.25F, 0.95F, 1.0F, 1.0F };
            proto.layer      = 110;
            proto.lua_script =
                "function on_tick(id, x, y, vx, vy, t, dt)\n"
                "  return x, y + math.sin(t * 4.0 + id) * 0.015, vx, vy\n"
                "end\n";
            break;
        case 1: // Patrol Drone (+Lua AI)
            proto.name        = "Patrol_Drone_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role        = cd::world2d::EntityRole2D::kPatrolEnemy;
            proto.size        = { 0.95F, 0.85F };
            proto.color_rgba  = { 1.0F, 0.32F, 0.42F, 1.0F };
            proto.layer       = 115;
            proto.patrol_span = 2.8F;
            proto.move_speed  = 2.6F;
            break;
        case 2: // Moving Platform (+Lua)
            proto.name        = "Moving_Lift_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role        = cd::world2d::EntityRole2D::kMovingPlatform;
            proto.size        = { 2.8F, 0.48F };
            proto.color_rgba  = { 0.30F, 0.86F, 0.65F, 1.0F };
            proto.layer       = 95;
            proto.patrol_span = 2.0F;
            proto.move_speed  = 1.6F;
            proto.lua_script  =
                "function on_tick(id, x, y, vx, vy, t, dt)\n"
                "  return x, y + math.cos(t * 1.8) * 0.025, vx, vy\n"
                "end\n";
            break;
        case 3: // Jump Pad
            proto.name           = "Jump_Pad_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role           = cd::world2d::EntityRole2D::kJumpPad;
            proto.size           = { 1.2F, 0.40F };
            proto.color_rgba     = { 1.0F, 0.78F, 0.20F, 1.0F };
            proto.layer          = 95;
            proto.bounce_impulse = 17.5F;
            break;
        case 4: // Dynamic Physics Crate
            proto.name       = "Physics_Crate_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role       = cd::world2d::EntityRole2D::kDynamicCrate;
            proto.size       = { 0.90F, 0.90F };
            proto.color_rgba = { 0.86F, 0.60F, 0.30F, 1.0F };
            proto.layer      = 105;
            break;
        case 5: // 2D Point Light (Soft Shadows)
            proto.name            = "PointLight2D_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role            = cd::world2d::EntityRole2D::kPointLight;
            proto.size            = { 0.55F, 0.55F };
            proto.color_rgba      = { 1.0F, 0.82F, 0.45F, 1.0F };
            proto.layer           = 140;
            proto.light_intensity = 1.65F;
            proto.light_radius    = 9.0F;
            proto.cast_shadows    = true;
            break;
        case 6: // 2D Shadow Occluder Pillar
            proto.name         = "Shadow_Pillar_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role         = cd::world2d::EntityRole2D::kShadowOccluder;
            proto.size         = { 0.85F, 2.2F };
            proto.color_rgba   = { 0.24F, 0.28F, 0.38F, 1.0F };
            proto.layer        = 92;
            proto.cast_shadows = true;
            break;
        case 8: // Kinematic Arcade Paddle
            proto.name        = "Arcade_Paddle_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role        = cd::world2d::EntityRole2D::kKinematicPaddle;
            proto.size        = { 2.8F, 0.50F };
            proto.color_rgba  = { 0.22F, 0.92F, 1.0F, 1.0F };
            proto.layer       = 120;
            proto.move_speed  = 14.5F;
            proto.patrol_span = 8.0F;
            break;
        case 9: // Bouncing Energy Ball
            proto.name             = "Energy_Ball_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role             = cd::world2d::EntityRole2D::kBouncingBall;
            proto.size             = { 0.58F, 0.58F };
            proto.runtime_velocity = { 7.5F, 9.5F };
            proto.color_rgba       = { 1.0F, 0.92F, 0.35F, 1.0F };
            proto.layer            = 130;
            proto.move_speed       = 11.5F;
            proto.bounce_impulse   = 22.0F;
            break;
        case 10: // Destructible Bridge Span
            proto.name       = "Bridge_Span_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role       = cd::world2d::EntityRole2D::kDestructibleBlock;
            proto.size       = { 1.90F, 0.48F };
            proto.color_rgba = { 0.96F, 0.66F, 0.24F, 1.0F };
            proto.layer      = 108;
            break;
        default: // Victory Goal Portal
            proto.name            = "Goal_Portal_" + std::to_string(s2d.stage.entities().size() + 1U);
            proto.role            = cd::world2d::EntityRole2D::kGoalPortal;
            proto.size            = { 1.3F, 2.1F };
            proto.color_rgba      = { 0.62F, 0.38F, 1.0F, 1.0F };
            proto.layer           = 98;
            proto.light_intensity = 1.4F;
            proto.light_radius    = 7.5F;
            break;
    }

    const std::uint32_t new_id = s2d.command_bus.spawn_entity(s2d.stage, proto);
    s2d.selected_entity_id = new_id;
    s2d.sync_lua_buffer_from_selected();
    s2d.last_status_msg = "Spawned 2D prefab '" + proto.name + "' (id=" + std::to_string(new_id) + ")";
}

inline void draw_schema_driven_entity2d_inspector(Studio2DState& s2d)
{
    auto* ent = s2d.stage.find_entity(s2d.selected_entity_id);
    if (ent == nullptr)
    {
        ImGui::TextDisabled("No 2D entity selected. Click an entity on the canvas or Scene list.");
        return;
    }

    ImGui::TextColored(
        ImVec4(0.45F, 0.92F, 1.0F, 1.0F),
        "Selected: %s (id=%u)",
        ent->name.c_str(),
        ent->id);
    ImGui::TextDisabled("Auto-generated from cd::schema::TypeSchema (%zu fields)", s2d.entity_schema.fields.size());
    ImGui::Separator();

    for (const auto& field : s2d.entity_schema.fields)
    {
        if (!field.getter || !field.setter || field.name == "lua_script")
        {
            continue;
        }

        const std::string widget_lbl = field.label + "##schema_" + field.name;
        const cd::schema::FieldValue cur_val = field.getter(ent);

        if (field.kind == cd::schema::FieldKind::kBool)
        {
            bool b = std::get<bool>(cur_val);
            if (ImGui::Checkbox(widget_lbl.c_str(), &b))
            {
                (void)s2d.command_bus.set_entity_field(s2d.stage, ent->id, field.name, b);
            }
        }
        else if (field.kind == cd::schema::FieldKind::kInt32)
        {
            int iv = static_cast<int>(std::get<std::int32_t>(cur_val));
            const int lo = field.has_range ? static_cast<int>(field.min_value) : -500;
            const int hi = field.has_range ? static_cast<int>(field.max_value) : 500;
            if (ImGui::SliderInt(widget_lbl.c_str(), &iv, lo, hi))
            {
                (void)s2d.command_bus.set_entity_field(
                    s2d.stage, ent->id, field.name, static_cast<std::int32_t>(iv));
            }
        }
        else if (field.kind == cd::schema::FieldKind::kFloat)
        {
            float fv = std::get<float>(cur_val);
            const float lo = field.has_range ? field.min_value : -1000.0F;
            const float hi = field.has_range ? field.max_value : 1000.0F;
            const float spd = (field.step > 0.0F) ? field.step : 0.1F;
            if (ImGui::DragFloat(widget_lbl.c_str(), &fv, spd, lo, hi))
            {
                (void)s2d.command_bus.set_entity_field(s2d.stage, ent->id, field.name, fv);
            }
        }
        else if (field.kind == cd::schema::FieldKind::kVec2f)
        {
            const auto v2 = std::get<cd::math::Vec2f>(cur_val);
            float tmp[3] { v2.x, v2.y, 0.0F };
            if (ImGui::DragFloat3(widget_lbl.c_str(), tmp, 0.1F))
            {
                (void)s2d.command_bus.set_entity_field(
                    s2d.stage, ent->id, field.name, cd::math::Vec2f { tmp[0], tmp[1] });
            }
        }
        else if (field.kind == cd::schema::FieldKind::kVec3f)
        {
            const auto v3 = std::get<cd::math::Vec3f>(cur_val);
            float col[3] { v3.x, v3.y, v3.z };
            if (ImGui::ColorEdit3(widget_lbl.c_str(), col))
            {
                (void)s2d.command_bus.set_entity_field(
                    s2d.stage, ent->id, field.name, cd::math::Vec3f { col[0], col[1], col[2] });
            }
        }
        else if (field.kind == cd::schema::FieldKind::kColorRgba)
        {
            const auto v4 = std::get<cd::math::Vec4f>(cur_val);
            float col[3] { v4.x, v4.y, v4.z };
            if (ImGui::ColorEdit3(widget_lbl.c_str(), col))
            {
                (void)s2d.command_bus.set_entity_field(
                    s2d.stage, ent->id, field.name, cd::math::Vec4f { col[0], col[1], col[2], v4.w });
            }
        }
        else if (field.kind == cd::schema::FieldKind::kEnum && !field.enum_labels.empty())
        {
            int cur_idx = std::clamp(
                static_cast<int>(std::get<std::int32_t>(cur_val)),
                0,
                static_cast<int>(field.enum_labels.size()) - 1);
            std::vector<const char*> items;
            items.reserve(field.enum_labels.size());
            for (const auto& lbl : field.enum_labels)
            {
                items.push_back(lbl.c_str());
            }
            if (ImGui::Combo(widget_lbl.c_str(), &cur_idx, items.data(), static_cast<int>(items.size())))
            {
                (void)s2d.command_bus.set_entity_field(
                    s2d.stage, ent->id, field.name, static_cast<std::int32_t>(cur_idx));
            }
        }
        else if (field.kind == cd::schema::FieldKind::kString || field.kind == cd::schema::FieldKind::kAssetPath)
        {
            std::array<char, 128> sbuf {};
            const std::string& sv = std::get<std::string>(cur_val);
            std::snprintf(sbuf.data(), sbuf.size(), "%s", sv.c_str());
            if (ImGui::InputText(widget_lbl.c_str(), sbuf.data(), sbuf.size()))
            {
                (void)s2d.command_bus.set_entity_field(
                    s2d.stage, ent->id, field.name, std::string(sbuf.data()));
            }
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.40F, 0.85F, 1.0F, 1.0F), "Role-Specific Prefab Parameters");
    if (ent->role == cd::world2d::EntityRole2D::kPatrolEnemy)
    {
        ImGui::DragFloat("Patrol Span (m)", &ent->patrol_span, 0.1F, 0.5F, 50.0F);
        ImGui::DragFloat("Move Speed (m/s)", &ent->move_speed, 0.1F, 0.1F, 20.0F);
    }
    else if (ent->role == cd::world2d::EntityRole2D::kJumpPad)
    {
        ImGui::DragFloat("Bounce Impulse", &ent->bounce_impulse, 0.5F, 5.0F, 50.0F);
    }
    else if (ent->role == cd::world2d::EntityRole2D::kMovingPlatform)
    {
        ImGui::DragFloat("Platform Span (m)", &ent->patrol_span, 0.1F, 0.5F, 50.0F);
    }
    else if (ent->role == cd::world2d::EntityRole2D::kPointLight)
    {
        ImGui::DragFloat("Light Intensity", &ent->light_intensity, 0.05F, 0.0F, 10.0F);
        ImGui::DragFloat("Light Radius (m)", &ent->light_radius, 0.2F, 1.0F, 50.0F);
        ImGui::Checkbox("Cast Soft Shadows", &ent->cast_shadows);
    }
}

template <typename StateT, typename LogFn>
inline void draw_2d_studio_workspace_panels(
    StateT&       /*s*/,
    float         dt,
    std::uint32_t frame_w,
    std::uint32_t frame_h,
    LogFn&&       log_push)
{
    auto& s2d = studio_2d();
    auto& cst = cd::ui::chroma::state();
    const float sw = static_cast<float>(frame_w);
    const float sh = static_cast<float>(frame_h);

    if (s2d.lua_synced_entity_id != s2d.selected_entity_id)
    {
        s2d.sync_lua_buffer_from_selected();
    }

    // =========================================================================
    // 1. Compute 2D GPU Canvas Viewport Rect (Docked Edit vs Fullscreen 2D Play)
    // =========================================================================
    const bool playing_2d = s2d.stage.is_playing();
    const float vp_x0 = playing_2d ? 0.0F : std::round(sw * cst.dock_left_w_ratio);
    const float vp_x1 = playing_2d ? sw   : std::round(sw * (1.0F - cst.dock_right_w_ratio));
    const float vp_y0 = cst.top_bar_height;
    const float vp_y1 = playing_2d
        ? sh
        : std::round(vp_y0 + std::max(100.0F, sh - vp_y0) * (1.0F - cst.dock_bottom_h_ratio));
    const float vp_w  = std::max(64.0F, vp_x1 - vp_x0);
    const float vp_h  = std::max(64.0F, vp_y1 - vp_y0);
    const cd::render2d::ViewportRect2D vp_rect { vp_x0, vp_y0, vp_w, vp_h };

    // =========================================================================
    // 2. Keyboard Shortcuts & 2D Play-In-Editor (PIE) Simulation Step
    // =========================================================================
    const auto& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && playing_2d)
        {
            s2d.stage.exit_play();
            s2d.last_status_msg = "Exited 2D Play Mode -> restored edit snapshot.";
            log_push("[2d-pie] Stopped 2D Play Mode");
        }
        else if (!playing_2d)
        {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
            {
                if (s2d.command_bus.undo(s2d.stage))
                {
                    s2d.sync_lua_buffer_from_selected();
                    s2d.last_status_msg = "Undo 2D action.";
                }
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
            {
                if (s2d.command_bus.redo(s2d.stage))
                {
                    s2d.sync_lua_buffer_from_selected();
                    s2d.last_status_msg = "Redo 2D action.";
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_1, false)) s2d.active_tool = Tool2DKind::kSelectMove;
            if (ImGui::IsKeyPressed(ImGuiKey_2, false)) s2d.active_tool = Tool2DKind::kTileBrush;
            if (ImGui::IsKeyPressed(ImGuiKey_3, false)) s2d.active_tool = Tool2DKind::kRectFill;
            if (ImGui::IsKeyPressed(ImGuiKey_4, false)) s2d.active_tool = Tool2DKind::kEraser;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && s2d.selected_entity_id != 0U)
            {
                const std::uint32_t del_id = s2d.selected_entity_id;
                if (s2d.command_bus.delete_entity(s2d.stage, del_id))
                {
                    s2d.selected_entity_id = s2d.stage.entities().empty() ? 0U : s2d.stage.entities().front().id;
                    s2d.sync_lua_buffer_from_selected();
                    s2d.last_status_msg = "Deleted 2D entity id=" + std::to_string(del_id);
                }
            }
        }
    }

    if (s2d.stage.is_playing())
    {
        cd::world2d::PlayerInput2D p_in {};
        if (s2d.autopilot_in_pie)
        {
            p_in = cd::world2d::compute_autopilot_input_2d(s2d.stage);
        }
        else
        {
            float mx = 0.0F;
            float my = 0.0F;
            if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow))
            {
                mx -= 1.0F;
            }
            if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow))
            {
                mx += 1.0F;
            }
            if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow))
            {
                my -= 1.0F;
            }
            if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow))
            {
                my += 1.0F;
            }
            p_in.move_x       = mx;
            p_in.move_y       = my;
            p_in.jump_held    = ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_W) ||
                                ImGui::IsKeyDown(ImGuiKey_UpArrow);
            p_in.jump_pressed = ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
                                ImGui::IsKeyPressed(ImGuiKey_W, false) ||
                                ImGui::IsKeyPressed(ImGuiKey_UpArrow, false);
            if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const ImVec2 mpos = ImGui::GetMousePos();
                const cd::math::Vec2f world_aim =
                    s2d.stage.camera().screen_to_world({ mpos.x, mpos.y }, vp_rect);
                for (const auto& ent : s2d.stage.entities())
                {
                    if (ent.visible && ent.role == cd::world2d::EntityRole2D::kPlayerHero)
                    {
                        p_in.aim_angle_rad = std::atan2(
                            world_aim.y - ent.position.y,
                            world_aim.x - ent.position.x);
                        break;
                    }
                }
                p_in.fire_held    = true;
                p_in.fire_pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            }
        }
        s2d.stage.step_play(std::clamp(dt, 1.0F / 240.0F, 1.0F / 20.0F), p_in);
    }

    // =========================================================================
    // 3. Floating Top-Center 2D Tool Pill Overlay (Edit Mode) or 2D Gameplay HUD
    // =========================================================================
    bool pill_hovered = false;
    if (s2d.stage.is_playing())
    {
        ImGui::SetNextWindowPos(ImVec2(18.0F, 66.0F));
        ImGui::SetNextWindowSize(ImVec2(390.0F, 134.0F));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06F, 0.08F, 0.13F, 0.88F));
        if (ImGui::Begin("2D Live Gameplay HUD##pie2d_hud", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar))
        {
            const ImVec2 hmp = ImGui::GetMousePos();
            pill_hovered = (hmp.x >= 18.0F && hmp.x <= 408.0F && hmp.y >= 66.0F && hmp.y <= 200.0F);
            const auto& hud = s2d.stage.hud();
            const std::string st_name(s2d.stage.name());
            ImGui::TextColored(ImVec4(0.35F, 0.95F, 0.65F, 1.0F),
                               "2D PLAY-IN-EDITOR  |  %s", st_name.c_str());
            ImGui::Text("HP: %d/%d   Score: %d   Gems: %d/%d   Lua VMs: %u",
                        hud.player_hp,
                        hud.max_hp,
                        hud.score,
                        hud.gems_collected,
                        hud.total_gems,
                        hud.active_lua_vms);
            ImGui::TextDisabled("Controls: A/D or Arrows = Run | Space/W = Jump | Time: %.1fs",
                                static_cast<double>(hud.sim_time_sec));
            if (hud.victory)
            {
                ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.25F, 1.0F),
                                   "VICTORY! Goal Portal Reached!");
            }
            ImGui::Checkbox("AI Autopilot##pie2d_auto", &s2d.autopilot_in_pie);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75F, 0.22F, 0.24F, 1.0F));
            if (ImGui::SmallButton("Stop 2D Play (Esc)"))
            {
                s2d.stage.exit_play();
                log_push("[2d-pie] Stopped 2D Play Mode");
            }
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
    else if (vp_w > 420.0F)
    {
        const float pill_w = std::min(vp_w - 16.0F, 920.0F);
        const float pill_x = vp_x0 + (vp_w - pill_w) * 0.5F;
        ImGui::SetNextWindowPos(ImVec2(pill_x, vp_y0 + 8.0F));
        ImGui::SetNextWindowSize(ImVec2(pill_w, 34.0F));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07F, 0.09F, 0.14F, 0.90F));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0F, 4.0F));
        if (ImGui::Begin("2D Canvas Tool Pill##pill2d", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar))
        {
            const ImVec2 pmp = ImGui::GetMousePos();
            pill_hovered = (pmp.x >= pill_x && pmp.x <= pill_x + pill_w &&
                            pmp.y >= vp_y0 + 8.0F && pmp.y <= vp_y0 + 42.0F);
            auto tool_btn = [&](const char* lbl, Tool2DKind kind) {
                const bool active = (s2d.active_tool == kind);
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    active ? ImVec4(0.16F, 0.55F, 0.92F, 1.0F) : ImVec4(0.14F, 0.17F, 0.24F, 1.0F));
                if (ImGui::SmallButton(lbl))
                {
                    s2d.active_tool = kind;
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
            };
            tool_btn("1:Select", Tool2DKind::kSelectMove);
            tool_btn("2:AutoTile", Tool2DKind::kTileBrush);
            tool_btn("3:RectFill", Tool2DKind::kRectFill);
            tool_btn("4:Erase", Tool2DKind::kEraser);
            tool_btn("5:Stamp", Tool2DKind::kStamp);
            tool_btn("6:Fill", Tool2DKind::kFloodFill);

            static const char* kGenreNames[] = {
                "Platformer",
                "Top-Down RPG",
                "Twin-Stick Arena",
                "Empty 2D Project",
                "Pong Arcade",
                "Break the Bridges"
            };
            ImGui::SetNextItemWidth(142.0F);
            if (ImGui::Combo("##genre_dropdown", &s2d.active_genre_template_idx, kGenreNames, 6))
            {
                s2d.command_bus.load_genre_template(
                    s2d.stage,
                    static_cast<cd::world2d::GenreTemplateKind2D>(s2d.active_genre_template_idx));
                s2d.selected_entity_id = s2d.stage.entities().empty() ? 0U : s2d.stage.entities().front().id;
                s2d.sync_lua_buffer_from_selected();
                s2d.last_status_msg = std::string("Loaded Template: ") + kGenreNames[s2d.active_genre_template_idx];
            }
            ImGui::SameLine();

            ImGui::Checkbox("Grid", &s2d.debug_opts.show_grid);
            ImGui::SameLine();
            ImGui::Checkbox("Colliders", &s2d.debug_opts.show_greedy_colliders);
            ImGui::SameLine();
            if (ImGui::SmallButton("Center"))
            {
                s2d.stage.camera().set_position({ 11.0F, 4.8F });
                s2d.stage.camera().set_zoom(0.92F);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Zoom %.2fx", static_cast<double>(s2d.stage.camera().zoom()));
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // =========================================================================
    // 4. Direct 2D GPU Canvas Mouse Interaction (Zoom, Pan, Select/Drag, Paint)
    // =========================================================================
    const ImVec2 mp = ImGui::GetMousePos();
    const bool inside_vp =
        (mp.x >= vp_x0 && mp.x <= vp_x1 && mp.y >= vp_y0 && mp.y <= vp_y1) && !pill_hovered;

    if (!s2d.stage.is_playing() && inside_vp && !io.WantCaptureMouse)
    {
        // Smooth cursor-anchored zoom
        if (std::abs(io.MouseWheel) > 1e-4F)
        {
            const cd::math::Vec2f screen_px { mp.x, mp.y };
            const cd::math::Vec2f before_w = s2d.stage.camera().screen_to_world(screen_px, vp_rect);
            const float factor = (io.MouseWheel > 0.0F) ? 1.12F : (1.0F / 1.12F);
            s2d.stage.camera().set_zoom(std::clamp(s2d.stage.camera().zoom() * factor, 0.25F, 6.0F));
            const cd::math::Vec2f after_w = s2d.stage.camera().screen_to_world(screen_px, vp_rect);
            const auto cpos = s2d.stage.camera().position();
            s2d.stage.camera().set_position({
                cpos.x + (before_w.x - after_w.x),
                cpos.y + (before_w.y - after_w.y)
            });
        }

        // RMB / MMB camera pan
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            if (s2d.pan_dragging)
            {
                const float dx = mp.x - s2d.prev_mouse_pos.x;
                const float dy = mp.y - s2d.prev_mouse_pos.y;
                const float inv_s = 1.0F / s2d.stage.camera().effective_scale_px_per_unit();
                const auto cpos = s2d.stage.camera().position();
                s2d.stage.camera().set_position({
                    cpos.x - dx * inv_s,
                    cpos.y + dy * inv_s
                });
            }
            s2d.pan_dragging = true;
        }
        else
        {
            s2d.pan_dragging = false;
        }

        const cd::math::Vec2f world_mouse =
            s2d.stage.camera().screen_to_world(cd::math::Vec2f { mp.x, mp.y }, vp_rect);
        const float ts = std::max(0.05F, s2d.stage.tilemap().tile_size());
        const int gx = static_cast<int>(std::floor(world_mouse.x / ts));
        const int gy = static_cast<int>(std::floor(world_mouse.y / ts));

        // Tool-specific Left Mouse Button authoring
        if (s2d.active_tool == Tool2DKind::kSelectMove)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const std::uint32_t hit = pick_entity2d_at_world(s2d.stage, world_mouse);
                if (hit != 0U)
                {
                    s2d.selected_entity_id = hit;
                    s2d.sync_lua_buffer_from_selected();
                    if (const auto* ent = s2d.stage.find_entity(hit))
                    {
                        s2d.command_bus.begin_stroke(s2d.stage, "Move Entity " + ent->name);
                        s2d.entity_drag_active   = true;
                        s2d.entity_drag_offset_x = ent->position.x - world_mouse.x;
                        s2d.entity_drag_offset_y = ent->position.y - world_mouse.y;
                    }
                }
            }
            if (s2d.entity_drag_active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (auto* ent = s2d.stage.find_entity(s2d.selected_entity_id))
                {
                    ent->position.x = std::round((world_mouse.x + s2d.entity_drag_offset_x) * 10.0F) * 0.1F;
                    ent->position.y = std::round((world_mouse.y + s2d.entity_drag_offset_y) * 10.0F) * 0.1F;
                    ent->home_position = ent->position;
                }
            }
            if (s2d.entity_drag_active && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                s2d.entity_drag_active = false;
                s2d.command_bus.end_stroke(s2d.stage);
            }
        }
        else if (s2d.active_tool == Tool2DKind::kTileBrush || s2d.active_tool == Tool2DKind::kEraser)
        {
            const std::uint16_t paint_id =
                (s2d.active_tool == Tool2DKind::kEraser) ? static_cast<std::uint16_t>(0U) : s2d.selected_tile_id;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                s2d.command_bus.begin_stroke(
                    s2d.stage,
                    paint_id == 0U ? "Erase 2D Tiles" : "Paint 2D Auto-Tiles");
                s2d.brush_stroke_open = true;
                s2d.last_painted_gx   = -99999;
                s2d.last_painted_gy   = -99999;
            }
            if (s2d.brush_stroke_open && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (gx != s2d.last_painted_gx || gy != s2d.last_painted_gy)
                {
                    s2d.stage.tilemap().set_tile(gx, gy, paint_id, s2d.auto_wang_tiling);
                    s2d.stage.rebuild_baked_colliders();
                    s2d.last_painted_gx = gx;
                    s2d.last_painted_gy = gy;
                }
            }
            if (s2d.brush_stroke_open && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                s2d.brush_stroke_open = false;
                s2d.command_bus.end_stroke(s2d.stage);
            }
        }
        else if (s2d.active_tool == Tool2DKind::kRectFill)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                s2d.rect_drag_active = true;
                s2d.rect_start_gx    = gx;
                s2d.rect_start_gy    = gy;
                s2d.rect_cur_gx      = gx;
                s2d.rect_cur_gy      = gy;
            }
            if (s2d.rect_drag_active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                s2d.rect_cur_gx = gx;
                s2d.rect_cur_gy = gy;
            }
            if (s2d.rect_drag_active && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                s2d.rect_drag_active = false;
                s2d.command_bus.paint_rect(
                    s2d.stage,
                    s2d.rect_start_gx,
                    s2d.rect_start_gy,
                    s2d.rect_cur_gx,
                    s2d.rect_cur_gy,
                    s2d.selected_tile_id);
                s2d.last_status_msg = "Painted 2D auto-tile rectangle.";
            }
        }
        else if (s2d.active_tool == Tool2DKind::kFloodFill)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const std::size_t filled = s2d.command_bus.flood_fill(s2d.stage, gx, gy, s2d.selected_tile_id);
                s2d.last_status_msg = "Flood-filled " + std::to_string(filled) + " tiles.";
            }
        }
        else if (s2d.active_tool == Tool2DKind::kStamp)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (s2d.stamp_cfg.pattern.size() !=
                    static_cast<std::size_t>(s2d.stamp_cfg.pattern_width * s2d.stamp_cfg.pattern_height))
                {
                    s2d.stamp_cfg.pattern.assign(
                        static_cast<std::size_t>(s2d.stamp_cfg.pattern_width * s2d.stamp_cfg.pattern_height),
                        s2d.selected_tile_id);
                }
                const std::size_t stamped = s2d.command_bus.stamp(
                    s2d.stage,
                    gx,
                    gy,
                    s2d.stamp_cfg.pattern_width,
                    s2d.stamp_cfg.pattern_height,
                    s2d.stamp_cfg.pattern,
                    s2d.auto_wang_tiling);
                s2d.last_status_msg = "Stamped " + std::to_string(stamped) + " tiles.";
            }
        }
    }
    else
    {
        s2d.pan_dragging = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (s2d.brush_stroke_open)
            {
                s2d.brush_stroke_open = false;
                s2d.command_bus.end_stroke(s2d.stage);
            }
            if (s2d.entity_drag_active)
            {
                s2d.entity_drag_active = false;
                s2d.command_bus.end_stroke(s2d.stage);
            }
            s2d.rect_drag_active = false;
        }
    }
    s2d.prev_mouse_pos = mp;

    // =========================================================================
    // 5. Render Stage2D into `s2d.canvas` (Tiles + Wang + Soft Shadows + Sprites)
    // =========================================================================
    cd::render2d::Render2DDebugOptions render_opts = s2d.debug_opts;
    if (s2d.stage.is_playing())
    {
        render_opts.show_grid          = false;
        render_opts.show_light_gizmos  = false;
        render_opts.show_camera_frame  = false;
    }
    s2d.stage.render(s2d.canvas, vp_rect, render_opts);

    // Highlight selected entity & interactive RectFill preview in Edit Mode
    if (!s2d.stage.is_playing())
    {
        constexpr std::uint64_t kOverlayKey = 0xFF00000000000000ULL;
        if (const auto* sel = s2d.stage.find_entity(s2d.selected_entity_id); sel != nullptr && sel->visible)
        {
            const auto p0 = s2d.stage.camera().world_to_screen(
                { sel->position.x - sel->size.x * 0.5F, sel->position.y + sel->size.y * 0.5F }, vp_rect);
            const auto p1 = s2d.stage.camera().world_to_screen(
                { sel->position.x + sel->size.x * 0.5F, sel->position.y - sel->size.y * 0.5F }, vp_rect);
            s2d.canvas.add_rect_outline(
                p0.x - 3.0F,
                p0.y - 3.0F,
                std::max(6.0F, (p1.x - p0.x) + 6.0F),
                std::max(6.0F, (p1.y - p0.y) + 6.0F),
                2.0F,
                cd::render2d::ColorRgba8 { 85U, 235U, 255U, 240U },
                kOverlayKey);
        }
        if (s2d.rect_drag_active)
        {
            const float ts = s2d.stage.tilemap().tile_size();
            const float wx0 = static_cast<float>(std::min(s2d.rect_start_gx, s2d.rect_cur_gx)) * ts;
            const float wy0 = static_cast<float>(std::min(s2d.rect_start_gy, s2d.rect_cur_gy)) * ts;
            const float wx1 = static_cast<float>(std::max(s2d.rect_start_gx, s2d.rect_cur_gx) + 1) * ts;
            const float wy1 = static_cast<float>(std::max(s2d.rect_start_gy, s2d.rect_cur_gy) + 1) * ts;
            const auto s0 = s2d.stage.camera().world_to_screen({ wx0, wy1 }, vp_rect);
            const auto s1 = s2d.stage.camera().world_to_screen({ wx1, wy0 }, vp_rect);
            s2d.canvas.add_solid_rect(
                s0.x,
                s0.y,
                std::max(2.0F, s1.x - s0.x),
                std::max(2.0F, s1.y - s0.y),
                cd::render2d::ColorRgba8 { 55U, 210U, 255U, 95U },
                kOverlayKey);
        }
        if (s2d.active_tool == Tool2DKind::kStamp && inside_vp)
        {
            const cd::math::Vec2f wm  = s2d.stage.camera().screen_to_world(cd::math::Vec2f { mp.x, mp.y }, vp_rect);
            const float           ts  = std::max(0.05F, s2d.stage.tilemap().tile_size());
            const int             sgx = static_cast<int>(std::floor(wm.x / ts));
            const int             sgy = static_cast<int>(std::floor(wm.y / ts));
            const float           wx0 = static_cast<float>(sgx) * ts;
            const float           wy0 = static_cast<float>(sgy) * ts;
            const float           wx1 = static_cast<float>(sgx + s2d.stamp_cfg.pattern_width) * ts;
            const float           wy1 = static_cast<float>(sgy + s2d.stamp_cfg.pattern_height) * ts;
            const auto            s0  = s2d.stage.camera().world_to_screen({ wx0, wy1 }, vp_rect);
            const auto            s1  = s2d.stage.camera().world_to_screen({ wx1, wy0 }, vp_rect);
            s2d.canvas.add_rect_outline(
                s0.x,
                s0.y,
                std::max(4.0F, s1.x - s0.x),
                std::max(4.0F, s1.y - s0.y),
                2.0F,
                cd::render2d::ColorRgba8 { 255U, 215U, 64U, 220U },
                kOverlayKey);
        }
        s2d.canvas.sort_commands();
    }

    if (s2d.stage.is_playing())
    {
        return;
    }

    // =========================================================================
    // 6. DOCKED PANELS (Left: Studio Workbench | Right: Inspector | Bottom: History)
    // =========================================================================

    // ======== LEFT DOCK: "Scene Outliner" ========
    if (ImGui::Begin("Scene Outliner", nullptr, ImGuiWindowFlags_NoTitleBar))
    {
        if (ImGui::BeginTabBar("Studio2DLeftTabs"))
        {
            if (ImGui::BeginTabItem("2D Scene"))
            {
                const std::string st_name(s2d.stage.name());
                ImGui::TextColored(ImVec4(0.45F, 0.90F, 1.0F, 1.0F),
                                   "%s (%zu entities)",
                                   st_name.c_str(),
                                   s2d.stage.entities().size());
                if (ImGui::SmallButton("+ Gem")) spawn_2d_prefab_from_catalog(0);
                ImGui::SameLine();
                if (ImGui::SmallButton("+ Drone")) spawn_2d_prefab_from_catalog(1);
                ImGui::SameLine();
                if (ImGui::SmallButton("+ Light")) spawn_2d_prefab_from_catalog(5);
                ImGui::Separator();

                for (const auto& ent : s2d.stage.entities())
                {
                    const bool is_sel = (ent.id == s2d.selected_entity_id);
                    const std::string row_lbl =
                        "[" + std::string(cd::world2d::entity_role_name_2d(ent.role)) + "] " +
                        ent.name + "##ent2d_" + std::to_string(ent.id);
                    if (ImGui::Selectable(row_lbl.c_str(), is_sel))
                    {
                        s2d.selected_entity_id = ent.id;
                        s2d.sync_lua_buffer_from_selected();
                    }
                }
                ImGui::Separator();
                if (s2d.selected_entity_id != 0U)
                {
                    if (ImGui::Button("Focus Camera on Selected", ImVec2(-1.0F, 22.0F)))
                    {
                        if (const auto* sel = s2d.stage.find_entity(s2d.selected_entity_id))
                        {
                            s2d.stage.camera().set_position(sel->position);
                        }
                    }
                    if (ImGui::Button("Delete Selected (Del)", ImVec2(-1.0F, 22.0F)))
                    {
                        (void)s2d.command_bus.delete_entity(s2d.stage, s2d.selected_entity_id);
                        s2d.selected_entity_id =
                            s2d.stage.entities().empty() ? 0U : s2d.stage.entities().front().id;
                        s2d.sync_lua_buffer_from_selected();
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Tiles & Wang"))
            {
                ImGui::TextColored(ImVec4(0.95F, 0.80F, 0.35F, 1.0F), "Tile Palette & 16-Tile Wang");
                ImGui::Checkbox("4-Bit Wang Auto-Tiling", &s2d.auto_wang_tiling);
                ImGui::Separator();

                for (const auto& pal : s2d.stage.tilemap().palette())
                {
                    if (pal.palette_id == 0U)
                    {
                        continue;
                    }
                    const bool active =
                        (s2d.selected_tile_id == pal.palette_id && s2d.active_tool != Tool2DKind::kEraser);

                    // Swatch color
                    const ImVec4 swatch_col(
                        static_cast<float>(pal.fill_color.r) / 255.0F,
                        static_cast<float>(pal.fill_color.g) / 255.0F,
                        static_cast<float>(pal.fill_color.b) / 255.0F,
                        1.0F);

                    ImGui::PushStyleColor(ImGuiCol_Button, swatch_col);
                    if (ImGui::Button(("##swatch_" + std::to_string(pal.palette_id)).c_str(), ImVec2(24.0F, 24.0F)))
                    {
                        s2d.selected_tile_id = pal.palette_id;
                        if (s2d.active_tool == Tool2DKind::kSelectMove || s2d.active_tool == Tool2DKind::kEraser)
                        {
                            s2d.active_tool = Tool2DKind::kTileBrush;
                        }
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine();

                    ImGui::PushStyleColor(
                        ImGuiCol_Button,
                        active ? ImVec4(0.18F, 0.56F, 0.90F, 1.0F) : ImVec4(0.14F, 0.17F, 0.24F, 0.9F));
                    std::string badges;
                    if (pal.solid)    badges += "[SOLID] ";
                    if (pal.one_way)  badges += "[1-WAY] ";
                    if (pal.hazard)   badges += "[HAZARD] ";
                    if (pal.emissive) badges += "[GLOW] ";
                    const std::string card_lbl =
                        "[" + std::to_string(pal.palette_id) + "] " + pal.name + "\n" +
                        (badges.empty() ? "[FLOOR] " : badges) +
                        (s2d.auto_wang_tiling ? "Wang: [^] [>] [v] [<]" : "") +
                        "##card_" + std::to_string(pal.palette_id);
                    if (ImGui::Button(card_lbl.c_str(), ImVec2(-1.0F, 38.0F)))
                    {
                        s2d.selected_tile_id = pal.palette_id;
                        if (s2d.active_tool == Tool2DKind::kSelectMove || s2d.active_tool == Tool2DKind::kEraser)
                        {
                            s2d.active_tool = Tool2DKind::kTileBrush;
                        }
                    }
                    ImGui::PopStyleColor();
                }

                ImGui::Separator();
                ImGui::Text("Sparse Chunks (16x16): %zu", s2d.stage.tilemap().active_chunk_count());
                ImGui::Text("Placed Tiles: %zu", s2d.stage.tilemap().non_empty_tile_count());
                ImGui::TextColored(
                    ImVec4(0.40F, 0.95F, 0.68F, 1.0F),
                    "Greedy Physics Boxes: %zu",
                    s2d.stage.baked_tile_colliders().size());
                ImGui::Checkbox("Visualize Greedy Colliders", &s2d.debug_opts.show_greedy_colliders);

                ImGui::Separator();
                if (ImGui::Button("Reset Starter Platformer Stage", ImVec2(-1.0F, 24.0F)))
                {
                    s2d.command_bus.load_starter_platformer(s2d.stage);
                    s2d.selected_entity_id =
                        s2d.stage.entities().empty() ? 0U : s2d.stage.entities().front().id;
                    s2d.sync_lua_buffer_from_selected();
                    s2d.last_status_msg = "Restored Neon Cavern starter stage.";
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("2D Prefabs"))
            {
                ImGui::TextColored(ImVec4(0.55F, 0.88F, 1.0F, 1.0F), "1-Click 2D Gameplay Prefabs");
                ImGui::TextDisabled("Spawns at current 2D camera center (Undoable):");
                ImGui::Separator();

                struct PrefabCard
                {
                    int         kind;
                    const char* label;
                };
                static constexpr std::array<PrefabCard, 11> kCards { {
                    { 0, "[+] Crystal Gem (+Lua Hover)" },
                    { 1, "[+] Patrol Drone (AI Enemy)" },
                    { 2, "[+] Moving Elevator (+Lua)" },
                    { 3, "[+] Bounce Jump Pad (Impulse)" },
                    { 4, "[+] Dynamic Physics Crate" },
                    { 5, "[+] 2D Point Light (Soft Shadows)" },
                    { 6, "[+] 2D Shadow Pillar (Occluder)" },
                    { 7, "[+] Victory Goal Portal" },
                    { 8, "[+] Arcade Player Paddle" },
                    { 9, "[+] Bouncing Energy Ball" },
                    { 10, "[+] Destructible Bridge Span" },
                } };
                for (const auto& card : kCards)
                {
                    if (ImGui::Button(card.label, ImVec2(-1.0F, 26.0F)))
                    {
                        spawn_2d_prefab_from_catalog(card.kind);
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Layers"))
            {
                ImGui::TextColored(ImVec4(0.45F, 0.90F, 1.0F, 1.0F), "2D Multi-Layer & Parallax Management");
                ImGui::Separator();

                auto& layers = s2d.stage.parallax_layers();
                for (std::size_t i = 0U; i < layers.size(); ++i)
                {
                    auto& l = layers[i];
                    ImGui::PushID(static_cast<int>(i));

                    // Visibility eye toggle
                    if (ImGui::SmallButton(l.visible ? "[Eye]" : "[ - ]"))
                    {
                        l.visible = !l.visible;
                    }
                    ImGui::SameLine();

                    // Lock padlock toggle
                    const bool is_locked = (s2d.locked_layers_mask & (1U << i)) != 0U;
                    if (ImGui::SmallButton(is_locked ? "[Lock]" : "[Unl]"))
                    {
                        s2d.locked_layers_mask ^= (1U << i);
                    }
                    ImGui::SameLine();

                    ImGui::Text("%s", l.name.c_str());

                    // Parallax scroll factor and auto-scroll speed controls
                    ImGui::SetNextItemWidth(70.0F);
                    ImGui::DragFloat("Parallax X", &l.scroll_factor.x, 0.02F, 0.0F, 3.0F, "%.2f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70.0F);
                    ImGui::DragFloat("Y##par_y", &l.scroll_factor.y, 0.02F, 0.0F, 3.0F, "%.2f");

                    ImGui::SetNextItemWidth(70.0F);
                    ImGui::DragFloat("Auto-Scroll X", &l.auto_scroll_velocity.x, 0.05F, -20.0F, 20.0F, "%.1f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70.0F);
                    ImGui::DragFloat("Y##scroll_y", &l.auto_scroll_velocity.y, 0.05F, -20.0F, 20.0F, "%.1f");
                    ImGui::Separator();
                    ImGui::PopID();
                }

                if (ImGui::Button("+ Add Skyline Parallax Layer", ImVec2(-1.0F, 24.0F)))
                {
                    cd::render2d::ParallaxLayer2D nl {};
                    nl.name                 = "Custom_Parallax_" + std::to_string(layers.size() + 1U);
                    nl.scroll_factor        = { 0.35F, 0.10F };
                    nl.auto_scroll_velocity = { 0.15F, 0.0F };
                    nl.procedural_style     = cd::render2d::ParallaxProceduralStyle::kCitySkyline;
                    layers.push_back(nl);
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // ======== RIGHT DOCK: "Inspector" ========
    if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar))
    {
        if (ImGui::BeginTabBar("Studio2DRightTabs"))
        {
            if (ImGui::BeginTabItem("Schema"))
            {
                draw_schema_driven_entity2d_inspector(s2d);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Lua 5.4"))
            {
                auto* ent = s2d.stage.find_entity(s2d.selected_entity_id);
                if (ent == nullptr)
                {
                    ImGui::TextDisabled("Select an entity to attach/edit a sandboxed Lua 5.4 script.");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.45F, 0.95F, 0.72F, 1.0F),
                                       "Entity Lua VM: %s", ent->name.c_str());
                    if (ent->lua_last_error.empty())
                    {
                        ImGui::TextColored(ImVec4(0.40F, 0.95F, 0.55F, 1.0F),
                                           "Status: Sandboxed Lua 5.4 Ready (on_tick)");
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(1.0F, 0.38F, 0.38F, 1.0F),
                                           "Lua Error: %s", ent->lua_last_error.c_str());
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("Presets:");
                    if (ImGui::SmallButton("Hover Sine"))
                    {
                        std::snprintf(
                            s2d.lua_script_buf.data(),
                            s2d.lua_script_buf.size(),
                            "%s",
                            "function on_tick(id, x, y, vx, vy, t, dt) return x, y + math.sin(t * 4.0) * 0.02, vx, vy end");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Orbit Drift"))
                    {
                        std::snprintf(
                            s2d.lua_script_buf.data(),
                            s2d.lua_script_buf.size(),
                            "%s",
                            "function on_tick(id, x, y, vx, vy, t, dt) return x + math.cos(t * 2.0) * 0.02, y, vx, vy end");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear"))
                    {
                        s2d.lua_script_buf[0] = '\0';
                    }

                    ImGui::InputText("on_tick##lua2d", s2d.lua_script_buf.data(), s2d.lua_script_buf.size());
                    if (ImGui::Button("Compile & Attach Lua Script", ImVec2(-1.0F, 25.0F)))
                    {
                        const bool ok = s2d.command_bus.attach_lua_script(
                            s2d.stage, ent->id, std::string(s2d.lua_script_buf.data()));
                        s2d.last_status_msg = ok
                            ? ("Compiled Lua 5.4 script on '" + ent->name + "'")
                            : ("Lua compile error on '" + ent->name + "'");
                        log_push("[lua-2d] " + s2d.last_status_msg);
                    }
                    ImGui::TextDisabled("Signature: on_tick(id, x, y, vx, vy, t, dt) -> x, y, vx, vy");
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("World & Light"))
            {
                ImGui::TextColored(ImVec4(1.0F, 0.82F, 0.38F, 1.0F), "2D Lighting & Shadow Solver");
                const auto amb_c = s2d.stage.ambient_color();
                float amb[3] { amb_c.x, amb_c.y, amb_c.z };
                if (ImGui::ColorEdit3("Ambient Color", amb))
                {
                    s2d.stage.set_ambient_color({ amb[0], amb[1], amb[2] });
                }
                float amb_i = s2d.stage.ambient_intensity();
                if (ImGui::SliderFloat("Ambient Intensity", &amb_i, 0.0F, 2.5F))
                {
                    s2d.stage.set_ambient_intensity(amb_i);
                }
                float grav = s2d.stage.gravity_y();
                if (ImGui::SliderFloat("Gravity Y", &grav, -60.0F, -2.0F))
                {
                    s2d.stage.set_gravity_y(grav);
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.48F, 0.88F, 1.0F, 1.0F), "Camera2D (Subpixel & Smooth Follow)");
                auto cam_cfg = s2d.stage.camera().config();
                bool cfg_dirty = false;
                cfg_dirty |= ImGui::SliderFloat("Zoom", &cam_cfg.zoom, 0.25F, 5.0F);
                cfg_dirty |= ImGui::SliderFloat("Follow Stiffness", &cam_cfg.follow_stiffness, 1.0F, 25.0F);
                cfg_dirty |= ImGui::SliderFloat("Lookahead (s)", &cam_cfg.lookahead_seconds, 0.0F, 0.6F);
                cfg_dirty |= ImGui::Checkbox("Pixel Snap", &cam_cfg.pixel_snap);
                if (cfg_dirty)
                {
                    s2d.stage.camera().set_config(cam_cfg);
                }
                if (ImGui::Button("Test Camera Trauma Shake", ImVec2(-1.0F, 22.0F)))
                {
                    s2d.stage.camera().add_trauma(0.65F);
                }

                ImGui::Separator();
                ImGui::TextDisabled("Debug Overlays:");
                ImGui::Checkbox("Show Tile Grid", &s2d.debug_opts.show_grid);
                ImGui::Checkbox("Show Greedy Colliders", &s2d.debug_opts.show_greedy_colliders);
                ImGui::Checkbox("Show 2D Light Gizmos", &s2d.debug_opts.show_light_gizmos);
                ImGui::Checkbox("Show Camera Frame", &s2d.debug_opts.show_camera_frame);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Flipbook"))
            {
                ImGui::TextColored(ImVec4(0.45F, 0.95F, 0.72F, 1.0F), "Flipbook Animation Preview & Scrubber");
                ImGui::Separator();

                static const char* kClips[] = { "hero_idle", "hero_run", "gem_spin", "drone_patrol" };
                ImGui::Combo("Clip", &s2d.anim_preview_clip_idx, kClips, 4);

                if (ImGui::Button(s2d.anim_preview_playing ? "|| Pause" : "> Play", ImVec2(75.0F, 24.0F)))
                {
                    s2d.anim_preview_playing = !s2d.anim_preview_playing;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &s2d.anim_preview_loop);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0F);
                ImGui::SliderFloat("FPS", &s2d.anim_preview_fps, 1.0F, 60.0F, "%.0f");

                constexpr int kFrameCount = 4;
                ImGui::SliderInt("Timeline Scrubber", &s2d.anim_preview_frame, 0, kFrameCount - 1);
                ImGui::TextDisabled("Frame: %d/%d | Duration: %.0f ms | UV: [0.0, 0.0, 1.0, 1.0]",
                                    s2d.anim_preview_frame + 1, kFrameCount, static_cast<double>(1000.0F / s2d.anim_preview_fps));

                if (ImGui::Button("Attach to Selected Entity", ImVec2(-1.0F, 24.0F)))
                {
                    if (auto* ent = s2d.stage.find_entity(s2d.selected_entity_id))
                    {
                        ent->anim_frame     = static_cast<std::uint32_t>(s2d.anim_preview_frame);
                        s2d.last_status_msg = "Attached animation frame to " + ent->name;
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Particles"))
            {
                ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.35F, 1.0F), "ParticleSystem2D Live Emitter Inspector");
                ImGui::Separator();

                auto& ps  = s2d.stage.particle_system();
                auto& cfg = ps.config();

                int                shape_idx = static_cast<int>(cfg.shape);
                static const char* kShapes[] = { "Point", "Circle", "Box", "Cone" };
                if (ImGui::Combo("Shape", &shape_idx, kShapes, 4))
                {
                    cfg.shape = static_cast<cd::world2d::ParticleEmitterShape2D>(shape_idx);
                }
                ImGui::DragFloat("Rate (part/s)", &cfg.emission_rate, 1.0F, 1.0F, 500.0F);

                ImGui::SetNextItemWidth(70.0F);
                ImGui::DragFloat("Life Min", &cfg.lifetime_min, 0.05F, 0.05F, 10.0F);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0F);
                ImGui::DragFloat("Max##life_max", &cfg.lifetime_max, 0.05F, 0.05F, 10.0F);

                ImGui::SetNextItemWidth(70.0F);
                ImGui::DragFloat("Speed Min", &cfg.speed_min, 0.1F, 0.0F, 50.0F);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0F);
                ImGui::DragFloat("Max##spd_max", &cfg.speed_max, 0.1F, 0.0F, 50.0F);

                ImGui::SetNextItemWidth(70.0F);
                ImGui::DragFloat("Gravity X", &cfg.gravity.x, 0.2F, -50.0F, 50.0F);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0F);
                ImGui::DragFloat("Y##grav_y", &cfg.gravity.y, 0.2F, -50.0F, 50.0F);

                ImGui::DragFloat("Drag", &cfg.drag, 0.05F, 0.0F, 5.0F);

                ImGui::TextDisabled("Presets:");
                if (ImGui::SmallButton("Sparkle")) { cfg = cd::world2d::ParticleEmitterPresets2D::sparkle_burst(); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Jump Dust")) { cfg = cd::world2d::ParticleEmitterPresets2D::jump_dust(); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Hit Impact")) { cfg = cd::world2d::ParticleEmitterPresets2D::hit_impact(); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Enemy Defeat")) { cfg = cd::world2d::ParticleEmitterPresets2D::enemy_defeat(); }

                ImGui::Separator();
                if (ImGui::Button("Test Burst at Camera", ImVec2(-1.0F, 26.0F)))
                {
                    s2d.stage.particle_system().spawn_burst(s2d.stage.camera().position(), cfg.burst_count, cfg);
                    s2d.last_status_msg = "Spawned particle burst at camera.";
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // ======== BOTTOM DOCK: "Project & Tools" ========
    if (ImGui::Begin("Project & Tools", nullptr, ImGuiWindowFlags_NoTitleBar))
    {
        if (ImGui::BeginTabBar("Studio2DBottomTabs"))
        {
            if (ImGui::BeginTabItem("AI Co-Pilot & CommandBus2D"))
            {
                ImGui::TextColored(
                    ImVec4(0.45F, 0.95F, 0.72F, 1.0F),
                    "Symmetric Human + AI CommandBus2D  |  Undo: %zu  |  Redo: %zu  |  %s",
                    s2d.command_bus.undo_depth(),
                    s2d.command_bus.redo_depth(),
                    s2d.last_status_msg.c_str());

                if (ImGui::SmallButton("Undo (Ctrl+Z)"))
                {
                    (void)s2d.command_bus.undo(s2d.stage);
                    s2d.sync_lua_buffer_from_selected();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Redo (Ctrl+Y)"))
                {
                    (void)s2d.command_bus.redo(s2d.stage);
                    s2d.sync_lua_buffer_from_selected();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("AI Macro: Sky Island + Gems"))
                {
                    static constexpr std::string_view kMacro =
                        "{\"commands\":["
                        "{\"op\":\"paint_rect_2d\",\"x0\":8,\"y0\":7,\"x1\":13,\"y1\":7,\"palette_id\":2},"
                        "{\"op\":\"spawn_entity_2d\",\"name\":\"Sky_Gem_A\",\"role\":3,\"pos\":[9.5,8.4],\"size\":[0.65,0.65]},"
                        "{\"op\":\"spawn_entity_2d\",\"name\":\"Sky_Gem_B\",\"role\":3,\"pos\":[11.5,8.4],\"size\":[0.65,0.65]}"
                        "]}";
                    const auto r = s2d.command_bus.execute_json(s2d.stage, kMacro);
                    s2d.last_status_msg = r.ok ? r.summary : ("AI Macro error: " + r.error_message);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("AI Macro: Warm Cave Torches"))
                {
                    static constexpr std::string_view kLights =
                        "{\"commands\":["
                        "{\"op\":\"spawn_entity_2d\",\"name\":\"Torch_West\",\"role\":8,\"pos\":[5.5,4.2],\"color\":[1.0,0.78,0.38,1.0]},"
                        "{\"op\":\"spawn_entity_2d\",\"name\":\"Torch_East\",\"role\":8,\"pos\":[16.5,5.2],\"color\":[0.45,0.88,1.0,1.0]}"
                        "]}";
                    const auto r = s2d.command_bus.execute_json(s2d.stage, kLights);
                    s2d.last_status_msg = r.ok ? r.summary : ("AI Lighting error: " + r.error_message);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Verify: Run 240f Headless Playtest"))
                {
                    s2d.last_playtest = cd::world2d::run_playtest_2d(s2d.stage, 240U, true, true);
                    s2d.has_playtest_report = true;
                    s2d.last_status_msg =
                        "Playtest 240f: gems=" + std::to_string(s2d.last_playtest.gems_collected) +
                        "/" + std::to_string(s2d.last_playtest.total_gems) +
                        ", victory=" + std::string(s2d.last_playtest.victory ? "YES" : "NO") +
                        ", dist=" + std::to_string(static_cast<int>(s2d.last_playtest.horizontal_distance_covered));
                    log_push("[playtest-2d] " + s2d.last_status_msg);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Export JSON Schema"))
                {
                    const std::string js = s2d.schema_registry.export_json_schema_draft2020();
                    std::ofstream out("chromodynamic_2d_schema.json", std::ios::trunc);
                    if (out.is_open())
                    {
                        out << js;
                        s2d.last_status_msg = "Exported Draft 2020-12 schema -> chromodynamic_2d_schema.json";
                    }
                }

                ImGui::InputText("AI JSON##cmd2d", s2d.ai_json_cmd_buf.data(), s2d.ai_json_cmd_buf.size());
                ImGui::SameLine();
                if (ImGui::SmallButton("Execute JSON"))
                {
                    const auto rep = s2d.command_bus.execute_json(
                        s2d.stage, std::string_view(s2d.ai_json_cmd_buf.data()));
                    if (rep.ok)
                    {
                        s2d.last_status_msg = rep.summary;
                        log_push("[ai-bus] " + rep.summary);
                    }
                    else
                    {
                        s2d.last_status_msg = "JSON Error: " + rep.error_message;
                    }
                }

                if (s2d.has_playtest_report)
                {
                    ImGui::TextColored(
                        ImVec4(0.55F, 0.88F, 1.0F, 1.0F),
                        "Last Playtest Telemetry: frames=%u | dist=%.1fm | gems=%d/%d | score=%d | victory=%s | lua_err=%u",
                        s2d.last_playtest.frames_simulated,
                        static_cast<double>(s2d.last_playtest.horizontal_distance_covered),
                        s2d.last_playtest.gems_collected,
                        s2d.last_playtest.total_gems,
                        s2d.last_playtest.score,
                        s2d.last_playtest.victory ? "YES" : "IN-PROGRESS",
                        s2d.last_playtest.lua_error_count);
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Pipelines (.cd2d / .aseprite / .ldtk / .cdpak)"))
            {
                ImGui::InputText("Stage File (.cd2d.json)", s2d.stage_file_buf.data(), s2d.stage_file_buf.size());
                ImGui::SameLine();
                if (ImGui::SmallButton("Save .cd2d"))
                {
                    const auto r = s2d.stage.save_to_file(s2d.stage_file_buf.data());
                    s2d.last_status_msg = r.has_value()
                        ? ("Saved " + std::string(s2d.stage_file_buf.data()))
                        : ("Save error: " + std::string(r.error().message));
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Load .cd2d"))
                {
                    const auto r = s2d.stage.load_from_file(s2d.stage_file_buf.data());
                    if (r.has_value())
                    {
                        s2d.command_bus.clear_history();
                        s2d.selected_entity_id =
                            s2d.stage.entities().empty() ? 0U : s2d.stage.entities().front().id;
                        s2d.sync_lua_buffer_from_selected();
                        s2d.last_status_msg = "Loaded " + std::string(s2d.stage_file_buf.data());
                    }
                    else
                    {
                        s2d.last_status_msg = "Load error: " + std::string(r.error().message);
                    }
                }

                ImGui::InputText("Import (.ldtk / .aseprite)", s2d.import_file_buf.data(), s2d.import_file_buf.size());
                ImGui::SameLine();
                if (ImGui::SmallButton("Import Asset"))
                {
                    const std::string path_str(s2d.import_file_buf.data());
                    if (path_str.find(".ldtk") != std::string::npos)
                    {
                        std::ifstream lf(path_str);
                        if (lf.is_open())
                        {
                            std::ostringstream ss;
                            ss << lf.rdbuf();
                            s2d.command_bus.begin_stroke(s2d.stage, "Import LDtk Level");
                            const auto lr = cd::render2d::import_ldtk_level_json(ss.str(), s2d.stage.tilemap());
                            if (lr.has_value())
                            {
                                s2d.stage.rebuild_baked_colliders();
                                s2d.command_bus.end_stroke(s2d.stage);
                                s2d.last_status_msg = "Imported LDtk '" + lr->level_identifier + "' (" +
                                                      std::to_string(lr->imported_tiles) + " tiles)";
                            }
                            else
                            {
                                s2d.last_status_msg = "LDtk parse error: " + std::string(lr.error().message);
                            }
                        }
                        else
                        {
                            s2d.last_status_msg = "File not found: " + path_str;
                        }
                    }
                    else
                    {
                        std::ifstream af(path_str, std::ios::binary);
                        if (af.is_open())
                        {
                            const std::vector<std::uint8_t> bytes(
                                (std::istreambuf_iterator<char>(af)),
                                std::istreambuf_iterator<char>());
                            const auto ar = cd::render2d::parse_aseprite_binary(bytes);
                            if (ar.has_value())
                            {
                                s2d.last_status_msg =
                                    "Imported Aseprite " + std::to_string(ar->width) + "x" +
                                    std::to_string(ar->height) + " (" + std::to_string(ar->frame_count) +
                                    " frames, " + std::to_string(ar->tags.size()) + " tags)";
                            }
                            else
                            {
                                s2d.last_status_msg = "Aseprite parse error: " + std::string(ar.error().message);
                            }
                        }
                        else
                        {
                            s2d.last_status_msg = "File not found: " + path_str;
                        }
                    }
                }

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.56F, 0.38F, 1.0F));
                if (ImGui::SmallButton("Package Standalone .cdpak + cd_player (F9)"))
                {
                    (void)export_2d_and_3d_standalone_bundle("build/export/ChromodynamicGame", log_push);
                }
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

inline void record_2d_canvas_if_active(
    cd::ui::renderer::DrawBatcher&   batcher,
    cd::ui::renderer_rhi::Submitter* submitter,
    cd::rhi::ICommandBuffer&         cmd,
    cd::rhi::Extent2D                extent)
{
    if (submitter == nullptr || !submitter->is_valid())
    {
        return;
    }
    auto& s2d = studio_2d();
    batcher.begin_frame();
    s2d.canvas.replay_into<
        cd::ui::renderer::DrawBatcher,
        cd::ui::renderer::Color,
        cd::ui::renderer::ScissorRect,
        cd::ui::renderer::AtlasUv>(batcher);
    if (submitter->upload(batcher))
    {
        submitter->record(cmd, extent);
    }
}

} // namespace cd::studio
