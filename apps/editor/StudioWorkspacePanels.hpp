// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioWorkspacePanels.hpp
//
// Studio Workbench (2-column Drag & Drop 3D Objects and Modifier Classes),
// Scene Hierarchy Outliner, Node3D Transform & Modifier Stack Inspector,
// Physics Telemetry Console, and Standalone Game Exporter.
// =============================================================================
#pragma once

#include "StudioPhysicsSolver.hpp"
#include <filesystem>

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
    ImGui::Button("[+] DROP MODIFIER CLASS HERE (or click + Add below)", ImVec2(-1.0F, 24.0F));
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

    // Top Row: Category Filter Tabs (-1=All 250, 0=Core(21), 1=2D(50), 2=3D(88), 3=UI(60), 4=SOTA(31)) + Search
    struct CatFilterItem
    {
        const char* label;
        int         cat_idx;
    };
    constexpr std::array<CatFilterItem, 6> kCatButtons {{
        { "All (250)",        -1 },
        { "Core (21)",         0 },
        { "2D (50)",           1 },
        { "3D (88)",           2 },
        { "UI (60)",           3 },
        { "Next-Gen SOTA (31)", 4 },
    }};
    for (std::size_t ci = 0; ci < kCatButtons.size(); ++ci)
    {
        if (ci > 0) ImGui::SameLine();
        const bool is_active_cat = (w.node_catalog_category_filter == kCatButtons[ci].cat_idx);
        ImGui::PushStyleColor(ImGuiCol_Button,
            is_active_cat ? ImVec4(0.15F, 0.52F, 0.91F, 1.0F)
                          : ImVec4(0.15F, 0.18F, 0.25F, 1.0F));
        if (ImGui::SmallButton(kCatButtons[ci].label))
        {
            w.node_catalog_category_filter = kCatButtons[ci].cat_idx;
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0F);
    ImGui::InputTextWithHint("##node250_search", "Search 250 constructs...",
                             w.node_catalog_search, sizeof(w.node_catalog_search));
    if (w.node_catalog_search[0] != '\0')
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##clr250"))
            w.node_catalog_search[0] = '\0';
    }

    const auto filtered = filter_studio_nodes(w.node_catalog_category_filter, w.node_catalog_search);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45F, 0.88F, 1.0F, 1.0F),
                       "| %zu / %zu Constructs", filtered.size(), kStudioTotalCatalogCount);

    // Left Pane: Scrollable Icon-Driven List of Matching Constructs
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
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("#%03u %s  <->  Godot 4: %s (%s)",
                              static_cast<unsigned>(desc->id),
                              desc->chroma_name,
                              desc->godot_name,
                              desc->video_timestamp);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Pane: Pinned Action Buttons on Top + Clean Construct Specification Card
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

    // Row 1: Pinned 3 Icon-Driven Composition Mode Action Buttons
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0F, 2.0F));
    const cd::math::Vec3f spawn_pt = camera_spawn_point(s, 3.5F);
    if (ImGui::IconButton("Spawn Root Element##cat_spawn", cd::ui::chroma::IconKind::kPlus,
                          IM_COL32(85, 245, 165, 255), false, ImVec2(148.0F, 21.0F)))
    {
        (void)spawn_or_compose_studio_catalog_node(
            s, sel_desc->id, CatalogComposeMode::SpawnRootElement, { spawn_pt.x, 0.0F, spawn_pt.z }, log_push);
    }
    ImGui::SameLine();
    if (ImGui::IconButton("Nest as Child##cat_nest", cd::ui::chroma::IconKind::kLayers,
                          IM_COL32(105, 200, 255, 255), false, ImVec2(124.0F, 21.0F)))
    {
        (void)spawn_or_compose_studio_catalog_node(
            s, sel_desc->id, CatalogComposeMode::NestAsChildElement, { spawn_pt.x, 0.0F, spawn_pt.z }, log_push);
    }
    ImGui::SameLine();
    if (ImGui::IconButton("Fold Capsule##cat_fold", cd::ui::chroma::IconKind::kNode,
                          IM_COL32(205, 155, 255, 255), false, ImVec2(118.0F, 21.0F)))
    {
        (void)spawn_or_compose_studio_catalog_node(
            s, sel_desc->id, CatalogComposeMode::FoldAsInlineCapsule, { spawn_pt.x, 0.0F, spawn_pt.z }, log_push);
    }

    // Row 2: Construct Title & Godot 4 Equivalent (Clean without text bracket clutter)
    ImGui::TextColored(ImVec4(0.38F, 0.94F, 0.68F, 1.0F),
                       "#%03u  %s  (%s)",
                       static_cast<unsigned>(sel_desc->id),
                       sel_desc->chroma_name,
                       chroma_construct_kind_label(sel_desc->construct_kind));
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.96F, 0.80F, 0.36F, 1.0F),
                       "  <->  Godot Equivalent: %s (%s | %s)",
                       sel_desc->godot_name,
                       sel_desc->video_timestamp,
                       studio_node_category_label(sel_desc->category));

    // Row 3: Auto-Bundled Stack & Layer
    ImGui::TextColored(ImVec4(0.54F, 0.88F, 1.00F, 1.0F),
                       "Auto-Bundled: %s   [Layer: %s]",
                       sel_desc->spawn_preset.bundled_summary != nullptr
                           ? sel_desc->spawn_preset.bundled_summary
                           : "Transform3D + PbrMaterialModifier",
                       sel_desc->spawn_preset.semantic_layer != nullptr
                           ? sel_desc->spawn_preset.semantic_layer
                           : "Default");

    // Row 4: UX Upgrade & SOTA Tech (auto-wrapped to available card width so nothing ever clips horizontally!)
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
    draw_wrapped_labeled_line(ImVec4(0.84F, 0.88F, 0.96F, 1.0F), "UX Upgrade:   ", sel_desc->ux_upgrade);
    draw_wrapped_labeled_line(ImVec4(0.72F, 0.82F, 0.94F, 1.0F), "SOTA Tech:    ", sel_desc->tech_upgrade);

    ImGui::PopStyleVar();
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

        ImGui::SeparatorText("250 Element & Vessel Universal Catalog (219 Godot + 31 Next-Gen SOTA)");
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
inline void draw_clean_studio_workspace_panels(
    StateT&                s,
    float                  dt,
    const cd::math::Mat4f& vp,
    std::uint32_t          frame_w,
    std::uint32_t          frame_h,
    LogFn&&                log_push)
{
    auto& w   = studio_world();
    auto& cst = cd::ui::chroma::state();

    // Draw 3D Collider wireframes + full-width Ribbon Toolbar + right-click context menu
    draw_studio_viewport_overlays_and_drop_targets(s, vp, frame_w, frame_h, log_push);

    if (s.pie_playing || cst.workspace_mode == 1)
    {
        draw_ingame_hud_workspace(s, dt, vp, frame_w, frame_h, log_push);
        return;
    }
    if (cst.workspace_mode == 2)
    {
        draw_node_and_app_lab_workspace(s, frame_w, frame_h, log_push);
        return;
    }

    // =========================================================================
    // PANEL 1 (Left Full-Height Dock): Outliner (Scene) | Toolbox & Prefabs | Worlds & Layers
    // =========================================================================
    if (ImGui::Begin("Studio Workbench", nullptr, ImGuiWindowFlags_NoTitleBar))
    {
        if (ImGui::BeginTabBar("StudioLeftDockTabs"))
        {
            // -----------------------------------------------------------------
            // TAB 1 (DEFAULT): Full-Height Scene Outliner (Godot 4 / UE5 / Source 2)
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Outliner"))
            {
                // Row 1: Icon-Driven Quick-Add Bar
                if (ImGui::IconButton("Add##out_add", cd::ui::chroma::IconKind::kPlus,
                                      IM_COL32(85, 245, 165, 255), false, ImVec2(50.0F, 20.0F)))
                {
                    w.focus_bottom_node_catalog_tab = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Open 250-Construct Catalog");

                ImGui::SameLine();
                if (ImGui::IconButton("Cube##out_cube", cd::ui::chroma::IconKind::kCube,
                                      IM_COL32(100, 195, 255, 255), false, ImVec2(54.0F, 20.0F)))
                {
                    const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                    (void)spawn_entity_with_modifiers(s, "Dynamic_Cube", { pt.x, 0.0F, pt.z }, log_push);
                }
                ImGui::SameLine();
                if (ImGui::IconButton("Sphere##out_sph", cd::ui::chroma::IconKind::kSphere,
                                      IM_COL32(115, 225, 255, 255), false, ImVec2(64.0F, 20.0F)))
                {
                    const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                    (void)spawn_entity_with_modifiers(s, "Dynamic_Sphere", { pt.x, 0.0F, pt.z }, log_push);
                }
                ImGui::SameLine();
                if (ImGui::IconButton("Light##out_lgt", cd::ui::chroma::IconKind::kLight,
                                      IM_COL32(255, 220, 90, 255), false, ImVec2(54.0F, 20.0F)))
                {
                    const auto pos = camera_spawn_point(s, 2.5F);
                    s.lights.push_back({ "Point Light (2700K)",
                        cd::light::point(pos, { 1.0F, 0.9F, 0.75F }, 2800.0F, 10.0F), true, 2700.0F });
                    log_push("[studio] added Point Light");
                }
                ImGui::SameLine();
                if (ImGui::IconButton("Avatar##out_av", cd::ui::chroma::IconKind::kAvatar,
                                      IM_COL32(85, 245, 165, 255), false, ImVec2(62.0F, 20.0F)))
                {
                    const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                    (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { pt.x, 0.0F, pt.z }, log_push);
                }

                // Row 2: Search Filter + Save / Load
                ImGui::SetNextItemWidth(205.0F);
                ImGui::InputTextWithHint("##filter_nodes", "Filter scene objects...", w.hierarchy_filter, sizeof(w.hierarchy_filter));
                ImGui::SameLine();
                if (ImGui::SmallButton("Save"))
                    (void)save_full_studio_scene(s, "studio_scene.cdscene.json", log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Load"))
                    (void)load_full_studio_scene(s, "studio_scene.cdscene.json", log_push);

                // Row 3: Stage Header & Unparent Drop Target
                std::string outliner_lvl = w.active_level_name;
                if (const auto paren = outliner_lvl.find(" ("); paren != std::string::npos)
                    outliner_lvl = outliner_lvl.substr(0, paren);
                const std::string stage_hdr = w.active_project_name + " / " + outliner_lvl + "##stage_root_drop";
                const std::string stage_cnt = std::to_string(s.entities.size()) + " obj";
                (void)ImGui::IconSelectable(stage_hdr.c_str(), cd::ui::chroma::IconKind::kWorld,
                                            IM_COL32(105, 205, 255, 255), false, stage_cnt.c_str());
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CD_STUDIO_NODE_IDX"))
                    {
                        const int child_idx = *static_cast<const int*>(p->Data);
                        if (child_idx >= 0 && std::cmp_less(child_idx, s.entities.size()))
                        {
                            s.scene.detach(s.entities[static_cast<std::size_t>(child_idx)].handle);
                            s.scene.update_transforms();
                            log_push("[hierarchy] Detached '" + s.entities[static_cast<std::size_t>(child_idx)].name + "' to Root");
                        }
                    }
                    if (const ImGuiPayload* mp = ImGui::AcceptDragDropPayload("CD_STUDIO_MODIFIER"))
                    {
                        const char* cls_id = static_cast<const char*>(mp->Data);
                        if (auto mod = instantiate_modifier_by_class(cls_id))
                        {
                            w.environment_modifiers.add_or_replace(std::move(mod));
                            w.is_dragging_modifier = false;
                            log_push(std::string("[modifier] Attached ") + cls_id + " to World Root");
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Full-Height Scrollable Icon-Driven Scene Tree
                ImGui::BeginChild("scene_hierarchy_scroll", ImVec2(0.0F, 0.0F), true);
                int deferred_dup_idx = -1;
                int deferred_del_idx = -1;
                for (std::size_t i = 0; i < s.entities.size(); ++i)
                {
                    auto& ent = s.entities[i];
                    if (w.hierarchy_filter[0] != '\0' &&
                        ent.name.find(w.hierarchy_filter) == std::string::npos)
                    {
                        continue;
                    }
                    ModifierStack& ms = w.entity_modifiers[ent.handle.id];
                    std::uint32_t hierarchy_depth = 0U;
                    {
                        auto cur_p = s.scene.parent_of(ent.handle);
                        while (cur_p.is_valid() && hierarchy_depth < 8U)
                        {
                            ++hierarchy_depth;
                            cur_p = s.scene.parent_of(cur_p);
                        }
                    }
                    const bool has_parent = (hierarchy_depth > 0U);

                    // Visual Eye & Lock Icon Toggles
                    const std::string vis_btn = "##vis_" + std::to_string(i);
                    if (ImGui::IconButton(vis_btn.c_str(),
                                          ent.visible ? cd::ui::chroma::IconKind::kEyeOpen : cd::ui::chroma::IconKind::kEyeClosed,
                                          ent.visible ? IM_COL32(115, 215, 255, 255) : IM_COL32(150, 110, 110, 185),
                                          false, ImVec2(20.0F, 18.0F)))
                    {
                        ent.visible = !ent.visible;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Visibility: %s", ent.visible ? "Visible" : "Hidden");

                    ImGui::SameLine();
                    const std::string lck_btn = "##lck_" + std::to_string(i);
                    if (ImGui::IconButton(lck_btn.c_str(),
                                          ent.locked ? cd::ui::chroma::IconKind::kLockClosed : cd::ui::chroma::IconKind::kLockOpen,
                                          ent.locked ? IM_COL32(255, 185, 85, 255) : IM_COL32(135, 150, 175, 180),
                                          ent.locked, ImVec2(20.0F, 18.0F)))
                    {
                        ent.locked = !ent.locked;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Selection Lock: %s", ent.locked ? "Locked" : "Unlocked");
                    ImGui::SameLine();

                    // Clean entity name (strip any legacy "[VSL]" / "[ELM]" prefix or parenthetical tag)
                    std::string clean_name = ent.name;
                    bool is_vessel_ent     = false;
                    if (clean_name.rfind("[VSL]", 0) == 0)
                    {
                        is_vessel_ent = true;
                        clean_name    = clean_name.substr(5);
                    }
                    else if (clean_name.rfind("[ELM]", 0) == 0)
                    {
                        clean_name = clean_name.substr(5);
                    }
                    if (!clean_name.empty() && clean_name.front() == ' ')
                        clean_name.erase(0, clean_name.find_first_not_of(' '));
                    if (const auto paren = clean_name.find(" ("); paren != std::string::npos)
                        clean_name = clean_name.substr(0, paren);

                    // Choose visual icon + color by entity geometry & attached modifiers
                    cd::ui::chroma::IconKind ent_icon = cd::ui::chroma::IconKind::kCube;
                    ImU32 ent_col = IM_COL32(102, 196, 255, 255);
                    switch (ent.kind)
                    {
                        case PrimitiveKind::kSphere:     ent_icon = cd::ui::chroma::IconKind::kSphere;   break;
                        case PrimitiveKind::kCylinder:   ent_icon = cd::ui::chroma::IconKind::kCylinder; break;
                        case PrimitiveKind::kCone:       ent_icon = cd::ui::chroma::IconKind::kCone;     break;
                        case PrimitiveKind::kTorus:
                        case PrimitiveKind::kTorusKnot:  ent_icon = cd::ui::chroma::IconKind::kTorus;    break;
                        case PrimitiveKind::kGltf:       ent_icon = cd::ui::chroma::IconKind::kAvatar;   ent_col = IM_COL32(85, 240, 165, 255); break;
                        case PrimitiveKind::kSponza:     ent_icon = cd::ui::chroma::IconKind::kWorld;    ent_col = IM_COL32(255, 205, 105, 255); break;
                        case PrimitiveKind::kCustomGltf: ent_icon = cd::ui::chroma::IconKind::kMesh3D;   break;
                        default:                         ent_icon = cd::ui::chroma::IconKind::kCube;     break;
                    }
                    if (is_vessel_ent || clean_name.find("Avatar") != std::string::npos ||
                        clean_name.find("Hero") != std::string::npos || clean_name.find("CesiumMan") != std::string::npos ||
                        clean_name.find("Ragdoll") != std::string::npos)
                    {
                        ent_icon = cd::ui::chroma::IconKind::kAvatar;
                        ent_col  = IM_COL32(85, 240, 165, 255);
                    }
                    else if (clean_name.find("Vehicle") != std::string::npos ||
                             clean_name.find("Rover") != std::string::npos ||
                             clean_name.find("Wheel") != std::string::npos)
                    {
                        ent_icon = cd::ui::chroma::IconKind::kVehicle;
                        ent_col  = IM_COL32(95, 235, 210, 255);
                    }
                    else if (clean_name.find("Camera") != std::string::npos ||
                             clean_name.find("SpringArm") != std::string::npos)
                    {
                        ent_icon = cd::ui::chroma::IconKind::kCamera;
                        ent_col  = IM_COL32(200, 155, 255, 255);
                    }
                    else if (clean_name.find("Ground_Floor") != std::string::npos)
                    {
                        ent_icon = cd::ui::chroma::IconKind::kCollider;
                        ent_col  = IM_COL32(130, 175, 220, 255);
                    }

                    std::size_t active_mod_count = 0U;
                    std::size_t capsule_count    = 0U;
                    std::string tooltip_mods {};
                    for (const auto& m : ms.items)
                    {
                        if (!m || !m->enabled) continue;
                        if (m->class_name() == "UniversalNodeModifier")
                        {
                            ++capsule_count;
                        }
                        else
                        {
                            ++active_mod_count;
                            if (m->class_name() == "CharacterControllerModifier")
                            {
                                ent_icon = cd::ui::chroma::IconKind::kAvatar;
                                ent_col  = IM_COL32(85, 240, 165, 255);
                            }
                            else if (m->class_name() == "TriggerVolumeModifier" ||
                                     m->class_name() == "PatrolWaypointModifier")
                            {
                                ent_icon = cd::ui::chroma::IconKind::kSparkles;
                                ent_col  = IM_COL32(255, 196, 82, 255);
                            }
                            else if (m->class_name() == "LightEmitterModifier")
                            {
                                ent_icon = cd::ui::chroma::IconKind::kLight;
                                ent_col  = IM_COL32(255, 224, 96, 255);
                            }
                        }
                        if (!tooltip_mods.empty()) tooltip_mods += ", ";
                        tooltip_mods += m->display_name();
                    }

                    std::string display_row_name {};
                    if (hierarchy_depth > 0U)
                    {
                        for (std::uint32_t d = 1U; d < hierarchy_depth; ++d)
                            display_row_name += "  ";
                        display_row_name += "|- ";
                    }
                    display_row_name += clean_name;

                    std::string pill_txt {};
                    if (capsule_count > 0U)
                    {
                        pill_txt = std::to_string(active_mod_count) + "+" + std::to_string(capsule_count);
                    }
                    else if (active_mod_count > 0U)
                    {
                        pill_txt = std::to_string(active_mod_count);
                    }

                    const std::string row_id_label = display_row_name + "##node_" + std::to_string(i);
                    const bool is_sel = (s.selected_kind == SelKind::kEntity && std::cmp_equal(s.selected, i));
                    if (ImGui::IconSelectable(row_id_label.c_str(), ent_icon, ent_col, is_sel, pill_txt))
                    {
                        s.selected      = static_cast<int>(i);
                        s.selected_kind = SelKind::kEntity;
                    }

                    const std::string_view ent_lyr = cd::world_container::layer_of(s.ecs_world, ent.handle);
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Object: %s\nLayer: %s\nModifiers: %s",
                                          clean_name.c_str(),
                                          ent_lyr.empty() ? "Default" : std::string(ent_lyr).c_str(),
                                          tooltip_mods.empty() ? "None" : tooltip_mods.c_str());
                    }

                    // Right-Click Context Menu on any Scene Hierarchy row
                    if (ImGui::BeginPopupContextItem())
                    {
                        s.selected      = static_cast<int>(i);
                        s.selected_kind = SelKind::kEntity;
                        ImGui::TextColored(ImVec4(0.45F, 0.95F, 0.65F, 1.0F), "%s", clean_name.c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Focus Camera (F)"))
                            focus_selected(s, log_push);
                        if (ImGui::MenuItem("Duplicate with Modifiers (Ctrl+D)"))
                            deferred_dup_idx = static_cast<int>(i);
                        if (ImGui::MenuItem("Snap to Ground Floor (G)"))
                            snap_selected_entity_to_ground(s, log_push);
                        if (has_parent && ImGui::MenuItem("Unparent to Root"))
                        {
                            s.scene.detach(ent.handle);
                            s.scene.update_transforms();
                        }
                        if (auto* active_lvl = s.cd_world.active_level_ptr())
                        {
                            if (ImGui::BeginMenu("Assign to Layer"))
                            {
                                for (std::size_t li = 0; li < active_lvl->layer_count(); ++li)
                                {
                                    const auto* lyr = active_lvl->layer(li);
                                    if (!lyr) continue;
                                    const bool on_lyr = (ent_lyr == lyr->name());
                                    const std::string lyr_nm { lyr->name() };
                                    if (ImGui::MenuItem(lyr_nm.c_str(), nullptr, on_lyr))
                                    {
                                        cd::world_container::assign_layer(s.ecs_world, ent.handle, lyr_nm);
                                        log_push("[layer] Assigned '" + ent.name + "' -> Layer '" + lyr_nm + "'");
                                    }
                                }
                                ImGui::EndMenu();
                            }
                        }
                        if (ImGui::MenuItem("Delete Object (Del)"))
                            deferred_del_idx = static_cast<int>(i);
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginDragDropSource())
                    {
                        const int idx_payload = static_cast<int>(i);
                        ImGui::SetDragDropPayload("CD_STUDIO_NODE_IDX", &idx_payload, sizeof(int));
                        ImGui::Text("Reparent '%s'", clean_name.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* np = ImGui::AcceptDragDropPayload("CD_STUDIO_NODE_IDX"))
                        {
                            const int child_idx = *static_cast<const int*>(np->Data);
                            if (child_idx != static_cast<int>(i) &&
                                child_idx >= 0 && std::cmp_less(child_idx, s.entities.size()))
                            {
                                s.scene.attach(s.entities[static_cast<std::size_t>(child_idx)].handle, ent.handle);
                                s.scene.update_transforms();
                                log_push("[hierarchy] Attached '" + s.entities[static_cast<std::size_t>(child_idx)].name +
                                         "' -> '" + ent.name + "'");
                            }
                        }
                        if (const ImGuiPayload* mp = ImGui::AcceptDragDropPayload("CD_STUDIO_MODIFIER"))
                        {
                            const char* cls_id = static_cast<const char*>(mp->Data);
                            if (auto mod = instantiate_modifier_by_class(cls_id))
                            {
                                ms.add_or_replace(std::move(mod));
                                w.is_dragging_modifier = false;
                                s.selected      = static_cast<int>(i);
                                s.selected_kind = SelKind::kEntity;
                                log_push(std::string("[modifier] Dropped ") + cls_id + " onto '" + ent.name + "'");
                            }
                        }
                        if (const ImGuiPayload* mesh_p = ImGui::AcceptDragDropPayload("CD_ASSET_MESH"))
                        {
                            const char* mesh_id = static_cast<const char*>(mesh_p->Data);
                            (void)assign_mesh_asset_to_entity(s, static_cast<int>(i), mesh_id, log_push);
                            w.is_dragging_asset_mesh = false;
                            s.selected      = static_cast<int>(i);
                            s.selected_kind = SelKind::kEntity;
                        }
                        if (const ImGuiPayload* tex_p = ImGui::AcceptDragDropPayload("CD_ASSET_TEXTURE"))
                        {
                            const char* tex_id = static_cast<const char*>(tex_p->Data);
                            (void)assign_texture_asset_to_entity(s, static_cast<int>(i), tex_id, log_push);
                            w.is_dragging_asset_texture = false;
                            s.selected      = static_cast<int>(i);
                            s.selected_kind = SelKind::kEntity;
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
                if (deferred_dup_idx >= 0) duplicate_entity_with_modifiers(s, deferred_dup_idx, log_push);
                if (deferred_del_idx >= 0)
                {
                    s.selected      = deferred_del_idx;
                    s.selected_kind = SelKind::kEntity;
                    delete_selected(s, log_push);
                }

                // Scene Lights in the Outliner (Visual Light Icon + Clean Name)
                ImGui::SeparatorText("Scene Lights");
                for (std::size_t li = 0; li < s.lights.size(); ++li)
                {
                    const bool is_lsel = (s.selected_kind == SelKind::kLight && std::cmp_equal(s.selected, li));
                    const std::string l_lbl = s.lights[li].name + "##scene_l_" + std::to_string(li);
                    char k_buf[24] {};
                    std::snprintf(k_buf, sizeof(k_buf), "%.0fK", static_cast<double>(s.lights[li].kelvin));
                    if (ImGui::IconSelectable(l_lbl.c_str(), cd::ui::chroma::IconKind::kLight,
                                              IM_COL32(255, 224, 96, 255), is_lsel, k_buf))
                    {
                        s.selected      = static_cast<int>(li);
                        s.selected_kind = SelKind::kLight;
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // -----------------------------------------------------------------
            // TAB 2: Toolbox & Prefabs (Visual Icon Tiles & Modifier Classes)
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Toolbox"))
            {
                ImGui::BeginChild("toolbox_scroll_child", ImVec2(0.0F, 0.0F), false);

                ImGui::SeparatorText("1. Drag & Drop 3D Objects & Prefabs");
                struct SpawnCard
                {
                    const char*              id;
                    const char*              short_label;
                    const char*              tip;
                    cd::ui::chroma::IconKind icon;
                    ImU32                    col;
                };
                constexpr std::array<SpawnCard, 18> kSpawnCards {{
                    { "Dynamic_Cube",           "Dynamic Cube",   "Dynamic RigidBody Cube + BoxCollider3D",        cd::ui::chroma::IconKind::kCube,     IM_COL32(95, 195, 255, 255) },
                    { "Dynamic_Sphere",         "Bounce Sphere",  "Dynamic Metallic Sphere + SphereCollider3D",    cd::ui::chroma::IconKind::kSphere,   IM_COL32(115, 225, 255, 255) },
                    { "Static_Ramp",            "Ramp 25 deg",    "Angled 25 deg OBB Ramp",                        cd::ui::chroma::IconKind::kCollider, IM_COL32(130, 210, 180, 255) },
                    { "Trampoline_Pad",         "Trampoline",     "High-restitution (0.96) Bouncing Pad",          cd::ui::chroma::IconKind::kPhysics,  IM_COL32(95, 240, 175, 255) },
                    { "Moving_Platform",        "Moving Platf.",  "KinematicMover3D platform that carries riders", cd::ui::chroma::IconKind::kCube,     IM_COL32(125, 185, 255, 255) },
                    { "Collectible_EnergyCore", "Energy Core",    "TriggerVolume3D collectible (+250 score)",      cd::ui::chroma::IconKind::kSparkles, IM_COL32(255, 215, 85, 255) },
                    { "JumpPad_Trigger",        "Boost JumpPad",  "TriggerVolume3D vertical launch pad",           cd::ui::chroma::IconKind::kPhysics,  IM_COL32(115, 245, 195, 255) },
                    { "Hazard_LaserBarrier",    "Laser Hazard",   "Moving TriggerVolume3D hazard",                 cd::ui::chroma::IconKind::kLight,    IM_COL32(255, 110, 110, 255) },
                    { "Patrol_SynthBot",        "AI Patrol Bot",  "Autonomous PatrolWaypoint3D AI Drone",          cd::ui::chroma::IconKind::kAvatar,   IM_COL32(255, 165, 95, 255) },
                    { "Goal_Beacon",            "Goal Beacon",    "TriggerVolume3D Level Goal",                    cd::ui::chroma::IconKind::kSparkles, IM_COL32(95, 245, 165, 255) },
                    { "Prefab_CrateStack",      "5x Crate Tower", "Stack of 5 Dynamic Physics Crates",             cd::ui::chroma::IconKind::kCube,     IM_COL32(235, 185, 105, 255) },
                    { "Prefab_DominoRow",       "6x Domino Row",  "Row of 6 Dynamic Dominoes",                     cd::ui::chroma::IconKind::kLayers,   IM_COL32(185, 205, 245, 255) },
                    { "Static_Wall",            "Static Wall",    "Immovable Barrier Wall + BoxCollider3D",        cd::ui::chroma::IconKind::kCollider, IM_COL32(150, 175, 205, 255) },
                    { "Cylinder",               "Dyn Cylinder",   "Dynamic Cylinder + CapsuleCollider3D",          cd::ui::chroma::IconKind::kCylinder, IM_COL32(110, 210, 245, 255) },
                    { "Cone",                   "Dynamic Cone",   "Dynamic Cone + BoxCollider3D",                  cd::ui::chroma::IconKind::kCone,     IM_COL32(245, 185, 115, 255) },
                    { "Torus",                  "Dynamic Torus",  "Dynamic Torus + BoxCollider3D",                 cd::ui::chroma::IconKind::kTorus,    IM_COL32(205, 155, 255, 255) },
                    { "Character_CesiumMan",    "Player Avatar",  "Skinned Animated Character + WASD Controller",  cd::ui::chroma::IconKind::kAvatar,   IM_COL32(85, 245, 165, 255) },
                    { "World_Sponza",           "Sponza Atrium",  "Crytek Sponza Atrium World (glTF)",             cd::ui::chroma::IconKind::kWorld,    IM_COL32(255, 210, 115, 255) },
                }};

                if (ImGui::BeginTable("spawn_grid_2col", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    for (const auto& card : kSpawnCards)
                    {
                        ImGui::TableNextColumn();
                        const std::string btn_id = std::string(card.short_label) + "##sp_" + card.id;
                        if (ImGui::IconButton(btn_id.c_str(), card.icon, card.col, false, ImVec2(-1.0F, 20.0F)))
                        {
                            const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                            (void)spawn_entity_with_modifiers(s, card.id, { pt.x, 0.0F, pt.z }, log_push);
                        }
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                        {
                            ImGui::SetDragDropPayload("CD_STUDIO_SPAWN", card.id, std::strlen(card.id) + 1);
                            w.is_dragging_spawn = true;
                            w.drag_spawn_id     = card.id;
                            ImGui::Text("Drop '%s' onto 3D Viewport", card.short_label);
                            ImGui::EndDragDropSource();
                        }
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s\n(Drag onto 3D Viewport or Click to Spawn)", card.tip);
                        }
                    }
                    ImGui::EndTable();
                }

                ImGui::SeparatorText("2. Drag & Drop Modifier Classes");
                if (ImGui::BeginTable("mod_grid_2col", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    for (const auto& mdesc : kModifierCatalog)
                    {
                        ImGui::TableNextColumn();
                        bool active_here = false;
                        if (mdesc.category == ModifierCategory::kEnvironment)
                        {
                            for (const auto& m : w.environment_modifiers.items)
                                if (m && m->enabled && m->class_name() == mdesc.class_id) active_here = true;
                        }
                        else if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                        {
                            const auto& ems = w.entity_modifiers[s.entities[static_cast<std::size_t>(s.selected)].handle.id];
                            for (const auto& m : ems.items)
                                if (m && m->enabled && m->class_name() == mdesc.class_id) active_here = true;
                        }

                        cd::ui::chroma::IconKind m_icon = cd::ui::chroma::IconKind::kNode;
                        ImU32 m_col = IM_COL32(115, 195, 255, 255);
                        switch (mdesc.category)
                        {
                            case ModifierCategory::kPhysics:
                                m_icon = cd::ui::chroma::IconKind::kPhysics;
                                m_col  = IM_COL32(95, 235, 195, 255);
                                break;
                            case ModifierCategory::kEnvironment:
                                m_icon = cd::ui::chroma::IconKind::kWorld;
                                m_col  = IM_COL32(255, 210, 105, 255);
                                break;
                            case ModifierCategory::kMaterial:
                                m_icon = cd::ui::chroma::IconKind::kMaterial;
                                m_col  = IM_COL32(215, 155, 255, 255);
                                break;
                            default:
                                m_icon = cd::ui::chroma::IconKind::kSparkles;
                                m_col  = IM_COL32(115, 205, 255, 255);
                                break;
                        }

                        const std::string btn_txt = std::string(mdesc.title) + "##mod_" + mdesc.class_id;
                        if (ImGui::IconButton(btn_txt.c_str(), m_icon, m_col, active_here, ImVec2(-1.0F, 20.0F)))
                        {
                            if (auto mod = instantiate_modifier_by_class(mdesc.class_id))
                            {
                                if (mod->category() == ModifierCategory::kEnvironment)
                                {
                                    w.environment_modifiers.add_or_replace(std::move(mod));
                                    log_push(std::string("[modifier] Instantiated ") + mdesc.class_id + " on Environment");
                                }
                                else if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                                {
                                    auto& ent = s.entities[static_cast<std::size_t>(s.selected)];
                                    w.entity_modifiers[ent.handle.id].add_or_replace(std::move(mod));
                                    log_push(std::string("[modifier] Instantiated ") + mdesc.class_id + " on " + ent.name);
                                }
                            }
                        }
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                        {
                            ImGui::SetDragDropPayload("CD_STUDIO_MODIFIER",
                                                      mdesc.class_id,
                                                      std::strlen(mdesc.class_id) + 1);
                            w.is_dragging_modifier = true;
                            w.drag_modifier_class  = mdesc.class_id;
                            ImGui::Text("Drop class %s onto Entity / Scene", mdesc.class_id);
                            ImGui::EndDragDropSource();
                        }
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s\nC++ Class: cd::studio::%s\n(Drag onto Entity or Click to Attach)",
                                              mdesc.tooltip, mdesc.class_id);
                        }
                    }
                    ImGui::EndTable();
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // -----------------------------------------------------------------
            // TAB 3: Worlds, Sample Projects & Semantic Layers Manager
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Worlds & Layers"))
            {
                if (ImGui::IconButton("New Empty Project##wl_empty", cd::ui::chroma::IconKind::kFileScene,
                                      IM_COL32(130, 210, 255, 255), false, ImVec2(142.0F, 20.0F)))
                {
                    reset_to_clean_empty_project(s, log_push);
                }
                ImGui::SameLine();
                if (ImGui::IconButton("Load 6-World Showcase##wl_master", cd::ui::chroma::IconKind::kFileProject,
                                      IM_COL32(255, 195, 95, 255), false, ImVec2(-1.0F, 20.0F)))
                {
                    s.cd_world.set_project(nullptr);
                    populate_master_showcase_project(s.cd_world);
                    if (const auto* np = s.cd_world.project())
                    {
                        w.active_project_name = std::string(np->name());
                        if (const auto* lvl0 = np->level(0))
                            (void)switch_to_project_level(s, lvl0->name(), log_push);
                    }
                    log_push("[project] Loaded ChromodynamicMasterShowcase from disk");
                }

                if (auto* proj = s.cd_world.project())
                {
                    ImGui::TextColored(ImVec4(0.45F, 0.90F, 1.0F, 1.0F),
                                       "Active Project: %s (%zu Stages)",
                                       w.active_project_name.c_str(), proj->level_count());

                    ImGui::BeginChild("worlds_levels_scroll", ImVec2(0.0F, 345.0F), true);
                    std::string deferred_switch_level {};
                    if (const auto* cur_proj = s.cd_world.project())
                    {
                        const auto worlds = cur_proj->world_names();
                        for (const auto& wname : worlds)
                        {
                            ImGui::SeparatorText(wname.c_str());
                            const auto lvl_indices = cur_proj->levels_in_world(wname);
                            for (std::size_t li : lvl_indices)
                            {
                                const auto* lvl = cur_proj->level(li);
                                if (!lvl) continue;
                                const std::string lvl_nm   { lvl->name() };
                                const std::string wn_str   { lvl->world_name() };
                                const std::string desc_str { lvl->description() };
                                const bool is_active = (cur_proj->active_level() == li);
                                const std::string btn_lbl = lvl_nm + "##wl_" + lvl_nm;
                                if (ImGui::IconSelectable(btn_lbl.c_str(),
                                                          cd::ui::chroma::IconKind::kFileScene,
                                                          is_active ? IM_COL32(85, 245, 165, 255) : IM_COL32(115, 195, 255, 255),
                                                          is_active,
                                                          is_active ? "Active" : ""))
                                {
                                    deferred_switch_level = lvl_nm;
                                }
                                if (!desc_str.empty() && ImGui::IsItemHovered())
                                {
                                    ImGui::SetTooltip("World: %s\nStage: %s\n%s",
                                                      wn_str.c_str(), lvl_nm.c_str(), desc_str.c_str());
                                }
                            }
                        }
                    }
                    ImGui::EndChild();
                    if (!deferred_switch_level.empty())
                    {
                        (void)switch_to_project_level(s, deferred_switch_level, log_push);
                    }

                    ImGui::SetNextItemWidth(165.0F);
                    ImGui::InputTextWithHint("##new_lvl_nm", "New stage name...", w.new_level_name_buf, sizeof(w.new_level_name_buf));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+ Add Stage") && w.new_level_name_buf[0] != '\0')
                    {
                        if (auto* mut_proj = s.cd_world.project())
                        {
                            auto* nl = mut_proj->add_level(w.new_level_name_buf);
                            nl->set_world_name(w.active_world_name.empty() ? "Default_World" : w.active_world_name);
                            nl->set_description("User-created custom stage");
                            nl->set_template_id(0);
                            populate_standard_level_layers(*nl);
                            (void)switch_to_project_level(s, w.new_level_name_buf, log_push);
                        }
                    }
                }

                ImGui::SeparatorText("Active Stage Semantic Layers");
                if (auto* active_lvl = s.cd_world.active_level_ptr())
                {
                    for (std::size_t li = 0; li < active_lvl->layer_count(); ++li)
                    {
                        auto* lyr = active_lvl->layer(li);
                        if (!lyr) continue;
                        ImGui::PushID(static_cast<int>(li));
                        const std::string lyr_nm { lyr->name() };
                        bool vis = lyr->visible();
                        if (ImGui::IconButton("##lyr_vis",
                                              vis ? cd::ui::chroma::IconKind::kEyeOpen : cd::ui::chroma::IconKind::kEyeClosed,
                                              vis ? IM_COL32(115, 215, 255, 255) : IM_COL32(150, 110, 110, 185),
                                              false, ImVec2(20.0F, 18.0F)))
                        {
                            vis = !vis;
                            lyr->set_visible(vis);
                            for (auto& e : s.entities)
                            {
                                if (cd::world_container::layer_of(s.ecs_world, e.handle) == lyr_nm)
                                {
                                    e.visible = vis;
                                }
                            }
                        }
                        ImGui::SameLine();
                        bool lck = lyr->locked();
                        if (ImGui::IconButton("##lyr_lck",
                                              lck ? cd::ui::chroma::IconKind::kLockClosed : cd::ui::chroma::IconKind::kLockOpen,
                                              lck ? IM_COL32(255, 185, 85, 255) : IM_COL32(135, 150, 175, 180),
                                              lck, ImVec2(20.0F, 18.0F)))
                        {
                            lck = !lck;
                            lyr->set_locked(lck);
                            for (auto& e : s.entities)
                            {
                                if (cd::world_container::layer_of(s.ecs_world, e.handle) == lyr_nm)
                                {
                                    e.locked = lck;
                                }
                            }
                        }
                        ImGui::SameLine();
                        const bool is_spawn_lyr = (w.active_layer_name == lyr_nm);
                        if (ImGui::IconSelectable(lyr_nm.c_str(), cd::ui::chroma::IconKind::kLayers,
                                                  IM_COL32(120, 200, 255, 255), is_spawn_lyr,
                                                  is_spawn_lyr ? "Spawn" : ""))
                        {
                            w.active_layer_name = lyr_nm;
                            log_push("[layer] Active spawn layer set to '" + lyr_nm + "'");
                        }
                        ImGui::PopID();
                    }

                    ImGui::SetNextItemWidth(165.0F);
                    ImGui::InputTextWithHint("##new_lyr_nm", "New layer...", w.new_layer_name_buf, sizeof(w.new_layer_name_buf));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+ Add Layer") && w.new_layer_name_buf[0] != '\0')
                    {
                        if (active_lvl->find_layer(w.new_layer_name_buf) == nullptr)
                        {
                            (void)active_lvl->add_layer(w.new_layer_name_buf);
                        }
                        w.active_layer_name = w.new_layer_name_buf;
                        log_push(std::string("[layer] Added layer '") + w.new_layer_name_buf + "'");
                    }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // =========================================================================
    // PANEL 3 (Right): Full Transform (Pos + Euler Deg + Scale) & Modifier Stack
    // =========================================================================
    if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar))
    {
        if (ImGui::BeginTabBar("StudioInspectorTabs"))
        {
            if (ImGui::BeginTabItem("Inspector"))
            {
                if (s.selected_kind == SelKind::kEntity &&
                    s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                {
                    auto& ent = s.entities[static_cast<std::size_t>(s.selected)];

                    // Quick Icon Action Toolbar pinned at top of Inspector
                    if (ImGui::IconButton("Focus##insp_foc", cd::ui::chroma::IconKind::kCamera,
                                          IM_COL32(105, 205, 255, 255), false, ImVec2(64.0F, 20.0F)))
                    {
                        focus_selected(s, log_push);
                    }
                    ImGui::SameLine(0.0F, 4.0F);
                    if (ImGui::IconButton("Dup##insp_dup", cd::ui::chroma::IconKind::kDuplicate,
                                          IM_COL32(115, 235, 175, 255), false, ImVec2(54.0F, 20.0F)))
                    {
                        duplicate_entity_with_modifiers(s, s.selected, log_push);
                    }
                    ImGui::SameLine(0.0F, 4.0F);
                    if (ImGui::IconButton("Floor##insp_flr", cd::ui::chroma::IconKind::kCollider,
                                          IM_COL32(255, 205, 95, 255), false, ImVec2(58.0F, 20.0F)))
                    {
                        snap_selected_entity_to_ground(s, log_push);
                    }
                    ImGui::SameLine(0.0F, 4.0F);
                    if (ImGui::IconButton("Delete##insp_del", cd::ui::chroma::IconKind::kTrash,
                                          IM_COL32(255, 110, 110, 255), false, ImVec2(64.0F, 20.0F)))
                    {
                        delete_selected(s, log_push);
                    }

                    ImGui::BeginChild("inspector_entity_scroll", ImVec2(0.0F, 0.0F), false);
                    // Editable Entity Name + Visual Visibility/Lock Icons
                    char name_buf[128] {};
                    std::snprintf(name_buf, sizeof(name_buf), "%s", ent.name.c_str());
                    ImGui::PushItemWidth(175.0F);
                    if (ImGui::InputText("Name", name_buf, sizeof(name_buf)))
                    {
                        ent.name = name_buf;
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine(0.0F, 4.0F);
                    if (ImGui::IconButton("##insp_vis",
                                          ent.visible ? cd::ui::chroma::IconKind::kEyeOpen : cd::ui::chroma::IconKind::kEyeClosed,
                                          ent.visible ? IM_COL32(115, 215, 255, 255) : IM_COL32(150, 110, 110, 185),
                                          false, ImVec2(22.0F, 20.0F)))
                    {
                        ent.visible = !ent.visible;
                    }
                    ImGui::SameLine(0.0F, 3.0F);
                    if (ImGui::IconButton("##insp_lck",
                                          ent.locked ? cd::ui::chroma::IconKind::kLockClosed : cd::ui::chroma::IconKind::kLockOpen,
                                          ent.locked ? IM_COL32(255, 185, 85, 255) : IM_COL32(135, 150, 175, 180),
                                          ent.locked, ImVec2(22.0F, 20.0F)))
                    {
                        ent.locked = !ent.locked;
                    }

                    // Layer assignment on a compact row
                    if (auto* active_lvl = s.cd_world.active_level_ptr())
                    {
                        const std::string cur_lyr_str { cd::world_container::layer_of(s.ecs_world, ent.handle) };
                        ImGui::PushItemWidth(175.0F);
                        if (ImGui::BeginCombo("Layer", cur_lyr_str.c_str()))
                        {
                            for (std::size_t li = 0; li < active_lvl->layer_count(); ++li)
                            {
                                const auto* lyr = active_lvl->layer(li);
                                if (!lyr) continue;
                                const std::string lyr_nm { lyr->name() };
                                const bool sel = (cur_lyr_str == lyr_nm);
                                if (ImGui::Selectable(lyr_nm.c_str(), sel))
                                {
                                    cd::world_container::assign_layer(s.ecs_world, ent.handle, lyr_nm);
                                    ent.visible = lyr->visible();
                                    ent.locked  = lyr->locked();
                                    log_push("[layer] Assigned '" + ent.name + "' -> Layer '" + lyr_nm + "'");
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::PopItemWidth();
                    }

                    ImGui::PushItemWidth(210.0F);
                    if (auto* lt = s.scene.local(ent.handle))
                    {
                        ImGui::SeparatorText("Transform (Position / Rotation / Scale)");
                        if (ImGui::DragFloat3("Position (m)", &lt->value.position.x, 0.05F, -500.0F, 500.0F, "%.2f"))
                        {
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }

                        if (w.euler_cache_deg.find(ent.handle.id) == w.euler_cache_deg.end())
                        {
                            w.euler_cache_deg[ent.handle.id] = quat_to_euler_deg(lt->value.rotation);
                        }
                        cd::math::Vec3f& euler_deg = w.euler_cache_deg[ent.handle.id];
                        if (ImGui::DragFloat3("Rotation (deg)", &euler_deg.x, 1.0F, -180.0F, 180.0F, "%.1f"))
                        {
                            lt->value.rotation = euler_deg_to_quat(euler_deg);
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }

                        const cd::math::Vec3f old_scale = lt->value.scale;
                        if (ImGui::DragFloat3("Scale", &lt->value.scale.x, 0.05F, 0.01F, 100.0F, "%.2f"))
                        {
                            if (w.uniform_scale_lock && old_scale.x > 1e-4F)
                            {
                                const float ratio = lt->value.scale.x / old_scale.x;
                                lt->value.scale.y = std::max(0.01F, old_scale.y * ratio);
                                lt->value.scale.z = std::max(0.01F, old_scale.z * ratio);
                            }
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }

                        if (ImGui::SmallButton("Reset 0"))
                        {
                            euler_deg = { 0.0F, 0.0F, 0.0F };
                            lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }
                        ImGui::SameLine(0.0F, 5.0F);
                        if (ImGui::SmallButton("Tilt -25"))
                        {
                            euler_deg = { 0.0F, 0.0F, -25.0F };
                            lt->value.rotation = euler_deg_to_quat(euler_deg);
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }
                        ImGui::SameLine(0.0F, 5.0F);
                        if (ImGui::SmallButton("Yaw +45"))
                        {
                            euler_deg.y += 45.0F;
                            lt->value.rotation = euler_deg_to_quat(euler_deg);
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }
                        ImGui::SameLine(0.0F, 6.0F);
                        ImGui::Checkbox("Uni Scale", &w.uniform_scale_lock);
                    }
                    ImGui::PopItemWidth();

                    // ---- Context-Driven Mesh & Albedo Texture Slots (Visual Icons, No Text Brackets) ----
                    ImGui::SeparatorText("Context Mesh & Albedo Texture");
                    auto mesh_kind_name = [](PrimitiveKind k) noexcept -> const char*
                    {
                        switch (k)
                        {
                            case PrimitiveKind::kCube:       return "Cube.mesh";
                            case PrimitiveKind::kSphere:     return "Sphere.mesh";
                            case PrimitiveKind::kCylinder:   return "Cylinder.mesh";
                            case PrimitiveKind::kCone:       return "Cone.mesh";
                            case PrimitiveKind::kTorus:      return "Torus.mesh";
                            case PrimitiveKind::kTorusKnot:  return "TorusKnot.mesh";
                            case PrimitiveKind::kGltf:       return "CesiumMan.glb";
                            case PrimitiveKind::kSponza:     return "Sponza.gltf";
                            case PrimitiveKind::kCustomGltf: return "CustomModel.gltf";
                        }
                        return "Cube.mesh";
                    };
                    const std::string cur_mesh_clean = !ent.custom_mesh_path.empty()
                        ? std::filesystem::path(ent.custom_mesh_path).filename().string()
                        : std::string(mesh_kind_name(ent.kind));
                    const std::string cur_mesh_lbl = cur_mesh_clean + "  (From Context)##insp_mesh_slot";
                    if (ImGui::IconButton(cur_mesh_lbl.c_str(), cd::ui::chroma::IconKind::kMesh3D,
                                          IM_COL32(95, 195, 255, 255), false, ImVec2(-1.0F, 21.0F)))
                    {
                        ImGui::OpenPopup("inspector_context_mesh_popup");
                    }
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* mp = ImGui::AcceptDragDropPayload("CD_ASSET_MESH"))
                        {
                            const char* mesh_id = static_cast<const char*>(mp->Data);
                            (void)assign_mesh_asset_to_entity(s, s.selected, mesh_id, log_push);
                            w.is_dragging_asset_mesh = false;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::BeginPopup("inspector_context_mesh_popup"))
                    {
                        ImGui::TextColored(ImVec4(0.45F, 0.88F, 1.0F, 1.0F), "Loaded Context Meshes");
                        ImGui::Separator();
                        bool any_ctx_mesh = false;
                        for (const auto& ca : w.loaded_context_assets)
                        {
                            if (ca.kind != 0U) continue;
                            any_ctx_mesh = true;
                            if (ImGui::MenuItem(ca.name.c_str()))
                                (void)assign_mesh_asset_to_entity(s, s.selected, ca.path, log_push);
                        }
                        if (!any_ctx_mesh)
                            ImGui::TextDisabled("No meshes in Context yet — click a Mesh in Disk Files below");
                        ImGui::EndPopup();
                    }

                    auto tex_slot_name = [](int slot, const std::string& path) -> std::string
                    {
                        if (!path.empty()) return std::filesystem::path(path).filename().string();
                        switch (slot)
                        {
                            case 1: return "Earth_Albedo.png";
                            case 2: return "UV_Checkerboard_2K.tex";
                            case 3: return "Brick_Wall_PBR.tex";
                            case 4: return "Carbon_Fiber_Twill.tex";
                            case 5: return "Carrara_Marble.tex";
                            case 6: return "Oak_Wood_Planks.tex";
                            default: return "Solid Albedo Tint";
                        }
                    };
                    const std::string cur_tex_clean = tex_slot_name(ent.use_texture ? ent.custom_texture_id : 0, ent.custom_texture_path);
                    const std::string cur_tex_lbl   = cur_tex_clean + "  (From Context)##insp_tex_slot";
                    if (ImGui::IconButton(cur_tex_lbl.c_str(), cd::ui::chroma::IconKind::kTexture2D,
                                          IM_COL32(85, 235, 165, 255), false, ImVec2(-1.0F, 21.0F)))
                    {
                        ImGui::OpenPopup("inspector_context_tex_popup");
                    }
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* tp = ImGui::AcceptDragDropPayload("CD_ASSET_TEXTURE"))
                        {
                            const char* tex_id = static_cast<const char*>(tp->Data);
                            (void)assign_texture_asset_to_entity(s, s.selected, tex_id, log_push);
                            w.is_dragging_asset_texture = false;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::BeginPopup("inspector_context_tex_popup"))
                    {
                        ImGui::TextColored(ImVec4(0.45F, 0.94F, 0.68F, 1.0F), "Loaded Context Textures");
                        ImGui::Separator();
                        bool any_ctx_tex = false;
                        for (const auto& ca : w.loaded_context_assets)
                        {
                            if (ca.kind != 1U && ca.kind != 2U) continue;
                            any_ctx_tex = true;
                            if (ImGui::MenuItem(ca.name.c_str()))
                                (void)assign_texture_asset_to_entity(s, s.selected, ca.path, log_push);
                        }
                        if (!any_ctx_tex)
                            ImGui::TextDisabled("No textures in Context yet — click a Texture in Disk Files below");
                        ImGui::Separator();
                        if (ImGui::MenuItem("Clear Texture (Solid Albedo)"))
                            (void)assign_texture_asset_to_entity(s, s.selected, "None", log_push);
                        ImGui::EndPopup();
                    }

                    ImGui::Separator();
                    draw_studio_modifier_stack_ui(
                        w.entity_modifiers[ent.handle.id],
                        ent.name.c_str(),
                        s, log_push);
                    ImGui::EndChild();
                }
                else if (s.selected_kind == SelKind::kLight &&
                         s.selected >= 0 && std::cmp_less(s.selected, s.lights.size()))
                {
                    auto& lr = s.lights[static_cast<std::size_t>(s.selected)];
                    ImGui::TextColored(ImVec4(1.0F, 0.88F, 0.35F, 1.0F), "Selected Light: %s", lr.name.c_str());
                    ImGui::Checkbox("Enabled", &lr.enabled);
                    ImGui::DragFloat3("Position (m)", &lr.light.position.x, 0.1F);
                    ImGui::DragFloat3("Direction", &lr.light.direction.x, 0.02F, -1.0F, 1.0F);
                    if (ImGui::SliderFloat("Kelvin (CCT)", &lr.kelvin, 1800.0F, 15000.0F, "%.0f K"))
                        lr.light.color = cd::light::cct_to_linear_rgb(lr.kelvin);
                    ImGui::ColorEdit3("Color Tint", &lr.light.color.x);
                    ImGui::SliderFloat("Intensity", &lr.light.intensity, 100.0F, 120000.0F);
                    ImGui::SliderFloat("Range (m)", &lr.light.range, 1.0F, 40.0F);
                }
                else
                {
                    ImGui::SeparatorText("Project & Context Workflow");
                    ImGui::TextDisabled("1. Open a Sample Project from Disk (showcases/)");
                    ImGui::TextDisabled("2. Click Disk Files to Load into Active Context");
                    ImGui::TextDisabled("3. Click/Drag Context Assets into the 3D Stage");
                    ImGui::Separator();
                    if (ImGui::IconButton("Browse Sample Projects (showcases/)##insp_proj_btn",
                                          cd::ui::chroma::IconKind::kFileProject,
                                          IM_COL32(255, 200, 75, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        w.current_content_folder = "showcases";
                    }
                    if (ImGui::IconButton("Browse Disk Meshes (assets/meshes)##insp_mesh_btn",
                                          cd::ui::chroma::IconKind::kMesh3D,
                                          IM_COL32(95, 195, 255, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        w.current_content_folder = "assets/meshes";
                    }
                    if (ImGui::IconButton("Browse Disk Textures (assets/textures)##insp_tex_btn",
                                          cd::ui::chroma::IconKind::kTexture2D,
                                          IM_COL32(85, 235, 165, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        w.current_content_folder = "assets/textures";
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("World & Fog"))
            {
                ImGui::BeginChild("inspector_world_scroll", ImVec2(0.0F, 0.0F), false);
                ImGui::SliderFloat("World Gravity Y (m/s^2)", &w.world_gravity_y, -25.0F, 5.0F, "%.2f");
                ImGui::Separator();
                draw_studio_modifier_stack_ui(
                    w.environment_modifiers,
                    "World Environment",
                    s, log_push);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Lights & Export"))
            {
                ImGui::BeginChild("inspector_lights_scroll", ImVec2(0.0F, 0.0F), false);
                ImGui::SeparatorText("Clustered Scene Lights");
                if (ImGui::IconButton("Add Point Light##insp_add_pt", cd::ui::chroma::IconKind::kLight,
                                      IM_COL32(255, 220, 90, 255), false, ImVec2(130.0F, 20.0F)))
                {
                    const auto pos = camera_spawn_point(s, 2.5F);
                    s.lights.push_back({ "Point Light (2700K)",
                        cd::light::point(pos, { 1.0F, 0.9F, 0.75F }, 2800.0F, 10.0F), true, 2700.0F });
                    log_push("[studio] added Point Light");
                }
                ImGui::SameLine();
                if (ImGui::IconButton("Add Spot Light##insp_add_sp", cd::ui::chroma::IconKind::kLight,
                                      IM_COL32(255, 195, 85, 255), false, ImVec2(130.0F, 20.0F)))
                {
                    const auto pos = camera_spawn_point(s, 2.5F);
                    s.lights.push_back({ "Spot Light (3200K)",
                        cd::light::spot(pos, { 0.0F, -1.0F, 0.0F }, { 1.0F, 0.95F, 0.85F }, 5000.0F, 12.0F, 0.30F, 0.50F),
                        true, 3200.0F });
                    log_push("[studio] added Spot Light");
                }
                for (std::size_t li = 0; li < s.lights.size(); ++li)
                {
                    auto& lr = s.lights[li];
                    ImGui::PushID(static_cast<int>(li));
                    ImGui::Checkbox(lr.name.c_str(), &lr.enabled);
                    if (lr.enabled)
                    {
                        ImGui::DragFloat3("Pos", &lr.light.position.x, 0.1F);
                        ImGui::SliderFloat("Intensity", &lr.light.intensity, 100.0F, 120000.0F);
                    }
                    ImGui::PopID();
                }

                ImGui::SeparatorText("Standalone Game Export");
                if (ImGui::IconButton("Export Standalone Game (F9)##insp_exp", cd::ui::chroma::IconKind::kFileProject,
                                      IM_COL32(95, 240, 165, 255), false, ImVec2(-1.0F, 26.0F)))
                {
                    (void)export_standalone_game_package(s, "build/export/ChromodynamicGame", log_push);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // =========================================================================
    // PANEL 4 (Bottom): Disk & Context Browser (Stage 1: Disk -> Stage 2: Context -> Scene)
    // =========================================================================
    if (ImGui::Begin("History", nullptr, ImGuiWindowFlags_NoTitleBar))
    {
        if (w.focus_bottom_node_catalog_tab)
        {
            cd::ui::chroma::state().tab_map[ImGui::GetID("BottomDockTabBar")] = 1;
            w.focus_bottom_node_catalog_tab = false;
        }
        if (ImGui::BeginTabBar("BottomDockTabBar"))
        {
            if (ImGui::BeginTabItem("Disk & Context Browser"))
            {
                struct DiskAssetEntry
                {
                    std::string              name;
                    std::string              path;
                    int                      kind; // 0 = Dir, 1 = Mesh/Object, 2 = Texture, 3 = Material, 4 = Project (.cdproj/.cdscene.json)
                    cd::ui::chroma::IconKind icon;
                    ImU32                    color;
                };
                static std::string                 s_cached_folder {};
                static std::vector<DiskAssetEntry> s_cached_entries {};

                auto build_disk_entries = [&](const std::string& folder)
                {
                    s_cached_folder = folder;
                    s_cached_entries.clear();
                    s_cached_entries.reserve(32);

                    if (folder == "showcases")
                    {
                        struct ShowcaseDiskItem { const char* name; const char* path; };
                        constexpr std::array<ShowcaseDiskItem, 10> kDiskProjects {{
                            { "01_PBR_Materials",  "showcases/01_pbr_materials_sss_hair/project.cdproj" },
                            { "02_Realtime_GI",    "showcases/02_realtime_gi_raytracing/project.cdproj" },
                            { "03_Sky_Ocean",      "showcases/03_atmosphere_clouds_ocean_world/project.cdproj" },
                            { "04_GPU_Geometry",   "showcases/04_gpu_driven_geometry_textures/project.cdproj" },
                            { "05_Rockstar_Phys",  "showcases/05_physics_destruction_fluids/project.cdproj" },
                            { "06_PostFX_Optics",  "showcases/06_postfx_upscaling_camera/project.cdproj" },
                            { "07_VFX_Particles",  "showcases/07_particles_decals_foliage/project.cdproj" },
                            { "08_Proc_Synth",     "showcases/08_procedural_asset_synthesis/project.cdproj" },
                            { "09_Studio_Tools",   "showcases/09_editor_tools_observability/project.cdproj" },
                            { "10_Gameplay_Net",   "showcases/10_gameplay_animation_networking/project.cdproj" },
                        }};
                        for (const auto& sp : kDiskProjects)
                        {
                            s_cached_entries.push_back({
                                sp.name, sp.path, 4,
                                cd::ui::chroma::IconKind::kFileProject,
                                IM_COL32(255, 198, 88, 255)
                            });
                        }
                        if (std::filesystem::exists("chromodynamic_studio_scene.cdscene.json"))
                        {
                            s_cached_entries.push_back({
                                "Saved_Scene", "chromodynamic_studio_scene.cdscene.json", 4,
                                cd::ui::chroma::IconKind::kFileScene,
                                IM_COL32(115, 225, 255, 255)
                            });
                        }
                    }
                    else if (folder == "assets" || folder == "assets/meshes" ||
                             folder == "assets/samples/Generic" || folder == "assets/samples/Sponza")
                    {
                        s_cached_entries.push_back({ "Sphere",       "assets/meshes/Sphere.mesh",               1, cd::ui::chroma::IconKind::kSphere,   IM_COL32(110, 220, 255, 255) });
                        s_cached_entries.push_back({ "Cube",         "assets/meshes/Cube.mesh",                 1, cd::ui::chroma::IconKind::kCube,     IM_COL32(95, 195, 255, 255) });
                        s_cached_entries.push_back({ "Cylinder",     "assets/meshes/Cylinder.mesh",             1, cd::ui::chroma::IconKind::kCylinder, IM_COL32(115, 210, 245, 255) });
                        s_cached_entries.push_back({ "Cone",         "assets/meshes/Cone.mesh",                 1, cd::ui::chroma::IconKind::kCone,     IM_COL32(245, 185, 110, 255) });
                        s_cached_entries.push_back({ "Torus",        "assets/meshes/Torus.mesh",                1, cd::ui::chroma::IconKind::kTorus,    IM_COL32(205, 155, 255, 255) });
                        s_cached_entries.push_back({ "TorusKnot",    "assets/meshes/TorusKnot.mesh",            1, cd::ui::chroma::IconKind::kTorus,    IM_COL32(235, 155, 245, 255) });
                        s_cached_entries.push_back({ "CesiumMan",    "assets/samples/Generic/CesiumMan.gltf",   1, cd::ui::chroma::IconKind::kAvatar,   IM_COL32(85, 245, 165, 255) });
                        s_cached_entries.push_back({ "Star_Crystal", "assets/samples/Generic/Star_Crystal.obj", 1, cd::ui::chroma::IconKind::kMesh3D,   IM_COL32(130, 225, 255, 255) });
                        s_cached_entries.push_back({ "Sponza_World", "assets/samples/Sponza/Sponza.gltf",       1, cd::ui::chroma::IconKind::kWorld,    IM_COL32(255, 210, 110, 255) });
                        if (folder == "assets")
                        {
                            s_cached_entries.push_back({ "Gold_24K",      "assets/textures/Gold_Leaf_24K.tex",                    2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(255, 215, 85, 255) });
                            s_cached_entries.push_back({ "Checker_2K",    "assets/textures/UV_Checkerboard_2K.tex",               2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(95, 235, 175, 255) });
                            s_cached_entries.push_back({ "Sponza_Brick",  "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg",2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(235, 145, 105, 255) });
                            s_cached_entries.push_back({ "Carbon_Fiber",  "assets/textures/Carbon_Fiber_Twill.tex",               2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(150, 185, 225, 255) });
                            s_cached_entries.push_back({ "Marble_White",  "assets/textures/Carrara_Marble.tex",                   2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(220, 230, 245, 255) });
                            s_cached_entries.push_back({ "Oak_Planks",    "assets/textures/Oak_Wood_Planks.tex",                  2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(225, 175, 110, 255) });
                        }
                    }
                    else if (folder == "assets/textures" || folder == "assets/samples/Sponza/textures")
                    {
                        s_cached_entries.push_back({ "Checker_2K",    "assets/textures/UV_Checkerboard_2K.tex",                2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(95, 235, 175, 255) });
                        s_cached_entries.push_back({ "Gold_24K",      "assets/textures/Gold_Leaf_24K.tex",                     2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(255, 215, 85, 255) });
                        s_cached_entries.push_back({ "Sponza_Brick",  "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg",2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(235, 145, 105, 255) });
                        s_cached_entries.push_back({ "Carbon_Fiber",  "assets/textures/Carbon_Fiber_Twill.tex",                2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(150, 185, 225, 255) });
                        s_cached_entries.push_back({ "Marble_White",  "assets/textures/Carrara_Marble.tex",                    2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(220, 230, 245, 255) });
                        s_cached_entries.push_back({ "Oak_Planks",    "assets/textures/Oak_Wood_Planks.tex",                   2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(225, 175, 110, 255) });
                        s_cached_entries.push_back({ "Earth_Albedo",  "assets/textures/Earth_Albedo.png",                      2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(95, 195, 255, 255) });
                        s_cached_entries.push_back({ "Lion_Relief",   "assets/samples/Sponza/textures/lion.jpg",               2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(240, 195, 115, 255) });
                        s_cached_entries.push_back({ "Mat_Gold_PBR",  "assets/materials/Mat_Gold_24K.pbr",                     3, cd::ui::chroma::IconKind::kMaterial,  IM_COL32(255, 215, 90, 255) });
                        s_cached_entries.push_back({ "Mat_Irid_440",  "assets/materials/Mat_Iridescent_440nm.pbr",             3, cd::ui::chroma::IconKind::kMaterial,  IM_COL32(210, 155, 255, 255) });
                    }

                    // Scan real disk directory via std::filesystem::directory_iterator
                    std::error_code ec {};
                    if (folder != "showcases" &&
                        std::filesystem::exists(folder, ec) &&
                        std::filesystem::is_directory(folder, ec))
                    {
                        std::size_t disk_count = 0;
                        for (const auto& dir_ent : std::filesystem::directory_iterator(folder, ec))
                        {
                            if (disk_count >= 18U) break;
                            const auto p = dir_ent.path();
                            const std::string fname = p.stem().string();
                            const std::string ext   = p.extension().string();
                            const std::string full  = p.generic_string();
                            bool already_listed = false;
                            for (const auto& ex : s_cached_entries)
                                if (ex.path == full) { already_listed = true; break; }
                            if (already_listed) continue;

                            if (dir_ent.is_directory(ec))
                            {
                                s_cached_entries.push_back({ fname, full, 0, cd::ui::chroma::IconKind::kFolder, IM_COL32(245, 195, 95, 255) });
                                ++disk_count;
                            }
                            else if (ext == ".cdproj" || ext == ".cdproject")
                            {
                                s_cached_entries.push_back({ fname, full, 4, cd::ui::chroma::IconKind::kFileProject, IM_COL32(255, 198, 88, 255) });
                                ++disk_count;
                            }
                            else if (ext == ".json")
                            {
                                s_cached_entries.push_back({ fname, full, 4, cd::ui::chroma::IconKind::kFileScene, IM_COL32(115, 220, 255, 255) });
                                ++disk_count;
                            }
                            else if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".mesh")
                            {
                                s_cached_entries.push_back({ fname, full, 1, cd::ui::chroma::IconKind::kMesh3D, IM_COL32(95, 195, 255, 255) });
                                ++disk_count;
                            }
                            else if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".tga" || ext == ".bmp" || ext == ".tex")
                            {
                                s_cached_entries.push_back({ fname, full, 2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(85, 235, 165, 255) });
                                ++disk_count;
                            }
                        }
                    }
                };

                if (s_cached_folder != w.current_content_folder || s_cached_entries.empty())
                {
                    build_disk_entries(w.current_content_folder);
                }

                // Top Icon Navigation & Pipeline Control Bar
                if (ImGui::IconButton("New Empty##cb_empty", cd::ui::chroma::IconKind::kFileScene,
                                      IM_COL32(130, 210, 255, 255), false, ImVec2(80.0F, 20.0F)))
                {
                    reset_to_clean_empty_project(s, log_push);
                    build_disk_entries(w.current_content_folder);
                }
                ImGui::SameLine(0.0F, 4.0F);
                if (ImGui::IconButton("Projects##cb_prj", cd::ui::chroma::IconKind::kFileProject,
                                      IM_COL32(255, 198, 88, 255), w.current_content_folder == "showcases", ImVec2(72.0F, 20.0F)))
                {
                    w.current_content_folder = "showcases";
                    build_disk_entries(w.current_content_folder);
                }
                ImGui::SameLine(0.0F, 4.0F);
                if (ImGui::IconButton("Objects##cb_obj", cd::ui::chroma::IconKind::kMesh3D,
                                      IM_COL32(95, 195, 255, 255), w.current_content_folder == "assets/meshes", ImVec2(68.0F, 20.0F)))
                {
                    w.current_content_folder = "assets/meshes";
                    build_disk_entries(w.current_content_folder);
                }
                ImGui::SameLine(0.0F, 4.0F);
                if (ImGui::IconButton("Textures##cb_tex", cd::ui::chroma::IconKind::kTexture2D,
                                      IM_COL32(85, 235, 165, 255), w.current_content_folder == "assets/textures", ImVec2(74.0F, 20.0F)))
                {
                    w.current_content_folder = "assets/textures";
                    build_disk_entries(w.current_content_folder);
                }
                ImGui::SameLine(0.0F, 6.0F);
                if (ImGui::IconButton("Load Folder to Context##cb_imp_all", cd::ui::chroma::IconKind::kImport,
                                      IM_COL32(85, 245, 165, 255), false, ImVec2(154.0F, 20.0F)))
                {
                    std::size_t added_cnt = 0U;
                    for (const auto& entry : s_cached_entries)
                    {
                        if (entry.kind == 1 || entry.kind == 2 || entry.kind == 3)
                        {
                            const std::uint8_t ckind = (entry.kind == 1) ? 0U : (entry.kind == 2 ? 1U : 2U);
                            if (w.load_asset_into_context(entry.name, entry.path, ckind))
                                ++added_cnt;
                        }
                    }
                    w.last_asset_status_msg = "Loaded " + std::to_string(added_cnt) + " Disk assets into Active Context";
                    log_push("[context] " + w.last_asset_status_msg);
                }
                ImGui::SameLine(0.0F, 6.0F);
                ImGui::SetNextItemWidth(125.0F);
                ImGui::InputTextWithHint("##asset_search", "Search...",
                                         w.content_search_filter, sizeof(w.content_search_filter));
                ImGui::SameLine(0.0F, 6.0F);
                ImGui::TextColored(ImVec4(0.42F, 0.92F, 0.65F, 1.0F),
                                   "Context: %zu Loaded  |  Disk: %s",
                                   w.loaded_context_assets.size(),
                                   w.current_content_folder.c_str());

                // ---- PANE 1 (Left): Disk Directory Tree (Visual Folder Icons) ----
                ImGui::BeginChild("disk_dir_tree_pane", ImVec2(180.0F, 0.0F), true);
                ImGui::TextColored(ImVec4(0.95F, 0.80F, 0.42F, 1.0F), "1. Disk Directory");
                struct FolderNode
                {
                    const char* label;
                    const char* path;
                    const char* badge;
                };
                constexpr std::array<FolderNode, 7> kFolders {{
                    { "showcases (Projects)", "showcases",                       "10" },
                    { "assets/meshes",        "assets/meshes",                   "3D" },
                    { "assets/textures",      "assets/textures",                 "PBR" },
                    { "assets (All)",         "assets",                          "All" },
                    { "samples/Generic",      "assets/samples/Generic",          "GLTF" },
                    { "samples/Sponza",       "assets/samples/Sponza",           "Atrium" },
                    { "Sponza/textures",      "assets/samples/Sponza/textures",  "69" },
                }};
                for (const auto& fn : kFolders)
                {
                    const bool is_cur_dir = (w.current_content_folder == fn.path);
                    const std::string row_id = std::string(fn.label) + "##dir_" + fn.path;
                    if (ImGui::IconSelectable(
                            row_id.c_str(),
                            is_cur_dir ? cd::ui::chroma::IconKind::kFolderOpen : cd::ui::chroma::IconKind::kFolder,
                            is_cur_dir ? IM_COL32(255, 210, 95, 255) : IM_COL32(225, 180, 95, 255),
                            is_cur_dir, fn.badge))
                    {
                        w.current_content_folder = fn.path;
                        build_disk_entries(w.current_content_folder);
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine();

                // ---- PANE 2 (Center): Disk Files Grid (Click Project to Open, Click Asset to Load to Context) ----
                ImGui::BeginChild("disk_files_grid_pane", ImVec2(325.0F, 0.0F), true);
                ImGui::TextColored(ImVec4(0.52F, 0.86F, 1.0F, 1.0F),
                                   "2. Disk Files (Click to Open / Load)");
                if (ImGui::BeginTable("disk_tiles_grid_4col", 4, ImGuiTableFlags_SizingStretchSame))
                {
                    int disk_tile_id = 0;
                    for (const auto& item : s_cached_entries)
                    {
                        if (w.content_search_filter[0] != '\0' &&
                            item.name.find(w.content_search_filter) == std::string::npos)
                            continue;

                        ImGui::TableNextColumn();
                        ImGui::PushID(disk_tile_id++);
                        const bool in_ctx = w.is_asset_in_context(item.path);
                        const bool is_sel = (w.selected_content_asset == item.path);
                        const std::string tile_lbl = item.name + "##dsk_" + std::to_string(disk_tile_id);
                        if (ImGui::IconTile(tile_lbl.c_str(), item.icon, item.color, is_sel, in_ctx, ImVec2(-1.0F, 52.0F)))
                        {
                            w.selected_content_asset = item.path;
                            if (item.kind == 0)
                            {
                                w.current_content_folder = item.path;
                                build_disk_entries(w.current_content_folder);
                            }
                            else if (item.kind == 4)
                            {
                                (void)load_project_or_scene_from_disk_path(s, item.path, log_push);
                            }
                            else
                            {
                                const std::uint8_t ckind = (item.kind == 1) ? 0U : (item.kind == 2 ? 1U : 2U);
                                (void)w.load_asset_into_context(item.name, item.path, ckind);
                                w.last_asset_status_msg = "Loaded '" + item.name + "' from Disk -> Active Context";
                                w.hud_toast_message     = w.last_asset_status_msg;
                                w.hud_toast_timer       = 2.8F;
                                log_push("[context] " + w.last_asset_status_msg);
                            }
                        }
                        if (ImGui::IsItemHovered())
                        {
                            if (item.kind == 4)
                            {
                                ImGui::SetTooltip("Sample Project on Disk: %s\nPath: %s\nClick to Load Project from Disk",
                                                  item.name.c_str(), item.path.c_str());
                            }
                            else
                            {
                                ImGui::SetTooltip("Disk Asset: %s\nPath: %s\nStatus: %s\nClick to Load into Active Project Context",
                                                  item.name.c_str(), item.path.c_str(),
                                                  in_ctx ? "Loaded in Context (Ready)" : "On Disk (Not in Context)");
                            }
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();

                ImGui::SameLine();

                // ---- PANE 3 (Right): Active Project Context (Loaded Assets Ready for Scene Spawning & Assignment) ----
                ImGui::BeginChild("active_context_tiles_pane", ImVec2(0.0F, 0.0F), true);
                ImGui::TextColored(ImVec4(0.38F, 0.95F, 0.66F, 1.0F),
                                   "3. Active Context (%zu Loaded — Click to Use)",
                                   w.loaded_context_assets.size());
                if (w.loaded_context_assets.empty())
                {
                    ImGui::TextDisabled("Active Context is empty (0 assets loaded).");
                    ImGui::TextDisabled("Click any Mesh/Texture in Disk Files (Pane 2)");
                    ImGui::TextDisabled("to load into Context, or load Starter Pack:");
                    if (ImGui::IconButton("Load Starter Assets from Disk##ctx_seed_btn",
                                          cd::ui::chroma::IconKind::kImport,
                                          IM_COL32(85, 245, 165, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)w.load_asset_into_context("Sphere",       "assets/meshes/Sphere.mesh", 0U);
                        (void)w.load_asset_into_context("Cube",         "assets/meshes/Cube.mesh", 0U);
                        (void)w.load_asset_into_context("Cylinder",     "assets/meshes/Cylinder.mesh", 0U);
                        (void)w.load_asset_into_context("Torus",        "assets/meshes/Torus.mesh", 0U);
                        (void)w.load_asset_into_context("CesiumMan",    "assets/samples/Generic/CesiumMan.gltf", 0U);
                        (void)w.load_asset_into_context("Gold_24K",     "assets/textures/Gold_Leaf_24K.tex", 1U);
                        (void)w.load_asset_into_context("Checker_2K",   "assets/textures/UV_Checkerboard_2K.tex", 1U);
                        (void)w.load_asset_into_context("Sponza_Brick", "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg", 1U);
                        w.last_asset_status_msg = "Loaded 8 Disk Assets into Active Context";
                        log_push("[context] " + w.last_asset_status_msg);
                    }
                }
                else if (ImGui::BeginTable("context_tiles_grid_4col", 4, ImGuiTableFlags_SizingStretchSame))
                {
                    int ctx_tile_id = 0;
                    for (const auto& ca : w.loaded_context_assets)
                    {
                        ImGui::TableNextColumn();
                        ImGui::PushID(ctx_tile_id++);
                        cd::ui::chroma::IconKind c_icon = cd::ui::chroma::IconKind::kMesh3D;
                        ImU32 c_col = IM_COL32(95, 195, 255, 255);
                        if (ca.kind == 0U)
                        {
                            if (ca.name.find("Sphere") != std::string::npos)        c_icon = cd::ui::chroma::IconKind::kSphere;
                            else if (ca.name.find("Cube") != std::string::npos)     c_icon = cd::ui::chroma::IconKind::kCube;
                            else if (ca.name.find("Cylinder") != std::string::npos) c_icon = cd::ui::chroma::IconKind::kCylinder;
                            else if (ca.name.find("Cone") != std::string::npos)     c_icon = cd::ui::chroma::IconKind::kCone;
                            else if (ca.name.find("Torus") != std::string::npos)    c_icon = cd::ui::chroma::IconKind::kTorus;
                            else if (ca.name.find("Cesium") != std::string::npos)   { c_icon = cd::ui::chroma::IconKind::kAvatar; c_col = IM_COL32(85, 245, 165, 255); }
                            else if (ca.name.find("Sponza") != std::string::npos)   { c_icon = cd::ui::chroma::IconKind::kWorld;  c_col = IM_COL32(255, 210, 110, 255); }
                        }
                        else if (ca.kind == 1U)
                        {
                            c_icon = cd::ui::chroma::IconKind::kTexture2D;
                            c_col  = IM_COL32(85, 235, 165, 255);
                        }
                        else
                        {
                            c_icon = cd::ui::chroma::IconKind::kMaterial;
                            c_col  = IM_COL32(210, 155, 255, 255);
                        }

                        const bool is_sel = (w.selected_content_asset == ca.path);
                        const std::string tile_lbl = ca.name + "##ctx_" + std::to_string(ctx_tile_id);
                        if (ImGui::IconTile(tile_lbl.c_str(), c_icon, c_col, is_sel, true, ImVec2(-1.0F, 52.0F)))
                        {
                            w.selected_content_asset = ca.path;
                            if (ca.kind == 0U)
                            {
                                // Spawn Object from Active Context into the 3D Scene!
                                const cd::math::Vec3f pt = camera_spawn_point(s, 3.6F);
                                const int nidx = spawn_entity_with_modifiers(s, "Dynamic_Cube", { pt.x, 0.75F, pt.z }, log_push);
                                if (nidx >= 0 && std::cmp_less(nidx, s.entities.size()))
                                {
                                    s.entities[static_cast<std::size_t>(nidx)].name = ca.name;
                                    (void)assign_mesh_asset_to_entity(s, nidx, ca.path, log_push);
                                    s.selected      = nidx;
                                    s.selected_kind = SelKind::kEntity;
                                }
                            }
                            else
                            {
                                // Apply Texture/Material from Active Context to Selected Scene Entity!
                                const int tidx = (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                                    ? s.selected
                                    : (!s.entities.empty() ? static_cast<int>(s.entities.size() - 1U) : -1);
                                if (tidx >= 0)
                                    (void)assign_texture_asset_to_entity(s, tidx, ca.path, log_push);
                            }
                        }

                        // Drag-and-Drop Source from Active Project Context -> 3D Scene / Outliner / Inspector
                        if (ca.kind == 0U && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                        {
                            ImGui::SetDragDropPayload("CD_ASSET_MESH", ca.path.c_str(), ca.path.size() + 1);
                            w.is_dragging_asset_mesh = true;
                            w.drag_asset_mesh_id     = ca.path;
                            ImGui::Text("Use Context Object '%s' in Scene", ca.name.c_str());
                            ImGui::EndDragDropSource();
                        }
                        else if (ca.kind != 0U && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                        {
                            ImGui::SetDragDropPayload("CD_ASSET_TEXTURE", ca.path.c_str(), ca.path.size() + 1);
                            w.is_dragging_asset_texture = true;
                            w.drag_asset_texture_id     = ca.path;
                            ImGui::Text("Apply Context Texture '%s' to Object", ca.name.c_str());
                            ImGui::EndDragDropSource();
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("Active Context Asset: %s\nDisk Source: %s\n%s",
                                              ca.name.c_str(), ca.path.c_str(),
                                              ca.kind == 0U
                                                  ? "Click to Spawn in Scene, or Drag onto any Object"
                                                  : "Click or Drag to Apply Texture to Selected Object");
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("250 Constructs"))
            {
                draw_universal_250_node_catalog_panel(s, log_push);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Console & Physics"))
            {
                std::size_t dyn_rb_count = 0;
                std::size_t col_count    = 0;
                for (const auto& ent : s.entities)
                {
                    ModifierStack& ms = w.entity_modifiers[ent.handle.id];
                    if (const auto* rb = ms.template find<RigidBodyModifier>(); rb && rb->enabled &&
                        rb->body_type == cd::physics::BodyType::kDynamic)
                    {
                        ++dyn_rb_count;
                    }
                    if (ms.find_collider() != nullptr) ++col_count;
                }

                if (ImGui::SmallButton("Clean Ultra-FPS"))
                    load_studio_interactive_template(s, 0, log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Ramp & Tower"))
                    load_studio_interactive_template(s, 1, log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Domino Chain"))
                    load_studio_interactive_template(s, 2, log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Shoot Ball (B)"))
                    shoot_physics_ball_from_camera(s, log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset Pos (R)"))
                    reset_physics_to_home_positions(s, log_push);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.42F, 0.96F, 0.64F, 1.0F),
                                   "| %.0f FPS (%.2f ms) | Phys: %s | RBs: %zu | Cols: %zu | Contacts: %zu",
                                   static_cast<double>(w.smoothed_fps),
                                   static_cast<double>(w.smoothed_frame_ms),
                                   (s.pie_playing || w.simulate_physics_in_edit) ? "RUNNING" : "PAUSED",
                                   dyn_rb_count, col_count, w.live_contacts.size());

                const auto rtel = w.rockstar_physics_engine.telemetry();
                float rag_err = 0.0F;
                if (const auto* rag = w.rockstar_physics_engine.get_euphoria_ragdoll(0u))
                    rag_err = rag->balance_state().balance_error_m;
                ImGui::TextColored(ImVec4(0.56F, 0.86F, 1.0F, 1.0F),
                                   "Rockstar 2D+3D (33 Mod): 2D=%u | 3D=%u | Euphoria=%u (ZMP=%.2fm) | Pacejka=%u | Fracture=%u | XPBD=%u",
                                   rtel.bodies_2d, rtel.bodies_3d, rtel.active_ragdolls,
                                   static_cast<double>(rag_err),
                                   rtel.active_vehicles, rtel.fracture_systems, rtel.xpbd_soft_bodies);
                ImGui::SameLine();
                if (ImGui::SmallButton("Impulse Euphoria"))
                {
                    if (auto* rag = w.rockstar_physics_engine.get_euphoria_ragdoll(0u))
                    {
                        rag->apply_ballistic_hit_impulse(
                            cd::physics::rockstar::RagdollBoneId::kChest,
                            { 120.0F, 18.0F, 0.0F },
                            { 0.0F, 1.3F, 0.0F });
                        log_push("[physics] Applied ballistic impulse to Euphoria Active Ragdoll (Capture Point step active)");
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Shatter Voronoi"))
                {
                    if (auto* fr = w.rockstar_physics_engine.get_fracture_system(0u))
                    {
                        const auto brk = fr->apply_impact_impulse({ 0.0F, 0.7F, 0.0F }, 1.2F, 320.0F);
                        log_push("[physics] Shattered " + std::to_string(brk) + " structural bonds in Voronoi Destruction Graph");
                    }
                }

                ImGui::Separator();
                ImGui::BeginChild("studio_log_scroll", ImVec2(0, 0), false);
                for (const auto& line : s.log)
                {
                    ImGui::TextUnformatted(line.c_str());
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Project Hub (15)"))
            {
                ImGui::TextColored(ImVec4(0.55F, 0.88F, 1.0F, 1.0F),
                                   "Active Project: %s  |  World: %s  |  Level: %s",
                                   w.active_project_name.c_str(),
                                   w.active_world_name.c_str(),
                                   w.active_level_name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset Master 6-World Project"))
                {
                    s.cd_world.set_project(nullptr);
                    populate_master_showcase_project(s.cd_world);
                    if (const auto* np = s.cd_world.project())
                    {
                        w.active_project_name = std::string(np->name());
                        if (const auto* lvl0 = np->level(0))
                            (void)switch_to_project_level(s, lvl0->name(), log_push);
                    }
                }
                ImGui::Separator();

                if (ImGui::BeginTable("showcase_hub_grid", 4, ImGuiTableFlags_SizingStretchSame))
                {
                    for (std::size_t idx = 0; idx < kShowcaseCatalog.size(); ++idx)
                    {
                        const auto& sc = kShowcaseCatalog[idx];
                        ImGui::TableNextColumn();
                        ImGui::PushID(static_cast<int>(idx));
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.25F, 0.36F, 1.0F));
                        if (ImGui::Button(sc.title, ImVec2(-1.0F, 26.0F)))
                        {
                            load_showcase_project_by_index(s, idx, log_push);
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("Project: %s\nWorld: %s\nFile: %s",
                                              sc.id, sc.world_group, sc.cdproj_path);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("GPU Profiler"))
            {
                const bool bloom_active    = (s.fx.bloom_post > 0.001F);
                const bool velocity_active = (s.fx.taa_amount > 0.001F || s.fx.motion_blur > 0.001F);
                ImGui::TextColored(ImVec4(0.40F, 0.98F, 0.62F, 1.0F),
                                   "Smoothed: %.0f FPS (%.2f ms)   |   Peak: %.0f FPS   |   Entities: %zu   |   Lights: %zu",
                                   static_cast<double>(w.smoothed_fps),
                                   static_cast<double>(w.smoothed_frame_ms),
                                   static_cast<double>(w.peak_fps),
                                   s.entities.size(),
                                   s.lights.size());
                ImGui::Text("Render Pass Bypass State:  ShadowMap [Cached Static]  |  TLAS [Hash-Gated]  |  Velocity GBuffer [%s]  |  7-Pass Bloom [%s]",
                            velocity_active ? "ACTIVE" : "BYPASSED (0 ms)",
                            bloom_active    ? "ACTIVE" : "BYPASSED (0 ms)");
                ImGui::PlotLines("Frame Time (ms)",
                                 w.frame_ms_history.data(),
                                 static_cast<int>(w.frame_ms_history.size()),
                                 static_cast<int>(w.frame_ms_cursor % w.frame_ms_history.size()),
                                 nullptr,
                                 0.0F, 16.6F,
                                 ImVec2(0.0F, 68.0F));
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Shader Graph"))
            {
                ImGui::BeginNodeGraph("BottomShaderGraph", ImVec2(0.0F, 150.0F));
                ImGui::BeginNode(10U, "PBR Texture Sampler", ImVec2(24.0F, 22.0F),
                                 ImVec2(190.0F, 72.0F), IM_COL32(45, 115, 185, 255));
                ImGui::NodePin(1001U, "Albedo RGB", true, IM_COL32(255, 180, 80, 255));
                ImGui::EndNode();

                ImGui::BeginNode(11U, "GGX Direct + IBL Eval", ImVec2(275.0F, 18.0F),
                                 ImVec2(200.0F, 92.0F), IM_COL32(35, 145, 95, 255));
                ImGui::NodePin(1002U, "BaseColor", false, IM_COL32(255, 180, 80, 255));
                ImGui::NodePin(1003U, "HDR Out", true, IM_COL32(95, 195, 255, 255));
                ImGui::EndNode();

                ImGui::BeginNode(12U, "Surface Output", ImVec2(540.0F, 26.0F),
                                 ImVec2(180.0F, 72.0F), IM_COL32(185, 85, 45, 255));
                ImGui::NodePin(1004U, "Final Color", false, IM_COL32(95, 195, 255, 255));
                ImGui::EndNode();

                ImGui::NodeLink(1001U, 1002U, IM_COL32(255, 180, 80, 235), 2.5F);
                ImGui::NodeLink(1003U, 1004U, IM_COL32(95, 195, 255, 245), 2.5F);
                ImGui::EndNodeGraph();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Timeline & Curves"))
            {
                static float s_dock_time = 1.65F;
                static float s_dock_curve[8] = {
                    0.0F, 0.0F, 0.25F, 0.80F, 0.75F, 0.20F, 1.0F, 1.0F
                };
                const float kfs[4] = { 0.4F, 1.65F, 3.1F, 4.5F };
                ImGui::TimelineScrubber("Camera Sequencer", &s_dock_time, 5.0F, kfs, 4);
                ImGui::CurveEditor("Easing", s_dock_curve, ImVec2(0.0F, 95.0F));
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace cd::studio
