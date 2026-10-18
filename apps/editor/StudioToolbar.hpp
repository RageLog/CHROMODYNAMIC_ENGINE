// =============================================================================
// CHROMODYNAMIC STUDIO — StudioToolbar.hpp
//
// Godot-inspired top menu bar + quick creation/manipulation workbench for
// CHROMODYNAMIC 3D Engine Studio (editor.exe).
//
// Gives the user 1-click manual control over:
//   * 3D Object Creation (Cube, Sphere, Cylinder, Cone, Torus, PBR Spheres,
//     Skinned Animated Character) spawned directly in front of the camera
//   * Dynamic Light Creation (Point, Spot, Rect-Area, Directional Sun)
//   * Transform Gizmo Mode (Translate [W], Rotate [E], Scale [R]), Duplicate,
//     Delete, Undo/Redo, Focus Selected [F]
//   * Skeletal Animation Playback (Play/Pause, Speed, Timeline Scrubber)
//   * Sponza Atrium & Scene Camera Bookmarks
//   * Hardware Z-Buffer / G-Buffer / PBR Shading View Modes
//   * Play-In-Editor (PIE) Mode [F5 / F8]
// =============================================================================
#pragma once

#include "StudioModifierClasses.hpp"
#include "StudioCollisionMath.hpp"

