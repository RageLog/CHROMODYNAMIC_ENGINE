// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/UnifiedTopBar.cpp
// =============================================================================
#include "ui/UnifiedTopBar.hpp"

#include "physics/PhysicsPieSolver.hpp"
#include "pipeline/AssetIngest.hpp"
#include "scene/SceneOperations.hpp"
#include "Studio2DWorkspace.hpp"
#include "StudioModifierClasses.hpp"

#include <cd/ui/ChromaUi.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

namespace cd::studio::ui
{

void draw_unified_top_bar(StudioEngineState& s, float dt)
{
    (void)dt;
    auto& w   = studio_world();
    auto& cst = cd::ui::chroma::state();

    // Enforce unified single 32px top bar across ChromaUi dockspace & menus.
    cst.top_bar_height  = 32.0F;
    cst.menu_bar_height = 32.0F;

    // Global studio keyboard shortcuts (when text input is not active).
    if (!ImGui::GetIO().WantCaptureKeyboard)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
        {
            w.show_project_manager = false;
            if (cst.workspace_mode == 3)
            {
                auto& s2d = studio_2d();
                if (!s2d.stage.is_playing())
                {
                    s2d.stage.enter_play();
                }
                else
                {
                    s2d.stage.exit_play();
                }
            }
            else if (!s.pie_playing)
            {
                cst.workspace_mode = 0;
                physics_pie::enter_pie(s);
            }
            else
            {
                physics_pie::exit_pie(s);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F8, false))
        {
            if (cst.workspace_mode == 3 && studio_2d().stage.is_playing())
            {
                studio_2d().stage.exit_play();
            }
            else if (s.pie_playing)
            {
                physics_pie::exit_pie(s);
                if (cst.workspace_mode == 1)
                {
                    cst.workspace_mode = 0;
                }
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F9, false))
        {
            (void)scene_ops::export_standalone_package(s, "build/export/ChromodynamicGame");
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        {
            (void)scene_ops::save_scene(s, "studio_scene.cdscene.json");
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
        {
            (void)scene_ops::load_scene(s, "studio_scene.cdscene.json");
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D) && !s.pie_playing && cst.workspace_mode == 0)
        {
            scene_ops::duplicate_selected_node(s);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z) && !s.pie_playing)
        {
            if (cst.workspace_mode == 3)
            {
                (void)studio_2d().command_bus.undo(studio_2d().stage);
            }
            else if (s.history.can_undo())
            {
                (void)s.history.undo();
                s.scene.update_transforms();
            }
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) && !s.pie_playing)
        {
            if (cst.workspace_mode == 3)
            {
                (void)studio_2d().command_bus.redo(studio_2d().stage);
            }
            else if (s.history.can_redo())
            {
                (void)s.history.redo();
                s.scene.update_transforms();
            }
        }
    }

    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    // ---- 1. Left Menus: File | Edit | Create | View | Project | Help ----
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Project Hub..."))
        {
            if (s.pie_playing)
            {
                physics_pie::exit_pie(s);
            }
            w.show_project_manager = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("New 2D Empty Project"))
        {
            w.show_project_manager = false;
            cst.workspace_mode     = 3;
            studio_2d().reset_to_empty_stage();
            w.active_project_name = "Empty_2D_Project";
            w.active_level_name   = "stages/main.cd2d.json";
        }
        if (ImGui::MenuItem("New 2D Platformer Stage"))
        {
            w.show_project_manager = false;
            cst.workspace_mode     = 3;
            studio_2d().reset_to_starter_stage();
            w.active_project_name = std::string(studio_2d().stage.name());
            w.active_level_name   = "stages/main.cd2d.json";
        }
        if (ImGui::MenuItem("New 3D Empty Stage"))
        {
            w.show_project_manager = false;
            cst.workspace_mode     = 0;
            scene_ops::reset_to_clean_empty_project(s, true);
        }
        if (ImGui::MenuItem("New 3D Blank Stage (Floor + Sun)"))
        {
            w.show_project_manager = false;
            cst.workspace_mode     = 0;
            scene_ops::reset_to_clean_empty_project(s, true);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Open Scene / Project from Disk..."))
        {
            std::string picked;
            if (scene_ops::open_os_file_dialog(
                    "Chromodynamic Files (*.cdscene.json;*.cdproject;*.cdproj)\0*.cdscene.json;*.cdproject;*.cdproj\0All Files (*.*)\0*.*\0",
                    picked,
                    "Open Scene or Project"))
            {
                w.show_project_manager = false;
                (void)scene_ops::load_project_or_scene_from_disk(s, picked);
            }
        }
        if (ImGui::MenuItem("Quick Load (studio_scene.cdscene.json)", "Ctrl+O"))
        {
            w.show_project_manager = false;
            (void)scene_ops::load_scene(s, "studio_scene.cdscene.json");
        }
        if (ImGui::MenuItem("Save Scene (studio_scene.cdscene.json)", "Ctrl+S"))
        {
            (void)scene_ops::save_scene(s, "studio_scene.cdscene.json");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import 3D Mesh / Texture..."))
        {
            std::string picked;
            if (scene_ops::open_os_file_dialog(
                    "Assets (*.obj;*.gltf;*.glb;*.png;*.jpg)\0*.obj;*.gltf;*.glb;*.png;*.jpg\0All Files (*.*)\0*.*\0",
                    picked,
                    "Import Asset"))
            {
                w.show_project_manager = false;
                (void)pipeline::import_or_assign_file(s, picked, false);
            }
        }
        if (ImGui::MenuItem("Export Standalone Game (.cdpak)", "F9"))
        {
            (void)scene_ops::export_standalone_package(s, "build/export/ChromodynamicGame");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit Studio"))
        {
            if (s.window)
            {
                s.window->request_close();
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, s.history.can_undo()))
        {
            (void)s.history.undo();
            s.scene.update_transforms();
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, s.history.can_redo()))
        {
            (void)s.history.redo();
            s.scene.update_transforms();
        }
        ImGui::Separator();
        const bool has_sel = (s.selected >= 0);
        if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, has_sel))
        {
            scene_ops::duplicate_selected_node(s);
        }
        if (ImGui::MenuItem("Delete Selected", "Del", false, has_sel))
        {
            scene_ops::delete_selected_node(s);
        }
        if (ImGui::MenuItem("Focus Camera on Selected", "F", false, has_sel))
        {
            scene_ops::focus_selected_node(s);
        }
        if (ImGui::MenuItem("Snap Selected to Ground", "G", false, has_sel))
        {
            scene_ops::snap_selected_to_ground(s);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Command Palette...", "Ctrl+Shift+P"))
        {
            s.palette_visible = true;
            s.palette_query.clear();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Create"))
    {
        if (ImGui::BeginMenu("3D Primitives"))
        {
            if (ImGui::MenuItem("Cube"))
                (void)scene_ops::spawn_primitive_node(s, "Cube", PrimitiveKind::kCube, { 0.85F, 0.87F, 0.90F }, true, 0.05F, 0.38F);
            if (ImGui::MenuItem("Sphere"))
                (void)scene_ops::spawn_primitive_node(s, "Sphere", PrimitiveKind::kSphere, { 0.85F, 0.87F, 0.90F }, true, 0.15F, 0.25F);
            if (ImGui::MenuItem("Cylinder"))
                (void)scene_ops::spawn_primitive_node(s, "Cylinder", PrimitiveKind::kCylinder, { 0.82F, 0.85F, 0.88F }, true, 0.10F, 0.35F);
            if (ImGui::MenuItem("Cone"))
                (void)scene_ops::spawn_primitive_node(s, "Cone", PrimitiveKind::kCone, { 0.90F, 0.78F, 0.55F }, true, 0.05F, 0.42F);
            if (ImGui::MenuItem("Torus"))
                (void)scene_ops::spawn_primitive_node(s, "Torus", PrimitiveKind::kTorus, { 0.75F, 0.82F, 0.95F }, true, 0.35F, 0.22F);
            ImGui::Separator();
            if (ImGui::MenuItem("Ground Floor (Static Collider)"))
                (void)scene_ops::spawn_ground_floor(s);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Physics & Gameplay"))
        {
            if (ImGui::MenuItem("Dynamic RigidBody Cube"))
                (void)scene_ops::spawn_entity_by_preset(s, "Dynamic_Cube", { 0.0F, 1.8F, 0.0F });
            if (ImGui::MenuItem("Bouncing Sphere"))
                (void)scene_ops::spawn_entity_by_preset(s, "Dynamic_Sphere", { 0.0F, 2.4F, 0.0F });
            if (ImGui::MenuItem("Inclined Ramp (25 deg OBB)"))
                (void)scene_ops::spawn_entity_by_preset(s, "Static_Ramp", { 0.0F, 0.0F, 0.0F });
            if (ImGui::MenuItem("Trampoline Bounce Pad"))
                (void)scene_ops::spawn_entity_by_preset(s, "Trampoline_Pad", { 0.0F, 0.0F, 0.0F });
            if (ImGui::MenuItem("5x Physics Crate Tower"))
                (void)scene_ops::spawn_entity_by_preset(s, "Prefab_CrateStack", { 0.0F, 0.0F, 0.0F });
            if (ImGui::MenuItem("6x Domino Chain"))
                (void)scene_ops::spawn_entity_by_preset(s, "Prefab_DominoRow", { 0.0F, 0.0F, 0.0F });
            ImGui::Separator();
            if (ImGui::MenuItem("Playable 3D Character Vessel (WASD)"))
                (void)scene_ops::spawn_character_vessel(s, false);
            if (ImGui::MenuItem("Skinned Animated Character (CesiumMan)"))
                (void)scene_ops::spawn_entity_by_preset(s, "Character_CesiumMan", { 0.0F, 0.0F, 0.0F });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Lights"))
        {
            if (ImGui::MenuItem("Directional Sun Light (5800K)"))
                (void)scene_ops::spawn_light_preset(s, 0);
            if (ImGui::MenuItem("Point Omni Light (3200K)"))
                (void)scene_ops::spawn_light_preset(s, 1);
            if (ImGui::MenuItem("Spot Cone Light (4200K)"))
                (void)scene_ops::spawn_light_preset(s, 2);
            if (ImGui::MenuItem("Rect Area Softbox (5600K)"))
                (void)scene_ops::spawn_light_preset(s, 3);
            if (ImGui::MenuItem("Sky Ambient Fill (6500K)"))
                (void)scene_ops::spawn_light_preset(s, 4);
            ImGui::Separator();
            if (ImGui::MenuItem("3-Point Studio Lighting Rig"))
                (void)scene_ops::spawn_light_preset(s, 5);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Cameras & Atmosphere"))
        {
            if (ImGui::MenuItem("Scene Camera (3D Perspective)"))
                (void)scene_ops::spawn_scene_camera(s, false);
            if (ImGui::MenuItem("Volumetric Clouds"))
                (void)scene_ops::spawn_volumetric_clouds(s);
            if (ImGui::MenuItem("Volumetric Fog & God-Rays"))
                (void)scene_ops::spawn_volumetric_fog(s);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Browse 250 Native Constructs..."))
        {
            w.show_project_manager          = false;
            cst.workspace_mode              = 0;
            w.focus_bottom_node_catalog_tab = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("2D World Workspace", nullptr, cst.workspace_mode == 3 && !w.show_project_manager))
        {
            w.show_project_manager = false;
            if (s.pie_playing)
            {
                physics_pie::exit_pie(s);
            }
            cst.workspace_mode = 3;
        }
        if (ImGui::MenuItem("3D World Workspace", nullptr, cst.workspace_mode == 0 && !w.show_project_manager))
        {
            w.show_project_manager = false;
            cst.workspace_mode     = 0;
        }
        if (ImGui::MenuItem("Shader & Node Graph Workspace", nullptr, cst.workspace_mode == 2 && !w.show_project_manager))
        {
            w.show_project_manager = false;
            cst.workspace_mode     = 2;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("3D Shading Mode"))
        {
            if (ImGui::MenuItem("Lit PBR", nullptr, s.fx.view_mode == 0)) s.fx.view_mode = 0;
            if (ImGui::MenuItem("World Normals", nullptr, s.fx.view_mode == 1)) s.fx.view_mode = 1;
            if (ImGui::MenuItem("Base Albedo", nullptr, s.fx.view_mode == 2)) s.fx.view_mode = 2;
            if (ImGui::MenuItem("Roughness / Metallic", nullptr, s.fx.view_mode == 3)) s.fx.view_mode = 3;
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Editor Reference Floor", nullptr, &s.show_editor_floor);
        ImGui::MenuItem("Collider Wireframes", nullptr, &w.show_all_collider_wires);
        ImGui::MenuItem("Grid Snapping (0.25m)", nullptr, &w.grid_snap_enabled);
        ImGui::Separator();
        ImGui::MenuItem("Diagnostic 3D Render Probes", nullptr, &w.show_diagnostic_probes);
        ImGui::MenuItem("Behavior Tree Overlay", nullptr, &s.show_behavior_designer);
        ImGui::MenuItem("UI Flex Layout Overlay", nullptr, &s.show_ui_widgets);
        ImGui::Separator();
        if (ImGui::BeginMenu("Editor Theme"))
        {
            if (ImGui::MenuItem("Source 2 Dark Graphite", nullptr, cst.theme_index == 0))
                ImGui::ApplyTheme(0);
            if (ImGui::MenuItem("Studio Daylight", nullptr, cst.theme_index == 1))
                ImGui::ApplyTheme(1);
            if (ImGui::MenuItem("Midnight Slate", nullptr, cst.theme_index == 2))
                ImGui::ApplyTheme(2);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Project"))
    {
        if (ImGui::MenuItem("Open Project Hub..."))
        {
            if (s.pie_playing)
            {
                physics_pie::exit_pie(s);
            }
            w.show_project_manager = true;
        }
        if (ImGui::BeginMenu("Load Interactive Template"))
        {
            if (ImGui::MenuItem("1. Clean Empty Stage"))
            {
                w.show_project_manager = false;
                cst.workspace_mode     = 0;
                scene_ops::load_interactive_template(s, 0);
            }
            if (ImGui::MenuItem("2. OBB Ramp & 5x Crate Tower"))
            {
                w.show_project_manager = false;
                cst.workspace_mode     = 0;
                scene_ops::load_interactive_template(s, 1);
            }
            if (ImGui::MenuItem("3. 6x Domino Chain & Striker"))
            {
                w.show_project_manager = false;
                cst.workspace_mode     = 0;
                scene_ops::load_interactive_template(s, 2);
            }
            if (ImGui::MenuItem("4. PBR 5x5 Sphere Matrix"))
            {
                w.show_project_manager = false;
                cst.workspace_mode     = 0;
                scene_ops::load_interactive_template(s, 4);
            }
            if (ImGui::MenuItem("5. Classical Sponza Atrium & DDGI"))
            {
                w.show_project_manager = false;
                cst.workspace_mode     = 0;
                scene_ops::load_interactive_template(s, 5);
            }
            if (ImGui::MenuItem("6. Cyber-Core Action Platformer (Playable)"))
            {
                w.show_project_manager = false;
                cst.workspace_mode     = 0;
                scene_ops::load_interactive_template(s, 13);
            }
            if (ImGui::MenuItem("7. Unified 2D+3D Physics & Ragdoll Arena"))
            {
                w.show_project_manager = false;
                cst.workspace_mode     = 0;
                scene_ops::load_interactive_template(s, 15);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Package Standalone Game (.cdpak)", "F9"))
        {
            (void)scene_ops::export_standalone_package(s, "build/export/ChromodynamicGame");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        ImGui::TextColored(ImVec4(0.45F, 0.88F, 1.0F, 1.0F), "CHROMODYNAMIC STUDIO CONTROLS");
        ImGui::Separator();
        ImGui::TextUnformatted("RMB Drag + WASD/QE : Fly Camera");
        ImGui::TextUnformatted("MMB Drag / Scroll  : Pan / Orbit Zoom");
        ImGui::TextUnformatted("LMB Click          : Select Node / Light");
        ImGui::TextUnformatted("1 / 2 / 3 (Space)  : Move / Rotate / Scale Gizmo");
        ImGui::TextUnformatted("F / G / Del        : Focus / Snap Floor / Delete");
        ImGui::TextUnformatted("Ctrl+D / Z / Y     : Duplicate / Undo / Redo");
        ImGui::TextUnformatted("F5 / Esc           : Play-In-Editor / Stop");
        ImGui::TextUnformatted("F9                 : Export Standalone .cdpak");
        ImGui::TextUnformatted("Ctrl+Shift+P       : Command Palette");
        ImGui::EndMenu();
    }

    // ---- 2. Center Workspace Pills + Play/Stop + Package (.cdpak) ----
    const float sw         = cst.io.DisplaySize.x;
    const float left_end_x = cst.window_stack.empty() ? 310.0F : cst.window_stack.back().cursor.x;
    const float center_w   = 410.0F;
    const float target_cx  = std::max(left_end_x + 18.0F, (sw - center_w) * 0.5F - 24.0F);

    if (target_cx + center_w < sw - 230.0F)
    {
        auto* dl = ImGui::GetForegroundDrawList();
        const ImVec2 mp        = ImGui::GetMousePos();
        const bool   lmb_click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        auto draw_top_pill = [&](float& cur_x, float w_btn, const char* label, bool active,
                                 ImU32 active_bg, ImU32 idle_bg, ImU32 txt_col) -> bool
        {
            const ImVec2 b0(cur_x, 4.0F);
            const ImVec2 b1(cur_x + w_btn, 28.0F);
            const bool   hov = (mp.x >= b0.x && mp.x <= b1.x && mp.y >= b0.y && mp.y <= b1.y);
            const ImU32 bg = active
                ? active_bg
                : (hov ? IM_COL32(58, 66, 78, 255) : idle_bg);
            dl->AddRectFilled(b0, b1, bg, 4.0F);
            if (active)
            {
                dl->AddRect(b0, b1, IM_COL32(110, 195, 255, 220), 4.0F, 0, 1.2F);
            }
            else
            {
                dl->AddRect(b0, b1, IM_COL32(68, 76, 88, 170), 4.0F);
            }
            const ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(b0.x + (w_btn - ts.x) * 0.5F, b0.y + (24.0F - ts.y) * 0.5F),
                        txt_col, label);
            cur_x += w_btn + 4.0F;
            if (hov)
            {
                cst.any_item_hovered    = true;
                cst.mouse_in_any_window = true;
            }
            if (hov && lmb_click)
            {
                cst.open_popup_id = 0U;
                cst.active_menu_name.clear();
                cst.active_submenu_name.clear();
                return true;
            }
            return false;
        };

        float cx = target_cx;
        const bool hub_open = w.show_project_manager;
        if (draw_top_pill(cx, 44.0F, "Hub", hub_open,
                          IM_COL32(48, 98, 165, 255), IM_COL32(34, 39, 47, 255), IM_COL32(235, 240, 248, 255)))
        {
            if (s.pie_playing)
            {
                physics_pie::exit_pie(s);
            }
            w.show_project_manager = !w.show_project_manager;
        }
        if (draw_top_pill(cx, 54.0F, "2D", !hub_open && cst.workspace_mode == 3,
                          IM_COL32(36, 118, 178, 255), IM_COL32(34, 39, 47, 255), IM_COL32(235, 240, 248, 255)))
        {
            w.show_project_manager = false;
            if (s.pie_playing)
            {
                physics_pie::exit_pie(s);
            }
            cst.workspace_mode = 3;
        }
        if (draw_top_pill(cx, 54.0F, "3D", !hub_open && cst.workspace_mode == 0,
                          IM_COL32(36, 118, 178, 255), IM_COL32(34, 39, 47, 255), IM_COL32(235, 240, 248, 255)))
        {
            w.show_project_manager = false;
            if (studio_2d().stage.is_playing())
            {
                studio_2d().stage.exit_play();
            }
            cst.workspace_mode = 0;
        }
        if (draw_top_pill(cx, 58.0F, "Graph", !hub_open && cst.workspace_mode == 2,
                          IM_COL32(36, 118, 178, 255), IM_COL32(34, 39, 47, 255), IM_COL32(235, 240, 248, 255)))
        {
            w.show_project_manager = false;
            cst.workspace_mode     = 2;
        }

        cx += 8.0F;
        const bool is_2d_mode = (cst.workspace_mode == 3);
        const bool playing    = is_2d_mode ? studio_2d().stage.is_playing() : s.pie_playing;

        if (!playing)
        {
            if (draw_top_pill(cx, 86.0F, "> Play (F5)", false,
                              IM_COL32(38, 142, 78, 255), IM_COL32(32, 112, 64, 255), IM_COL32(235, 255, 240, 255)))
            {
                w.show_project_manager = false;
                if (is_2d_mode)
                {
                    studio_2d().stage.enter_play();
                }
                else
                {
                    cst.workspace_mode = 0;
                    physics_pie::enter_pie(s);
                }
            }
        }
        else
        {
            if (draw_top_pill(cx, 86.0F, "Stop (Esc)", true,
                              IM_COL32(185, 46, 46, 255), IM_COL32(165, 40, 40, 255), IM_COL32(255, 240, 240, 255)))
            {
                if (is_2d_mode)
                {
                    studio_2d().stage.exit_play();
                }
                else
                {
                    physics_pie::exit_pie(s);
                    if (cst.workspace_mode == 1)
                    {
                        cst.workspace_mode = 0;
                    }
                }
            }
        }

        if (draw_top_pill(cx, 88.0F, "Export .cdpak", false,
                          IM_COL32(52, 88, 138, 255), IM_COL32(38, 48, 64, 255), IM_COL32(215, 232, 252, 255)))
        {
            (void)scene_ops::export_standalone_package(s, "build/export/ChromodynamicGame");
        }
    }

    // ---- 3. Right Discrete Telemetry Pill (clear of Min/Max/Close window buttons) ----
    if (sw > 960.0F)
    {
        auto* dl = ImGui::GetForegroundDrawList();
        char fps_buf[48] {};
        const float fps_v = (w.smoothed_fps > 0.5F) ? w.smoothed_fps : 60.0F;
        const float ms_v  = (w.smoothed_frame_ms > 0.05F) ? w.smoothed_frame_ms : 16.6F;
        std::snprintf(fps_buf, sizeof(fps_buf), "%.0f FPS  %.2f ms",
                      static_cast<double>(fps_v), static_cast<double>(ms_v));
        const ImVec2 ts = ImGui::CalcTextSize(fps_buf);
        const float  bx = sw - 128.0F - ts.x - 18.0F;
        const ImVec2 p0(bx, 5.0F);
        const ImVec2 p1(bx + ts.x + 14.0F, 27.0F);
        dl->AddRectFilled(p0, p1, IM_COL32(24, 29, 36, 235), 4.0F);
        dl->AddRect(p0, p1, IM_COL32(55, 72, 88, 180), 4.0F);
        dl->AddText(ImVec2(p0.x + 7.0F, p0.y + (22.0F - ts.y) * 0.5F),
                    IM_COL32(115, 232, 165, 255), fps_buf);
    }

    ImGui::EndMainMenuBar();
}

}  // namespace cd::studio::ui
