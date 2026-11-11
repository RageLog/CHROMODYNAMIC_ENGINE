// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioWorkspacePanels.hpp
//
// Studio Workbench (2-column Drag & Drop 3D Objects and Modifier Classes),
// Scene Hierarchy Outliner, Node3D Transform & Modifier Stack Inspector,
// Physics Telemetry Console, and Standalone Game Exporter.
// =============================================================================
#pragma once

#include "StudioPhysicsSolver.hpp"
#include <cd/app_common/CameraMath3D.hpp>
#include <filesystem>
#if defined(_WIN32)
#include <commdlg.h>
#endif

namespace cd::studio
{
// 11. Ergonomic Studio UI Panels (2-Col Drag & Drop Grid, Inspector, Hierarchy)
// =============================================================================
template <typename StateT, typename LogFn>
inline void draw_studio_modifier_stack_ui(
    ModifierStack& stack,
    const char*    stack_owner_label,
    StateT&        /*s*/,
    LogFn&&        log_push)
{
    ImGui::TextColored(ImVec4(0.55F, 0.85F, 1.0F, 1.0F),
                       "Modifier Stack (%s) [%zu active]",
                       stack_owner_label, stack.items.size());

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.24F, 0.34F, 0.90F));
    if (ImGui::Button("[+] DROP MODIFIER CLASS HERE (or click to browse)", ImVec2(-1.0F, 24.0F)))
    {
        ImGui::OpenPopup("add_modifier_class_popup");
    }
    ImGui::PopStyleColor();
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CD_STUDIO_MODIFIER"))
        {
            const char* cls_id = static_cast<const char*>(payload->Data);
            if (auto mod = instantiate_modifier_by_class(cls_id))
            {
                stack.add_or_replace(std::move(mod));
                studio_world().is_dragging_modifier = false;
                log_push(std::string("[modifier] Instantiated class ") + cls_id + " on " + stack_owner_label);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Quick single-click modifier add buttons
    auto try_quick_add = [&](const char* cid) {
        if (auto mod = instantiate_modifier_by_class(cid))
        {
            stack.add_or_replace(std::move(mod));
            log_push(std::string("[modifier] Quick-Added ") + cid + " to " + stack_owner_label);
        }
    };
    if (ImGui::SmallButton("+ RigidBody")) try_quick_add("RigidBodyModifier");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Collider"))  try_quick_add("BoxColliderModifier");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Character")) try_quick_add("CharacterControllerModifier");
    if (ImGui::SmallButton("+ Animator"))  try_quick_add("AnimatorStateMachineModifier");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Camera Lens")) try_quick_add("CameraLensModifier");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Cloud Layer")) try_quick_add("CloudLayerModifier");

    int remove_idx  = -1;
    int move_up_idx = -1;
    for (std::size_t i = 0; i < stack.items.size(); ++i)
    {
        auto& mod = stack.items[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();
        ImGui::Checkbox("##en", &mod->enabled);
        ImGui::SameLine();
        if (ImGui::SmallButton("^") && i > 0) move_up_idx = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) remove_idx = static_cast<int>(i);
        ImGui::SameLine();
        const bool open = ImGui::CollapsingHeader(
            std::string(mod->display_name()).c_str(),
            ImGuiTreeNodeFlags_DefaultOpen);

        // Right-click context menu on Modifier Header
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem(mod->enabled ? "Disable Modifier" : "Enable Modifier"))
            {
                mod->enabled = !mod->enabled;
            }
            if (ImGui::MenuItem("Reset to Defaults"))
            {
                if (auto fresh = instantiate_modifier_by_class(mod->class_name()))
                {
                    mod = std::move(fresh);
                }
            }
            if (ImGui::MenuItem("Remove Modifier"))
            {
                remove_idx = static_cast<int>(i);
            }
            ImGui::EndPopup();
        }

        if (open && mod->enabled)
        {
            ImGui::Indent(10.0F);
            mod->draw_inspector_ui();
            ImGui::Unindent(10.0F);
        }
        ImGui::PopID();
    }

    if (move_up_idx > 0)
    {
        std::swap(stack.items[static_cast<std::size_t>(move_up_idx)],
                  stack.items[static_cast<std::size_t>(move_up_idx - 1)]);
    }
    if (remove_idx >= 0)
    {
        log_push(std::string("[modifier] Removed class ") +
                 std::string(stack.items[static_cast<std::size_t>(remove_idx)]->class_name()));
        stack.items.erase(stack.items.begin() + remove_idx);
    }

    ImGui::Separator();
    if (ImGui::Button("+ Instantiate Modifier Class...", ImVec2(-1.0F, 26.0F)))
    {
        ImGui::OpenPopup("add_modifier_class_popup");
    }
    if (ImGui::BeginPopup("add_modifier_class_popup"))
    {
        ImGui::TextDisabled("Select C++ Modifier Class to Instantiate:");
        ImGui::Separator();
        for (const auto& desc : kModifierCatalog)
        {
            if (ImGui::MenuItem(
                    (std::string(desc.title) + "  (" + desc.class_id + ")").c_str()))
            {
                if (auto mod = instantiate_modifier_by_class(desc.class_id))
                {
                    stack.add_or_replace(std::move(mod));
                    log_push(std::string("[modifier] Instantiated ") + desc.class_id);
                }
            }
        }
        ImGui::EndPopup();
    }
}

