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

    if (!ImGui::Begin("ProjectManagerMainWindow", nullptr, win_flags))
    {
        ImGui::End();
        return;
    }

    // -------------------------------------------------------------------------
    // Top Header Row: Logo/Title (Left), Tab Buttons (Center), Settings/Theme (Right)
    // -------------------------------------------------------------------------
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0F, 6.0F));
    ImGui::TextColored(ImVec4(0.35F, 0.85F, 1.0F, 1.0F), "CHROMODYNAMIC");
    ImGui::SameLine();
    ImGui::TextDisabled("— Project Manager");

    ImGui::SameLine(sw * 0.5F - 130.0F);
    const bool is_proj_tab = (w.project_manager_tab == 0);
    if (is_proj_tab) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.48F, 0.80F, 1.0F));
    if (ImGui::Button("  Projects  ", ImVec2(110.0F, 26.0F)))
    {
        w.project_manager_tab = 0;
    }
    if (is_proj_tab) ImGui::PopStyleColor();

    ImGui::SameLine();
    const bool is_tpl_tab = (w.project_manager_tab == 1);
    if (is_tpl_tab) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.48F, 0.80F, 1.0F));
    if (ImGui::Button("Templates & Showcases", ImVec2(165.0F, 26.0F)))
    {
        w.project_manager_tab = 1;
    }
    if (is_tpl_tab) ImGui::PopStyleColor();

    ImGui::SameLine(sw - 140.0F);
    if (ImGui::Button("Settings / Theme", ImVec2(125.0F, 26.0F)))
    {
        ImGui::OpenPopup("pm_theme_popup");
    }
    if (ImGui::BeginPopup("pm_theme_popup"))
    {
        ImGui::TextDisabled("Select Studio Theme:");
        ImGui::Separator();
        if (ImGui::MenuItem("Chroma Dark (Default)")) ImGui::ApplyTheme(0);
        if (ImGui::MenuItem("Modern Slate"))          ImGui::ApplyTheme(1);
        if (ImGui::MenuItem("High Contrast"))         ImGui::ApplyTheme(2);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    ImGui::Separator();

    // -------------------------------------------------------------------------
    // Toolbar & Search Row: [+ Create] [Import] [Scan] | Filter Projects... | Sort: Last Edited
    // -------------------------------------------------------------------------
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0F, 4.0F));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.58F, 0.38F, 1.0F));
    if (ImGui::Button("+ Create", ImVec2(78.0F, 24.0F)))
    {
        w.pm_creating_new = !w.pm_creating_new;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    if (ImGui::Button("Import", ImVec2(68.0F, 24.0F)))
    {
        w.hud_toast_message = "Select a .cdproj or .cdscene.json file from disk";
        w.hud_toast_timer   = 3.0F;
    }
    ImGui::SameLine();

    if (ImGui::Button("Scan", ImVec2(60.0F, 24.0F)))
    {
        w.hud_toast_message = "Scanned workspace projects directory";
        w.hud_toast_timer   = 2.0F;
    }
    ImGui::SameLine(240.0F);

    ImGui::SetNextItemWidth(std::max(180.0F, sw - 560.0F));
    ImGui::InputTextWithHint("##pm_filter", "Filter Projects...", w.pm_filter_buf, sizeof(w.pm_filter_buf));

    ImGui::SameLine(sw - 290.0F);
    ImGui::TextDisabled("Sort:");
    ImGui::SameLine();
    ImGui::Text("Last Edited v");

    ImGui::PopStyleVar();

    // Optional New Project inline bar
    if (w.pm_creating_new)
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.40F, 0.90F, 0.60F, 1.0F), "New Project:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0F);
        ImGui::InputText("##pm_new_proj_name", w.pm_new_project_buf, sizeof(w.pm_new_project_buf));
        ImGui::SameLine();
        if (ImGui::Button("Create & Edit", ImVec2(110.0F, 24.0F)))
        {
            reset_to_clean_empty_project(s, log_push);
            w.active_project_name = w.pm_new_project_buf;
            w.show_project_manager = false;
            w.pm_creating_new = false;
            log_push("[project] Created new project: " + w.active_project_name);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(70.0F, 24.0F)))
        {
            w.pm_creating_new = false;
        }
    }

    ImGui::Separator();

    // -------------------------------------------------------------------------
    // Project item catalog
    // -------------------------------------------------------------------------
    struct PMItem
    {
        std::string              name;
        std::string              path;
        std::string              version;
        std::string              date;
        cd::ui::chroma::IconKind icon;
        int                      action_type; // 0 = empty, 1 = project hope, 2 = scene file, 3 = showcase
        std::size_t              showcase_idx;
        bool                     favorite;
    };

    static std::array<bool, 32> s_pm_favorites {};

    std::vector<PMItem> items;
    items.reserve(24);

    if (w.project_manager_tab == 0)
    {
        items.push_back({
            "Empty Project (New 3D Stage)",
            "c:/UserFiles/Project/CHROMODYNAMIC_ENGINE/workspace/empty_project",
            "1.0",
            "2026-10-18 22:00",
            cd::ui::chroma::IconKind::kFileScene,
            0, 0, s_pm_favorites[0]
        });

        items.push_back({
            "Project Hope",
            "c:/UserFiles/Project/CHROMODYNAMIC_ENGINE/workspace/project_hope",
            "1.0",
            "2026-10-18 22:00",
            cd::ui::chroma::IconKind::kWorld,
            1, 0, s_pm_favorites[1]
        });

        if (std::filesystem::exists("studio_scene.cdscene.json") ||
            std::filesystem::exists("chromodynamic_studio_scene.cdscene.json"))
        {
            items.push_back({
                "Saved Studio Workspace",
                "c:/UserFiles/Project/CHROMODYNAMIC_ENGINE/studio_scene.cdscene.json",
                "1.0",
                "2026-10-18 21:30",
                cd::ui::chroma::IconKind::kFileScene,
                2, 0, s_pm_favorites[2]
            });
        }

        constexpr std::array<const char*, 12> kShowcaseCleanTitles {{
            "PBR Materials & Surface Scatter",
            "Realtime GI & Raytracing Lab",
            "Atmosphere, Clouds & Ocean World",
            "GPU Geometry & Textures",
            "Physics Destruction & Fluids",
            "PostFX Optics & Camera Pipeline",
            "VFX Particles & Foliage",
            "Procedural Asset Synthesis",
            "Studio Tools & Observability",
            "Gameplay, Animation & Networking",
            "Universal Node Constructs",
            "Next-Gen SOTA Engine Showcase"
        }};

        for (std::size_t i = 0; i < kShowcaseCatalog.size(); ++i)
        {
            const auto& sc = kShowcaseCatalog[i];
            const char* title = (i < kShowcaseCleanTitles.size()) ? kShowcaseCleanTitles[i] : sc.title;
            const std::size_t fav_idx = 3 + i;
            const bool fav = (fav_idx < s_pm_favorites.size()) ? s_pm_favorites[fav_idx] : false;
            items.push_back({
                title,
                std::string("c:/UserFiles/Project/CHROMODYNAMIC_ENGINE/") + sc.cdproj_path,
                "1.0",
                "2026-10-18 20:00",
                cd::ui::chroma::IconKind::kWorld,
                3, i, fav
            });
        }
    }
    else
    {
        // Templates tab
        items.push_back({
            "Clean 3D Stage (Blank Canvas)",
            "templates/clean_3d_stage",
            "1.0", "Template",
            cd::ui::chroma::IconKind::kFileScene,
            0, 0, false
        });
        items.push_back({
            "Third-Person Action Arena",
            "templates/action_arena",
            "1.0", "Template",
            cd::ui::chroma::IconKind::kAvatar,
            1, 0, false
        });
        items.push_back({
            "Physics Simulation Playground",
            "templates/physics_playground",
            "1.0", "Template",
            cd::ui::chroma::IconKind::kPhysics,
            3, 4, false
        });
        items.push_back({
            "Atmospheric Ocean & Sky",
            "templates/outdoor_ocean_sky",
            "1.0", "Template",
            cd::ui::chroma::IconKind::kSparkles,
            3, 2, false
        });
    }

    // Filter items
    const std::string filter_str(w.pm_filter_buf);
    std::vector<std::size_t> visible_indices;
    visible_indices.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (filter_str.empty())
        {
            visible_indices.push_back(i);
        }
        else
        {
            auto to_lower = [](std::string str) {
                for (char& c : str) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return str;
            };
            const std::string needle = to_lower(filter_str);
            if (to_lower(items[i].name).find(needle) != std::string::npos ||
                to_lower(items[i].path).find(needle) != std::string::npos)
            {
                visible_indices.push_back(i);
            }
        }
    }

    if (w.selected_pm_project_idx >= static_cast<int>(visible_indices.size()))
    {
        w.selected_pm_project_idx = 0;
    }

    auto load_project_item = [&](const PMItem& itm) {
        if (itm.action_type == 0)
        {
            reset_to_clean_empty_project(s, log_push);
            w.active_project_name = itm.name;
        }
        else if (itm.action_type == 1)
        {
            reset_to_clean_empty_project(s, log_push);
            w.active_project_name = "Project Hope";
            load_studio_interactive_template(s, 0, log_push);
        }
        else if (itm.action_type == 2)
        {
            const char* path = std::filesystem::exists("studio_scene.cdscene.json")
                ? "studio_scene.cdscene.json" : "chromodynamic_studio_scene.cdscene.json";
            (void)load_full_studio_scene(s, path, log_push);
            w.active_project_name = "Saved Studio Workspace";
        }
        else if (itm.action_type == 3)
        {
            load_showcase_project_by_index(s, itm.showcase_idx, log_push);
        }
        w.show_project_manager = false;
    };

    // -------------------------------------------------------------------------
    // Main Projects List (Left-Center) + Actions Column (Right)
    // -------------------------------------------------------------------------
    const float list_w   = std::max(200.0F, sw - 215.0F);
    const float remain_h = std::max(120.0F, sh - 145.0F);

    ImGui::BeginChild("pm_project_list", ImVec2(list_w, remain_h), true);
    for (std::size_t vi = 0; vi < visible_indices.size(); ++vi)
    {
        const std::size_t orig_idx = visible_indices[vi];
        const auto& itm = items[orig_idx];
        const bool is_sel = (w.selected_pm_project_idx == static_cast<int>(vi));

        ImGui::PushID(static_cast<int>(vi));

        if (is_sel)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18F, 0.32F, 0.48F, 0.85F));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12F, 0.14F, 0.18F, 0.50F));
        }

        const ImVec2 row_sz(ImGui::GetContentRegionAvail().x, 48.0F);
        if (ImGui::Selectable("##pm_row", is_sel, 0, row_sz))
        {
            w.selected_pm_project_idx = static_cast<int>(vi);
        }
        ImGui::PopStyleColor();

        // Double click detection
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            load_project_item(itm);
        }

        // Overlay row details
        const ImVec2 item_min = cd::ui::chroma::state().last_item_min;
        const ImVec2 item_max = cd::ui::chroma::state().last_item_max;

        ImGui::SetCursorScreenPos(ImVec2(item_min.x + 8.0F, item_min.y + 6.0F));

        // Favorite star toggle
        const char* star_sym = itm.favorite ? "[*]" : "[ ]";
        if (ImGui::SmallButton(star_sym))
        {
            if (orig_idx < s_pm_favorites.size())
                s_pm_favorites[orig_idx] = !s_pm_favorites[orig_idx];
        }
        ImGui::SameLine();

        // Project Name
        ImGui::TextColored(is_sel ? ImVec4(0.45F, 0.95F, 1.0F, 1.0F) : ImVec4(0.92F, 0.92F, 0.92F, 1.0F),
                           "%s", itm.name.c_str());

        // Path & Metadata
        ImGui::SetCursorScreenPos(ImVec2(item_min.x + 42.0F, item_min.y + 26.0F));
        ImGui::TextDisabled("%s", itm.path.c_str());

        // Right side: Version & Date
        ImGui::SetCursorScreenPos(ImVec2(item_max.x - 170.0F, item_min.y + 14.0F));
        ImGui::TextDisabled("v%s  %s", itm.version.c_str(), itm.date.c_str());

        ImGui::SetCursorScreenPos(ImVec2(item_min.x, item_max.y));

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // -------------------------------------------------------------------------
    // Right Action Column (Edit, Run, Rename, Duplicate, Manage Tags, Remove)
    // -------------------------------------------------------------------------
    ImGui::BeginChild("pm_actions", ImVec2(185.0F, remain_h), false);

    const bool has_sel = (!visible_indices.empty() &&
                          w.selected_pm_project_idx >= 0 &&
                          w.selected_pm_project_idx < static_cast<int>(visible_indices.size()));

    const PMItem* sel_item = has_sel ? &items[visible_indices[static_cast<std::size_t>(w.selected_pm_project_idx)]] : nullptr;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0F, 6.0F));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.44F, 0.74F, 1.0F));
    if (ImGui::Button("Edit", ImVec2(-1.0F, 30.0F)))
    {
        if (sel_item) load_project_item(*sel_item);
    }
    ImGui::PopStyleColor();

    if (ImGui::Button("Run", ImVec2(-1.0F, 28.0F)))
    {
        if (sel_item)
        {
            load_project_item(*sel_item);
            enter_pie_play_mode(s, log_push);
        }
    }

    if (ImGui::Button("Rename", ImVec2(-1.0F, 26.0F)))
    {
        ImGui::OpenPopup("pm_rename_popup");
    }
    if (ImGui::BeginPopup("pm_rename_popup"))
    {
        static char s_rename_buf[64] = "Renamed_Project";
        ImGui::Text("Rename Project:");
        ImGui::InputText("##rename_val", s_rename_buf, sizeof(s_rename_buf));
        if (ImGui::Button("Apply"))
        {
            w.active_project_name = s_rename_buf;
            cd::ui::chroma::state().open_popup_id = 0U;
        }
        ImGui::EndPopup();
    }

    if (ImGui::Button("Duplicate", ImVec2(-1.0F, 26.0F)))
    {
        w.hud_toast_message = "Project duplicated to workspace directory";
        w.hud_toast_timer   = 2.5F;
    }

    if (ImGui::Button("Manage Tags", ImVec2(-1.0F, 26.0F)))
    {
        w.hud_toast_message = "Tags: [3D, PBR, Active]";
        w.hud_toast_timer   = 2.0F;
    }

    if (ImGui::Button("Remove", ImVec2(-1.0F, 26.0F)))
    {
        w.hud_toast_message = "Removed project from recents list";
        w.hud_toast_timer   = 2.0F;
    }

    if (ImGui::Button("Remove Missing", ImVec2(-1.0F, 26.0F)))
    {
        w.hud_toast_message = "Missing projects purged";
        w.hud_toast_timer   = 2.0F;
    }

    ImGui::Separator();
    ImGui::TextDisabled("v1.0.0-stable");
    ImGui::SameLine();
    if (ImGui::SmallButton("<3 Donate"))
    {
        w.hud_toast_message = "Thank you for supporting CHROMODYNAMIC Engine!";
        w.hud_toast_timer   = 3.0F;
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();

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

    // If scene is clean/empty and demo HUD overlay is not explicitly enabled:
    if (!has_scene_ui_nodes && !w.enable_demo_hud_overlay)
    {
        // Minimal, elegant top-right floating pill for Play controls
        ImGui::SetNextWindowPos(ImVec2(sw - 235.0F, 58.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(225.0F, 36.0F), ImGuiCond_Always);
        ImGuiWindowFlags pill_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoBackground;
        if (ImGui::Begin("##clean_play_controls", nullptr, pill_flags))
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85F, 0.22F, 0.22F, 0.90F));
            if (ImGui::Button("[ ] Stop Play (Esc)", ImVec2(125.0F, 24.0F)))
            {
                exit_pie_play_mode(s, log_push);
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.40F, 0.65F, 0.85F));
            if (ImGui::Button("+ HUD", ImVec2(80.0F, 24.0F)))
            {
                w.enable_demo_hud_overlay = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggle Sample Demo HUD Overlay");
            }
            ImGui::PopStyleColor();
        }
        ImGui::End();
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
            ImGui::SetTooltip("#%03u %s  |  Role: %s (%s)",
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

    // Row 2: Construct Title & Standard Role (Clean without external engine references)
    ImGui::TextColored(ImVec4(0.38F, 0.94F, 0.68F, 1.0F),
                       "#%03u  %s  (%s)",
                       static_cast<unsigned>(sel_desc->id),
                       sel_desc->chroma_name,
                       chroma_construct_kind_label(sel_desc->construct_kind));
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.96F, 0.80F, 0.36F, 1.0F),
                       "  |  Role: %s (%s | %s)",
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
            // TAB 2: Create (3D Objects, Prefabs & Modifier Classes)
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Create"))
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
            // TAB 3: Stages & Layers (World, Level & Layer Hierarchy)
            // -----------------------------------------------------------------
            if (ImGui::BeginTabItem("Stages"))
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
                constexpr std::array<FolderNode, 5> kFolders {{
                    { "showcases",       "showcases",                       "Show" },
                    { "assets/meshes",   "assets/meshes",                   "3D" },
                    { "assets/textures", "assets/textures",                 "PBR" },
                    { "samples/Generic", "assets/samples/Generic",          "GLTF" },
                    { "samples/Sponza",  "assets/samples/Sponza",           "Atrium" },
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
                    for (const auto& dir_entry : std::filesystem::directory_iterator(w.current_content_folder, dire_ec))
                    {
                        if (dir_entry.is_directory(dire_ec))
                        {
                            const std::string sub_name = dir_entry.path().filename().string();
                            const std::string sub_full = dir_entry.path().generic_string();
                            const std::string sub_id   = "[DIR] " + sub_name + "##sub_" + sub_full;
                            if (ImGui::Selectable(sub_id.c_str(), false))
                            {
                                w.current_content_folder = sub_full;
                                std::snprintf(s_custom_disk_path, sizeof(s_custom_disk_path), "%s", sub_full.c_str());
                                build_disk_entries(w.current_content_folder);
                            }
                        }
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine(0.0F, 6.0F);

                // ---- PANE 2 (Center-Left): Disk Files Grid (3D/Texture Previews — Click to Open/Load) ----
                ImGui::BeginChild("disk_files_grid_pane", ImVec2(pane2_w, 0.0F), false);
                ImGui::TextColored(ImVec4(0.52F, 0.86F, 1.0F, 1.0F),
                                   "2. Disk Files (Click -> Context)");
                const int disk_cols = std::max(2, static_cast<int>(pane2_w / 92.0F));
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

                ImGui::SameLine(0.0F, 6.0F);

                // ---- PANE 3 (Center-Right): Active Project Context (Visual 3D & Texture Previews) ----
                ImGui::BeginChild("active_context_tiles_pane", ImVec2(pane3_w, 0.0F), false);
                ImGui::TextColored(ImVec4(0.38F, 0.95F, 0.66F, 1.0F),
                                   "3. Active Context (%zu Loaded)",
                                   w.loaded_context_assets.size());
                const int ctx_cols = std::max(2, static_cast<int>(pane3_w / 92.0F));
                if (w.loaded_context_assets.empty())
                {
                    ImGui::TextDisabled("Active Context is empty (0 loaded).");
                    ImGui::TextDisabled("Click Disk Files (Pane 2) to load");
                    ImGui::TextDisabled("assets into Context, or load pack:");
                    if (ImGui::IconButton("Load Starter Pack##ctx_seed_btn",
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
                        w.selected_content_asset = "assets/meshes/Sphere.mesh";
                        w.last_asset_status_msg = "Loaded 8 Disk Assets into Active Context";
                        log_push("[context] " + w.last_asset_status_msg);
                    }
                }
                else if (ImGui::BeginTable("context_tiles_grid_col", ctx_cols, ImGuiTableFlags_SizingStretchSame))
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
                            ImGui::SetTooltip("Active Context Asset: %s\nDisk Source: %s\n%s",
                                              ca.name.c_str(), ca.path.c_str(),
                                              ca.kind == 0U
                                                  ? "Click to Spawn at (0, 0, 0), or Drag onto any Object"
                                                  : "Click or Drag to Apply Texture to Selected Object");
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
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
