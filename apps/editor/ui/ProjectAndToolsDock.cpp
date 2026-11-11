// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/ProjectAndToolsDock.cpp
// =============================================================================
#include "ui/ProjectAndToolsDock.hpp"

#include "pipeline/AssetIngest.hpp"
#include "scene/SceneOperations.hpp"
#include "StudioModifierClasses.hpp"
#include "StudioNodeCatalog.hpp"

#include <cd/ui/ChromaUi.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::studio::ui
{

namespace
{

struct BrowserAssetEntry
{
    std::string              name;
    std::string              path;
    int                      kind { 0 };  // 0 = Dir, 1 = Mesh, 2 = Texture, 3 = Material, 4 = Project/Scene
    cd::ui::chroma::IconKind icon { cd::ui::chroma::IconKind::kFolder };
    ImU32                    color { IM_COL32(200, 200, 200, 255) };
};

[[nodiscard]] bool contains_icase(std::string_view haystack, std::string_view needle) noexcept
{
    if (needle.empty())
    {
        return true;
    }
    const auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) noexcept {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

void build_browser_entries(const std::string& folder, std::vector<BrowserAssetEntry>& out)
{
    out.clear();
    out.reserve(64);

    if (folder == "showcases")
    {
        struct ShowcaseCard
        {
            const char*              name;
            const char*              path;
            cd::ui::chroma::IconKind icon;
            ImU32                    color;
        };
        constexpr std::array<ShowcaseCard, 10> kProjects {{
            { "01_PBR_Materials", "showcases/01_pbr_materials_sss_hair/project.cdproj",          cd::ui::chroma::IconKind::kSphere,   IM_COL32(255, 208, 88, 255) },
            { "02_Realtime_GI",   "showcases/02_realtime_gi_raytracing/project.cdproj",          cd::ui::chroma::IconKind::kWorld,    IM_COL32(255, 178, 95, 255) },
            { "03_Sky_Ocean",     "showcases/03_atmosphere_clouds_ocean_world/project.cdproj",   cd::ui::chroma::IconKind::kSparkles, IM_COL32(105, 215, 255, 255) },
            { "04_GPU_Geometry",  "showcases/04_gpu_driven_geometry_textures/project.cdproj",    cd::ui::chroma::IconKind::kMesh3D,   IM_COL32(95, 195, 255, 255) },
            { "05_Physics_Sim",   "showcases/05_physics_destruction_fluids/project.cdproj",      cd::ui::chroma::IconKind::kPhysics,  IM_COL32(85, 240, 165, 255) },
            { "06_PostFX_Optics", "showcases/06_postfx_upscaling_camera/project.cdproj",         cd::ui::chroma::IconKind::kCamera,   IM_COL32(205, 155, 255, 255) },
            { "07_VFX_Particles", "showcases/07_particles_decals_foliage/project.cdproj",        cd::ui::chroma::IconKind::kLight,    IM_COL32(255, 165, 95, 255) },
            { "08_Proc_Synth",    "showcases/08_procedural_asset_synthesis/project.cdproj",      cd::ui::chroma::IconKind::kTorus,    IM_COL32(235, 145, 245, 255) },
            { "09_Studio_Tools",  "showcases/09_editor_tools_observability/project.cdproj",      cd::ui::chroma::IconKind::kUiWidget, IM_COL32(115, 205, 255, 255) },
            { "10_Gameplay_Net",  "showcases/10_gameplay_animation_networking/project.cdproj",   cd::ui::chroma::IconKind::kAvatar,   IM_COL32(95, 245, 175, 255) },
        }};
        for (const auto& sp : kProjects)
        {
            out.push_back({ sp.name, sp.path, 4, sp.icon, sp.color });
        }
        std::error_code ec {};
        if (std::filesystem::exists("studio_scene.cdscene.json", ec))
        {
            out.push_back({
                "Saved_Studio_Scene",
                "studio_scene.cdscene.json",
                4,
                cd::ui::chroma::IconKind::kFileScene,
                IM_COL32(115, 225, 255, 255)
            });
        }
        return;
    }

    if (folder == "assets/meshes" || folder == "primitives")
    {
        out.push_back({ "Cube",      "assets/meshes/Cube.mesh",      1, cd::ui::chroma::IconKind::kCube,     IM_COL32(95, 195, 255, 255) });
        out.push_back({ "Sphere",    "assets/meshes/Sphere.mesh",    1, cd::ui::chroma::IconKind::kSphere,   IM_COL32(110, 220, 255, 255) });
        out.push_back({ "Cylinder",  "assets/meshes/Cylinder.mesh",  1, cd::ui::chroma::IconKind::kCylinder, IM_COL32(115, 210, 245, 255) });
        out.push_back({ "Cone",      "assets/meshes/Cone.mesh",      1, cd::ui::chroma::IconKind::kCone,     IM_COL32(245, 185, 110, 255) });
        out.push_back({ "Torus",     "assets/meshes/Torus.mesh",     1, cd::ui::chroma::IconKind::kTorus,    IM_COL32(205, 155, 255, 255) });
        out.push_back({ "TorusKnot", "assets/meshes/TorusKnot.mesh", 1, cd::ui::chroma::IconKind::kTorus,    IM_COL32(235, 155, 245, 255) });
    }
    else if (folder == "assets/textures")
    {
        out.push_back({ "Checker_2K",   "assets/textures/UV_Checkerboard_2K.tex", 2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(95, 235, 175, 255) });
        out.push_back({ "Gold_24K",     "assets/textures/Gold_Leaf_24K.tex",      2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(255, 215, 85, 255) });
        out.push_back({ "Carbon_Fiber", "assets/textures/Carbon_Fiber_Twill.tex", 2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(150, 185, 225, 255) });
        out.push_back({ "Marble_White", "assets/textures/Carrara_Marble.tex",     2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(220, 230, 245, 255) });
        out.push_back({ "Oak_Planks",   "assets/textures/Oak_Wood_Planks.tex",    2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(225, 175, 110, 255) });
        out.push_back({ "Earth_Albedo", "assets/textures/Earth_Albedo.png",       2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(95, 195, 255, 255) });
    }

    std::error_code ec {};
    if (std::filesystem::exists(folder, ec) && std::filesystem::is_directory(folder, ec))
    {
        std::size_t disk_count = 0U;
        for (const auto& dir_ent : std::filesystem::directory_iterator(folder, ec))
        {
            if (disk_count >= 128U)
            {
                break;
            }
            const auto p          = dir_ent.path();
            const std::string nm  = p.stem().string();
            const std::string ext = p.extension().string();
            const std::string fp  = p.generic_string();

            bool duplicate = false;
            for (const auto& existing : out)
            {
                if (existing.path == fp)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
            {
                continue;
            }

            if (dir_ent.is_directory(ec))
            {
                out.push_back({ nm, fp, 0, cd::ui::chroma::IconKind::kFolder, IM_COL32(245, 195, 95, 255) });
                ++disk_count;
            }
            else if (ext == ".cdproj" || ext == ".cdproject")
            {
                out.push_back({ nm, fp, 4, cd::ui::chroma::IconKind::kFileProject, IM_COL32(255, 198, 88, 255) });
                ++disk_count;
            }
            else if (ext == ".cdscene.json" || ext == ".json")
            {
                out.push_back({ nm, fp, 4, cd::ui::chroma::IconKind::kFileScene, IM_COL32(115, 220, 255, 255) });
                ++disk_count;
            }
            else if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".mesh")
            {
                out.push_back({ nm, fp, 1, cd::ui::chroma::IconKind::kMesh3D, IM_COL32(95, 195, 255, 255) });
                ++disk_count;
            }
            else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".tex")
            {
                out.push_back({ nm, fp, 2, cd::ui::chroma::IconKind::kTexture2D, IM_COL32(85, 235, 165, 255) });
                ++disk_count;
            }
            else if (ext == ".pbr" || ext == ".mat")
            {
                out.push_back({ nm, fp, 3, cd::ui::chroma::IconKind::kMaterial, IM_COL32(215, 155, 255, 255) });
                ++disk_count;
            }
        }
    }
}

void draw_assets_filesystem_tab(StudioEngineState& s)
{
    auto& w = studio_world();
    if (w.current_content_folder.empty())
    {
        w.current_content_folder = "assets/meshes";
    }

    static std::string                     s_cached_folder {};
    static std::vector<BrowserAssetEntry>  s_entries {};

    auto refresh_folder = [&](const std::string& folder)
    {
        w.current_content_folder = folder;
        s_cached_folder          = folder;
        build_browser_entries(folder, s_entries);
    };

    if (s_cached_folder != w.current_content_folder || s_entries.empty())
    {
        refresh_folder(w.current_content_folder);
    }

    // ---- Top Toolbar: [+ Import...] | [Up ..] | Path | Type Filter Pills | Search ----
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.48F, 0.78F, 1.0F));
    if (ImGui::Button("+ Import...", ImVec2(86.0F, 22.0F)))
    {
        std::string picked;
        if (scene_ops::open_os_file_dialog(
                "Assets (*.obj;*.gltf;*.glb;*.png;*.jpg;*.cdscene.json)\0*.obj;*.gltf;*.glb;*.png;*.jpg;*.jpeg;*.tga;*.cdscene.json\0All Files (*.*)\0*.*\0",
                picked,
                "Import Asset into Studio"))
        {
            (void)pipeline::import_or_assign_file(s, picked, false);
            refresh_folder(w.current_content_folder);
        }
    }
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0F, 6.0F);
    if (ImGui::Button("Up ..", ImVec2(50.0F, 22.0F)))
    {
        const auto par = std::filesystem::path(w.current_content_folder).parent_path();
        std::error_code ec {};
        if (!par.empty() && std::filesystem::exists(par, ec))
        {
            refresh_folder(par.generic_string());
        }
    }

    ImGui::SameLine(0.0F, 8.0F);
    ImGui::TextColored(ImVec4(0.55F, 0.85F, 1.0F, 1.0F), "res://%s", w.current_content_folder.c_str());

    ImGui::SameLine(0.0F, 14.0F);
    constexpr std::array<std::pair<const char*, int>, 5> kFilterPills {{
        { "All",      0 },
        { "Meshes",   1 },
        { "Textures", 2 },
        { "Materials",3 },
        { "Projects", 4 },
    }};
    for (std::size_t fi = 0; fi < kFilterPills.size(); ++fi)
    {
        if (fi > 0U)
        {
            ImGui::SameLine(0.0F, 4.0F);
        }
        const bool active = (w.content_type_filter == kFilterPills[fi].second);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.52F, 0.88F, 1.0F));
        }
        if (ImGui::SmallButton(kFilterPills[fi].first))
        {
            w.content_type_filter = kFilterPills[fi].second;
        }
        if (active)
        {
            ImGui::PopStyleColor();
        }
    }

    ImGui::SameLine(0.0F, 10.0F);
    ImGui::SetNextItemWidth(175.0F);
    ImGui::InputTextWithHint("##asset_search", "Search assets...", w.content_search_filter, sizeof(w.content_search_filter));
    if (w.content_search_filter[0] != '\0')
    {
        ImGui::SameLine(0.0F, 4.0F);
        if (ImGui::SmallButton("X##clr_as"))
        {
            w.content_search_filter[0] = '\0';
        }
    }

    ImGui::Separator();

    // ---- 2-Pane Split: Left Folder Tree (200px) + Right Responsive Asset Grid ----
    ImGui::BeginChild("fs_folder_tree", ImVec2(200.0F, 0.0F), true);
    ImGui::TextDisabled("FOLDERS");
    struct RootBookmark
    {
        const char*              label;
        const char*              folder;
        cd::ui::chroma::IconKind icon;
        ImU32                    col;
    };
    constexpr std::array<RootBookmark, 6> kBookmarks {{
        { "Meshes",          "assets/meshes",          cd::ui::chroma::IconKind::kMesh3D,      IM_COL32(95, 195, 255, 255) },
        { "Textures",        "assets/textures",        cd::ui::chroma::IconKind::kTexture2D,   IM_COL32(85, 235, 165, 255) },
        { "glTF Samples",    "assets/samples/Generic", cd::ui::chroma::IconKind::kFolder,      IM_COL32(245, 195, 95, 255) },
        { "Sponza Atrium",   "assets/samples/Sponza",  cd::ui::chroma::IconKind::kWorld,       IM_COL32(255, 205, 105, 255) },
        { "Assets Root",     "assets",                 cd::ui::chroma::IconKind::kFolder,      IM_COL32(235, 190, 95, 255) },
        { "Sample Projects", "showcases",              cd::ui::chroma::IconKind::kFileProject, IM_COL32(255, 198, 88, 255) },
    }};
    for (const auto& bm : kBookmarks)
    {
        const bool selected_bm = (w.current_content_folder == bm.folder);
        if (ImGui::IconSelectable(bm.label, bm.icon, bm.col, selected_bm))
        {
            refresh_folder(bm.folder);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0F, 8.0F);

    ImGui::BeginChild("fs_asset_grid", ImVec2(0.0F, 0.0F), false);
    const float avail_w   = std::max(120.0F, ImGui::GetContentRegionAvail().x);
    constexpr float kTileW = 92.0F;
    constexpr float kTileH = 74.0F;
    constexpr float kGapX  = 8.0F;
    const int cols = std::max(1, static_cast<int>((avail_w + kGapX) / (kTileW + kGapX)));

    // Copy snapshot so folder changes inside loop never invalidate iterators.
    const std::vector<BrowserAssetEntry> snapshot = s_entries;
    int col_idx = 0;

    for (std::size_t idx = 0; idx < snapshot.size(); ++idx)
    {
        const auto& item = snapshot[idx];
        if (w.content_type_filter != 0 && item.kind != 0 && item.kind != w.content_type_filter)
        {
            continue;
        }
        if (!contains_icase(item.name, w.content_search_filter))
        {
            continue;
        }

        if (col_idx > 0)
        {
            ImGui::SameLine(0.0F, kGapX);
        }

        ImGui::PushID(static_cast<int>(idx));
        const bool is_sel    = (w.selected_content_asset == item.path);
        const bool in_ctx    = w.is_asset_in_context(item.path);
        const std::string tile_lbl = item.name + "##tile";
        if (ImGui::IconTile(tile_lbl.c_str(), item.icon, item.color, is_sel, in_ctx, ImVec2(kTileW, kTileH)))
        {
            w.selected_content_asset = item.path;
            if (item.kind == 1 || item.kind == 2 || item.kind == 3)
            {
                const std::uint8_t ckind = (item.kind == 1)
                    ? std::uint8_t { 0U }
                    : (item.kind == 2 ? std::uint8_t { 1U } : std::uint8_t { 2U });
                (void)w.load_asset_into_context(item.name, item.path, ckind);
            }
        }

        // Double-click direct workflow: enter folder / open scene / spawn mesh / assign texture
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (item.kind == 0)
            {
                refresh_folder(item.path);
            }
            else if (item.kind == 4)
            {
                (void)scene_ops::load_project_or_scene_from_disk(s, item.path);
            }
            else if (item.kind == 1)
            {
                const int nidx = scene_ops::spawn_entity_by_preset(s, "Dynamic_Cube", { 0.0F, 0.5F, 0.0F });
                if (nidx >= 0)
                {
                    (void)pipeline::assign_mesh_to_entity(s, nidx, item.path);
                }
            }
            else if (item.kind == 2 && s.selected_kind == SelKind::kEntity &&
                     s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
            {
                (void)pipeline::assign_texture_to_entity(s, s.selected, item.path);
            }
        }

        // Drag-and-drop source for 3D Viewport and Inspector slots
        if ((item.kind == 1 || item.kind == 2) &&
            ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            if (item.kind == 1)
            {
                ImGui::SetDragDropPayload("CD_ASSET_MESH", item.path.c_str(), item.path.size() + 1U);
                w.is_dragging_asset_mesh = true;
                ImGui::Text("Mesh: %s", item.name.c_str());
            }
            else
            {
                ImGui::SetDragDropPayload("CD_ASSET_TEXTURE", item.path.c_str(), item.path.size() + 1U);
                w.is_dragging_asset_texture = true;
                ImGui::Text("Texture: %s", item.name.c_str());
            }
            ImGui::EndDragDropSource();
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem("asset_tile_ctx"))
        {
            ImGui::TextColored(ImVec4(0.45F, 0.88F, 1.0F, 1.0F), "%s", item.name.c_str());
            ImGui::Separator();
            if (item.kind == 0 && ImGui::MenuItem("Open Folder"))
            {
                refresh_folder(item.path);
            }
            if (item.kind == 4 && ImGui::MenuItem("Open Project / Scene"))
            {
                (void)scene_ops::load_project_or_scene_from_disk(s, item.path);
            }
            if (item.kind == 1)
            {
                if (ImGui::MenuItem("Spawn in 3D Scene"))
                {
                    const int nidx = scene_ops::spawn_entity_by_preset(s, "Dynamic_Cube", { 0.0F, 0.5F, 0.0F });
                    if (nidx >= 0)
                    {
                        (void)pipeline::assign_mesh_to_entity(s, nidx, item.path);
                    }
                }
                if (s.selected_kind == SelKind::kEntity &&
                    s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()) &&
                    ImGui::MenuItem("Assign Mesh to Selected"))
                {
                    (void)pipeline::assign_mesh_to_entity(s, s.selected, item.path);
                }
            }
            if (item.kind == 2 &&
                s.selected_kind == SelKind::kEntity &&
                s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()) &&
                ImGui::MenuItem("Assign Texture to Selected"))
            {
                (void)pipeline::assign_texture_to_entity(s, s.selected, item.path);
            }
            ImGui::EndPopup();
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s\nDouble-click to open/spawn, or drag onto Viewport/Inspector", item.path.c_str());
        }

        ImGui::PopID();

        ++col_idx;
        if (col_idx >= cols)
        {
            col_idx = 0;
        }
    }

    ImGui::EndChild();
}

