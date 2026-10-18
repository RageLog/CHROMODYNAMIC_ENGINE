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

struct HubShowcaseMeta
{
    const char* badge;
    ImVec4      badge_color;
    int         category_group; // 1 = 3D Rendering & World, 2 = Physics & Simulation, 3 = Gameplay, Audio & UI
};

[[nodiscard]] inline constexpr HubShowcaseMeta get_hub_showcase_meta(std::size_t idx) noexcept
{
    switch (idx)
    {
        case 0:  return { "[RENDER]",   ImVec4(0.35F, 0.85F, 1.0F, 1.0F), 1 };
        case 1:  return { "[WORLD]",    ImVec4(0.40F, 0.90F, 0.70F, 1.0F), 1 };
        case 2:  return { "[RENDER]",   ImVec4(0.35F, 0.85F, 1.0F, 1.0F), 1 };
        case 3:  return { "[RENDER]",   ImVec4(0.35F, 0.85F, 1.0F, 1.0F), 1 };
        case 4:  return { "[GAMEPLAY]", ImVec4(1.0F, 0.65F, 0.35F, 1.0F), 3 };
        case 5:  return { "[PHYSICS]",  ImVec4(0.95F, 0.50F, 0.50F, 1.0F), 2 };
        case 6:  return { "[WORLD]",    ImVec4(0.40F, 0.90F, 0.70F, 1.0F), 1 };
        case 7:  return { "[GAMEPLAY]", ImVec4(1.0F, 0.65F, 0.35F, 1.0F), 3 };
        case 8:  return { "[PHYSICS]",  ImVec4(0.95F, 0.50F, 0.50F, 1.0F), 2 };
        case 9:  return { "[PHYSICS]",  ImVec4(0.95F, 0.50F, 0.50F, 1.0F), 2 };
        case 10: return { "[UI / 2D]",  ImVec4(0.85F, 0.60F, 1.0F, 1.0F), 3 };
        case 11: return { "[GAMEPLAY]", ImVec4(1.0F, 0.65F, 0.35F, 1.0F), 3 };
        default: return { "[PROJECT]",  ImVec4(0.70F, 0.70F, 0.70F, 1.0F), 0 };
    }
}

[[nodiscard]] inline bool matches_hub_category(const HubShowcaseMeta& meta, int tab) noexcept
{
    if (tab <= 0) return true;
    return meta.category_group == tab;
}