template <typename StateT, typename LogFn>
inline void draw_ingame_hud_workspace(
    StateT&                s,
    float                  dt,
    const cd::math::Mat4f& vp,
    std::uint32_t          frame_w,
    std::uint32_t          frame_h,
    LogFn&&                log_push)
{
    auto& w = studio_world();
    const float sw = static_cast<float>(frame_w);
    const float sh = static_cast<float>(frame_h);

    // Check if the scene has user-placed UI/HUD elements or modifiers
    bool has_scene_ui_nodes = false;
    for (const auto& ent : s.entities)
    {
        if (ent.name.find("Ui") != std::string::npos ||
            ent.name.find("UI") != std::string::npos ||
            ent.name.find("HUD") != std::string::npos ||
            ent.name.find("Radar") != std::string::npos ||
            ent.name.find("Health") != std::string::npos ||
            ent.name.find("Crosshair") != std::string::npos ||
            ent.name.find("Hotbar") != std::string::npos)
        {
            has_scene_ui_nodes = true;
            break;
        }
        auto it = w.entity_modifiers.find(ent.handle.id);
        if (it != w.entity_modifiers.end())
        {
            for (const auto& mod : it->second.items)
            {
                if (mod && (mod->class_name().find("UI") != std::string_view::npos ||
                            mod->class_name().find("Hud") != std::string_view::npos))
                {
                    has_scene_ui_nodes = true;
                    break;
                }
            }
        }
        if (has_scene_ui_nodes) break;
    }

    // Minimal, elegant top-right floating pill for Play controls in Game View (always rendered)
    ImGui::SetNextWindowPos(ImVec2(sw - 255.0F, 56.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(242.0F, 40.0F), ImGuiCond_Always);
    ImGuiWindowFlags pill_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("##clean_play_controls", nullptr, pill_flags))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85F, 0.22F, 0.22F, 0.90F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95F, 0.32F, 0.32F, 1.0F));
        const bool stop_clicked = ImGui::Button("[ ] Stop Play (Esc)", ImVec2(132.0F, 26.0F));
        ImGui::PopStyleColor(2);

        if (stop_clicked || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            if (s.pie_playing) exit_pie_play_mode(s, log_push);
            cd::ui::chroma::state().workspace_mode = 0;
            w.pilot_scene_camera_idx = -1;
            log_push("[studio] Exited Game View / Play Mode to 3D Editor");
        }

        ImGui::SameLine();
        const bool hud_active = w.enable_demo_hud_overlay;
        if (hud_active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.60F, 0.35F, 0.90F));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25F, 0.75F, 0.45F, 1.0F));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.40F, 0.65F, 0.85F));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25F, 0.50F, 0.80F, 1.0F));
        }
        if (ImGui::Button(hud_active ? "- HUD" : "+ HUD", ImVec2(80.0F, 26.0F)))
        {
            w.enable_demo_hud_overlay = !w.enable_demo_hud_overlay;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Toggle Sample Demo HUD Overlay");
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::End();

    // If scene is clean/empty and demo HUD overlay is not explicitly enabled:
    if (!has_scene_ui_nodes && !w.enable_demo_hud_overlay)
    {
        return;
    }

    static float s_spread      = 0.18F;
    static int   s_active_slot = 0;
    static bool  s_show_tags   = true;

    // 1. Project 3D entities to screen and draw World-Space Entity Health/Status Tags
    if (s_show_tags)
    {
        std::vector<ImVec2> placed_tags;
        placed_tags.reserve(s.entities.size());
        for (std::size_t i = 0; i < s.entities.size(); ++i)
        {
            const auto& ent = s.entities[i];
            if (!ent.visible || ent.kind == PrimitiveKind::kSponza ||
                ent.name.find("Floor") != std::string::npos ||
                ent.name.find("Kinematic_Platform") != std::string::npos)
                continue;
            const auto* lt = s.scene.local(ent.handle);
            if (!lt) continue;

            const cd::math::Vec3f tag_world = {
                lt->value.position.x,
                lt->value.position.y + lt->value.scale.y * 0.85F + 0.45F,
                lt->value.position.z
            };
            ImVec2 tag_px{};
            if (project_world_to_screen(tag_world, vp, sw, sh, tag_px))
            {
                // De-overlap screen-space tag boxes so labels never collide
                for (int pass = 0; pass < 4; ++pass)
                {
                    bool collided = false;
                    for (const auto& prev : placed_tags)
                    {
                        if (std::fabs(tag_px.x - prev.x) < 150.0F &&
                            std::fabs(tag_px.y - prev.y) < 27.0F)
                        {
                            tag_px.y = prev.y - 28.0F;
                            collided = true;
                        }
                    }
                    if (!collided) break;
                }
                tag_px.y = std::clamp(tag_px.y, 92.0F, sh - 120.0F);
                placed_tags.push_back(tag_px);

                const float dx = lt->value.position.x - s.cam.eye.x;
                const float dy = lt->value.position.y - s.cam.eye.y;
                const float dz = lt->value.position.z - s.cam.eye.z;
                const float dist_m = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float hp_frac = (ent.name.find("Player") != std::string::npos)
                    ? std::clamp(w.player_hp / std::max(1.0F, w.player_max_hp), 0.0F, 1.0F)
                    : 1.0F;
                cd::ui::chroma::hud::DrawWorldSpaceEntityTag(
                    tag_px, ent.name.c_str(), dist_m, hp_frac, static_cast<int>(i) == s.selected);
            }
        }
    }

    // 2. Top-Right Radar Minimap with live entity blips
    ImVec2 blip_coords[16]{};
    std::size_t blip_count = 0;
    for (std::size_t i = 0; i < s.entities.size() && blip_count < 16U; ++i)
    {
        if (!s.entities[i].visible) continue;
        if (const auto* lt = s.scene.local(s.entities[i].handle))
        {
            blip_coords[blip_count] = ImVec2((lt->value.position.x - s.cam.eye.x) * 0.08F,
                                             (lt->value.position.z - s.cam.eye.z) * 0.08F);
            ++blip_count;
        }
    }
    const float cam_yaw = std::atan2(s.cam.target.z - s.cam.eye.z, s.cam.target.x - s.cam.eye.x);
    cd::ui::chroma::hud::DrawMinimapRadar(
        ImVec2(sw - 110.0F, 155.0F), 82.0F, cam_yaw,
        std::span<const ImVec2>(blip_coords, blip_count));

    // 3. Center Dynamic Crosshair
    cd::ui::chroma::hud::DrawDynamicCrosshair(
        ImVec2(sw * 0.5F, sh * 0.5F), 6.0F + s_spread * 16.0F, IM_COL32(90, 245, 165, 230));

    // 4. Bottom-Left Player Health, Shield & Energy Bars (driven by live StudioRuntimeWorld!)
    cd::ui::chroma::hud::DrawHealthShieldBar(
        ImVec2(28.0F, sh - 92.0F), 280.0F,
        w.player_hp, w.player_max_hp,
        w.player_shield, 100.0F,
        std::clamp(w.player_energy / 100.0F, 0.0F, 1.0F));

    // 5. Bottom-Center Inventory Hotbar (8 slots)
    const char* slot_names[8] = {
        "Pulse Rifle", "Gravity Beam", "Omni Wrench", "Nano Medkit",
        "EMP Grenade", "Scanner", "Jet Thruster", "Chrono Core"
    };
    const float slot_cooldowns[8] = { 0.0F, 0.25F, 0.0F, 0.0F, 0.60F, 0.0F, 0.15F, 0.0F };
    s_active_slot = cd::ui::chroma::hud::DrawInventoryHotbar(
        ImVec2(sw * 0.5F, sh - 10.0F), s_active_slot, slot_names, slot_cooldowns);

    // 6. Top-Left Mission & Live Gameplay Telemetry HUD Panel
    ImGui::SetNextWindowPos(ImVec2(24.0F, 72.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(385.0F, 285.0F), ImGuiCond_Always);
    if (ImGui::Begin("In-Game HUD & Mission Objectives"))
    {
        const double fps = (w.smoothed_fps > 1.0F)
            ? static_cast<double>(w.smoothed_fps)
            : ((dt > 1e-5F) ? (1.0 / static_cast<double>(dt)) : 60.0);
        ImGui::TextColored(ImVec4(0.35F, 0.95F, 0.55F, 1.0F),
                           "%s (%.0f FPS)",
                           w.active_level_name.c_str(), fps);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0F, 0.86F, 0.28F, 1.0F),
                           "SCORE: %d PTS  |  CORES: %d / %d  |  BOTS: %d",
                           w.game_score, w.collected_cores,
                           std::max(w.total_cores_in_level, w.collected_cores),
                           w.defeated_enemies);
        if (w.level_goal_reached)
        {
            ImGui::TextColored(ImVec4(0.25F, 1.0F, 0.65F, 1.0F),
                               "*** VICTORY! LEVEL GOAL REACHED! ***");
        }
        else
        {
            ImGui::TextDisabled("%s", w.hud_toast_message.c_str());
        }
        ImGui::TextDisabled("Controls: WASD/Arrows=Move | Space=Jump | B/LMB=Fire | P=AutoPlay");

        ImGui::PushStyleColor(ImGuiCol_Button, w.autopilot_play_active
            ? ImVec4(0.18F, 0.68F, 0.38F, 1.0F)
            : ImVec4(0.20F, 0.44F, 0.74F, 1.0F));
        if (ImGui::Button(w.autopilot_play_active ? "[Auto-Play AI: ACTIVE (P)]" : "[>] Auto-Play Level (P)", ImVec2(175.0F, 22.0F)))
        {
            if (!s.pie_playing) enter_pie_play_mode(s, log_push);
            w.autopilot_play_active = !w.autopilot_play_active;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75F, 0.32F, 0.16F, 1.0F));
        if (ImGui::Button("Fire(B)", ImVec2(76.0F, 22.0F)))
            shoot_physics_ball_from_camera(s, log_push);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Reset(R)", ImVec2(-1.0F, 22.0F)))
            reset_physics_to_home_positions(s, log_push);

        ImGui::Separator();
        ImGui::SliderFloat("Armor Integrity", &w.player_hp, 0.0F, 100.0F, "%.0f HP");
        ImGui::SliderFloat("Kinetic Shield", &w.player_shield, 0.0F, 100.0F, "%.0f SP");
        ImGui::SliderFloat("Reticle Bloom", &s_spread, 0.0F, 1.0F, "%.2f");
        ImGui::ToggleSwitch("3D World-Space Entity Tags", &s_show_tags);
    }
    ImGui::End();
}