#include <cd/editor/TransformCommands.hpp>
#include <cd/light/ColorTemperature.hpp>
#include <cd/light/Light.hpp>
#include <cd/math/Vector.hpp>
#include <cd/ui/ChromaUi.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace cd::studio
{

template <typename StateT>
[[nodiscard]] inline cd::math::Vec3f camera_spawn_point(const StateT& s, float dist = 3.5F) noexcept
{
    (void)s;
    (void)dist;
    return { 0.0F, 0.0F, 0.0F };
}

template <typename StateT, typename PrimKindT, typename LogFn>
inline void spawn_studio_entity(StateT& s,
                                const char* base_name,
                                PrimKindT kind,
                                cd::math::Vec3f tint,
                                bool is_pbr,
                                float metallic,
                                float roughness,
                                LogFn&& log_push)
{
    SceneEntity e;
    e.handle    = s.scene.create_node();
    e.name      = std::string { base_name } + " #" + std::to_string(s.entities.size() + 1);
    e.kind      = kind;
    e.tint      = tint;
    e.is_pbr    = is_pbr;
    e.metallic  = metallic;
    e.roughness = roughness;
    cd::world_container::assign_layer(s.ecs_world, e.handle, studio_world().active_layer_name);
    const auto pos = camera_spawn_point(s, 3.5F);
    if (auto* lt = s.scene.local(e.handle))
    {
        lt->value.position = pos;
        if (kind == PrimitiveKind::kGltf)
        {
            lt->value.scale    = { 2.0F, 2.0F, 2.0F };
            lt->value.rotation = { -0.7071068F, 0.0F, 0.0F, 0.7071068F };
        }
        else
        {
            lt->value.scale    = { 0.85F, 0.85F, 0.85F };
            lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
        }
    }
    auto& ms = studio_world().entity_modifiers[e.handle.id];
    ms.add_or_replace(std::make_unique<PbrMaterialModifier>(tint, metallic, roughness));
    if (kind == PrimitiveKind::kSphere)
        ms.add_or_replace(std::make_unique<SphereColliderModifier>(0.5F));
    else
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
    record_entity_home_transform(s, e.handle);
    s.entities.push_back(std::move(e));
    s.scene.update_transforms();
    s.selected      = static_cast<int>(s.entities.size()) - 1;
    s.selected_kind = SelKind::kEntity;
    log_push("[studio] spawned " + s.entities.back().name);
}

template <typename StateT, typename LogFn>
inline void duplicate_selected(StateT& s, LogFn&& log_push)
{
    if (s.selected < 0) return;
    if (s.selected_kind == SelKind::kEntity &&
        std::cmp_less(s.selected, s.entities.size()))
    {
        duplicate_entity_with_modifiers(s, s.selected, log_push);
    }
    else if (s.selected_kind == SelKind::kLight &&
             std::cmp_less(s.selected, s.lights.size()))
    {
        LightRow copy = s.lights[static_cast<std::size_t>(s.selected)];
        copy.name += " (Copy)";
        copy.light.position.x += 1.0F;
        s.lights.push_back(std::move(copy));
        s.selected = static_cast<int>(s.lights.size()) - 1;
        log_push("[studio] duplicated light -> " + s.lights.back().name);
    }
}

template <typename StateT, typename LogFn>
inline void delete_selected(StateT& s, LogFn&& log_push)
{
    if (s.selected < 0) return;
    auto& w = studio_world();
    if (s.selected_kind == SelKind::kEntity &&
        std::cmp_less(s.selected, s.entities.size()))
    {
        const auto& ent = s.entities[static_cast<std::size_t>(s.selected)];
        const std::string nm = ent.name;
        if (nm.find("Ground_Floor") != std::string::npos)
        {
            log_push("[studio] Ground_Floor is protected and cannot be deleted");
            return;
        }
        const auto hid = ent.handle;
        const bool is_clouds = (nm.find("Volumetric_Clouds") != std::string::npos) ||
                               (nm.find("Cloud") != std::string::npos) ||
                               (w.entity_modifiers[hid.id].template find<CloudLayerModifier>() != nullptr) ||
                               (w.entity_modifiers[hid.id].template find<SkyAtmosphereModifier>() != nullptr);
        if (is_clouds)
        {
            s.fx.clouds_coverage = 0.0F;
            if (auto* sky = w.environment_modifiers.template find<SkyAtmosphereModifier>())
                sky->clouds_coverage = 0.0F;
            if (auto* cld = w.environment_modifiers.template find<CloudLayerModifier>())
                cld->enabled = false;
        }
        if (w.pilot_scene_camera_idx == s.selected)
            w.pilot_scene_camera_idx = -1;
        else if (w.pilot_scene_camera_idx > s.selected)
            --w.pilot_scene_camera_idx;

        cd::world_container::clear_layer(s.ecs_world, hid);
        w.entity_modifiers.erase(hid.id);
        w.home_transforms.erase(hid.id);
        w.euler_cache_deg.erase(hid.id);
        s.scene.destroy_node(hid);
        s.entities.erase(s.entities.begin() + s.selected);
        s.selected = s.entities.empty() ? -1 : std::min(s.selected, static_cast<int>(s.entities.size()) - 1);
        s.last_tlas_scene_hash = 0;
        log_push("[studio] deleted entity: " + nm);
    }
    else if (s.selected_kind == SelKind::kLight &&
             std::cmp_less(s.selected, s.lights.size()))
    {
        const std::string nm = s.lights[static_cast<std::size_t>(s.selected)].name;
        s.lights.erase(s.lights.begin() + s.selected);
        if (s.lights.empty())
        {
            s.fx.clouds_coverage   = 0.0F;
            s.fx.volumetric_fog_on = false;
            if (auto* sky = w.environment_modifiers.template find<SkyAtmosphereModifier>())
            {
                sky->enabled         = false;
                sky->clouds_coverage = 0.0F;
            }
        }
        s.selected = s.lights.empty() ? -1 : std::min(s.selected, static_cast<int>(s.lights.size()) - 1);
        if (s.selected < 0 && !s.entities.empty())
        {
            s.selected      = 0;
            s.selected_kind = SelKind::kEntity;
        }
        s.last_tlas_scene_hash = 0;
        log_push("[studio] deleted light: " + nm);
    }
}

template <typename StateT, typename LogFn>
inline void focus_selected(StateT& s, LogFn&& log_push)
{
    if (s.selected < 0) return;
    cd::math::Vec3f tgt_pos {};
    std::string label;
    if (s.selected_kind == SelKind::kEntity &&
        std::cmp_less(s.selected, s.entities.size()))
    {
        const auto& ent = s.entities[static_cast<std::size_t>(s.selected)];
        const auto* lt  = s.scene.local(ent.handle);
        if (!lt) return;
        tgt_pos = lt->value.position;
        label   = ent.name;
    }
    else if (s.selected_kind == SelKind::kLight &&
             std::cmp_less(s.selected, s.lights.size()))
    {
        const auto& l = s.lights[static_cast<std::size_t>(s.selected)];
        tgt_pos = l.light.position;
        label   = l.name;
    }
    else
    {
        return;
    }
    const float dx = s.cam.eye.x - s.cam.target.x;
    const float dy = s.cam.eye.y - s.cam.target.y;
    const float dz = s.cam.eye.z - s.cam.target.z;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float dist = 3.5F;
    s.cam.target = tgt_pos;
    if (len > 1e-3F)
    {
        s.cam.eye = {
            tgt_pos.x + (dx / len) * dist,
            tgt_pos.y + std::max(0.6F, (dy / len) * dist),
            tgt_pos.z + (dz / len) * dist,
        };
    }
    s.app_state.free_look.manual_mode = true;
    s.scene_cam.set_auto_spin(false);
    log_push("[cam] focused on " + label);
}

template <typename StateT, typename LogFn>
inline void apply_camera_bookmark(StateT& s,
                                  cd::math::Vec3f eye,
                                  cd::math::Vec3f target,
                                  const char* name,
                                  LogFn&& log_push)
{
    s.cam.eye    = eye;
    s.cam.target = target;
    s.app_state.free_look.manual_mode = true;
    s.scene_cam.set_auto_spin(false);
    const float dx = target.x - eye.x;
    const float dy = target.y - eye.y;
    const float dz = target.z - eye.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist > 1e-3F)
    {
        s.app_state.free_look.dist  = dist;
        s.app_state.free_look.pitch = std::asin(dy / dist);
        s.app_state.free_look.yaw   = std::atan2(dx, -dz);
    }
    log_push(std::string { "[cam] bookmark: " } + name);
}

} // namespace cd::studio