void draw_console_tab(StudioEngineState& s)
{
    static char s_log_filter[96] {};
    if (ImGui::Button("Clear", ImVec2(60.0F, 22.0F)))
    {
        s.log.clear();
    }
    ImGui::SameLine(0.0F, 8.0F);
    ImGui::SetNextItemWidth(220.0F);
    ImGui::InputTextWithHint("##console_filter", "Filter log messages...", s_log_filter, sizeof(s_log_filter));
    ImGui::SameLine(0.0F, 10.0F);
    ImGui::TextDisabled("%zu entries", s.log.size());

    ImGui::Separator();
    ImGui::BeginChild("console_scroll_region", ImVec2(0.0F, 0.0F), false);
    for (const auto& line : s.log)
    {
        if (!contains_icase(line, s_log_filter))
        {
            continue;
        }
        if (line.find("error") != std::string::npos || line.find("FAIL") != std::string::npos)
        {
            ImGui::TextColored(ImVec4(1.0F, 0.42F, 0.42F, 1.0F), "%s", line.c_str());
        }
        else if (line.find("[pie]") != std::string::npos || line.find("[export]") != std::string::npos)
        {
            ImGui::TextColored(ImVec4(0.42F, 0.94F, 0.65F, 1.0F), "%s", line.c_str());
        }
        else
        {
            ImGui::TextUnformatted(line.c_str());
        }
    }
    ImGui::EndChild();
}