template <typename StateT, typename LogFn>
inline void draw_universal_250_node_catalog_panel(StateT& s, LogFn&& log_push)
{
    auto& w = studio_world();

    // 1. Quick Essentials Toolbar (Top Strip)
    ImGui::TextColored(ImVec4(0.95F, 0.82F, 0.40F, 1.0F), "Quick Essentials:");
    ImGui::SameLine();
    struct QuickEssential {
        const char* label;
        const char* spawn_id;
        cd::ui::chroma::IconKind icon;
        ImU32 col;
    };
    constexpr std::array<QuickEssential, 10> kQuickEssentials {{
        { "+ Cube##qe_cube",        "Dynamic_Cube",        cd::ui::chroma::IconKind::kCube,     IM_COL32(95, 195, 255, 255) },
        { "+ Sphere##qe_sph",       "Dynamic_Sphere",      cd::ui::chroma::IconKind::kSphere,   IM_COL32(115, 225, 255, 255) },
        { "+ 3D Char##qe_3dc",      "Character_CesiumMan", cd::ui::chroma::IconKind::kAvatar,   IM_COL32(85, 245, 165, 255) },
        { "+ 2D Char##qe_2dc",      "Character_2D",        cd::ui::chroma::IconKind::kAvatar,   IM_COL32(95, 235, 210, 255) },
        { "+ Sun Light##qe_sun",    "Sun_Light",           cd::ui::chroma::IconKind::kLight,    IM_COL32(255, 220, 90, 255) },
        { "+ Point Light##qe_pt",   "Point_Light",         cd::ui::chroma::IconKind::kLight,    IM_COL32(255, 200, 80, 255) },
        { "+ Clouds##qe_cld",       "Volumetric_Clouds",   cd::ui::chroma::IconKind::kWorld,    IM_COL32(185, 215, 255, 255) },
        { "+ Fog##qe_fog",          "Volumetric_Fog",      cd::ui::chroma::IconKind::kWorld,    IM_COL32(200, 225, 245, 255) },
        { "+ Camera##qe_cam",       "Camera_3D",           cd::ui::chroma::IconKind::kCamera,   IM_COL32(200, 155, 255, 255) },
        { "+ Physics Box##qe_pbox", "Dynamic_Cube",        cd::ui::chroma::IconKind::kPhysics,  IM_COL32(95, 240, 175, 255) },
    }};

    for (std::size_t qi = 0; qi < kQuickEssentials.size(); ++qi)
    {
        if (qi > 0) ImGui::SameLine();
        const auto& qe = kQuickEssentials[qi];
        if (ImGui::IconButton(qe.label, qe.icon, qe.col, false, ImVec2(0.0F, 20.0F)))
        {
            if (std::strcmp(qe.spawn_id, "Volumetric_Clouds") == 0)
                (void)spawn_volumetric_clouds_entity(s, log_push);
            else if (std::strcmp(qe.spawn_id, "Volumetric_Fog") == 0)
                (void)spawn_volumetric_fog_entity(s, log_push);
            else if (std::strcmp(qe.spawn_id, "Camera_3D") == 0)
                (void)spawn_scene_camera_entity(s, false, log_push);
            else if (std::strcmp(qe.spawn_id, "Character_2D") == 0)
                (void)spawn_empty_character_vessel(s, true, log_push);
            else if (std::strcmp(qe.spawn_id, "Sun_Light") == 0)
                (void)spawn_studio_light_by_type(s, 0, log_push);
            else if (std::strcmp(qe.spawn_id, "Point_Light") == 0)
                (void)spawn_studio_light_by_type(s, 1, log_push);
            else
            {
                const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                (void)spawn_entity_with_modifiers(s, qe.spawn_id, { pt.x, 0.0F, pt.z }, log_push);
            }
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("CD_STUDIO_SPAWN", qe.spawn_id, std::strlen(qe.spawn_id) + 1);
            w.is_dragging_spawn = true;
            w.drag_spawn_id     = qe.spawn_id;
            ImGui::Text("Drop %s onto 3D Viewport", qe.spawn_id);
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Quick Essential: Click to Spawn or Drag onto 3D Viewport");
        }
    }
    ImGui::Separator();

    // 2. Task-Oriented 8 Category Filter Tabs + Search
    struct TaskCatFilter {
        const char* label;
        int         cat_id;
    };
    constexpr std::array<TaskCatFilter, 8> kTaskCatButtons {{
        { "All (250)",            0 },
        { "3D Objects",           1 },
        { "Characters & AI",      2 },
        { "Lights & Sky",         3 },
        { "Physics & Colliders",  4 },
        { "Cameras & Audio",      5 },
        { "2D & Sprites",         6 },
        { "UI & Logic",           7 },
    }};

    static int s_task_cat = 0;
    for (std::size_t ci = 0; ci < kTaskCatButtons.size(); ++ci)
    {
        if (ci > 0) ImGui::SameLine();
        const bool is_active_cat = (s_task_cat == kTaskCatButtons[ci].cat_id);
        ImGui::PushStyleColor(ImGuiCol_Button,
            is_active_cat ? ImVec4(0.15F, 0.52F, 0.91F, 1.0F)
                          : ImVec4(0.15F, 0.18F, 0.25F, 1.0F));
        if (ImGui::SmallButton(kTaskCatButtons[ci].label))
        {
            s_task_cat = kTaskCatButtons[ci].cat_id;
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0F);
    ImGui::InputTextWithHint("##node250_search", "Search...",
                             w.node_catalog_search, sizeof(w.node_catalog_search));
    if (w.node_catalog_search[0] != '\0')
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("X##clr250"))
            w.node_catalog_search[0] = '\0';
    }

    const auto all_unfiltered = filter_studio_nodes(-1, w.node_catalog_search);

    auto matches_task_cat = [](const StudioNodeDescriptor& d, int cat) -> bool {
        if (cat == 0) return true; // All
        std::string_view name = d.chroma_name;
        auto contains_icase = [](std::string_view text, std::string_view pat) {
            if (pat.empty()) return true;
            auto it = std::search(text.begin(), text.end(), pat.begin(), pat.end(),
                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
            return it != text.end();
        };
        if (cat == 1) // 3D Objects
            return d.category == StudioNodeCategory::Node3D || contains_icase(name, "Mesh") || contains_icase(name, "3D");
        if (cat == 2) // Characters & AI
            return contains_icase(name, "Char") || contains_icase(name, "Avatar") || contains_icase(name, "Agent") ||
                   contains_icase(name, "AI") || contains_icase(name, "Nav") || contains_icase(name, "Path") || d.construct_kind == ChromaConstructKind::Vessel;
        if (cat == 3) // Lights & Sky
            return contains_icase(name, "Light") || contains_icase(name, "Sun") || contains_icase(name, "Sky") ||
                   contains_icase(name, "Fog") || contains_icase(name, "Cloud") || contains_icase(name, "Probe") || contains_icase(name, "Aether") || contains_icase(name, "Radiance");
        if (cat == 4) // Physics & Colliders
            return contains_icase(name, "Physic") || contains_icase(name, "Rigid") || contains_icase(name, "Colli") ||
                   contains_icase(name, "Joint") || contains_icase(name, "Body") || contains_icase(name, "Area") || contains_icase(name, "Ray");
        if (cat == 5) // Cameras & Audio
            return contains_icase(name, "Camera") || contains_icase(name, "Audio") || contains_icase(name, "Sound") ||
                   contains_icase(name, "Music") || contains_icase(name, "Listener") || contains_icase(name, "Voice");
        if (cat == 6) // 2D & Sprites
            return d.category == StudioNodeCategory::Node2D || contains_icase(name, "2D") || contains_icase(name, "Sprite") ||
                   contains_icase(name, "Tile") || contains_icase(name, "Canvas");
        if (cat == 7) // UI & Logic
            return d.category == StudioNodeCategory::ControlUI || d.category == StudioNodeCategory::CoreSystem ||
                   contains_icase(name, "UI") || contains_icase(name, "Button") || contains_icase(name, "Label") ||
                   contains_icase(name, "Timer") || contains_icase(name, "State") || contains_icase(name, "Tree");
        return true;
    };

    std::vector<const StudioNodeDescriptor*> filtered;
    filtered.reserve(all_unfiltered.size());
    for (const auto* d : all_unfiltered)
    {
        if (d && matches_task_cat(*d, s_task_cat))
            filtered.push_back(d);
    }

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45F, 0.88F, 1.0F, 1.0F),
                       "| %zu Nodes", filtered.size());

    // Left Pane: Scrollable List of Matching Constructs
    ImGui::BeginChild("catalog_250_list_pane", ImVec2(265.0F, 0.0F), true);
    for (const auto* desc : filtered)
    {
        if (!desc) continue;
        const bool is_sel = (w.selected_catalog_node_id == desc->id);
        cd::ui::chroma::IconKind c_icon = cd::ui::chroma::IconKind::kNode;
        ImU32 c_col = IM_COL32(105, 200, 255, 255);
        switch (desc->category)
        {
            case StudioNodeCategory::CoreSystem:
                c_icon = cd::ui::chroma::IconKind::kWorld;
                c_col  = IM_COL32(130, 210, 255, 255);
                break;
            case StudioNodeCategory::Node2D:
                c_icon = cd::ui::chroma::IconKind::kCollider;
                c_col  = IM_COL32(105, 235, 195, 255);
                break;
            case StudioNodeCategory::Node3D:
                c_icon = cd::ui::chroma::IconKind::kCube;
                c_col  = IM_COL32(95, 195, 255, 255);
                break;
            case StudioNodeCategory::ControlUI:
                c_icon = cd::ui::chroma::IconKind::kUiWidget;
                c_col  = IM_COL32(205, 155, 255, 255);
                break;
            case StudioNodeCategory::NextGenSOTA:
                c_icon = cd::ui::chroma::IconKind::kSparkles;
                c_col  = IM_COL32(255, 205, 95, 255);
                break;
            default:
                break;
        }
        if (desc->construct_kind == ChromaConstructKind::Vessel)
        {
            c_icon = ( std::string_view(desc->chroma_name).find("Rover") != std::string_view::npos ||
                       std::string_view(desc->chroma_name).find("Vehicle") != std::string_view::npos )
                ? cd::ui::chroma::IconKind::kVehicle
                : cd::ui::chroma::IconKind::kAvatar;
            c_col = IM_COL32(85, 245, 165, 255);
        }
        else if (std::string_view(desc->chroma_name).find("Light") != std::string_view::npos ||
                 std::string_view(desc->chroma_name).find("Irradiance") != std::string_view::npos ||
                 std::string_view(desc->chroma_name).find("Probe") != std::string_view::npos)
        {
            c_icon = cd::ui::chroma::IconKind::kLight;
            c_col  = IM_COL32(255, 220, 90, 255);
        }
        else if (std::string_view(desc->chroma_name).find("Camera") != std::string_view::npos ||
                 std::string_view(desc->chroma_name).find("Lens") != std::string_view::npos)
        {
            c_icon = cd::ui::chroma::IconKind::kCamera;
            c_col  = IM_COL32(200, 155, 255, 255);
        }

        char id_badge[16] {};
        std::snprintf(id_badge, sizeof(id_badge), "#%03u", static_cast<unsigned>(desc->id));
        const std::string row_lbl = std::string(desc->chroma_name) + "##cat250_" + std::to_string(desc->id);
        if (ImGui::IconSelectable(row_lbl.c_str(), c_icon, c_col, is_sel, id_badge))
        {
            w.selected_catalog_node_id = desc->id;
        }
        // Double-click directly adds to scene
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            const cd::math::Vec3f spawn_pt = camera_spawn_point(s, 3.5F);
            (void)spawn_or_compose_studio_catalog_node(
                s, desc->id, CatalogComposeMode::SpawnRootElement, { spawn_pt.x, 0.0F, spawn_pt.z }, log_push);
        }
        // Drag onto 3D viewport
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("CD_STUDIO_SPAWN", desc->chroma_name, std::strlen(desc->chroma_name) + 1);
            w.is_dragging_spawn = true;
            w.drag_spawn_id     = desc->chroma_name;
            ImGui::Text("Drop %s onto 3D Viewport", desc->chroma_name);
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s\nDouble-click or Drag onto 3D Viewport to Spawn", desc->chroma_name);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Pane: Simplified Detail Card with Large Add Button
    ImGui::BeginChild("CatalogDetailCard", ImVec2(0.0F, 0.0F), true);
    const StudioNodeDescriptor* sel_desc = find_studio_node_by_id(w.selected_catalog_node_id);
    if (!filtered.empty())
    {
        const bool in_filtered = (sel_desc != nullptr) &&
            (std::find(filtered.begin(), filtered.end(), sel_desc) != filtered.end());
        if (!in_filtered)
        {
            sel_desc                   = filtered.front();
            w.selected_catalog_node_id = sel_desc->id;
        }
    }
    if (!sel_desc) sel_desc = &kStudioUniversalNodeCatalog[117];

    const cd::math::Vec3f spawn_pt = camera_spawn_point(s, 3.5F);

    // Single prominent Add to Scene button + Attach button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.58F, 0.34F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24F, 0.72F, 0.42F, 1.0F));
    if (ImGui::IconButton("+ Add to Scene##cat_add_scene", cd::ui::chroma::IconKind::kPlus,
                          IM_COL32(255, 255, 255, 255), false, ImVec2(160.0F, 26.0F)))
    {
        (void)spawn_or_compose_studio_catalog_node(
            s, sel_desc->id, CatalogComposeMode::SpawnRootElement, { spawn_pt.x, 0.0F, spawn_pt.z }, log_push);
    }
    ImGui::PopStyleColor(2);

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("CD_STUDIO_SPAWN", sel_desc->chroma_name, std::strlen(sel_desc->chroma_name) + 1);
        w.is_dragging_spawn = true;
        w.drag_spawn_id     = sel_desc->chroma_name;
        ImGui::Text("Drop %s onto 3D Viewport", sel_desc->chroma_name);
        ImGui::EndDragDropSource();
    }

    if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.40F, 0.65F, 1.0F));
        if (ImGui::IconButton("+ Attach to Selected##cat_attach", cd::ui::chroma::IconKind::kLayers,
                              IM_COL32(200, 230, 255, 255), false, ImVec2(170.0F, 26.0F)))
        {
            (void)spawn_or_compose_studio_catalog_node(
                s, sel_desc->id, CatalogComposeMode::NestAsChildElement, { spawn_pt.x, 0.0F, spawn_pt.z }, log_push);
        }
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.38F, 0.94F, 0.68F, 1.0F),
                       "%s", sel_desc->chroma_name);
    ImGui::SameLine();
    ImGui::TextDisabled("(#%03u • %s)",
                       static_cast<unsigned>(sel_desc->id),
                       studio_node_category_label(sel_desc->category));

    if (sel_desc->spawn_preset.bundled_summary != nullptr)
    {
        ImGui::TextColored(ImVec4(0.54F, 0.88F, 1.00F, 1.0F),
                           "Components: %s", sel_desc->spawn_preset.bundled_summary);
    }

    const float card_avail_px = std::max(320.0F, ImGui::GetContentRegionAvail().x - 10.0F);
    const std::size_t max_chars = static_cast<std::size_t>(std::max(44.0F, card_avail_px / 7.68F));
    auto draw_wrapped_labeled_line = [&](const ImVec4& col, const char* prefix, std::string_view body)
    {
        const std::string full = std::string(prefix) + std::string(body);
        if (full.size() <= max_chars)
        {
            ImGui::TextColored(col, "%s", full.c_str());
            return;
        }
        std::size_t split_pos = full.rfind(' ', max_chars);
        if (split_pos == std::string::npos || split_pos < 16U) split_pos = max_chars;
        const std::string line1 = full.substr(0, split_pos);
        const std::string line2 = "              " + full.substr(split_pos + (full[split_pos] == ' ' ? 1U : 0U));
        ImGui::TextColored(col, "%s", line1.c_str());
        ImGui::TextColored(col, "%s", line2.c_str());
    };
    draw_wrapped_labeled_line(ImVec4(0.84F, 0.88F, 0.96F, 1.0F), "Feature:   ", sel_desc->ux_upgrade);
    draw_wrapped_labeled_line(ImVec4(0.72F, 0.82F, 0.94F, 1.0F), "Details:   ", sel_desc->tech_upgrade);

    ImGui::EndChild();
}

