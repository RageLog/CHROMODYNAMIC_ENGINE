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

#include "Studio2DWorkspace.hpp"
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
        const bool is_fog = (nm.find("Volumetric_Fog") != std::string::npos) ||
                            (nm.find("Fog") != std::string::npos) ||
                            (w.entity_modifiers[hid.id].template find<FogModifier>() != nullptr);
        if (is_fog)
        {
            s.fx.volumetric_fog_on = false;
            s.fx.fog_density       = 0.0F;
            if (auto* fg = w.environment_modifiers.template find<FogModifier>())
                fg->enabled = false;
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