void draw_construct_catalog_tab(StudioEngineState& s)
{
    auto& w = studio_world();
    static int s_cat_filter = -1;

    struct CatPill
    {
        const char* label;
        int         cat;
    };
    constexpr std::array<CatPill, 6> kCats {{
        { "All (250)", -1 },
        { "Core",      static_cast<int>(StudioNodeCategory::CoreSystem) },
        { "2D Nodes",  static_cast<int>(StudioNodeCategory::Node2D) },
        { "3D Nodes",  static_cast<int>(StudioNodeCategory::Node3D) },
        { "UI",        static_cast<int>(StudioNodeCategory::ControlUI) },
        { "Next-Gen",  static_cast<int>(StudioNodeCategory::NextGenSOTA) },
    }};

    for (std::size_t ci = 0; ci < kCats.size(); ++ci)
    {
        if (ci > 0U)
        {
            ImGui::SameLine(0.0F, 4.0F);
        }
        const bool active = (s_cat_filter == kCats[ci].cat);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.50F, 0.86F, 1.0F));
        }
        if (ImGui::SmallButton(kCats[ci].label))
        {
            s_cat_filter = kCats[ci].cat;
        }
        if (active)
        {
            ImGui::PopStyleColor();
        }
    }

    ImGui::SameLine(0.0F, 10.0F);
    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputTextWithHint("##cat250_q", "Search constructs...", w.node_catalog_search, sizeof(w.node_catalog_search));

    const auto matches = filter_studio_nodes(s_cat_filter, w.node_catalog_search);
    ImGui::SameLine(0.0F, 8.0F);
    ImGui::TextDisabled("%zu constructs", matches.size());
    ImGui::Separator();

    ImGui::BeginChild("cat250_list", ImVec2(270.0F, 0.0F), true);
    for (const auto* desc : matches)
    {
        if (desc == nullptr)
        {
            continue;
        }
        const bool is_sel = (w.selected_catalog_node_id == desc->id);
        char badge[16] {};
        std::snprintf(badge, sizeof(badge), "#%03u", static_cast<unsigned>(desc->id));
        const std::string lbl = std::string(desc->chroma_name) + "##c250_" + std::to_string(desc->id);
        if (ImGui::IconSelectable(lbl.c_str(), cd::ui::chroma::IconKind::kNode,
                                  IM_COL32(105, 200, 255, 255), is_sel, badge))
        {
            w.selected_catalog_node_id = desc->id;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            (void)scene_ops::spawn_catalog_construct(s, desc->id, { 0.0F, 0.0F, 0.0F }, false);
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("CD_STUDIO_SPAWN", desc->chroma_name, std::strlen(desc->chroma_name) + 1U);
            w.is_dragging_spawn = true;
            w.drag_spawn_id     = desc->chroma_name;
            ImGui::Text("Spawn %s", desc->chroma_name);
            ImGui::EndDragDropSource();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0F, 10.0F);
    ImGui::BeginChild("cat250_detail", ImVec2(0.0F, 0.0F), false);
    if (const auto* sel = find_studio_node_by_id(w.selected_catalog_node_id))
    {
        ImGui::TextColored(ImVec4(0.45F, 0.88F, 1.0F, 1.0F), "[#%03u] %s",
                           static_cast<unsigned>(sel->id), sel->chroma_name);
        ImGui::TextDisabled("Kind: %s  |  Category: %s",
                            chroma_construct_kind_label(sel->construct_kind),
                            studio_node_category_label(sel->category));
        ImGui::Separator();
        ImGui::Text("UX: %s", sel->ux_upgrade != nullptr ? sel->ux_upgrade : "");
        ImGui::TextDisabled("Tech: %s", sel->tech_upgrade != nullptr ? sel->tech_upgrade : "");
        ImGui::Separator();
        if (ImGui::Button("+ Spawn in Scene", ImVec2(150.0F, 26.0F)))
        {
            (void)scene_ops::spawn_catalog_construct(s, sel->id, { 0.0F, 0.0F, 0.0F }, false);
        }
        if (s.selected_kind == SelKind::kEntity &&
            s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
        {
            ImGui::SameLine(0.0F, 8.0F);
            if (ImGui::Button("Attach to Selected", ImVec2(150.0F, 26.0F)))
            {
                (void)scene_ops::spawn_catalog_construct(s, sel->id, { 0.0F, 0.0F, 0.0F }, true);
            }
        }
    }
    ImGui::EndChild();
}

void draw_profiler_tab(StudioEngineState& s)
{
    auto& w = studio_world();

    const float avg_ms = (w.smoothed_fps > 0.1F) ? (1000.0F / w.smoothed_fps) : 0.0F;
    ImGui::TextColored(ImVec4(0.42F, 0.94F, 0.65F, 1.0F),
                       "%.1f FPS  (%.2f ms/frame)   |   Peak: %.0f FPS",
                       static_cast<double>(w.smoothed_fps),
                       static_cast<double>(avg_ms),
                       static_cast<double>(w.peak_fps));
    std::size_t dyn_bodies = 0U;
    for (const auto& ent : s.entities)
    {
        if (const auto* rb = w.entity_modifiers[ent.handle.id].find<RigidBodyModifier>();
            rb != nullptr && rb->enabled && rb->body_type == cd::physics::BodyType::kDynamic)
        {
            ++dyn_bodies;
        }
    }

    ImGui::SameLine(0.0F, 18.0F);
    ImGui::TextDisabled("Entities: %zu   |   Lights: %zu   |   Dynamic Bodies: %zu   |   Contacts: %zu",
                        s.entities.size(),
                        s.lights.size(),
                        dyn_bodies,
                        w.live_contacts.size());

    ImGui::PlotLines("##frame_ms_plot",
                     w.frame_ms_history.data(),
                     static_cast<int>(w.frame_ms_history.size()),
                     static_cast<int>(w.frame_ms_cursor),
                     "Frame Time (ms)",
                     0.0F,
                     33.3F,
                     ImVec2(-1.0F, 56.0F));

    ImGui::Checkbox("Show 3D Diagnostic Render Probes in Viewport", &w.show_diagnostic_probes);
}

}  // namespace

void draw_project_and_tools_dock(StudioEngineState& s)
{
    auto& w = studio_world();

    if (!ImGui::Begin("Project & Tools", nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        return;
    }

    if (w.focus_bottom_node_catalog_tab)
    {
        cd::ui::chroma::state().tab_map[ImGui::GetID("BottomDockTabBar")] = 2;
        w.focus_bottom_node_catalog_tab = false;
    }

    if (ImGui::BeginTabBar("BottomDockTabBar"))
    {
        if (ImGui::BeginTabItem("Assets & FileSystem"))
        {
            draw_assets_filesystem_tab(s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Console"))
        {
            draw_console_tab(s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Construct Catalog"))
        {
            draw_construct_catalog_tab(s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Profiler"))
        {
            draw_profiler_tab(s);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

}  // namespace cd::studio::ui