#include "StudioModifierSystem.hpp"

namespace cd::studio
{

template <typename StateT, typename LogFn>
inline void draw_studio_main_menu_bar(StateT& s, float dt, LogFn&& log_push)
{
    // Keyboard shortcuts for Play/Stop (F5/F8), Export (F9), Duplicate (Ctrl+D) / Undo / Redo
    if (!ImGui::GetIO().WantCaptureKeyboard)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
        {
            if (!s.pie_playing) enter_pie_play_mode(s, log_push);
            else                exit_pie_play_mode(s, log_push);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F8, false) && s.pie_playing)
        {
            exit_pie_play_mode(s, log_push);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F9, false))
        {
            (void)export_standalone_game_package(s, "build/export/ChromodynamicGame", log_push);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        {
            (void)save_full_studio_scene(s, "studio_scene.cdscene.json", log_push);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
        {
            (void)load_full_studio_scene(s, "studio_scene.cdscene.json", log_push);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
        {
            duplicate_selected(s, log_push);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        {
            if (s.history.undo())
            {
                for (auto& ent : s.entities)
                    if (ent.name.find("CannonBall_") != 0)
                        record_entity_home_transform(s, ent.handle);
                s.scene.update_transforms();
                log_push("[undo] Ctrl+Z");
            }
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
        {
            if (s.history.redo())
            {
                for (auto& ent : s.entities)
                    if (ent.name.find("CannonBall_") != 0)
                        record_entity_home_transform(s, ent.handle);
                s.scene.update_transforms();
                log_push("[redo] Ctrl+Y");
            }
        }
    }

    if (!ImGui::BeginMainMenuBar())
        return;

    if (studio_world().show_project_manager)
    {
        const auto& w = studio_world();
        const double fps = (w.smoothed_fps > 1.0F)
            ? static_cast<double>(w.smoothed_fps)
            : ((dt > 1e-5F) ? (1.0 / static_cast<double>(dt)) : 60.0);
        ImGui::TextColored(ImVec4(0.35F, 0.85F, 1.0F, 1.0F), "CHROMODYNAMIC ENGINE  |  STUDIO HUB");
        char hub_telem[128]{};
        std::snprintf(hub_telem, sizeof(hub_telem), "Vulkan 1.3  •  C++23  |  %5.0f FPS", fps);
        const float telem_w = ImGui::CalcTextSize(hub_telem).x + 95.0F;
        ImGui::SameLine(std::max(320.0F, ImGui::GetWindowWidth() - telem_w));
        ImGui::TextColored(ImVec4(0.45F, 0.95F, 0.65F, 1.0F), "%s", hub_telem);
        ImGui::EndMainMenuBar();
        return;
    }

    ImGui::TextColored(ImVec4(0.35F, 0.85F, 1.0F, 1.0F), "CHROMODYNAMIC STUDIO");
    ImGui::Separator();

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Empty 3D Stage (Floor + Cube + Clouds + Camera + Sun)"))
        {
            reset_to_clean_empty_project(s, log_push, false);
        }
        if (ImGui::MenuItem("New Blank 3D Stage (Floor + Sun Only)"))
        {
            reset_to_clean_empty_project(s, log_push, true);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import Asset from OS Disk... (Mesh / Texture / Anim)"))
        {
            std::string picked_path {};
            const char* kAssetFilter =
                "All Studio Assets (*.obj;*.gltf;*.glb;*.mesh;*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.tex;*.anim;*.bvh)\0"
                "*.obj;*.gltf;*.glb;*.mesh;*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.tex;*.anim;*.bvh\0"
                "3D Models (*.obj;*.gltf;*.glb;*.mesh)\0*.obj;*.gltf;*.glb;*.mesh\0"
                "Textures (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.tex)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.tex\0"
                "Animations (*.anim;*.bvh;*.fbx)\0*.anim;*.bvh;*.fbx\0"
                "All Files (*.*)\0*.*\0";
            if (open_native_os_file_dialog(kAssetFilter, picked_path, "Import 3D Model, Texture or Animation from OS Disk"))
            {
                (void)import_or_assign_os_file(s, picked_path, true, log_push);
            }
        }
        if (ImGui::MenuItem("Open Scene or Project from OS Disk..."))
        {
            std::string picked_scene {};
            const char* kSceneFilter =
                "CHROMODYNAMIC Scenes & Projects (*.cdscene.json;*.cdproj;*.cdproject;*.json)\0"
                "*.cdscene.json;*.cdproj;*.cdproject;*.json\0"
                "All Files (*.*)\0*.*\0";
            if (open_native_os_file_dialog(kSceneFilter, picked_scene, "Open CHROMODYNAMIC Scene or Project"))
            {
                (void)load_project_or_scene_from_disk_path(s, picked_scene, log_push);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Scene (Full PBR + Node3D + Lights)", "Ctrl+S"))
        {
            (void)save_full_studio_scene(s, "studio_scene.cdscene.json", log_push);
        }
        if (ImGui::MenuItem("Load Scene (Full PBR + Node3D + Lights)", "Ctrl+O"))
        {
            (void)load_full_studio_scene(s, "studio_scene.cdscene.json", log_push);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export Standalone Game Package (.cdproj + .cdscene + Shaders)", "F9"))
        {
            (void)export_standalone_game_package(s, "build/export/ChromodynamicGame", log_push);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Command Palette...", "Ctrl+Shift+P"))
        {
            s.palette_visible = true;
            s.palette_query.clear();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, s.history.undo_depth() > 0))
        {
            if (s.history.undo())
            {
                for (auto& ent : s.entities)
                    if (ent.name.find("CannonBall_") != 0)
                        record_entity_home_transform(s, ent.handle);
                s.scene.update_transforms();
                log_push("[edit] undo");
            }
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, s.history.redo_depth() > 0))
        {
            if (s.history.redo())
            {
                for (auto& ent : s.entities)
                    if (ent.name.find("CannonBall_") != 0)
                        record_entity_home_transform(s, ent.handle);
                s.scene.update_transforms();
                log_push("[edit] redo");
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, s.selected >= 0))
        {
            duplicate_selected(s, log_push);
        }
        if (ImGui::MenuItem("Delete Selected", "Del", false, s.selected >= 0))
        {
            delete_selected(s, log_push);
        }
        if (ImGui::MenuItem("Focus Selected", "F", false, s.selected >= 0))
        {
            focus_selected(s, log_push);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Create 3D Object"))
    {
        if (ImGui::MenuItem("+ Cube (Mesh)"))
            spawn_studio_entity(s, "Cube", PrimitiveKind::kCube, { 0.90F, 0.35F, 0.25F }, true, 0.08F, 0.40F, log_push);
        if (ImGui::MenuItem("+ Sphere (Mesh)"))
            spawn_studio_entity(s, "Sphere", PrimitiveKind::kSphere, { 0.25F, 0.80F, 0.45F }, true, 0.08F, 0.35F, log_push);
        if (ImGui::MenuItem("+ Cylinder (Mesh)"))
            spawn_studio_entity(s, "Cylinder", PrimitiveKind::kCylinder, { 0.95F, 0.75F, 0.20F }, true, 0.12F, 0.38F, log_push);
        if (ImGui::MenuItem("+ Cone (Mesh)"))
            spawn_studio_entity(s, "Cone", PrimitiveKind::kCone, { 0.25F, 0.55F, 0.95F }, true, 0.08F, 0.40F, log_push);
        if (ImGui::MenuItem("+ Torus (Mesh)"))
            spawn_studio_entity(s, "Torus", PrimitiveKind::kTorus, { 0.85F, 0.30F, 0.85F }, true, 0.15F, 0.32F, log_push);
        ImGui::Separator();
        if (ImGui::MenuItem("+ Empty 3D Character Vessel (Playable WASD/Space + 6-State Animator)"))
            (void)spawn_empty_character_vessel(s, false, log_push);
        if (ImGui::MenuItem("+ Empty 2D Character Vessel (Planar 2D + 6-State Animator)"))
            (void)spawn_empty_character_vessel(s, true, log_push);
        if (ImGui::MenuItem("+ Scene Camera (3D Perspective)"))
            (void)spawn_scene_camera_entity(s, false, log_push);
        if (ImGui::MenuItem("+ Scene Camera (2D Orthographic)"))
            (void)spawn_scene_camera_entity(s, true, log_push);
        if (ImGui::MenuItem("+ Volumetric Clouds (Environment Layer)"))
            (void)spawn_volumetric_clouds_entity(s, log_push);
        ImGui::Separator();
        if (ImGui::MenuItem("+ PBR Chrome Mirror Sphere"))
            spawn_studio_entity(s, "PBR Chrome", PrimitiveKind::kSphere, { 0.95F, 0.93F, 0.88F }, true, 1.0F, 0.04F, log_push);
        if (ImGui::MenuItem("+ PBR Brushed Gold Sphere"))
            spawn_studio_entity(s, "PBR Gold", PrimitiveKind::kSphere, { 1.00F, 0.77F, 0.34F }, true, 1.0F, 0.18F, log_push);
        if (ImGui::MenuItem("+ PBR Glossy Ceramic Sphere"))
            spawn_studio_entity(s, "PBR Ceramic", PrimitiveKind::kSphere, { 0.15F, 0.65F, 0.95F }, true, 0.0F, 0.12F, log_push);
        ImGui::Separator();
        if (ImGui::MenuItem("+ Skinned Animated Character (CesiumMan)"))
            spawn_studio_entity(s, "CesiumMan", PrimitiveKind::kGltf, { 1.0F, 1.0F, 1.0F }, false, 0.0F, 0.8F, log_push);
        ImGui::Separator();
        if (ImGui::MenuItem("+ Prefab: 25 deg OBB Physics Ramp"))
            (void)spawn_entity_with_modifiers(s, "Static_Ramp", camera_spawn_point(s, 3.5F), log_push);
        if (ImGui::MenuItem("+ Prefab: 6x Dynamic Domino Row"))
            (void)spawn_entity_with_modifiers(s, "Prefab_DominoRow", camera_spawn_point(s, 3.5F), log_push);
        if (ImGui::MenuItem("+ Prefab: 5x Physics Crate Tower"))
            (void)spawn_entity_with_modifiers(s, "Prefab_CrateStack", camera_spawn_point(s, 3.5F), log_push);
        ImGui::Separator();
        if (ImGui::MenuItem("Open 250-Construct Library..."))
        {
            studio_world().focus_bottom_node_catalog_tab = true;
            log_push("[catalog] Opened 250-Construct Library");
        }
        if (ImGui::MenuItem("+ Vessel: CharacterBody3D (AvatarVessel3D #117)"))
            (void)spawn_or_compose_studio_catalog_node(s, 117, CatalogComposeMode::SpawnRootElement, camera_spawn_point(s, 3.5F), log_push);
        if (ImGui::MenuItem("+ Vessel: VehicleBody3D (RoverVessel3D #121)"))
            (void)spawn_or_compose_studio_catalog_node(s, 121, CatalogComposeMode::SpawnRootElement, camera_spawn_point(s, 3.5F), log_push);
        if (ImGui::MenuItem("+ SOTA: TurquinMultiScatterPBR3D (#221)"))
            (void)spawn_or_compose_studio_catalog_node(s, 221, CatalogComposeMode::SpawnRootElement, camera_spawn_point(s, 3.5F), log_push);
        if (ImGui::MenuItem("+ SOTA: SDFSmoothBooleanCSG3D (#226)"))
            (void)spawn_or_compose_studio_catalog_node(s, 226, CatalogComposeMode::SpawnRootElement, camera_spawn_point(s, 3.5F), log_push);
        if (ImGui::MenuItem("+ SOTA: DDGIIrradianceVolume3D / VoxelGI (#220)"))
            (void)spawn_or_compose_studio_catalog_node(s, 220, CatalogComposeMode::SpawnRootElement, camera_spawn_point(s, 3.5F), log_push);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Create Light"))
    {
        if (ImGui::MenuItem("+ Point Light (2700K Warm)"))
        {
            const auto pos = camera_spawn_point(s, 2.5F);
            s.lights.push_back({ "Point Light (2700K)",
                cd::light::point(pos, { 1, 1, 1 }, 2500.0F, 8.0F), true, 2700.0F });
            s.selected = static_cast<int>(s.lights.size()) - 1;
            s.selected_kind = SelKind::kLight;
            log_push("[studio] added Point Light");
        }
        if (ImGui::MenuItem("+ Spot Light (3200K Halogen)"))
        {
            const auto pos = camera_spawn_point(s, 2.5F);
            s.lights.push_back({ "Spot Light (3200K)",
                cd::light::spot(pos, { 0.0F, -1.0F, 0.0F }, { 1, 1, 1 }, 5000.0F, 10.0F, 0.30F, 0.50F),
                true, 3200.0F });
            s.selected = static_cast<int>(s.lights.size()) - 1;
            s.selected_kind = SelKind::kLight;
            log_push("[studio] added Spot Light");
        }
        if (ImGui::MenuItem("+ Rect Area Light (8000K Cyan)"))
        {
            const auto pos = camera_spawn_point(s, 2.5F);
            s.lights.push_back({ "Rect Area Light (8000K)",
                cd::light::rect_area(pos, { 0, 0, 1 }, { 1, 0, 0 }, 2.0F, 1.0F, { 0.6F, 0.85F, 1.0F }, 3000.0F),
                true, 8000.0F });
            s.selected = static_cast<int>(s.lights.size()) - 1;
            s.selected_kind = SelKind::kLight;
            log_push("[studio] added Rect Area Light");
        }
        if (ImGui::MenuItem("+ Directional Sun (6500K)"))
        {
            s.lights.push_back({ "Directional Sun (6500K)",
                cd::light::directional({ -0.35F, -0.65F, -0.50F }, { 1, 1, 1 }, 90000.0F),
                true, 6500.0F });
            s.selected = static_cast<int>(s.lights.size()) - 1;
            s.selected_kind = SelKind::kLight;
            log_push("[studio] added Directional Sun");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Camera & Bookmarks"))
    {
        if (ImGui::MenuItem("1. Studio Overview (Atrium + Character + PBR Grid)"))
            apply_camera_bookmark(s, { -7.5F, 2.2F, 1.2F }, { 0.0F, 1.5F, -0.2F }, "Studio Overview", log_push);
        if (ImGui::MenuItem("2. Atrium Main Entrance"))
            apply_camera_bookmark(s, { -11.5F, 1.6F, 0.0F }, { 0.0F, 1.6F, 0.0F }, "Atrium Entrance", log_push);
        if (ImGui::MenuItem("3. PBR Metallic/Roughness Sphere Grid Close-Up"))
            apply_camera_bookmark(s, { 0.0F, 2.1F, 4.2F }, { 0.0F, 1.9F, 0.0F }, "PBR Grid Close-Up", log_push);
        if (ImGui::MenuItem("4. Animated Character (CesiumMan) Close-Up"))
            apply_camera_bookmark(s, { -6.4F, 1.5F, 0.2F }, { -4.0F, 1.1F, -1.5F }, "CesiumMan Close-Up", log_push);
        if (ImGui::MenuItem("5. Atrium Upper Archway & Curtains"))
            apply_camera_bookmark(s, { 0.0F, 4.8F, -2.5F }, { 0.0F, 1.2F, 4.5F }, "Upper Archway", log_push);
        ImGui::Separator();
        if (ImGui::MenuItem("Focus Selected Object", "F", false, s.selected >= 0))
            focus_selected(s, log_push);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View & Z-Buffer"))
    {
        if (ImGui::MenuItem("Lit PBR + IBL + CSM Shadows", nullptr, s.fx.view_mode == 0))
            s.fx.view_mode = 0;
        if (ImGui::MenuItem("G-Buffer World Normals", nullptr, s.fx.view_mode == 1))
            s.fx.view_mode = 1;
        if (ImGui::MenuItem("G-Buffer Base Albedo", nullptr, s.fx.view_mode == 2))
            s.fx.view_mode = 2;
        if (ImGui::MenuItem("G-Buffer Metallic / Roughness", nullptr, s.fx.view_mode == 3))
            s.fx.view_mode = 3;
        if (ImGui::MenuItem("Hardware Z-Buffer Depth (D32_SFLOAT)", nullptr, s.fx.view_mode == 4))
            s.fx.view_mode = 4;
        ImGui::Separator();
        ImGui::MenuItem("DDGI Ray-Traced Diffuse GI", nullptr, &s.fx.ddgi_on);
        ImGui::MenuItem("Volumetric Fog (Wronski)", nullptr, &s.fx.volumetric_fog_on);
        ImGui::MenuItem("Editor XZ Floor Grid", nullptr, &s.show_editor_floor);
        ImGui::MenuItem("3D Transform Gizmo", "Space", &s.gizmo_state.visible);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("UI & Themes"))
    {
        auto& cst = cd::ui::chroma::state();
        if (ImGui::BeginMenu("Color Theme Presets (5)"))
        {
            if (ImGui::MenuItem("0: Chroma Carbon Dark (Default)", nullptr, cst.theme_index == 0))
                ImGui::ApplyTheme(0);
            if (ImGui::MenuItem("1: Chroma Slate Pro", nullptr, cst.theme_index == 1))
                ImGui::ApplyTheme(1);
            if (ImGui::MenuItem("2: Chroma Graphite", nullptr, cst.theme_index == 2))
                ImGui::ApplyTheme(2);
            if (ImGui::MenuItem("3: Cyberpunk Neon", nullptr, cst.theme_index == 3))
                ImGui::ApplyTheme(3);
            if (ImGui::MenuItem("4: High-Contrast Daylight", nullptr, cst.theme_index == 4))
                ImGui::ApplyTheme(4);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Workspace Layout Mode"))
        {
            if (ImGui::MenuItem("0: 3D Studio Editor (DockSpace)", nullptr, cst.workspace_mode == 0))
                cst.workspace_mode = 0;
            if (ImGui::MenuItem("1: In-Game HUD & 3D World UI", nullptr, cst.workspace_mode == 1))
                cst.workspace_mode = 1;
            if (ImGui::MenuItem("2: Standalone Node & Curve Lab", nullptr, cst.workspace_mode == 2))
                cst.workspace_mode = 2;
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Projects & Worlds"))
    {
        auto& w = studio_world();
        if (ImGui::MenuItem("Project Manager..."))
        {
            w.show_project_manager = true;
        }
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.45F, 0.90F, 1.0F, 1.0F),
                           "Active Project: %s", w.active_project_name.c_str());
        ImGui::TextDisabled("World: %s", w.active_world_name.c_str());
        ImGui::TextDisabled("Level: %s", w.active_level_name.c_str());
        ImGui::Separator();

        std::string deferred_menu_level {};
        if (const auto* proj = s.cd_world.project())
        {
            for (const auto& wname : proj->world_names())
            {
                if (ImGui::BeginMenu(wname.c_str()))
                {
                    for (std::size_t li : proj->levels_in_world(wname))
                    {
                        const auto* lvl = proj->level(li);
                        if (!lvl) continue;
                        const std::string lvl_nm { lvl->name() };
                        const bool is_cur = (w.active_level_name == lvl_nm);
                        if (ImGui::MenuItem(lvl_nm.c_str(), nullptr, is_cur))
                        {
                            deferred_menu_level = lvl_nm;
                        }
                    }
                    ImGui::EndMenu();
                }
            }
        }
        if (!deferred_menu_level.empty())
        {
            (void)switch_to_project_level(s, deferred_menu_level, log_push);
        }

        ImGui::Separator();
        if (ImGui::BeginMenu("Open Showcase Project (.cdproj) [12]"))
        {
            for (std::size_t si = 0; si < kShowcaseCatalog.size(); ++si)
            {
                const bool is_active_proj = (w.active_project_name == kShowcaseCatalog[si].id);
                if (ImGui::MenuItem(kShowcaseCatalog[si].title, kShowcaseCatalog[si].id, is_active_proj))
                {
                    load_showcase_project_by_index(s, si, log_push);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    const auto& w = studio_world();
    const double fps = (w.smoothed_fps > 1.0F)
        ? static_cast<double>(w.smoothed_fps)
        : ((dt > 1e-5F) ? (1.0 / static_cast<double>(dt)) : 60.0);
    const double frame_ms = (w.smoothed_frame_ms > 0.01F)
        ? static_cast<double>(w.smoothed_frame_ms)
        : static_cast<double>(dt * 1000.0F);
    char telem_buf[192]{};
    std::snprintf(telem_buf, sizeof(telem_buf),
                  "Entities: %zu | Lights: %zu | %5.0f FPS (%.2f ms)",
                  s.entities.size(), s.lights.size(), fps, frame_ms);
    const float telem_w = ImGui::CalcTextSize(telem_buf).x + 88.0F;
    ImGui::SameLine( std::max(540.0F, ImGui::GetWindowWidth() - telem_w) );
    ImGui::TextColored(ImVec4(0.45F, 0.95F, 0.65F, 1.0F), "%s", telem_buf);

    ImGui::EndMainMenuBar();
}

}  // namespace cd::studio