template <typename StateT, typename LogFn>
inline void draw_project_manager_screen(
    StateT&       s,
    std::uint32_t frame_w,
    std::uint32_t frame_h,
    LogFn&&       log_push)
{
    auto& w = studio_world();
    const float sw = static_cast<float>(frame_w);
    const float sh = static_cast<float>(frame_h);

    ImGui::SetNextWindowPos(ImVec2(0.0F, 25.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh - 25.0F), ImGuiCond_Always);

    ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("StudioHubMainWindow", nullptr, win_flags))
    {
        ImGui::End();
        return;
    }

    // =========================================================================
    // CHROMODYNAMIC Studio Hub — Native Card-Grid Architecture
    // =========================================================================

    // -------------------------------------------------------------------------
    // 1. Left Sidebar Column (240px width)
    // -------------------------------------------------------------------------
    ImGui::BeginChild("hub_sidebar", ImVec2(240.0F, sh - 45.0F), true);

    ImGui::TextColored(ImVec4(0.35F, 0.85F, 1.0F, 1.0F), "CHROMODYNAMIC");
    ImGui::SameLine();
    ImGui::TextDisabled("Studio Hub • v1.0");
    ImGui::Separator();

    ImGui::Checkbox("Preload Starter Assets##pm_preload", &w.preload_starter_context_assets);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("When checked, preloads 8 basic 3D meshes & materials into Active Context.\nWhen unchecked, starts with a completely empty Active Context.");
    }

    // Primary Action Button (Large Green): New Empty 3D Stage
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.54F, 0.34F, 1.0F));
    if (ImGui::Button("+ New Empty 3D Stage", ImVec2(-1.0F, 32.0F)))
    {
        reset_to_clean_empty_project(s, log_push, false);
        if (!w.preload_starter_context_assets)
        {
            w.loaded_context_assets.clear();
            w.selected_content_asset.clear();
            w.selected_context_idx = -1;
        }
        else
        {
            w.populate_starter_context_assets();
        }
        w.show_project_manager = false;
        log_push("[hub] Launched new clean empty 3D stage");
    }
    ImGui::PopStyleColor();

    // Secondary Action Button: Open Context Demo Stage
    if (ImGui::Button("Open Context Demo Stage", ImVec2(-1.0F, 26.0F)))
    {
        seed_demo_context_and_scene(s, log_push);
        w.show_project_manager = false;
        log_push("[hub] Loaded context demo stage");
    }

    // Tertiary Action Button: Load Saved Scene (.cdscene)
    if (ImGui::Button("Load Saved Scene (.cdscene)", ImVec2(-1.0F, 26.0F)))
    {
        (void)load_full_studio_scene(s, "studio_scene.cdscene.json", log_push);
        w.show_project_manager = false;
        log_push("[hub] Loaded saved studio scene");
    }

    ImGui::Separator();
    ImGui::TextDisabled("WORKSPACES & FILTERS");

    constexpr std::array<const char*, 4> kHubCategories {{
        "All Workspaces (12)",
        "3D Rendering & World",
        "Physics & Simulation",
        "Gameplay, Audio & UI"
    }};

    for (int ci = 0; ci < 4; ++ci)
    {
        const bool is_sel = (w.project_manager_tab == ci);
        if (is_sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.38F, 0.62F, 1.0F));
        if (ImGui::Button(kHubCategories[static_cast<std::size_t>(ci)], ImVec2(-1.0F, 24.0F)))
        {
            w.project_manager_tab = ci;
        }
        if (is_sel) ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::TextDisabled("New Project Name:");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputText("##pm_new_proj_name", w.pm_new_project_buf, sizeof(w.pm_new_project_buf));
    if (ImGui::Button("Create Named Project", ImVec2(-1.0F, 24.0F)))
    {
        reset_to_clean_empty_project(s, log_push, false);
        if (!w.preload_starter_context_assets)
        {
            w.loaded_context_assets.clear();
            w.selected_content_asset.clear();
            w.selected_context_idx = -1;
        }
        else
        {
            w.populate_starter_context_assets();
        }
        if (w.pm_new_project_buf[0] != '\0')
            w.active_project_name = w.pm_new_project_buf;
        w.show_project_manager = false;
        log_push("[project] Created named project: " + w.active_project_name);
    }

    ImGui::Separator();
    ImGui::TextDisabled("Theme:");
    if (ImGui::SmallButton("Chroma Slate"))    ImGui::ApplyTheme(0);
    ImGui::SameLine();
    if (ImGui::SmallButton("Obsidian"))        ImGui::ApplyTheme(1);
    if (ImGui::SmallButton("Graphite"))        ImGui::ApplyTheme(2);
    ImGui::SameLine();
    if (ImGui::SmallButton("Light"))           ImGui::ApplyTheme(3);

    ImGui::EndChild();

    ImGui::SameLine(0.0F, 10.0F);

    // -------------------------------------------------------------------------
    // 2. Right Main Area (Search bar, Quick-Start Hero cards, 3-column Workspaces grid)
    // -------------------------------------------------------------------------
    ImGui::BeginChild("hub_main", ImVec2(sw - 265.0F, sh - 45.0F), false);

    // Upper Search & Fast Transition Bar
    ImGui::SetNextItemWidth(std::max(180.0F, (sw - 265.0F) - 340.0F));
    ImGui::InputTextWithHint("##hub_search", "Search workspaces...", w.pm_filter_buf, sizeof(w.pm_filter_buf));
    ImGui::SameLine(0.0F, 8.0F);

    // Open Selected Project button
    if (ImGui::Button("Open Selected", ImVec2(110.0F, 24.0F)))
    {
        if (w.selected_pm_project_idx >= 0 && std::cmp_less(w.selected_pm_project_idx, kShowcaseCatalog.size()))
        {
            load_showcase_project_by_index(s, static_cast<std::size_t>(w.selected_pm_project_idx), log_push);
            w.show_project_manager = false;
        }
    }
    ImGui::SameLine(0.0F, 8.0F);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.35F, 0.52F, 1.0F));
    if (ImGui::Button("Skip to 3D Editor ->", ImVec2(160.0F, 24.0F)))
    {
        w.show_project_manager = false;
    }
    ImGui::PopStyleColor();

    ImGui::Separator();

    // -------------------------------------------------------------------------
    // Quick Start Hero Card Row (3 columns, height 92px)
    // -------------------------------------------------------------------------
    const float main_avail_w = ImGui::GetContentRegionAvail().x;
    const float hero_col_w   = (main_avail_w - 16.0F) / 3.0F;

    // Hero Card 1: Blank 3D Stage (Cyan)
    ImGui::BeginChild("hero_blank_stage", ImVec2(hero_col_w, 92.0F), true);
    ImGui::TextColored(ImVec4(0.35F, 0.85F, 1.0F, 1.0F), "Blank 3D Stage");
    ImGui::TextDisabled("Clean 3D scene with transparent 1m/5m floor grid & directional sun.");
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.45F, 0.65F, 1.0F));
    if (ImGui::Button("Launch Blank Stage##hero1", ImVec2(-1.0F, 24.0F)))
    {
        reset_to_clean_empty_project(s, log_push, true);
        if (!w.preload_starter_context_assets)
        {
            w.loaded_context_assets.clear();
            w.selected_content_asset.clear();
            w.selected_context_idx = -1;
        }
        else
        {
            w.populate_starter_context_assets();
        }
        w.show_project_manager = false;
        log_push("[hub] Launched blank 3D stage");
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::SameLine(0.0F, 8.0F);

    // Hero Card 2: Asset & Context Sandbox (Amber)
    ImGui::BeginChild("hero_context_sandbox", ImVec2(hero_col_w, 92.0F), true);
    ImGui::TextColored(ImVec4(1.0F, 0.78F, 0.35F, 1.0F), "Asset & Context Sandbox");
    ImGui::TextDisabled("Pre-loaded PBR meshes & textures ready for Context Browser testing.");
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55F, 0.42F, 0.18F, 1.0F));
    if (ImGui::Button("Open Sandbox##hero2", ImVec2(-1.0F, 24.0F)))
    {
        seed_demo_context_and_scene(s, log_push);
        w.show_project_manager = false;
        log_push("[hub] Opened asset & context sandbox");
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::SameLine(0.0F, 8.0F);

    // Hero Card 3: Saved Scene (Emerald)
    std::error_code secc{};
    const bool has_saved = std::filesystem::exists("studio_scene.cdscene.json", secc);
    ImGui::BeginChild("hero_saved_scene", ImVec2(hero_col_w, 92.0F), true);
    ImGui::TextColored(ImVec4(0.40F, 0.95F, 0.65F, 1.0F), "Saved Scene (.cdscene)");
    if (has_saved)
        ImGui::TextDisabled("studio_scene.cdscene.json available on disk.");
    else
        ImGui::TextDisabled("No saved scene yet. Start fresh & save anytime.");
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.50F, 0.35F, 1.0F));
    const char* saved_btn_lbl = has_saved ? "Resume Saved Scene##hero3" : "Start & Save New##hero3";
    if (ImGui::Button(saved_btn_lbl, ImVec2(-1.0F, 24.0F)))
    {
        if (has_saved)
            (void)load_full_studio_scene(s, "studio_scene.cdscene.json", log_push);
        else
            reset_to_clean_empty_project(s, log_push, false);
        w.show_project_manager = false;
        log_push("[hub] Activated studio scene");
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.68F, 0.74F, 0.82F, 1.0F), "SHOWCASE & TEMPLATE WORKSPACES");

    // -------------------------------------------------------------------------
    // Showcase & Template Workspaces Card Grid (3 Columns)
    // -------------------------------------------------------------------------
    const std::string filter_str(w.pm_filter_buf);
    auto to_lower = [](std::string str) {
        for (char& c : str) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return str;
    };
    const std::string needle = to_lower(filter_str);

    std::vector<std::size_t> matched_indices;
    matched_indices.reserve(kShowcaseCatalog.size());
    for (std::size_t i = 0; i < kShowcaseCatalog.size(); ++i)
    {
        const auto& sc   = kShowcaseCatalog[i];
        const auto  meta = get_hub_showcase_meta(i);

        if (!matches_hub_category(meta, w.project_manager_tab)) continue;

        if (!needle.empty())
        {
            const bool match_title = to_lower(sc.title).find(needle) != std::string::npos;
            const bool match_world = to_lower(sc.world_group).find(needle) != std::string::npos;
            const bool match_id    = to_lower(sc.id).find(needle) != std::string::npos;
            const bool match_badge = to_lower(meta.badge).find(needle) != std::string::npos;
            if (!match_title && !match_world && !match_id && !match_badge)
            {
                continue;
            }
        }
        matched_indices.push_back(i);
    }

    const float grid_avail_w = ImGui::GetContentRegionAvail().x;
    const float card_w       = (grid_avail_w - 16.0F) / 3.0F;
    constexpr float kCardH   = 96.0F;

    ImGui::BeginChild("hub_workspace_grid", ImVec2(0.0F, 0.0F), false);

    if (matched_indices.empty())
    {
        ImGui::TextDisabled("No showcase workspaces found matching filter: \"%s\"", w.pm_filter_buf);
        if (ImGui::Button("Reset Filter", ImVec2(120.0F, 24.0F)))
        {
            w.pm_filter_buf[0] = '\0';
            w.project_manager_tab = 0;
        }
    }
    else
    {
        for (std::size_t k = 0; k < matched_indices.size(); ++k)
        {
            const std::size_t sc_idx = matched_indices[k];
            const auto&       sc     = kShowcaseCatalog[sc_idx];
            const auto        meta   = get_hub_showcase_meta(sc_idx);

            if ((k % 3) != 0)
            {
                ImGui::SameLine(0.0F, 8.0F);
            }

            const bool is_sel_card = (w.selected_pm_project_idx == static_cast<int>(sc_idx));
            if (is_sel_card)
            {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35F, 0.85F, 1.0F, 1.0F));
            }

            const std::string card_id = "sc_card_" + std::to_string(sc_idx);
            ImGui::BeginChild(card_id.c_str(), ImVec2(card_w, kCardH), true);

            if (is_sel_card)
            {
                ImGui::PopStyleColor();
            }

            // Click to select card, double click to open
            if (ImGui::IsItemHovered())
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    w.selected_pm_project_idx = static_cast<int>(sc_idx);
                }
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    load_showcase_project_by_index(s, sc_idx, log_push);
                    w.show_project_manager = false;
                }
            }

            // Category Badge & Title
            ImGui::TextColored(meta.badge_color, "%s", meta.badge);
            ImGui::SameLine();
            ImGui::TextColored(is_sel_card ? ImVec4(0.40F, 0.95F, 1.0F, 1.0F) : ImVec4(0.92F, 0.95F, 1.0F, 1.0F),
                               "%s", sc.title);

            // World Group
            ImGui::TextDisabled("%s", sc.world_group);

            // Action Buttons: [ Open Project ] [ > Play ]
            const float inner_w    = ImGui::GetContentRegionAvail().x;
            const float open_btn_w = (inner_w - 6.0F) * 0.62F;
            const float play_btn_w = inner_w - open_btn_w - 6.0F;

            ImGui::PushStyleColor(ImGuiCol_Button, is_sel_card ? ImVec4(0.24F, 0.55F, 0.88F, 1.0F) : ImVec4(0.18F, 0.44F, 0.74F, 1.0F));
            if (ImGui::Button(("Open Project##" + card_id).c_str(), ImVec2(open_btn_w, 24.0F)))
            {
                w.selected_pm_project_idx = static_cast<int>(sc_idx);
                load_showcase_project_by_index(s, sc_idx, log_push);
                w.show_project_manager = false;
            }
            ImGui::PopStyleColor();

            ImGui::SameLine(0.0F, 6.0F);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.52F, 0.32F, 1.0F));
            if (ImGui::Button(("> Play##" + card_id).c_str(), ImVec2(play_btn_w, 24.0F)))
            {
                w.selected_pm_project_idx = static_cast<int>(sc_idx);
                load_showcase_project_by_index(s, sc_idx, log_push);
                enter_pie_play_mode(s, log_push);
                w.show_project_manager = false;
            }
            ImGui::PopStyleColor();

            ImGui::EndChild();
        }
    }

    ImGui::EndChild(); // hub_workspace_grid
    ImGui::EndChild(); // hub_main

    ImGui::End();
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

    if (w.show_project_manager)
    {
        draw_project_manager_screen(s, frame_w, frame_h, log_push);
        return;
    }

    // Draw 3D Collider wireframes + full-width Ribbon Toolbar + right-click context menu
    draw_studio_viewport_overlays_and_drop_targets(s, vp, frame_w, frame_h, log_push);

    // Pinned 16:9 Camera Preview overlay (active when Camera selected or piloted)
    draw_camera_preview_overlay(s, frame_w, frame_h, log_push);

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
    // PANEL 1 (Left Full-Height Dock): Scene | Create | Stages
    // =========================================================================
    if (ImGui::Begin("Studio Workbench", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (ImGui::BeginTabBar("StudioLeftDockTabs"))
        {
            // -----------------------------------------------------------------
            // TAB 1 (DEFAULT): Scene Hierarchy Outliner
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Scene"))
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
                if (ImGui::IconButton("+ Floor##out_flr", cd::ui::chroma::IconKind::kCollider,
                                      IM_COL32(130, 175, 220, 255), false, ImVec2(58.0F, 20.0F)))
                {
                    (void)spawn_ground_floor_entity(s, log_push);
                }
                ImGui::SameLine();
                if (ImGui::IconButton("Light##out_lgt", cd::ui::chroma::IconKind::kLight,
                                      IM_COL32(255, 220, 90, 255), false, ImVec2(54.0F, 20.0F)))
                {
                    ImGui::OpenPopup("scene_add_light_popup");
                }
                if (ImGui::BeginPopup("scene_add_light_popup"))
                {
                    ImGui::TextColored(ImVec4(1.0F, 0.88F, 0.35F, 1.0F), "Add Studio Light");
                    ImGui::Separator();
                    if (ImGui::MenuItem("+ Directional Sun Light (5800K)"))
                        (void)spawn_studio_light_by_type(s, 0, log_push);
                    if (ImGui::MenuItem("+ Point Omni Light (3200K)"))
                        (void)spawn_studio_light_by_type(s, 1, log_push);
                    if (ImGui::MenuItem("+ Spot Cone Light (4200K)"))
                        (void)spawn_studio_light_by_type(s, 2, log_push);
                    if (ImGui::MenuItem("+ Rect Area Softbox (5600K)"))
                        (void)spawn_studio_light_by_type(s, 3, log_push);
                    if (ImGui::MenuItem("+ Ambient Sky Fill (6500K)"))
                        (void)spawn_studio_light_by_type(s, 4, log_push);
                    ImGui::Separator();
                    if (ImGui::MenuItem("+ 3-Point Studio Rig (Key+Fill+Rim)"))
                        (void)spawn_studio_light_by_type(s, 5, log_push);
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                if (ImGui::IconButton("Avatar##out_av", cd::ui::chroma::IconKind::kAvatar,
                                      IM_COL32(85, 245, 165, 255), false, ImVec2(62.0F, 20.0F)))
                {
                    const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                    (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { pt.x, 0.0F, pt.z }, log_push);
                }
                ImGui::SameLine();
                if (ImGui::IconButton("+ 3D Char##out_3dchar", cd::ui::chroma::IconKind::kAvatar,
                                      IM_COL32(85, 245, 165, 255), false, ImVec2(72.0F, 20.0F)))
                {
                    (void)spawn_empty_character_vessel(s, false, log_push);
                }
                ImGui::SameLine();
                if (ImGui::IconButton("+ 2D Char##out_2dchar", cd::ui::chroma::IconKind::kAvatar,
                                      IM_COL32(95, 235, 210, 255), false, ImVec2(72.0F, 20.0F)))
                {
                    (void)spawn_empty_character_vessel(s, true, log_push);
                }
                ImGui::SameLine();
                if (ImGui::IconButton("+ Cam 3D##out_cam3d", cd::ui::chroma::IconKind::kCamera,
                                      IM_COL32(200, 155, 255, 255), false, ImVec2(72.0F, 20.0F)))
                {
                    (void)spawn_scene_camera_entity(s, false, log_push);
                }
                ImGui::SameLine();
                if (ImGui::IconButton("+ Clouds##out_cld", cd::ui::chroma::IconKind::kWorld,
                                      IM_COL32(185, 215, 255, 255), false, ImVec2(68.0F, 20.0F)))
                {
                    (void)spawn_volumetric_clouds_entity(s, log_push);
                }
                ImGui::SameLine();
                if (ImGui::IconButton("+ Fog##out_fog", cd::ui::chroma::IconKind::kWorld,
                                      IM_COL32(200, 225, 245, 255), false, ImVec2(58.0F, 20.0F)))
                {
                    (void)spawn_volumetric_fog_entity(s, log_push);
                }

                // Row 2: Search Filter + Save / Load
                static char s_scene_file_buf[128] = "studio_scene.cdscene.json";
                ImGui::SetNextItemWidth(170.0F);
                ImGui::InputTextWithHint("##filter_nodes", "Filter scene...", w.hierarchy_filter, sizeof(w.hierarchy_filter));
                ImGui::SameLine();
                if (ImGui::SmallButton("Save"))
                {
                    if (save_full_studio_scene(s, s_scene_file_buf, log_push))
                    {
                        w.hud_toast_message = std::string("Scene saved: ") + s_scene_file_buf;
                        w.hud_toast_timer   = 3.0F;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Load"))
                {
                    if (load_full_studio_scene(s, s_scene_file_buf, log_push))
                    {
                        w.hud_toast_message = std::string("Scene loaded: ") + s_scene_file_buf;
                        w.hud_toast_timer   = 3.0F;
                    }
                }

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

                // Full-Height Scrollable Icon-Driven Scene Tree (Frameless)
                ImGui::BeginChild("scene_hierarchy_scroll", ImVec2(0.0F, 0.0F), false);
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
                    // Fast Delete [X] button
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.35F, 0.35F, 1.0F));
                    const std::string del_btn = "[X]##del_ent_" + std::to_string(i);
                    if (ImGui::SmallButton(del_btn.c_str()))
                    {
                        ent.locked = false;
                        deferred_del_idx = static_cast<int>(i);
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Delete entity '%s'", clean_name.c_str());
                    ImGui::SameLine();

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
                        clean_name.find("Ragdoll") != std::string::npos || clean_name.find("Character") != std::string::npos)
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
                    else if (clean_name.find("Clouds") != std::string::npos)
                    {
                        ent_icon = cd::ui::chroma::IconKind::kWorld;
                        ent_col  = IM_COL32(185, 215, 255, 255);
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
                            else if (m->class_name() == "CameraLensModifier")
                            {
                                ent_icon = cd::ui::chroma::IconKind::kCamera;
                                ent_col  = IM_COL32(200, 155, 255, 255);
                            }
                            else if (m->class_name() == "CloudLayerModifier")
                            {
                                ent_icon = cd::ui::chroma::IconKind::kWorld;
                                ent_col  = IM_COL32(185, 215, 255, 255);
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

                    if (w.inline_rename_entity_idx == static_cast<int>(i))
                    {
                        ImGui::PushItemWidth(130.0F);
                        (void)ImGui::InputText("##inline_rename_ent", w.inline_rename_buf, sizeof(w.inline_rename_buf));
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("OK##ok_rent"))
                        {
                            if (w.inline_rename_buf[0] != '\0')
                            {
                                ent.name = w.inline_rename_buf;
                                log_push("[hierarchy] Renamed entity to '" + ent.name + "'");
                            }
                            w.inline_rename_entity_idx = -1;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##cnc_rent"))
                        {
                            w.inline_rename_entity_idx = -1;
                        }
                    }
                    else
                    {
                        if (ImGui::IconSelectable(row_id_label.c_str(), ent_icon, ent_col, is_sel, pill_txt))
                        {
                            s.selected      = static_cast<int>(i);
                            s.selected_kind = SelKind::kEntity;
                        }
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            w.inline_rename_entity_idx = static_cast<int>(i);
                            std::snprintf(w.inline_rename_buf, sizeof(w.inline_rename_buf), "%s", clean_name.c_str());
                        }
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
                        if (ImGui::MenuItem("Rename (Double-Click)"))
                        {
                            w.inline_rename_entity_idx = static_cast<int>(i);
                            std::snprintf(w.inline_rename_buf, sizeof(w.inline_rename_buf), "%s", clean_name.c_str());
                        }
                        if (ImGui::MenuItem("Duplicate Object (Ctrl+D)"))
                            deferred_dup_idx = static_cast<int>(i);
                        if (ImGui::MenuItem("Delete Object [X] (Del)"))
                        {
                            ent.locked = false;
                            deferred_del_idx = static_cast<int>(i);
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Focus Camera (F)"))
                            focus_selected(s, log_push);
                        if (ImGui::MenuItem("Snap to Ground Floor (G)"))
                            snap_selected_entity_to_ground(s, log_push);
                        if (ImGui::MenuItem("Attach Child Model 3D"))
                            (void)attach_empty_character_model_child(s, i, false, log_push);
                        if (ImGui::MenuItem("Attach Child Sprite 2D"))
                            (void)attach_empty_character_model_child(s, i, true, log_push);
                        if (ImGui::MenuItem("Assign Mesh from OS Disk..."))
                        {
                            std::string picked;
                            if (open_native_os_file_dialog("3D Mesh / glTF Files (*.obj;*.gltf;*.glb;*.mesh)\0*.obj;*.gltf;*.glb;*.mesh\0All Files (*.*)\0*.*\0", picked, "Select 3D Mesh to Assign"))
                            {
                                (void)import_or_assign_os_file(s, picked, true, log_push);
                            }
                        }
                        if (ImGui::MenuItem("Assign Texture from OS Disk..."))
                        {
                            std::string picked;
                            if (open_native_os_file_dialog("Texture Files (*.png;*.jpg;*.jpeg;*.tex;*.bmp)\0*.png;*.jpg;*.jpeg;*.tex;*.bmp\0All Files (*.*)\0*.*\0", picked, "Select Texture to Assign"))
                            {
                                (void)import_or_assign_os_file(s, picked, true, log_push);
                            }
                        }
                        if (ImGui::BeginMenu("+ Add Modifier"))
                        {
                            for (const auto& mc : kModifierCatalog)
                            {
                                if (mc.category != ModifierCategory::kEnvironment && ImGui::MenuItem(mc.title))
                                {
                                    if (auto mod = instantiate_modifier_by_class(mc.class_id))
                                    {
                                        ms.add_or_replace(std::move(mod));
                                        log_push(std::string("[modifier] Added ") + mc.class_id + " to '" + ent.name + "'");
                                    }
                                }
                            }
                            ImGui::EndMenu();
                        }
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
                    if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                    {
                        s.entities[static_cast<std::size_t>(s.selected)].locked = false;
                    }
                    delete_selected(s, log_push);
                }

                // Scene Lights in the Outliner (Visual Light Icon + Clean Name + [X] + Rename + Context)
                ImGui::SeparatorText("Scene Lights");
                if (ImGui::SmallButton("+Sun"))
                    (void)spawn_studio_light_by_type(s, 0, log_push);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Directional Sun Light (5800K)");
                ImGui::SameLine();
                if (ImGui::SmallButton("+Point"))
                    (void)spawn_studio_light_by_type(s, 1, log_push);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Point Omni Light (3200K)");
                ImGui::SameLine();
                if (ImGui::SmallButton("+Spot"))
                    (void)spawn_studio_light_by_type(s, 2, log_push);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spot Cone Light (4200K)");
                ImGui::SameLine();
                if (ImGui::SmallButton("+Area"))
                    (void)spawn_studio_light_by_type(s, 3, log_push);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rect Area Softbox (5600K)");
                ImGui::SameLine();
                if (ImGui::SmallButton("+Fill"))
                    (void)spawn_studio_light_by_type(s, 4, log_push);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ambient Sky Fill (6500K)");
                ImGui::SameLine();
                if (ImGui::SmallButton("+Rig"))
                    (void)spawn_studio_light_by_type(s, 5, log_push);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("3-Point Studio Rig (Key + Fill + Rim)");

                int deferred_del_light_idx = -1;
                int deferred_dup_light_idx = -1;
                for (std::size_t li = 0; li < s.lights.size(); ++li)
                {
                    // Eye toggle
                    const std::string lvis_btn = "##lvis_" + std::to_string(li);
                    if (ImGui::IconButton(lvis_btn.c_str(),
                                          s.lights[li].enabled ? cd::ui::chroma::IconKind::kEyeOpen : cd::ui::chroma::IconKind::kEyeClosed,
                                          s.lights[li].enabled ? IM_COL32(255, 224, 96, 255) : IM_COL32(150, 110, 110, 185),
                                          false, ImVec2(20.0F, 18.0F)))
                    {
                        s.lights[li].enabled = !s.lights[li].enabled;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Light: %s", s.lights[li].enabled ? "Enabled" : "Disabled");
                    ImGui::SameLine();

                    // Red [X] button
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.35F, 0.35F, 1.0F));
                    const std::string ldel_btn = "[X]##ldel_" + std::to_string(li);
                    if (ImGui::SmallButton(ldel_btn.c_str()))
                    {
                        deferred_del_light_idx = static_cast<int>(li);
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Delete Light '%s'", s.lights[li].name.c_str());
                    ImGui::SameLine();

                    if (w.inline_rename_light_idx == static_cast<int>(li))
                    {
                        ImGui::PushItemWidth(130.0F);
                        (void)ImGui::InputText("##inline_rename_lgt", w.inline_rename_buf, sizeof(w.inline_rename_buf));
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("OK##ok_rlgt"))
                        {
                            if (w.inline_rename_buf[0] != '\0')
                            {
                                s.lights[li].name = w.inline_rename_buf;
                                log_push("[light] Renamed light to '" + s.lights[li].name + "'");
                            }
                            w.inline_rename_light_idx = -1;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##cnc_rlgt"))
                        {
                            w.inline_rename_light_idx = -1;
                        }
                    }
                    else
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
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            w.inline_rename_light_idx = static_cast<int>(li);
                            std::snprintf(w.inline_rename_buf, sizeof(w.inline_rename_buf), "%s", s.lights[li].name.c_str());
                        }
                    }

                    if (ImGui::BeginPopupContextItem())
                    {
                        s.selected      = static_cast<int>(li);
                        s.selected_kind = SelKind::kLight;
                        ImGui::TextColored(ImVec4(1.0F, 0.88F, 0.35F, 1.0F), "%s", s.lights[li].name.c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Rename Light (Double-Click)"))
                        {
                            w.inline_rename_light_idx = static_cast<int>(li);
                            std::snprintf(w.inline_rename_buf, sizeof(w.inline_rename_buf), "%s", s.lights[li].name.c_str());
                        }
                        if (ImGui::MenuItem(s.lights[li].enabled ? "Turn Off Light" : "Turn On Light"))
                        {
                            s.lights[li].enabled = !s.lights[li].enabled;
                        }
                        if (ImGui::MenuItem("Duplicate Light"))
                        {
                            deferred_dup_light_idx = static_cast<int>(li);
                        }
                        if (ImGui::MenuItem("Delete Light [X]"))
                        {
                            deferred_del_light_idx = static_cast<int>(li);
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Set Warm Sunlight (2700K)"))
                        {
                            s.lights[li].kelvin = 2700.0F;
                            s.lights[li].light.color = cd::light::cct_to_linear_rgb(2700.0F);
                            s.lights[li].light.intensity = 12000.0F;
                        }
                        if (ImGui::MenuItem("Set Cool Sky Fill (6500K)"))
                        {
                            s.lights[li].kelvin = 6500.0F;
                            s.lights[li].light.color = cd::light::cct_to_linear_rgb(6500.0F);
                            s.lights[li].light.intensity = 4500.0F;
                        }
                        ImGui::EndPopup();
                    }
                }

                if (deferred_dup_light_idx >= 0 && std::cmp_less(deferred_dup_light_idx, s.lights.size()))
                {
                    auto cpy = s.lights[static_cast<std::size_t>(deferred_dup_light_idx)];
                    cpy.name += "_Copy";
                    cpy.light.position.x += 1.0F;
                    s.lights.push_back(cpy);
                    s.selected = static_cast<int>(s.lights.size()) - 1;
                    s.selected_kind = SelKind::kLight;
                    log_push("[light] Duplicated light: " + cpy.name);
                }

                if (deferred_del_light_idx >= 0)
                {
                    delete_studio_light_at(s, static_cast<std::size_t>(deferred_del_light_idx), log_push);
                }

                // Scene Empty Space catch-area for right-click context menu
                const float avail_y = std::max(60.0F, ImGui::GetContentRegionAvail().y);
                (void)ImGui::Selectable("##scene_empty_space_area", false, 0, ImVec2(-1.0F, avail_y));
                if (ImGui::BeginPopupContextItem("scene_empty_ctx_popup"))
                {
                    ImGui::TextColored(ImVec4(0.45F, 0.95F, 0.65F, 1.0F), "Create in Scene");
                    ImGui::Separator();
                    if (ImGui::MenuItem("+ Ground Floor (Static Collider)"))
                        (void)spawn_ground_floor_entity(s, log_push);
                    if (ImGui::MenuItem("+ Empty 3D Character"))
                        (void)spawn_empty_character_vessel(s, false, log_push);
                    if (ImGui::MenuItem("+ Empty 2D Character"))
                        (void)spawn_empty_character_vessel(s, true, log_push);
                    if (ImGui::MenuItem("+ Camera 3D"))
                        (void)spawn_scene_camera_entity(s, false, log_push);
                    if (ImGui::MenuItem("+ Volumetric Clouds"))
                        (void)spawn_volumetric_clouds_entity(s, log_push);
                    if (ImGui::MenuItem("+ Volumetric Fog"))
                        (void)spawn_volumetric_fog_entity(s, log_push);
                    ImGui::Separator();
                    if (ImGui::MenuItem("+ Dynamic Cube"))
                    {
                        const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                        (void)spawn_entity_with_modifiers(s, "Dynamic_Cube", { pt.x, 0.0F, pt.z }, log_push);
                    }
                    if (ImGui::MenuItem("+ Dynamic Sphere"))
                    {
                        const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                        (void)spawn_entity_with_modifiers(s, "Dynamic_Sphere", { pt.x, 0.0F, pt.z }, log_push);
                    }
                    ImGui::Separator();
                    if (ImGui::BeginMenu("+ Studio Lights"))
                    {
                        if (ImGui::MenuItem("+ Directional Sun Light (5800K)"))
                            (void)spawn_studio_light_by_type(s, 0, log_push);
                        if (ImGui::MenuItem("+ Point Omni Light (3200K)"))
                            (void)spawn_studio_light_by_type(s, 1, log_push);
                        if (ImGui::MenuItem("+ Spot Cone Light (4200K)"))
                            (void)spawn_studio_light_by_type(s, 2, log_push);
                        if (ImGui::MenuItem("+ Rect Area Softbox (5600K)"))
                            (void)spawn_studio_light_by_type(s, 3, log_push);
                        if (ImGui::MenuItem("+ Ambient Sky Fill (6500K)"))
                            (void)spawn_studio_light_by_type(s, 4, log_push);
                        ImGui::Separator();
                        if (ImGui::MenuItem("+ 3-Point Studio Rig (Key+Fill+Rim)"))
                            (void)spawn_studio_light_by_type(s, 5, log_push);
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import & Assign OS File..."))
                    {
                        std::string picked;
                        if (open_native_os_file_dialog("Supported Files (*.obj;*.gltf;*.glb;*.png;*.jpg;*.cdscene.json)\0*.obj;*.gltf;*.glb;*.png;*.jpg;*.cdscene.json\0All Files (*.*)\0*.*\0", picked, "Import OS File"))
                        {
                            (void)import_or_assign_os_file(s, picked, false, log_push);
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // -----------------------------------------------------------------
            // TAB 2: Create (3D Objects, Prefabs & Modifier Classes)
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Create"))
            {
                ImGui::BeginChild("toolbox_scroll_child", ImVec2(0.0F, 0.0F), false);

                ImGui::SeparatorText("0. Characters, Cameras & Environment");
                if (ImGui::IconButton("+ Ground Floor (Static Collider)##cr_flr", cd::ui::chroma::IconKind::kCollider,
                                      IM_COL32(130, 175, 220, 255), false, ImVec2(-1.0F, 22.0F)))
                {
                    (void)spawn_ground_floor_entity(s, log_push);
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    const char* cid = "Ground_Floor";
                    ImGui::SetDragDropPayload("CD_STUDIO_SPAWN", cid, std::strlen(cid) + 1);
                    w.is_dragging_spawn = true;
                    w.drag_spawn_id     = cid;
                    ImGui::Text("Drop Ground Floor onto 3D Viewport");
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IconButton("+ Empty 3D Character Vessel##cr_3dchar", cd::ui::chroma::IconKind::kAvatar,
                                      IM_COL32(85, 245, 165, 255), false, ImVec2(-1.0F, 22.0F)))
                {
                    (void)spawn_empty_character_vessel(s, false, log_push);
                }
                if (ImGui::IconButton("+ Empty 2D Character Vessel##cr_2dchar", cd::ui::chroma::IconKind::kAvatar,
                                      IM_COL32(95, 235, 210, 255), false, ImVec2(-1.0F, 22.0F)))
                {
                    (void)spawn_empty_character_vessel(s, true, log_push);
                }
                if (ImGui::IconButton("+ Camera 3D##cr_cam3d", cd::ui::chroma::IconKind::kCamera,
                                      IM_COL32(200, 155, 255, 255), false, ImVec2(-1.0F, 22.0F)))
                {
                    (void)spawn_scene_camera_entity(s, false, log_push);
                }
                if (ImGui::IconButton("+ Volumetric Clouds##cr_cld", cd::ui::chroma::IconKind::kWorld,
                                      IM_COL32(185, 215, 255, 255), false, ImVec2(-1.0F, 22.0F)))
                {
                    (void)spawn_volumetric_clouds_entity(s, log_push);
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    const char* cid = "Volumetric_Clouds";
                    ImGui::SetDragDropPayload("CD_STUDIO_SPAWN", cid, std::strlen(cid) + 1);
                    w.is_dragging_spawn = true;
                    w.drag_spawn_id     = cid;
                    ImGui::Text("Drop Volumetric Clouds onto 3D Viewport");
                    ImGui::EndDragDropSource();
                }

                if (ImGui::IconButton("+ Volumetric Fog##cr_fog", cd::ui::chroma::IconKind::kWorld,
                                      IM_COL32(200, 225, 245, 255), false, ImVec2(-1.0F, 22.0F)))
                {
                    (void)spawn_volumetric_fog_entity(s, log_push);
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    const char* cid = "Volumetric_Fog";
                    ImGui::SetDragDropPayload("CD_STUDIO_SPAWN", cid, std::strlen(cid) + 1);
                    w.is_dragging_spawn = true;
                    w.drag_spawn_id     = cid;
                    ImGui::Text("Drop Volumetric Fog onto 3D Viewport");
                    ImGui::EndDragDropSource();
                }

                ImGui::SeparatorText("1. Drag & Drop 3D Objects & Prefabs");
                struct SpawnCard
                {
                    const char*              id;
                    const char*              short_label;
                    const char*              tip;
                    cd::ui::chroma::IconKind icon;
                    ImU32                    col;
                };
                constexpr std::array<SpawnCard, 21> kSpawnCards {{
                    { "Ground_Floor",           "Ground Floor",   "Static Ground Floor Collider (Plane 50x50m)",   cd::ui::chroma::IconKind::kCollider, IM_COL32(130, 175, 220, 255) },
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
                    { "Volumetric_Clouds",      "Vol. Clouds",    "Volumetric Atmospheric Clouds Entity",          cd::ui::chroma::IconKind::kWorld,    IM_COL32(185, 215, 255, 255) },
                    { "Volumetric_Fog",         "Vol. Fog",       "Volumetric Height & Distance Fog Entity",       cd::ui::chroma::IconKind::kWorld,    IM_COL32(200, 225, 245, 255) },
                    { "Character_CesiumMan",    "Player Avatar",  "Skinned Animated Character + WASD Controller",  cd::ui::chroma::IconKind::kAvatar,   IM_COL32(85, 245, 165, 255) },
                    { "World_Sponza",           "Sponza Atrium",  "Classic Sponza Atrium World (glTF)",            cd::ui::chroma::IconKind::kWorld,    IM_COL32(255, 210, 115, 255) },
                }};

                if (ImGui::BeginTable("spawn_grid_2col", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    for (const auto& card : kSpawnCards)
                    {
                        ImGui::TableNextColumn();
                        const std::string btn_id = std::string(card.short_label) + "##sp_" + card.id;
                        if (ImGui::IconButton(btn_id.c_str(), card.icon, card.col, false, ImVec2(-1.0F, 20.0F)))
                        {
                            if (std::strcmp(card.id, "Ground_Floor") == 0)
                            {
                                (void)spawn_ground_floor_entity(s, log_push);
                            }
                            else if (std::strcmp(card.id, "Volumetric_Clouds") == 0)
                            {
                                (void)spawn_volumetric_clouds_entity(s, log_push);
                            }
                            else if (std::strcmp(card.id, "Volumetric_Fog") == 0)
                            {
                                (void)spawn_volumetric_fog_entity(s, log_push);
                            }
                            else
                            {
                                const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                                (void)spawn_entity_with_modifiers(s, card.id, { pt.x, 0.0F, pt.z }, log_push);
                            }
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

                ImGui::SeparatorText("Studio Lights & Lighting Rigs");
                if (ImGui::BeginTable("lights_spawn_grid_2col", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ Sun Light##cr_l_sun", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(255, 220, 90, 255), false, ImVec2(-1.0F, 20.0F)))
                        (void)spawn_studio_light_by_type(s, 0, log_push);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Directional Sun Light (5800K, 5.5 lux)");

                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ Point Omni##cr_l_pt", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(255, 200, 80, 255), false, ImVec2(-1.0F, 20.0F)))
                        (void)spawn_studio_light_by_type(s, 1, log_push);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Point Omni Light (3200K, 2800 lm)");

                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ Spot Cone##cr_l_sp", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(255, 175, 70, 255), false, ImVec2(-1.0F, 20.0F)))
                        (void)spawn_studio_light_by_type(s, 2, log_push);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spot Cone Light (4200K, 5500 lm)");

                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ Area Softbox##cr_l_area", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(215, 235, 255, 255), false, ImVec2(-1.0F, 20.0F)))
                        (void)spawn_studio_light_by_type(s, 3, log_push);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rect Area Softbox (5600K, 3800 lm)");

                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ Sky Fill##cr_l_sky", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(180, 215, 255, 255), false, ImVec2(-1.0F, 20.0F)))
                        (void)spawn_studio_light_by_type(s, 4, log_push);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ambient Sky Fill (6500K, 1600 lm)");

                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ 3-Point Rig##cr_l_rig", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(255, 140, 220, 255), false, ImVec2(-1.0F, 20.0F)))
                        (void)spawn_studio_light_by_type(s, 5, log_push);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("3-Point Studio Rig (Key + Fill + Rim)");

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

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // =========================================================================
    // PANEL 3 (Right): Full Transform (Pos + Euler Deg + Scale) & Modifier Stack
    // =========================================================================
    if (ImGui::Begin("Inspector", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
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
                        ent.locked = false;
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

                    if (ImGui::SmallButton("Browse Model OS Disk...##insp_br_mdl"))
                    {
                        std::string picked;
                        if (open_native_os_file_dialog("3D Mesh / glTF Files (*.obj;*.gltf;*.glb;*.mesh)\0*.obj;*.gltf;*.glb;*.mesh\0All Files (*.*)\0*.*\0", picked, "Select 3D Mesh to Assign"))
                        {
                            (void)import_or_assign_os_file(s, picked, true, log_push);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Browse Texture OS Disk...##insp_br_tx"))
                    {
                        std::string picked;
                        if (open_native_os_file_dialog("Texture Files (*.png;*.jpg;*.jpeg;*.tex;*.bmp)\0*.png;*.jpg;*.jpeg;*.tex;*.bmp\0All Files (*.*)\0*.*\0", picked, "Select Texture to Assign"))
                        {
                            (void)import_or_assign_os_file(s, picked, true, log_push);
                        }
                    }

                    // ---- Character Vessel & Nested Model Setup (2D & 3D Character Pipeline) ----
                    ImGui::SeparatorText("Character Vessel & Nested Model Setup");
                    if (ImGui::IconButton("+ Attach Empty 3D Model Child##insp_att_3d",
                                          cd::ui::chroma::IconKind::kAvatar,
                                          IM_COL32(85, 245, 165, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)attach_empty_character_model_child(s, static_cast<std::size_t>(s.selected), false, log_push);
                    }
                    if (ImGui::IconButton("+ Attach Empty 2D Sprite Child##insp_att_2d",
                                          cd::ui::chroma::IconKind::kAvatar,
                                          IM_COL32(95, 235, 210, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)attach_empty_character_model_child(s, static_cast<std::size_t>(s.selected), true, log_push);
                    }
                    if (ImGui::IconButton("Assign External Model (.obj/.gltf) from OS Disk...##insp_os_mdl",
                                          cd::ui::chroma::IconKind::kMesh3D,
                                          IM_COL32(110, 205, 255, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        std::string picked;
                        if (open_native_os_file_dialog("3D Mesh / glTF Files (*.obj;*.gltf;*.glb;*.mesh)\0*.obj;*.gltf;*.glb;*.mesh\0All Files (*.*)\0*.*\0", picked, "Select 3D Model from OS Disk"))
                        {
                            (void)import_or_assign_os_file(s, picked, true, log_push);
                        }
                    }
                    if (ImGui::IconButton("Assign External Texture (.png/.jpg) from OS Disk...##insp_os_tex",
                                          cd::ui::chroma::IconKind::kTexture2D,
                                          IM_COL32(255, 215, 110, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        std::string picked;
                        if (open_native_os_file_dialog("Texture Files (*.png;*.jpg;*.jpeg;*.tex;*.bmp)\0*.png;*.jpg;*.jpeg;*.tex;*.bmp\0All Files (*.*)\0*.*\0", picked, "Select Texture from OS Disk"))
                        {
                            (void)import_or_assign_os_file(s, picked, true, log_push);
                        }
                    }

                    // 6-state Animator State Machine Controls
                    auto* anim_mod = w.entity_modifiers[ent.handle.id].template find<AnimatorStateMachineModifier>();
                    if (anim_mod != nullptr)
                    {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.40F, 0.96F, 0.65F, 1.0F),
                                           "Animator State Machine (Active: [%d] %s)",
                                           anim_mod->current_state_idx,
                                           anim_mod->state_name.c_str());
                        ImGui::Checkbox("Auto Transitions (Velocity)##insp_auto_tr", &anim_mod->auto_stateful_transitions);
                        ImGui::SameLine();
                        ImGui::Checkbox("2D Mode##insp_anim_2d", &anim_mod->is_2d_character);

                        for (std::size_t si = 0; si < 6; ++si)
                        {
                            ImGui::PushID(static_cast<int>(si + 5000));
                            const bool is_active_st = (static_cast<int>(si) == anim_mod->current_state_idx);
                            if (is_active_st)
                            {
                                ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.25F, 1.0F), "[ACTIVE]");
                            }
                            else
                            {
                                ImGui::TextDisabled(" [    ] ");
                            }
                            ImGui::SameLine();
                            const std::string st_btn = std::string(AnimatorStateMachineModifier::kStateLabels[si]) + "##st_btn";
                            if (ImGui::SmallButton(st_btn.c_str()))
                            {
                                anim_mod->set_state(static_cast<int>(si));
                                log_push(std::string("[animator] Switched state to ") + anim_mod->state_name);
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Click to activate %s state", AnimatorStateMachineModifier::kStateLabels[si]);

                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(65.0F);
                            (void)ImGui::SliderFloat("##spd", &anim_mod->state_speeds[si], 0.1F, 3.0F, "%.1fx");
                            ImGui::SameLine();
                            ImGui::Checkbox("Loop##lp", &anim_mod->state_loop[si]);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Browse Clip...##br_clp"))
                            {
                                std::string picked;
                                if (open_native_os_file_dialog("Animation Files (*.anim;*.bvh;*.fbx;*.gltf;*.glb)\0*.anim;*.bvh;*.fbx;*.gltf;*.glb\0All Files (*.*)\0*.*\0", picked, "Select Animation Clip"))
                                {
                                    anim_mod->state_clip_paths[si] = picked;
                                    log_push("[animator] Assigned clip: " + picked);
                                }
                            }
                            ImGui::PopID();
                        }
                    }
                    else
                    {
                        if (ImGui::SmallButton("+ Add 6-State Animator State Machine##add_anim_sm"))
                        {
                            auto sm = std::make_unique<AnimatorStateMachineModifier>();
                            sm->is_2d_character = (ent.name.find("2D") != std::string::npos);
                            w.entity_modifiers[ent.handle.id].add_or_replace(std::move(sm));
                            log_push("[modifier] Added AnimatorStateMachineModifier to " + ent.name);
                        }
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
                    const std::size_t sel_li = static_cast<std::size_t>(s.selected);
                    auto& lr = s.lights[sel_li];
                    ImGui::TextColored(ImVec4(1.0F, 0.88F, 0.35F, 1.0F), "Selected Light: %s", lr.name.c_str());
                    ImGui::SameLine();
                    bool del_clicked = false;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.35F, 0.35F, 1.0F));
                    if (ImGui::SmallButton("Delete Light [X]##insp_del_lgt"))
                    {
                        del_clicked = true;
                    }
                    ImGui::PopStyleColor();

                    if (del_clicked)
                    {
                        delete_studio_light_at(s, sel_li, log_push);
                    }
                    else
                    {
                        char name_buf[128] {};
                        std::snprintf(name_buf, sizeof(name_buf), "%s", lr.name.c_str());
                        ImGui::PushItemWidth(180.0F);
                        if (ImGui::InputText("Light Name##insp_l_nm", name_buf, sizeof(name_buf)))
                        {
                            lr.name = name_buf;
                        }
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        ImGui::Checkbox("Enabled##insp_l_en", &lr.enabled);

                        if (lr.enabled)
                        {
                            ImGui::Separator();
                            draw_studio_light_controls(lr, log_push, "_insp");
                        }
                    }
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
                ImGui::SeparatorText("Clustered Scene Lights & Presets");
                if (ImGui::BeginTable("lights_add_grid_2col", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ Directional (Sun)##insp_add_sun", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(255, 220, 90, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)spawn_studio_light_by_type(s, 0, log_push);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distant Sun Light (5800K, 5.5 lux)");

                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ Point (Omni)##insp_add_pt", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(255, 200, 80, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)spawn_studio_light_by_type(s, 1, log_push);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Omnidirectional Point Light (3200K, 2800 lm, 12m)");

                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ Spot (Cone)##insp_add_sp", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(255, 175, 70, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)spawn_studio_light_by_type(s, 2, log_push);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spot Cone Light (4200K, 5500 lm, 16m)");

                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ Rect Area (Softbox)##insp_add_rc", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(215, 235, 255, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)spawn_studio_light_by_type(s, 3, log_push);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rectangular Studio Softbox (5600K, 3800 lm, 2.2x1.2m)");

                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ Sky Fill (6500K)##insp_add_sky", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(180, 215, 255, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)spawn_studio_light_by_type(s, 4, log_push);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Large Radius Ambient Sky Fill (6500K, 1600 lm)");

                    ImGui::TableNextColumn();
                    if (ImGui::IconButton("+ 3-Point Studio Rig##insp_add_rig", cd::ui::chroma::IconKind::kLight,
                                          IM_COL32(255, 140, 220, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)spawn_studio_light_by_type(s, 5, log_push);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("3-Point Studio Rig (Key + Fill + Rim)");

                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Active Lights List");
                int insp_del_li = -1;
                for (std::size_t li = 0; li < s.lights.size(); ++li)
                {
                    auto& lr = s.lights[li];
                    ImGui::PushID(static_cast<int>(li));

                    // Red [X] delete button
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.35F, 0.35F, 1.0F));
                    if (ImGui::SmallButton("[X]##del_l"))
                    {
                        insp_del_li = static_cast<int>(li);
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete Light '%s'", lr.name.c_str());
                    ImGui::SameLine();

                    ImGui::Checkbox("##l_en", &lr.enabled);
                    ImGui::SameLine();

                    const bool is_l_sel = (s.selected_kind == SelKind::kLight && std::cmp_equal(s.selected, li));
                    if (ImGui::Selectable(lr.name.c_str(), is_l_sel))
                    {
                        s.selected      = static_cast<int>(li);
                        s.selected_kind = SelKind::kLight;
                    }

                    if (lr.enabled)
                    {
                        ImGui::Indent(18.0F);
                        draw_studio_light_controls(lr, log_push, ("_tab_" + std::to_string(li)).c_str());
                        ImGui::Unindent(18.0F);
                    }

                    ImGui::PopID();
                }

                if (insp_del_li >= 0)
                {
                    delete_studio_light_at(s, static_cast<std::size_t>(insp_del_li), log_push);
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
    if (ImGui::Begin("History", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (w.focus_bottom_node_catalog_tab)
        {
            cd::ui::chroma::state().tab_map[ImGui::GetID("BottomDockTabBar")] = 1;
            w.focus_bottom_node_catalog_tab = false;
        }
        if (ImGui::BeginTabBar("BottomDockTabBar"))
        {
            if (ImGui::BeginTabItem("Assets & FileSystem"))
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
                    s_cached_entries.reserve(64);

                    if (folder == "showcases")
                    {
                        struct ShowcaseDiskItem
                        {
                            const char*              name;
                            const char*              path;
                            cd::ui::chroma::IconKind icon;
                            ImU32                    color;
                        };
                        constexpr std::array<ShowcaseDiskItem, 10> kDiskProjects {{
                            { "01_PBR_Materials",  "showcases/01_pbr_materials_sss_hair/project.cdproj",
                              cd::ui::chroma::IconKind::kSphere,    IM_COL32(255, 208, 88, 255) },
                            { "02_Realtime_GI",    "showcases/02_realtime_gi_raytracing/project.cdproj",
                              cd::ui::chroma::IconKind::kWorld,     IM_COL32(255, 178, 95, 255) },
                            { "03_Sky_Ocean",      "showcases/03_atmosphere_clouds_ocean_world/project.cdproj",
                              cd::ui::chroma::IconKind::kSparkles,  IM_COL32(105, 215, 255, 255) },
                            { "04_GPU_Geometry",   "showcases/04_gpu_driven_geometry_textures/project.cdproj",
                              cd::ui::chroma::IconKind::kMesh3D,    IM_COL32(95, 195, 255, 255) },
                            { "05_Physics_Sim",    "showcases/05_physics_destruction_fluids/project.cdproj",
                              cd::ui::chroma::IconKind::kPhysics,   IM_COL32(85, 240, 165, 255) },
                            { "06_PostFX_Optics",  "showcases/06_postfx_upscaling_camera/project.cdproj",
                              cd::ui::chroma::IconKind::kCamera,    IM_COL32(205, 155, 255, 255) },
                            { "07_VFX_Particles",  "showcases/07_particles_decals_foliage/project.cdproj",
                              cd::ui::chroma::IconKind::kLight,     IM_COL32(255, 165, 95, 255) },
                            { "08_Proc_Synth",     "showcases/08_procedural_asset_synthesis/project.cdproj",
                              cd::ui::chroma::IconKind::kTorus,     IM_COL32(235, 145, 245, 255) },
                            { "09_Studio_Tools",   "showcases/09_editor_tools_observability/project.cdproj",
                              cd::ui::chroma::IconKind::kUiWidget,  IM_COL32(115, 205, 255, 255) },
                            { "10_Gameplay_Net",   "showcases/10_gameplay_animation_networking/project.cdproj",
                              cd::ui::chroma::IconKind::kAvatar,    IM_COL32(95, 245, 175, 255) },
                        }};
                        for (const auto& sp : kDiskProjects)
                        {
                            s_cached_entries.push_back({
                                sp.name, sp.path, 4, sp.icon, sp.color
                            });
                        }
                        if (std::filesystem::exists("studio_scene.cdscene.json"))
                        {
                            s_cached_entries.push_back({
                                "Saved_Studio_Scene", "studio_scene.cdscene.json", 4,
                                cd::ui::chroma::IconKind::kFileScene,
                                IM_COL32(115, 225, 255, 255)
                            });
                        }
                    }
                    else if (folder == "assets/meshes" || folder == "primitives" || folder == "Primitives (Built-in)")
                    {
                        s_cached_entries.push_back({ "Sphere",       "assets/meshes/Sphere.mesh",               1, cd::ui::chroma::IconKind::kSphere,   IM_COL32(110, 220, 255, 255) });
                        s_cached_entries.push_back({ "Cube",         "assets/meshes/Cube.mesh",                 1, cd::ui::chroma::IconKind::kCube,     IM_COL32(95, 195, 255, 255) });
                        s_cached_entries.push_back({ "Cylinder",     "assets/meshes/Cylinder.mesh",             1, cd::ui::chroma::IconKind::kCylinder, IM_COL32(115, 210, 245, 255) });
                        s_cached_entries.push_back({ "Cone",         "assets/meshes/Cone.mesh",                 1, cd::ui::chroma::IconKind::kCone,     IM_COL32(245, 185, 110, 255) });
                        s_cached_entries.push_back({ "Torus",        "assets/meshes/Torus.mesh",                1, cd::ui::chroma::IconKind::kTorus,    IM_COL32(205, 155, 255, 255) });
                        s_cached_entries.push_back({ "TorusKnot",    "assets/meshes/TorusKnot.mesh",            1, cd::ui::chroma::IconKind::kTorus,    IM_COL32(235, 155, 245, 255) });
                    }
                    else if (folder == "assets/textures")
                    {
                        s_cached_entries.push_back({ "Checker_2K",    "assets/textures/UV_Checkerboard_2K.tex",                2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(95, 235, 175, 255) });
                        s_cached_entries.push_back({ "Gold_24K",      "assets/textures/Gold_Leaf_24K.tex",                     2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(255, 215, 85, 255) });
                        s_cached_entries.push_back({ "Carbon_Fiber",  "assets/textures/Carbon_Fiber_Twill.tex",                2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(150, 185, 225, 255) });
                        s_cached_entries.push_back({ "Marble_White",  "assets/textures/Carrara_Marble.tex",                    2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(220, 230, 245, 255) });
                        s_cached_entries.push_back({ "Oak_Planks",    "assets/textures/Oak_Wood_Planks.tex",                   2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(225, 175, 110, 255) });
                        s_cached_entries.push_back({ "Earth_Albedo",  "assets/textures/Earth_Albedo.png",                      2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(95, 195, 255, 255) });
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
                            if (disk_count >= 128U) break;
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
                            else if (ext == ".cdscene.json" || ext == ".json")
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
                            else if (ext == ".pbr" || ext == ".mat")
                            {
                                s_cached_entries.push_back({ fname, full, 3, cd::ui::chroma::IconKind::kMaterial, IM_COL32(215, 155, 255, 255) });
                                ++disk_count;
                            }
                        }
                    }
                };

                if (s_cached_folder != w.current_content_folder || s_cached_entries.empty())
                {
                    build_disk_entries(w.current_content_folder);
                }

                static char s_custom_disk_path[260] {};
                if (s_custom_disk_path[0] == '\0')
                {
                    std::snprintf(s_custom_disk_path, sizeof(s_custom_disk_path), "%s", w.current_content_folder.c_str());
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
                    std::snprintf(s_custom_disk_path, sizeof(s_custom_disk_path), "%s", w.current_content_folder.c_str());
                    build_disk_entries(w.current_content_folder);
                }
                ImGui::SameLine(0.0F, 4.0F);
                if (ImGui::IconButton("Objects##cb_obj", cd::ui::chroma::IconKind::kMesh3D,
                                      IM_COL32(95, 195, 255, 255), w.current_content_folder == "assets/meshes", ImVec2(68.0F, 20.0F)))
                {
                    w.current_content_folder = "assets/meshes";
                    std::snprintf(s_custom_disk_path, sizeof(s_custom_disk_path), "%s", w.current_content_folder.c_str());
                    build_disk_entries(w.current_content_folder);
                }
                ImGui::SameLine(0.0F, 4.0F);
                if (ImGui::IconButton("Textures##cb_tex", cd::ui::chroma::IconKind::kTexture2D,
                                      IM_COL32(85, 235, 165, 255), w.current_content_folder == "assets/textures", ImVec2(74.0F, 20.0F)))
                {
                    w.current_content_folder = "assets/textures";
                    std::snprintf(s_custom_disk_path, sizeof(s_custom_disk_path), "%s", w.current_content_folder.c_str());
                    build_disk_entries(w.current_content_folder);
                }
                ImGui::SameLine(0.0F, 4.0F);
                if (ImGui::SmallButton("^ Up (..)"))
                {
                    std::error_code upec{};
                    const auto par = std::filesystem::path(w.current_content_folder).parent_path();
                    if (!par.empty() && std::filesystem::exists(par, upec))
                    {
                        w.current_content_folder = par.generic_string();
                        std::snprintf(s_custom_disk_path, sizeof(s_custom_disk_path), "%s", w.current_content_folder.c_str());
                        build_disk_entries(w.current_content_folder);
                    }
                }
                ImGui::SameLine(0.0F, 4.0F);
                ImGui::SetNextItemWidth(150.0F);
                if (ImGui::InputText("##disk_path_input", s_custom_disk_path, sizeof(s_custom_disk_path)))
                {
                    std::error_code inpec{};
                    if (std::filesystem::exists(s_custom_disk_path, inpec))
                    {
                        w.current_content_folder = s_custom_disk_path;
                        build_disk_entries(w.current_content_folder);
                    }
                }
                ImGui::SameLine(0.0F, 4.0F);
                if (ImGui::SmallButton("Go"))
                {
                    std::error_code inpec{};
                    if (std::filesystem::exists(s_custom_disk_path, inpec))
                    {
                        w.current_content_folder = s_custom_disk_path;
                        build_disk_entries(w.current_content_folder);
                    }
                }
                ImGui::SameLine(0.0F, 4.0F);
                if (ImGui::IconButton("Load All -> Context##cb_imp_all", cd::ui::chroma::IconKind::kImport,
                                      IM_COL32(85, 245, 165, 255), false, ImVec2(135.0F, 20.0F)))
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
                ImGui::SameLine(0.0F, 4.0F);
                if (ImGui::IconButton("Browse OS Disk...##cb_browse_os", cd::ui::chroma::IconKind::kFolderOpen,
                                      IM_COL32(115, 215, 255, 255), false, ImVec2(135.0F, 20.0F)))
                {
                    std::string picked;
                    if (open_native_os_file_dialog("Supported Assets (*.obj;*.gltf;*.glb;*.png;*.jpg;*.cdscene.json)\0*.obj;*.gltf;*.glb;*.png;*.jpg;*.cdscene.json\0All Files (*.*)\0*.*\0", picked, "Select File from OS Disk"))
                    {
                        (void)import_or_assign_os_file(s, picked, false, log_push);
                    }
                }
                ImGui::SameLine(0.0F, 4.0F);
                ImGui::SetNextItemWidth(85.0F);
                ImGui::InputTextWithHint("##asset_search", "Search...",
                                         w.content_search_filter, sizeof(w.content_search_filter));
                ImGui::SameLine(0.0F, 4.0F);
                ImGui::TextColored(ImVec4(0.42F, 0.92F, 0.65F, 1.0F),
                                   "Ctx: %zu",
                                   w.loaded_context_assets.size());

                const float total_w = ImGui::GetContentRegionAvail().x;
                const float pane1_w = 220.0F; // 1. Disk Explorer fixed 220px
                const float pane4_w = 235.0F; // 4. Context Preview fixed 235px
                const float mid_w   = std::max(340.0F, total_w - pane1_w - pane4_w - 24.0F);
                const float pane2_w = mid_w * 0.50F; // 2. Disk Files
                const float pane3_w = mid_w * 0.50F; // 3. Active Context

                // ---- PANE 1 (Left): Disk Explorer & Folder Navigation ----
                ImGui::BeginChild("disk_dir_tree_pane", ImVec2(pane1_w, 0.0F), false);
                ImGui::TextColored(ImVec4(0.95F, 0.80F, 0.42F, 1.0F), "1. Disk Explorer");
                struct FolderNode
                {
                    const char* label;
                    const char* path;
                    const char* badge;
                };
                constexpr std::array<FolderNode, 6> kFolders {{
                    { "showcases",       "showcases",                       "Show" },
                    { "assets/meshes",   "assets/meshes",                   "3D" },
                    { "assets/textures", "assets/textures",                 "PBR" },
                    { "samples/Generic", "assets/samples/Generic",          "GLTF" },
                    { "samples/Sponza",  "assets/samples/Sponza",           "Atrium" },
                    { "primitives",      "primitives",                      "Basic" },
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
                        std::snprintf(s_custom_disk_path, sizeof(s_custom_disk_path), "%s", fn.path);
                        build_disk_entries(w.current_content_folder);
                    }
                }

                ImGui::Separator();
                ImGui::TextDisabled("Subdirectories:");
                std::error_code dire_ec{};
                if (std::filesystem::exists(w.current_content_folder, dire_ec) &&
                    std::filesystem::is_directory(w.current_content_folder, dire_ec))
                {
                    std::string pending_sub_dir;
                    for (const auto& dir_entry : std::filesystem::directory_iterator(w.current_content_folder, dire_ec))
                    {
                        if (dir_entry.is_directory(dire_ec))
                        {
                            const std::string sub_name = dir_entry.path().filename().string();
                            const std::string sub_full = dir_entry.path().generic_string();
                            const std::string sub_id   = "[DIR] " + sub_name + "##sub_" + sub_full;
                            if (ImGui::Selectable(sub_id.c_str(), false))
                            {
                                pending_sub_dir = sub_full;
                            }
                        }
                    }
                    if (!pending_sub_dir.empty())
                    {
                        w.current_content_folder = pending_sub_dir;
                        std::snprintf(s_custom_disk_path, sizeof(s_custom_disk_path), "%s", pending_sub_dir.c_str());
                        build_disk_entries(w.current_content_folder);
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine(0.0F, 6.0F);

                // ---- PANE 2 (Center-Left): Disk Files Grid (3D/Texture Previews — Click to Open/Load) ----
                ImGui::BeginChild("disk_files_grid_pane", ImVec2(pane2_w, 0.0F), false);
                ImGui::TextColored(ImVec4(0.52F, 0.86F, 1.0F, 1.0F),
                                   "2. Disk Files (Click -> Context)");
                const int disk_cols = std::max(2, static_cast<int>(pane2_w / 92.0F));
                std::string pending_folder_to_open;
                if (ImGui::BeginTable("disk_tiles_grid_col", disk_cols, ImGuiTableFlags_SizingStretchSame))
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
                                pending_folder_to_open = item.path;
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

                        // Drag onto 3D Viewport directly from Disk Files!
                        if (item.kind == 1) // Mesh
                        {
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                ImGui::SetDragDropPayload("CD_ASSET_MESH", item.path.c_str(), item.path.size() + 1);
                                w.is_dragging_asset_mesh = true;
                                w.drag_asset_mesh_id     = item.path;
                                ImGui::Text("Drop Mesh '%s' into Scene", item.name.c_str());
                                ImGui::EndDragDropSource();
                            }
                        }
                        else if (item.kind == 2) // Texture
                        {
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                ImGui::SetDragDropPayload("CD_ASSET_TEXTURE", item.path.c_str(), item.path.size() + 1);
                                w.is_dragging_asset_texture = true;
                                w.drag_asset_texture_id     = item.path;
                                ImGui::Text("Drop Texture '%s' onto Object", item.name.c_str());
                                ImGui::EndDragDropSource();
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
                                ImGui::SetTooltip("Disk Asset: %s\nPath: %s\nStatus: %s\nClick to Load into Active Project Context (or Drag into Scene)",
                                                  item.name.c_str(), item.path.c_str(),
                                                  in_ctx ? "Loaded in Context (Ready)" : "On Disk (Not in Context)");
                            }
                        }

                        if (ImGui::BeginPopupContextItem())
                        {
                            w.selected_content_asset = item.path;
                            ImGui::TextColored(ImVec4(0.45F, 0.95F, 0.65F, 1.0F), "%s", item.name.c_str());
                            ImGui::Separator();
                            if (item.kind == 1) // Mesh
                            {
                                if (ImGui::MenuItem("Spawn in Scene"))
                                {
                                    const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                                    const int nidx = spawn_entity_with_modifiers(s, item.name.c_str(), { pt.x, 0.0F, pt.z }, log_push);
                                    if (nidx >= 0 && std::cmp_less(nidx, s.entities.size()))
                                    {
                                        s.entities[static_cast<std::size_t>(nidx)].custom_mesh_path = item.path;
                                    }
                                }
                                if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                                {
                                    if (ImGui::MenuItem("Assign to Selected Object"))
                                    {
                                        (void)assign_mesh_asset_to_entity(s, s.selected, item.path, log_push);
                                    }
                                }
                            }
                            else if (item.kind == 2) // Texture
                            {
                                if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                                {
                                    if (ImGui::MenuItem("Assign to Selected Object"))
                                    {
                                        (void)assign_texture_asset_to_entity(s, s.selected, item.path, log_push);
                                    }
                                }
                            }
                            else if (item.kind == 4) // Project / Scene
                            {
                                if (ImGui::MenuItem("Open Project / Scene"))
                                {
                                    (void)load_project_or_scene_from_disk_path(s, item.path, log_push);
                                }
                            }

                            if (in_ctx)
                            {
                                if (ImGui::MenuItem("Remove from Context"))
                                {
                                    for (auto it = w.loaded_context_assets.begin(); it != w.loaded_context_assets.end(); ++it)
                                    {
                                        if (it->path == item.path)
                                        {
                                            w.loaded_context_assets.erase(it);
                                            log_push("[context] Removed asset from context: " + item.name);
                                            break;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                if (ImGui::MenuItem("Load into Context"))
                                {
                                    const std::uint8_t ckind = (item.kind == 1) ? 0U : (item.kind == 2 ? 1U : 2U);
                                    (void)w.load_asset_into_context(item.name, item.path, ckind);
                                }
                            }

                            ImGui::Separator();
                            if (ImGui::MenuItem("Browse & Import from OS Disk..."))
                            {
                                std::string picked;
                                if (open_native_os_file_dialog("Supported Assets (*.obj;*.gltf;*.glb;*.png;*.jpg;*.cdscene.json)\0*.obj;*.gltf;*.glb;*.png;*.jpg;*.cdscene.json\0All Files (*.*)\0*.*\0", picked, "Import from OS Disk"))
                                {
                                    (void)import_or_assign_os_file(s, picked, false, log_push);
                                }
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

                if (!pending_folder_to_open.empty())
                {
                    w.current_content_folder = pending_folder_to_open;
                    std::snprintf(s_custom_disk_path, sizeof(s_custom_disk_path), "%s", pending_folder_to_open.c_str());
                    build_disk_entries(w.current_content_folder);
                }
                ImGui::EndChild();

                ImGui::SameLine(0.0F, 6.0F);

                // ---- PANE 3 (Center-Right): Active Project Context (Visual 3D & Texture Previews) ----
                ImGui::BeginChild("active_context_tiles_pane", ImVec2(pane3_w, 0.0F), false);
                ImGui::TextColored(ImVec4(0.38F, 0.95F, 0.66F, 1.0F),
                                   "3. Active Context (%zu Loaded)",
                                   w.loaded_context_assets.size());
                if (!w.loaded_context_assets.empty())
                {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65F, 0.20F, 0.20F, 0.85F));
                    if (ImGui::SmallButton("Clear All##ctx_clear_all"))
                    {
                        w.loaded_context_assets.clear();
                        w.selected_content_asset.clear();
                        w.selected_context_idx = -1;
                        log_push("[context] Cleared all Active Context assets");
                    }
                    ImGui::PopStyleColor();
                }

                const int ctx_cols = std::max(2, static_cast<int>(pane3_w / 92.0F));
                if (w.loaded_context_assets.empty())
                {
                    ImGui::TextDisabled("Active Context is empty (0 loaded).");
                    ImGui::TextDisabled("Click Disk Files (Pane 2) to load");
                    ImGui::TextDisabled("assets into Context, or load starter pack:");
                    if (ImGui::IconButton("Load Starter Primitives & Materials##ctx_seed_btn",
                                          cd::ui::chroma::IconKind::kImport,
                                          IM_COL32(85, 245, 165, 255), false, ImVec2(-1.0F, 24.0F)))
                    {
                        w.populate_starter_context_assets();
                        w.last_asset_status_msg = "Loaded 8 Starter Assets into Active Context";
                        log_push("[context] " + w.last_asset_status_msg);
                    }
                }
                else
                {
                    int pending_erase_ctx = -1;
                    if (ImGui::BeginTable("context_tiles_grid_col", ctx_cols, ImGuiTableFlags_SizingStretchSame))
                    {
                        int ctx_tile_id = 0;
                        for (std::size_t ci = 0; ci < w.loaded_context_assets.size(); ++ci)
                        {
                            const auto& ca = w.loaded_context_assets[ci];
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
                            }
                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            {
                                w.selected_content_asset = ca.path;
                                if (ca.kind == 0U)
                                {
                                    const int nidx = spawn_entity_with_modifiers(s, "Dynamic_Cube", { 0.0F, 0.0F, 0.0F }, log_push);
                                    if (nidx >= 0 && std::cmp_less(nidx, s.entities.size()))
                                    {
                                        s.entities[static_cast<std::size_t>(nidx)].name = ca.name;
                                        (void)assign_mesh_asset_to_entity(s, nidx, ca.path, log_push);
                                        if (auto* lt = s.scene.local(s.entities[static_cast<std::size_t>(nidx)].handle))
                                            lt->value.position = { 0.0F, 0.0F, 0.0F };
                                        record_entity_home_transform(s, s.entities[static_cast<std::size_t>(nidx)].handle);
                                        s.scene.update_transforms();
                                        s.selected      = nidx;
                                        s.selected_kind = SelKind::kEntity;
                                    }
                                }
                                else
                                {
                                    const int tidx = (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                                        ? s.selected
                                        : (!s.entities.empty() ? static_cast<int>(s.entities.size() - 1U) : -1);
                                    if (tidx >= 0)
                                        (void)assign_texture_asset_to_entity(s, tidx, ca.path, log_push);
                                }
                            }

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
                                ImGui::SetTooltip("Active Context Asset: %s\nDisk Source: %s\n%s\nRight-click for options / remove",
                                                  ca.name.c_str(), ca.path.c_str(),
                                                  ca.kind == 0U
                                                      ? "Drag onto 3D Viewport (or Double-Click to Spawn at 0,0,0)"
                                                      : "Drag onto 3D Object (or Double-Click to Apply to Selected)");
                            }

                            if (ImGui::BeginPopupContextItem())
                            {
                                w.selected_content_asset = ca.path;
                                ImGui::TextColored(ImVec4(0.45F, 0.95F, 0.65F, 1.0F), "%s", ca.name.c_str());
                                ImGui::Separator();
                                if (ca.kind == 0U)
                                {
                                    if (ImGui::MenuItem("Spawn at (0, 0, 0)"))
                                    {
                                        const int nidx = spawn_entity_with_modifiers(s, "Dynamic_Cube", { 0.0F, 0.0F, 0.0F }, log_push);
                                        if (nidx >= 0 && std::cmp_less(nidx, s.entities.size()))
                                        {
                                            s.entities[static_cast<std::size_t>(nidx)].name = ca.name;
                                            (void)assign_mesh_asset_to_entity(s, nidx, ca.path, log_push);
                                        }
                                    }
                                    if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                                    {
                                        if (ImGui::MenuItem("Assign Mesh to Selected Object"))
                                        {
                                            (void)assign_mesh_asset_to_entity(s, s.selected, ca.path, log_push);
                                        }
                                    }
                                }
                                else
                                {
                                    if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                                    {
                                        if (ImGui::MenuItem("Assign Texture to Selected Object"))
                                        {
                                            (void)assign_texture_asset_to_entity(s, s.selected, ca.path, log_push);
                                        }
                                    }
                                }
                                ImGui::Separator();
                                if (ImGui::MenuItem("Remove from Context [X]"))
                                {
                                    pending_erase_ctx = static_cast<int>(ci);
                                }
                                ImGui::EndPopup();
                            }

                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }

                    if (pending_erase_ctx >= 0 && static_cast<std::size_t>(pending_erase_ctx) < w.loaded_context_assets.size())
                    {
                        const std::string rem_name = w.loaded_context_assets[static_cast<std::size_t>(pending_erase_ctx)].name;
                        const std::string rem_path = w.loaded_context_assets[static_cast<std::size_t>(pending_erase_ctx)].path;
                        w.loaded_context_assets.erase(w.loaded_context_assets.begin() + pending_erase_ctx);
                        if (w.selected_content_asset == rem_path)
                            w.selected_content_asset.clear();
                        w.last_asset_status_msg = "Removed '" + rem_name + "' from Active Context";
                        log_push("[context] " + w.last_asset_status_msg);
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine(0.0F, 6.0F);

                // ---- PANE 4 (Right): Live 3D / Texture Preview of Selected Context (or Disk) Object ----
                ImGui::BeginChild("context_live_preview_pane", ImVec2(pane4_w, 0.0F), false);
                ImGui::TextColored(ImVec4(0.62F, 0.88F, 1.0F, 1.0F), "4. Context Preview");

                std::string              prev_name  = "Sphere";
                std::string              prev_path  = "assets/meshes/Sphere.mesh";
                const char*              prev_badge = "CTX 3D MESH";
                cd::ui::chroma::IconKind prev_icon  = cd::ui::chroma::IconKind::kSphere;
                ImU32                    prev_col   = IM_COL32(110, 220, 255, 255);
                std::uint8_t             prev_kind  = 0U; // 0=Mesh, 1=Tex/Mat, 4=Proj
                bool                     found_prev = false;

                if (w.selected_content_asset.empty() && !w.loaded_context_assets.empty())
                {
                    w.selected_content_asset = w.loaded_context_assets.front().path;
                }

                for (const auto& ca : w.loaded_context_assets)
                {
                    if (ca.path == w.selected_content_asset)
                    {
                        prev_name  = ca.name;
                        prev_path  = ca.path;
                        prev_kind  = (ca.kind == 0U) ? 0U : 1U;
                        prev_badge = (ca.kind == 0U) ? "CTX 3D MESH" : "CTX TEXTURE";
                        if (ca.kind == 0U)
                        {
                            if (ca.name.find("Sphere") != std::string::npos)        prev_icon = cd::ui::chroma::IconKind::kSphere;
                            else if (ca.name.find("Cube") != std::string::npos)     prev_icon = cd::ui::chroma::IconKind::kCube;
                            else if (ca.name.find("Cylinder") != std::string::npos) prev_icon = cd::ui::chroma::IconKind::kCylinder;
                            else if (ca.name.find("Cone") != std::string::npos)     prev_icon = cd::ui::chroma::IconKind::kCone;
                            else if (ca.name.find("Torus") != std::string::npos)    prev_icon = cd::ui::chroma::IconKind::kTorus;
                            else if (ca.name.find("Cesium") != std::string::npos)   { prev_icon = cd::ui::chroma::IconKind::kAvatar; prev_col = IM_COL32(85, 245, 165, 255); }
                            else if (ca.name.find("Sponza") != std::string::npos)   { prev_icon = cd::ui::chroma::IconKind::kWorld;  prev_col = IM_COL32(255, 210, 110, 255); }
                            else prev_icon = cd::ui::chroma::IconKind::kMesh3D;
                        }
                        else
                        {
                            prev_icon = cd::ui::chroma::IconKind::kTexture2D;
                            prev_col  = IM_COL32(85, 235, 165, 255);
                        }
                        found_prev = true;
                        break;
                    }
                }
                if (!found_prev)
                {
                    for (const auto& de : s_cached_entries)
                    {
                        if (de.path == w.selected_content_asset || w.selected_content_asset.empty())
                        {
                            prev_name  = de.name;
                            prev_path  = de.path;
                            prev_icon  = de.icon;
                            prev_col   = de.color;
                            prev_kind  = (de.kind == 1) ? 0U : ((de.kind == 4) ? 4U : 1U);
                            prev_badge = (de.kind == 4) ? "DISK PROJECT" : ((de.kind == 1) ? "DISK 3D MESH" : "DISK TEXTURE");
                            found_prev = true;
                            break;
                        }
                    }
                }

                ImGui::ContextAssetPreview3D("##ctx_live_preview_3d",
                                             prev_icon,
                                             prev_name.c_str(),
                                             prev_path.c_str(),
                                             prev_badge,
                                             ImVec2(215.0F, 96.0F),
                                             prev_col);

                if (prev_kind == 4U)
                {
                    if (ImGui::IconButton("Open Project##prev_act", cd::ui::chroma::IconKind::kFileProject,
                                          IM_COL32(255, 200, 85, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)load_project_or_scene_from_disk_path(s, prev_path, log_push);
                    }
                }
                else if (prev_kind == 0U)
                {
                    if (ImGui::IconButton("Spawn to Stage (0,0,0)##prev_act", cd::ui::chroma::IconKind::kPlus,
                                          IM_COL32(85, 245, 165, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)w.load_asset_into_context(prev_name, prev_path, 0U);
                        const int nidx = spawn_entity_with_modifiers(s, "Dynamic_Cube", { 0.0F, 0.0F, 0.0F }, log_push);
                        if (nidx >= 0 && std::cmp_less(nidx, s.entities.size()))
                        {
                            s.entities[static_cast<std::size_t>(nidx)].name = prev_name;
                            (void)assign_mesh_asset_to_entity(s, nidx, prev_path, log_push);
                            if (auto* lt = s.scene.local(s.entities[static_cast<std::size_t>(nidx)].handle))
                                lt->value.position = { 0.0F, 0.0F, 0.0F };
                            record_entity_home_transform(s, s.entities[static_cast<std::size_t>(nidx)].handle);
                            s.scene.update_transforms();
                            s.selected      = nidx;
                            s.selected_kind = SelKind::kEntity;
                        }
                    }
                }
                else
                {
                    if (ImGui::IconButton("Apply Texture##prev_act", cd::ui::chroma::IconKind::kTexture2D,
                                          IM_COL32(85, 235, 165, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        (void)w.load_asset_into_context(prev_name, prev_path, 1U);
                        const int tidx = (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                            ? s.selected
                            : (!s.entities.empty() ? static_cast<int>(s.entities.size() - 1U) : -1);
                        if (tidx >= 0)
                            (void)assign_texture_asset_to_entity(s, tidx, prev_path, log_push);
                    }
                }

                // Delete / Remove from Context button in Preview pane
                bool is_in_context = false;
                for (const auto& ca : w.loaded_context_assets)
                {
                    if (ca.path == prev_path) { is_in_context = true; break; }
                }
                if (is_in_context)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70F, 0.22F, 0.22F, 0.90F));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85F, 0.28F, 0.28F, 1.0F));
                    if (ImGui::IconButton("Remove from Context [X]##prev_rm", cd::ui::chroma::IconKind::kTrash,
                                          IM_COL32(255, 180, 180, 255), false, ImVec2(-1.0F, 22.0F)))
                    {
                        for (auto it = w.loaded_context_assets.begin(); it != w.loaded_context_assets.end(); ++it)
                        {
                            if (it->path == prev_path)
                            {
                                const std::string rm_name = it->name;
                                w.loaded_context_assets.erase(it);
                                if (w.selected_content_asset == prev_path)
                                    w.selected_content_asset.clear();
                                w.last_asset_status_msg = "Removed '" + rm_name + "' from Active Context";
                                log_push("[context] " + w.last_asset_status_msg);
                                break;
                            }
                        }
                    }
                    ImGui::PopStyleColor(2);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Node & Construct Library"))
            {
                draw_universal_250_node_catalog_panel(s, log_push);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Console & Profiler"))
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
                                   "Dynamic Physics 2D+3D (33 Mod): 2D=%u | 3D=%u | Ragdolls=%u (ZMP=%.2fm) | Pacejka=%u | Fracture=%u | XPBD=%u",
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
                        log_push("[physics] Applied ballistic impulse to Active Ragdoll (Capture Point step active)");
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

                // Integrated GPU / Frame Profiler Telemetry
                const bool bloom_active    = (s.fx.bloom_post > 0.001F);
                const bool velocity_active = (s.fx.taa_amount > 0.001F || s.fx.motion_blur > 0.001F);
                ImGui::TextColored(ImVec4(0.40F, 0.98F, 0.62F, 1.0F),
                                   "GPU / Frame: Smoothed: %.0f FPS (%.2f ms) | Peak: %.0f FPS | Entities: %zu | Lights: %zu | Pass: Bloom [%s] Vel [%s]",
                                   static_cast<double>(w.smoothed_fps),
                                   static_cast<double>(w.smoothed_frame_ms),
                                   static_cast<double>(w.peak_fps),
                                   s.entities.size(),
                                   s.lights.size(),
                                   bloom_active    ? "ON" : "OFF",
                                   velocity_active ? "ON" : "OFF");

                ImGui::Separator();
                ImGui::BeginChild("studio_log_scroll", ImVec2(0, 0), false);
                for (const auto& line : s.log)
                {
                    ImGui::TextUnformatted(line.c_str());
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace cd::studio
