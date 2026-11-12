// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/ProjectHubModal.cpp
// =============================================================================
#include "ui/ProjectHubModal.hpp"

#include "scene/SceneOperations.hpp"
#include "Studio2DWorkspace.hpp"
#include "StudioModifierClasses.hpp"

#include <cd/ui/ChromaUi.hpp>
#include <cd/world2d/GenreTemplates2D.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>

namespace cd::studio::ui
{

void draw_project_hub_modal(StudioEngineState& s, std::uint32_t frame_w, std::uint32_t frame_h)
{
    auto& w   = studio_world();
    auto& cst = cd::ui::chroma::state();
    const float sw    = static_cast<float>(frame_w);
    const float sh    = static_cast<float>(frame_h);
    const float top_y = cst.top_bar_height;

    ImGui::SetNextWindowPos(ImVec2(0.0F, top_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, std::max(200.0F, sh - top_y)), ImGuiCond_Always);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

    if (!ImGui::Begin("##StudioProjectHubClean", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    // ---- Clean Header Strip ----
    ImGui::TextColored(ImVec4(0.45F, 0.86F, 1.0F, 1.0F), "CHROMODYNAMIC STUDIO");
    ImGui::SameLine(0.0F, 14.0F);
    ImGui::TextDisabled("Commercial 2D & 3D Engine  |  Source 2 Modular Architecture");

    const float right_btns_x = std::max(460.0F, sw - 410.0F);
    ImGui::SameLine(right_btns_x);
    if (ImGui::Button("Open Disk File...", ImVec2(150.0F, 26.0F)))
    {
        std::string picked;
        if (scene_ops::open_os_file_dialog(
                "Projects & Scenes (*.cdscene.json;*.cdproject;*.cdproj)\0*.cdscene.json;*.cdproject;*.cdproj\0All Files (*.*)\0*.*\0",
                picked,
                "Open Project or Scene"))
        {
            w.show_project_manager = false;
            (void)scene_ops::load_project_or_scene_from_disk(s, picked);
        }
    }
    ImGui::SameLine(0.0F, 8.0F);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.50F, 0.82F, 1.0F));
    if (ImGui::Button("Enter Editor ->", ImVec2(145.0F, 26.0F)))
    {
        w.show_project_manager = false;
    }
    ImGui::PopStyleColor();

    ImGui::Separator();

    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float avail_h = ImGui::GetContentRegionAvail().y - 34.0F;
    const float left_w  = std::max(420.0F, avail_w * 0.62F);
    const float right_w = std::max(260.0F, avail_w - left_w - 14.0F);

    // ---- Left Column: New Project Starter Templates ----
    ImGui::BeginChild("hub_templates_col", ImVec2(left_w, avail_h), true);
    ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.98F, 1.0F), "Create New Project");
    ImGui::TextDisabled("Choose a clean 2D or 3D template to begin authoring immediately.");
    ImGui::Separator();

    struct StarterCard
    {
        const char*              title;
        const char*              badge;
        const char*              summary;
        int                      action_id; // -10..-2 = 2D genre, -1 = Empty 3D, >=0 = 3D template_id
        cd::ui::chroma::IconKind icon;
        ImU32                    accent;
    };

    constexpr std::array<StarterCard, 8> kCards {{
        { "2D Empty Project",
          "EMPTY 2D",
          "Clean blank 2D canvas: 0 clutter entities, centered Camera2D, ready to author from scratch",
          -10, cd::ui::chroma::IconKind::kUiWidget, IM_COL32(130, 220, 255, 255) },
        { "3D Empty Stage",
          "EMPTY 3D",
          "Clean blank 3D world: Minimal Ground Floor, Directional Sun (5800K) & Perspective Camera",
          -1, cd::ui::chroma::IconKind::kCube, IM_COL32(140, 205, 255, 255) },
        { "2D Neon Cyber Pong",
          "ARCADE",
          "1P vs CPU arcade court: Spin paddles, accelerating plasma ball, spark FX & .exe export",
          -11, cd::ui::chroma::IconKind::kSphere, IM_COL32(55, 235, 255, 255) },
        { "2D Break the Bridges",
          "DEMOLITION",
          "Multi-span canyon bridges + marching raiders: shatter piers & collapse raiders into the abyss!",
          -12, cd::ui::chroma::IconKind::kPhysics, IM_COL32(255, 185, 65, 255) },
        { "2D Neon Platformer",
          "PLATFORMER",
          "16-Tile Wang Auto-Tiling, Coyote/Wall Jump, Greedy Colliders, Lua 5.4 & Soft Shadows",
          -2, cd::ui::chroma::IconKind::kUiWidget, IM_COL32(75, 240, 175, 255) },
        { "2D Top-Down RPG",
          "2D RPG",
          "Y-Sorted dungeon walls, flickering 2D shadow-casting torches, keys, chests & patrols",
          -13, cd::ui::chroma::IconKind::kWorld, IM_COL32(235, 160, 255, 255) },
        { "3D Physics Sandbox",
          "VPHYSICS",
          "25-deg OBB Ramp, 5x RigidBody Crate Stack, Metallic Sphere & Skinned Avatar",
          1, cd::ui::chroma::IconKind::kPhysics, IM_COL32(95, 235, 165, 255) },
        { "3D Sponza Atrium & GI",
          "WORLD GI",
          "Classical Atrium glTF, Clustered Multi-Light UBO, CSM Shadows & Ray-Traced DDGI",
          5, cd::ui::chroma::IconKind::kWorld, IM_COL32(255, 210, 110, 255) },
    }};

    if (ImGui::BeginTable("hub_starter_grid", 2, ImGuiTableFlags_SizingStretchSame))
    {
        for (std::size_t i = 0; i < kCards.size(); ++i)
        {
            const auto& c = kCards[i];
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginChild("card_box", ImVec2(0.0F, 114.0F), true);

            const ImVec4 accent_v4 {
                static_cast<float>(c.accent & 0xFFU) / 255.0F,
                static_cast<float>((c.accent >> 8U) & 0xFFU) / 255.0F,
                static_cast<float>((c.accent >> 16U) & 0xFFU) / 255.0F,
                static_cast<float>((c.accent >> 24U) & 0xFFU) / 255.0F,
            };
            ImGui::TextColored(accent_v4, "[%s]  %s", c.badge, c.title);
            ImGui::TextDisabled("%s", c.summary);
            ImGui::Separator();

            const std::string btn_lbl = std::string("Open ") + c.title + "##create_btn";
            if (ImGui::IconButton(btn_lbl.c_str(), c.icon, c.accent, false, ImVec2(-1.0F, 25.0F)))
            {
                w.show_project_manager = false;
                if (c.action_id <= -2)
                {
                    cst.workspace_mode = 3;
                    auto& s2d          = studio_2d();
                    auto gkind         = cd::world2d::GenreTemplateKind2D::kActionPlatformer;
                    if (c.action_id == -10)      gkind = cd::world2d::GenreTemplateKind2D::kEmpty2D;
                    else if (c.action_id == -11) gkind = cd::world2d::GenreTemplateKind2D::kPongArcade;
                    else if (c.action_id == -12) gkind = cd::world2d::GenreTemplateKind2D::kBreakTheBridges;
                    else if (c.action_id == -13) gkind = cd::world2d::GenreTemplateKind2D::kTopDownRpgDungeon;
                    s2d.command_bus.load_genre_template(s2d.stage, gkind);
                    s2d.stage.bind_starter_atlas(0U, 32U);
                    s2d.active_genre_template_idx = static_cast<int>(gkind);
                    s2d.selected_entity_id        = s2d.stage.entities().empty() ? 0U : s2d.stage.entities().front().id;
                    s2d.sync_lua_buffer_from_selected();
                    w.active_project_name = std::string(s2d.stage.name());
                    w.active_level_name   = "stages/main.cd2d.json";
                }
                else if (c.action_id == -1)
                {
                    cst.workspace_mode = 0;
                    scene_ops::reset_to_clean_empty_project(s, true);
                }
                else
                {
                    cst.workspace_mode = 0;
                    scene_ops::load_interactive_template(s, c.action_id);
                }
            }

            ImGui::EndChild();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0F, 10.0F);

    // ---- Right Column: Sample Projects & Showcases on Disk ----
    ImGui::BeginChild("hub_disk_projects_col", ImVec2(right_w, avail_h), true);
    ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.98F, 1.0F), "Sample Projects & Showcases on Disk");
    ImGui::TextDisabled("Click any Empty Project, 2D Sample Game, or 3D Showcase on disk.");
    ImGui::Separator();

    if (std::filesystem::exists("studio_scene.cdscene.json"))
    {
        if (ImGui::IconSelectable("studio_scene.cdscene.json##recent_scene",
                                  cd::ui::chroma::IconKind::kFileScene,
                                  IM_COL32(115, 225, 255, 255), false, "Saved"))
        {
            w.show_project_manager = false;
            cst.workspace_mode     = 0;
            (void)scene_ops::load_scene(s, "studio_scene.cdscene.json");
        }
        ImGui::Separator();
    }

    struct DiskShowcaseItem
    {
        const char* label;
        const char* path;
        const char* tag;
        ImU32       tint;
    };
    constexpr std::array<DiskShowcaseItem, 6> kSampleProjects2D {{
        { "00  Empty 2D Project (Blank)",    "projects/00_empty_project/project.cdproj",     "EMPTY",  IM_COL32(130, 220, 255, 255) },
        { "01  Neon Cyber Pong (Arcade)",    "projects/01_pong/project.cdproj",              "PONG",   IM_COL32(60, 240, 255, 255) },
        { "02  Break the Bridges (Action)",  "projects/02_break_the_bridges/project.cdproj", "BRIDGE", IM_COL32(255, 185, 70, 255) },
        { "03  Neon Cavern Platformer",      "projects/03_neon_platformer/project.cdproj",   "2D",     IM_COL32(80, 235, 170, 255) },
        { "04  Top-Down RPG Dungeon",        "projects/04_topdown_rpg/project.cdproj",       "RPG",    IM_COL32(225, 160, 255, 255) },
        { "05  Twin-Stick Survival Arena",   "projects/05_twinstick_arena/project.cdproj",   "ARENA",  IM_COL32(255, 130, 145, 255) },
    }};

    ImGui::TextColored(ImVec4(0.45F, 0.90F, 1.0F, 1.0F), "2D Empty & Sample Game Projects (projects/)");
    for (const auto& sp : kSampleProjects2D)
    {
        const std::string row_id = std::string(sp.label) + "##hub_sp_" + sp.path;
        if (ImGui::IconSelectable(row_id.c_str(),
                                  cd::ui::chroma::IconKind::kFileProject,
                                  sp.tint,
                                  false,
                                  sp.tag))
        {
            w.show_project_manager = false;
            (void)scene_ops::load_project_or_scene_from_disk(s, sp.path);
            studio_2d().stage.bind_starter_atlas(0U, 32U);
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.96F, 0.80F, 0.42F, 1.0F), "3D Rendering & Physics Showcases (showcases/)");
    constexpr std::array<DiskShowcaseItem, 8> kShowcases {{
        { "01  PBR Materials & Hair",        "showcases/01_pbr_materials_sss_hair/project.cdproj",         "PBR",  IM_COL32(245, 198, 92, 255) },
        { "02  Realtime GI & Sponza",        "showcases/02_realtime_gi_raytracing/project.cdproj",         "DDGI", IM_COL32(245, 198, 92, 255) },
        { "03  Atmosphere, Clouds & Ocean",  "showcases/03_atmosphere_clouds_ocean_world/project.cdproj",  "SKY",  IM_COL32(245, 198, 92, 255) },
        { "04  GPU-Driven Geometry",         "showcases/04_gpu_driven_geometry_textures/project.cdproj",   "GPU",  IM_COL32(245, 198, 92, 255) },
        { "05  Physics, Ragdoll & Fracture", "showcases/05_physics_destruction_fluids/project.cdproj",     "PHYS", IM_COL32(245, 198, 92, 255) },
        { "06  Post-FX, Optics & Camera",    "showcases/06_postfx_upscaling_camera/project.cdproj",        "POST", IM_COL32(245, 198, 92, 255) },
        { "07  Particles, Decals & Foliage", "showcases/07_particles_decals_foliage/project.cdproj",       "VFX",  IM_COL32(245, 198, 92, 255) },
        { "08  Gameplay & Skinned Avatars",  "showcases/10_gameplay_animation_networking/project.cdproj",  "GAME", IM_COL32(245, 198, 92, 255) },
    }};

    for (const auto& sc : kShowcases)
    {
        const std::string row_id = std::string(sc.label) + "##hub_sc_" + sc.path;
        if (ImGui::IconSelectable(row_id.c_str(),
                                  cd::ui::chroma::IconKind::kFileProject,
                                  sc.tint,
                                  false,
                                  sc.tag))
        {
            w.show_project_manager = false;
            cst.workspace_mode     = 0;
            (void)scene_ops::load_project_or_scene_from_disk(s, sc.path);
        }
    }

    ImGui::EndChild();

    // ---- Footer Option ----
    ImGui::Checkbox("Preload Starter Meshes & Textures in Project Context", &w.preload_starter_context_assets);
    ImGui::SameLine(0.0F, 24.0F);
    ImGui::TextDisabled("Active Project: %s  |  Level: %s",
                        w.active_project_name.c_str(), w.active_level_name.c_str());

    ImGui::End();
}

}  // namespace cd::studio::ui