template <typename StateT, typename LogFn>
inline void draw_node_and_app_lab_workspace(StateT& s, std::uint32_t frame_w, std::uint32_t frame_h, LogFn&& log_push)
{
    const float sw = static_cast<float>(frame_w);
    const float sh = static_cast<float>(frame_h);

    static float s_curve_pts[8] = {
        0.0F, 0.0F,
        0.28F, 0.72F,
        0.68F, 0.25F,
        1.0F, 1.0F
    };
    static float s_timeline_t  = 2.45F;
    static float s_knob_rough  = 0.32F;
    static float s_knob_metal  = 0.85F;
    static float s_knob_ior    = 1.50F;
    static float s_tint_col[3] = { 0.24F, 0.68F, 0.96F };
    static bool  s_hdr_bloom   = true;
    static bool  s_ray_shadows = true;
    static int   s_blend_mode  = 0;

    // Left Window: Node Graph & Shader Pipeline Editor + 250-Node Universal Catalog
    ImGui::SetNextWindowPos(ImVec2(14.0F, 68.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw * 0.62F - 20.0F, sh - 82.0F), ImGuiCond_Always);
    if (ImGui::Begin("ChromaUI Node Graph & 250-Node Universal Studio"))
    {
        ImGui::TextColored(ImVec4(0.45F, 0.85F, 1.0F, 1.0F),
                           "PBR Material & Post-FX Shader Graph (Drag Nodes, Cubic Bezier Wires)");
        ImGui::BeginNodeGraph("StudioShaderGraph", ImVec2(0.0F, sh * 0.42F));

        ImGui::BeginNode(1U, "Texture2D Sampler (Albedo)", ImVec2(32.0F, 42.0F),
                         ImVec2(205.0F, 86.0F), IM_COL32(45, 115, 185, 255));
        ImGui::NodePin(101U, "UV Coord", false, IM_COL32(120, 200, 255, 255));
        ImGui::NodePin(102U, "RGBA Out", true, IM_COL32(255, 180, 80, 255));
        ImGui::EndNode();

        ImGui::BeginNode(2U, "Fresnel & Roughness", ImVec2(32.0F, 175.0F),
                         ImVec2(205.0F, 86.0F), IM_COL32(145, 65, 175, 255));
        ImGui::NodePin(201U, "Roughness", true, IM_COL32(100, 235, 160, 255));
        ImGui::NodePin(202U, "Metallic", true, IM_COL32(100, 235, 160, 255));
        ImGui::EndNode();

        ImGui::BeginNode(3U, "PBR GGX Surface BRDF", ImVec2(315.0F, 82.0F),
                         ImVec2(215.0F, 118.0F), IM_COL32(35, 145, 95, 255));
        ImGui::NodePin(301U, "BaseColor", false, IM_COL32(255, 180, 80, 255));
        ImGui::NodePin(302U, "Roughness", false, IM_COL32(100, 235, 160, 255));
        ImGui::NodePin(303U, "Metallic", false, IM_COL32(100, 235, 160, 255));
        ImGui::NodePin(304U, "Radiance", true, IM_COL32(95, 195, 255, 255));
        ImGui::EndNode();

        ImGui::BeginNode(4U, "ACES Filmic Output", ImVec2(610.0F, 98.0F),
                         ImVec2(195.0F, 76.0F), IM_COL32(185, 85, 45, 255));
        ImGui::NodePin(401U, "Linear HDR", false, IM_COL32(95, 195, 255, 255));
        ImGui::EndNode();

        ImGui::NodeLink(102U, 301U, IM_COL32(255, 180, 80, 235), 2.6F);
        ImGui::NodeLink(201U, 302U, IM_COL32(100, 235, 160, 235), 2.4F);
        ImGui::NodeLink(202U, 303U, IM_COL32(100, 235, 160, 235), 2.4F);
        ImGui::NodeLink(304U, 401U, IM_COL32(95, 195, 255, 245), 2.8F);

        ImGui::EndNodeGraph();

        ImGui::SeparatorText("250 Element & Vessel Universal Catalog (219 Standard + 31 Next-Gen SOTA)");
        draw_universal_250_node_catalog_panel(s, log_push);
        (void)s_timeline_t;
    }
    ImGui::End();

    // Right Window: Standalone App Widget Gallery (Knobs, Curve Editor, Color Picker, Tree, Toggles)
    ImGui::SetNextWindowPos(ImVec2(sw * 0.62F + 6.0F, 68.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw * 0.38F - 20.0F, sh - 82.0F), ImGuiCond_Always);
    if (ImGui::Begin("ChromaUI Widget Catalog 2.0 Showcase"))
    {
        ImGui::TextColored(ImVec4(0.45F, 0.90F, 1.0F, 1.0F), "Rotary Knobs & Toggle Switches");
        ImGui::KnobFloat("Roughness", &s_knob_rough, 0.0F, 1.0F, 22.0F);
        ImGui::SameLine();
        ImGui::KnobFloat("Metallic", &s_knob_metal, 0.0F, 1.0F, 22.0F);
        ImGui::SameLine();
        ImGui::KnobFloat("IndexRefr", &s_knob_ior, 1.0F, 2.5F, 22.0F);

        ImGui::Separator();
        ImGui::ToggleSwitch("HDR Bloom Convolution Pass", &s_hdr_bloom);
        ImGui::ToggleSwitch("Ray-Traced Contact Shadows", &s_ray_shadows);

        ImGui::RadioButton("Opaque", &s_blend_mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("AlphaBlend", &s_blend_mode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Additive", &s_blend_mode, 2);

        ImGui::Separator();
        ImGui::ColorEdit3("Surface Albedo (Click Swatch)", s_tint_col);

        ImGui::Separator();
        ImGui::CurveEditor("Easing Response Curve", s_curve_pts, ImVec2(0.0F, 125.0F));

        ImGui::SeparatorText("Telemetry, Search & Hierarchy");
        static char s_node_filter[64] = "GGX Surface BRDF";
        ImGui::InputTextWithHint("Filter", "Search shader nodes...", s_node_filter, sizeof(s_node_filter));
        ImGui::ProgressBar(0.84F, ImVec2(-1.0F, 16.0F), "Shader Permutation Cache 84%");

        static constexpr float kFrameTimes[16] = {
            4.2F, 4.4F, 4.1F, 4.8F, 5.1F, 4.5F, 4.3F, 4.0F,
            4.2F, 4.6F, 4.3F, 4.1F, 3.9F, 4.2F, 4.4F, 4.1F
        };
        ImGui::PlotLines("##gpu_ft", kFrameTimes, 16, 0, "GPU Pass Frame Time (4.1 ms)", 3.0F, 6.5F, ImVec2(0.0F, 48.0F));

        if (ImGui::TreeNodeEx("RenderGraph Pass Pipeline", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TreeNodeEx("1. GBuffer Velocity + Normal (0.82 ms)", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::TreeNodeEx("2. Direct Lighting + Ray Shadows (1.45 ms)", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::TreeNodeEx("3. ACES Filmic + ChromaUI Pass (0.34 ms)", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

template <typename StateT, typename LogFn>
inline void draw_camera_preview_overlay(
    StateT&                s,
    std::uint32_t          frame_w,
    std::uint32_t          frame_h,
    LogFn&&                log_push)
{
    auto& w = studio_world();

    // Check if a camera is piloted or selected
    int target_cam_idx = w.pilot_scene_camera_idx;
    if (target_cam_idx < 0)
    {
        if (s.selected_kind == SelKind::kEntity && s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
        {
            auto& ent = s.entities[static_cast<std::size_t>(s.selected)];
            if (ent.name.find("Camera") != std::string::npos ||
                w.entity_modifiers[ent.handle.id].template find<CameraLensModifier>() != nullptr)
            {
                target_cam_idx = s.selected;
            }
        }
    }

    if (target_cam_idx < 0 || !std::cmp_less(target_cam_idx, s.entities.size())) return;

    auto& cam_ent = s.entities[static_cast<std::size_t>(target_cam_idx)];
    if (!cam_ent.visible) return;

    auto& cam_ems = w.entity_modifiers[cam_ent.handle.id];
    auto* cam_mod = cam_ems.template find<CameraLensModifier>();
    auto* cam_lt  = s.scene.local(cam_ent.handle);

    const float preview_w = 260.0F;
    const float preview_h = 205.0F;
    const float px = std::max(290.0F, static_cast<float>(frame_w) - 340.0F - preview_w - 12.0F);
    const float py = std::max(48.0F, static_cast<float>(frame_h) - 230.0F - preview_h - 12.0F);

    ImGui::SetNextWindowPos(ImVec2(px, py));
    ImGui::SetNextWindowSize(ImVec2(preview_w, preview_h));
    ImGui::SetNextWindowBgAlpha(0.92F);

    const std::string win_title = std::string("Camera: ") + cam_ent.name + " (16:9)##cam_prev_overlay";
    if (ImGui::Begin(win_title.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse))
    {
        const bool is_piloting = (w.pilot_scene_camera_idx == target_cam_idx);
        if (is_piloting)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.4F, 0.4F, 1.0F));
            if (ImGui::SmallButton("Exit Pilot##exit_pilot"))
            {
                w.pilot_scene_camera_idx = -1;
                log_push("[camera] Exited camera piloting");
            }
            ImGui::PopStyleColor();
        }
        else
        {
            if (ImGui::SmallButton("Pilot View##pilot_btn"))
            {
                w.pilot_scene_camera_idx = target_cam_idx;
                log_push(std::string("[camera] Piloting ") + cam_ent.name);
            }
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Align to View##align_cam"))
        {
            if (cam_lt != nullptr)
            {
                cam_lt->value.position = s.cam.eye;
                record_entity_home_transform(s, cam_ent.handle);
                s.scene.update_transforms();
                log_push("[camera] Aligned '" + cam_ent.name + "' to editor view");
            }
        }

        if (cam_mod != nullptr)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.42F, 0.95F, 0.65F, 1.0F), "FOV: %.0f deg", static_cast<double>(cam_mod->fov_deg));
        }

        // Draw 16:9 Horizon & Crosshair canvas
        const ImVec2 cpos = ImGui::GetCursorScreenPos();
        const float canvas_w = preview_w - 20.0F;
        const float canvas_h = canvas_w * (9.0F / 16.0F); // 16:9 aspect ratio
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (dl != nullptr)
        {
            const ImVec2 cmax(cpos.x + canvas_w, cpos.y + canvas_h);
            // Background fill
            dl->AddRectFilled(cpos, cmax, IM_COL32(18, 22, 28, 240), 4.0F);
            // Border
            dl->AddRect(cpos, cmax, is_piloting ? IM_COL32(255, 185, 65, 255) : IM_COL32(75, 140, 210, 220), 4.0F, 0, 1.5F);

            // Horizon line
            const float mid_y = cpos.y + canvas_h * 0.5F;
            const float mid_x = cpos.x + canvas_w * 0.5F;
            dl->AddLine(ImVec2(cpos.x + 8.0F, mid_y), ImVec2(cmax.x - 8.0F, mid_y), IM_COL32(120, 160, 200, 120), 1.0F);

            // Crosshair
            dl->AddLine(ImVec2(mid_x - 14.0F, mid_y), ImVec2(mid_x + 14.0F, mid_y), IM_COL32(255, 255, 255, 200), 1.0F);
            dl->AddLine(ImVec2(mid_x, mid_y - 10.0F), ImVec2(mid_x, mid_y + 10.0F), IM_COL32(255, 255, 255, 200), 1.0F);

            // 16:9 Safe Title Frame
            const float safe_inset_x = canvas_w * 0.10F;
            const float safe_inset_y = canvas_h * 0.10F;
            dl->AddRect(ImVec2(cpos.x + safe_inset_x, cpos.y + safe_inset_y),
                        ImVec2(cmax.x - safe_inset_x, cmax.y - safe_inset_y),
                        IM_COL32(100, 200, 255, 70), 2.0F, 0, 1.0F);

            // Overlay watermark text
            const char* tag = is_piloting ? "PILOTING CAMERA (ACTIVE)" : "16:9 PREVIEW";
            dl->AddText(ImVec2(cpos.x + 8.0F, cpos.y + 6.0F),
                        is_piloting ? IM_COL32(255, 200, 80, 230) : IM_COL32(160, 200, 240, 180),
                        tag);

            // Advance cursor below canvas
            ImGui::SetCursorScreenPos(ImVec2(cpos.x, cmax.y + 6.0F));
        }

        if (cam_mod != nullptr)
        {
            ImGui::PushItemWidth(110.0F);
            (void)ImGui::SliderFloat("FOV##prev_fov", &cam_mod->fov_deg, 20.0F, 110.0F, "%.0f deg");
            ImGui::SameLine();
            (void)ImGui::SliderFloat("Near##prev_nr", &cam_mod->near_plane, 0.05F, 5.0F, "%.2fm");
            ImGui::PopItemWidth();
        }
        else if (cam_lt != nullptr)
        {
            ImGui::TextDisabled("Pos: (%.1f, %.1f, %.1f)",
                                static_cast<double>(cam_lt->value.position.x),
                                static_cast<double>(cam_lt->value.position.y),
                                static_cast<double>(cam_lt->value.position.z));
        }
    }
    ImGui::End();
}

template <typename StateT, typename LogFn>
inline void delete_studio_light_at(StateT& s, std::size_t li, LogFn&& log_push)
{
    if (li >= s.lights.size()) return;
    auto& w = studio_world();
    const std::string nm = s.lights[li].name;
    s.lights.erase(s.lights.begin() + static_cast<std::ptrdiff_t>(li));
    log_push("[light] Deleted light: " + nm);
    if (s.selected_kind == SelKind::kLight)
    {
        if (std::cmp_equal(s.selected, li))
        {
            s.selected = s.lights.empty() ? -1 : std::min(s.selected, static_cast<int>(s.lights.size()) - 1);
            if (s.selected < 0) s.selected_kind = SelKind::kEntity;
        }
        else if (std::cmp_greater(s.selected, li))
        {
            --s.selected;
        }
    }
    if (s.lights.empty())
    {
        s.fx.clouds_coverage    = 0.0F;
        s.fx.volumetric_fog_on  = false;
        s.fx.fog_density        = 0.0F;
        s.fx.shafts_strength    = 0.0F;
        s.fx.aerial_perspective = 0.0F;
        if (auto* sky = w.environment_modifiers.template find<SkyAtmosphereModifier>())
        {
            sky->enabled         = false;
            sky->clouds_coverage = 0.0F;
        }
    }
}

template <typename LightRowT, typename LogFn>
inline void draw_studio_light_controls(LightRowT& lr, LogFn&& log_push, const char* id_suffix = "")
{
    // Light Type selector buttons
    ImGui::TextDisabled("Light Type:");
    ImGui::SameLine();
    const bool is_sun   = (lr.light.type == cd::light::LightType::kDirectional);
    const bool is_point = (lr.light.type == cd::light::LightType::kPoint);
    const bool is_spot  = (lr.light.type == cd::light::LightType::kSpot);
    const bool is_rect  = (lr.light.type == cd::light::LightType::kRectArea);

    if (is_sun) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35F, 0.45F, 0.20F, 1.0F));
    if (ImGui::SmallButton(("Sun##tp_sun" + std::string(id_suffix)).c_str()))
    {
        lr.light.type = cd::light::LightType::kDirectional;
        log_push("[light] Switched '" + lr.name + "' to Directional Sun");
    }
    if (is_sun) ImGui::PopStyleColor();
    ImGui::SameLine();

    if (is_point) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35F, 0.45F, 0.20F, 1.0F));
    if (ImGui::SmallButton(("Point##tp_pt" + std::string(id_suffix)).c_str()))
    {
        lr.light.type = cd::light::LightType::kPoint;
        log_push("[light] Switched '" + lr.name + "' to Point Omni");
    }
    if (is_point) ImGui::PopStyleColor();
    ImGui::SameLine();

    if (is_spot) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35F, 0.45F, 0.20F, 1.0F));
    if (ImGui::SmallButton(("Spot##tp_sp" + std::string(id_suffix)).c_str()))
    {
        lr.light.type = cd::light::LightType::kSpot;
        log_push("[light] Switched '" + lr.name + "' to Spot Cone");
    }
    if (is_spot) ImGui::PopStyleColor();
    ImGui::SameLine();

    if (is_rect) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35F, 0.45F, 0.20F, 1.0F));
    if (ImGui::SmallButton(("Rect##tp_rc" + std::string(id_suffix)).c_str()))
    {
        lr.light.type = cd::light::LightType::kRectArea;
        log_push("[light] Switched '" + lr.name + "' to Rect Area Softbox");
    }
    if (is_rect) ImGui::PopStyleColor();

    // Position (for non-directional)
    if (lr.light.type != cd::light::LightType::kDirectional)
    {
        ImGui::DragFloat3(("Pos (m)##pos" + std::string(id_suffix)).c_str(), &lr.light.position.x, 0.1F);
    }

    // Direction (for directional, spot, rect area)
    if (lr.light.type == cd::light::LightType::kDirectional ||
        lr.light.type == cd::light::LightType::kSpot ||
        lr.light.type == cd::light::LightType::kRectArea)
    {
        if (ImGui::DragFloat3(("Dir##dir" + std::string(id_suffix)).c_str(), &lr.light.direction.x, 0.02F, -1.0F, 1.0F))
        {
            const float len_sq = lr.light.direction.x * lr.light.direction.x +
                                 lr.light.direction.y * lr.light.direction.y +
                                 lr.light.direction.z * lr.light.direction.z;
            if (len_sq > 1e-4F)
            {
                const float inv_l = 1.0F / std::sqrt(len_sq);
                lr.light.direction.x *= inv_l;
                lr.light.direction.y *= inv_l;
                lr.light.direction.z *= inv_l;
            }
        }
    }

    // Color Temperature / Kelvin CCT
    if (ImGui::SliderFloat(("Kelvin (CCT)##cct" + std::string(id_suffix)).c_str(), &lr.kelvin, 1800.0F, 15000.0F, "%.0f K"))
    {
        lr.light.color = cd::light::cct_to_linear_rgb(lr.kelvin);
    }
    ImGui::ColorEdit3(("Tint##col" + std::string(id_suffix)).c_str(), &lr.light.color.x);

    // Intensity
    if (lr.light.type == cd::light::LightType::kDirectional && lr.light.intensity > 0.0F && lr.light.intensity <= 250.0F)
    {
        lr.light.intensity *= 4000.0F; // Upgrade legacy low scale (0..25) to physical lux (0..100000)
    }
    const float max_int = (lr.light.type == cd::light::LightType::kDirectional) ? 100000.0F : 120000.0F;
    const char* int_fmt = (lr.light.type == cd::light::LightType::kDirectional) ? "%.0f lux" : "%.0f lm";
    const char* int_lbl = (lr.light.type == cd::light::LightType::kDirectional) ? "Intensity (lux)##int" : "Intensity (lm)##int";
    ImGui::SliderFloat((std::string(int_lbl) + id_suffix).c_str(), &lr.light.intensity, 0.0F, max_int, int_fmt);

    // Range
    if (lr.light.type != cd::light::LightType::kDirectional)
    {
        ImGui::SliderFloat(("Range (m)##rng" + std::string(id_suffix)).c_str(), &lr.light.range, 0.5F, 100.0F, "%.1f m");
    }

    // Spot-specific cone controls
    if (lr.light.type == cd::light::LightType::kSpot)
    {
        bool cone_changed = false;
        if (ImGui::SliderFloat(("Inner Cone (cos)##ic" + std::string(id_suffix)).c_str(), &lr.light.cos_inner_cone, 0.0F, 1.0F, "%.3f"))
            cone_changed = true;
        if (ImGui::SliderFloat(("Outer Cone (cos)##oc" + std::string(id_suffix)).c_str(), &lr.light.cos_outer_cone, 0.0F, lr.light.cos_inner_cone, "%.3f"))
            cone_changed = true;
        if (cone_changed)
        {
            lr.light.cos_outer_cone = std::min(lr.light.cos_outer_cone, lr.light.cos_inner_cone - 0.001F);
            lr.light.inv_cone_range = 1.0F / std::max(0.001F, lr.light.cos_inner_cone - lr.light.cos_outer_cone);
        }
    }

    // Area-specific size controls
    if (lr.light.type == cd::light::LightType::kRectArea || lr.light.type == cd::light::LightType::kDiskArea)
    {
        ImGui::DragFloat(("Width (m)##aw" + std::string(id_suffix)).c_str(), &lr.light.area_width, 0.05F, 0.05F, 50.0F, "%.2f m");
        ImGui::DragFloat(("Height (m)##ah" + std::string(id_suffix)).c_str(), &lr.light.area_height, 0.05F, 0.05F, 50.0F, "%.2f m");
    }
}


} // namespace cd::studio
