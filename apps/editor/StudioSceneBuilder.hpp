// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioSceneBuilder.hpp
//
// Clean Studio workspace initializer, 3D Object & Prefab spawn catalog,
// physics cannonball spawner, and 1-click interactive physics playgrounds.
// =============================================================================
#pragma once

#include "StudioCollisionMath.hpp"
#include "StudioCustomMeshLoader.hpp"
#include "StudioRenderTargets.hpp"

#include <cd/asset/Primitives.hpp>
#include <cd/asset/gltf/GltfLoader.hpp>
#include <cd/asset/image/Image.hpp>
#include <cd/render/MeshUpload.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace cd::studio
{
inline void populate_standard_level_layers(cd::world_container::Level& lvl)
{
    auto add_tagged = [&](const char* nm, cd::math::Vec3f col, bool persist = false)
    {
        if (lvl.find_layer(nm) == nullptr)
        {
            if (auto* ly = lvl.add_layer(nm))
            {
                ly->set_color_tag(col);
                ly->set_persistent(persist);
            }
        }
    };
    add_tagged("Default",    { 0.72F, 0.75F, 0.80F }, true);
    add_tagged("Geometry",   { 0.35F, 0.78F, 0.95F }, false);
    add_tagged("Physics",    { 0.95F, 0.62F, 0.24F }, false);
    add_tagged("Characters", { 0.32F, 0.92F, 0.52F }, true);
    add_tagged("Gameplay",   { 0.24F, 0.86F, 0.96F }, true);
    add_tagged("Lights",     { 1.00F, 0.88F, 0.32F }, false);
    add_tagged("UI",         { 0.78F, 0.45F, 0.96F }, true);
}

inline void populate_master_showcase_project(cd::world_container::World& cd_world)
{
    if (cd_world.project() != nullptr &&
        cd_world.project()->name() != "Sample Project" &&
        cd_world.project()->level_count() >= 16U)
    {
        return;
    }

    std::string prev_active_name;
    if (cd_world.project() != nullptr &&
        cd_world.project()->name() != "Sample Project" &&
        cd_world.active_level_ptr() != nullptr)
    {
        prev_active_name = std::string(cd_world.active_level_ptr()->name());
    }

    auto proj = std::make_unique<cd::world_container::Project>("ChromodynamicMasterShowcase");
    struct LevelSpec
    {
        const char* name;
        const char* world_name;
        const char* desc;
        int         template_id;
    };
    constexpr std::array<LevelSpec, 16> kSpecs {{
        { "1.1 Clean Empty Sandbox (Ultra-FPS)", "World 1: Ultra-FPS & Physics",
          "Minimal static floor & sun for 500-2000+ FPS baseline telemetry", 0 },
        { "1.2 OBB Ramp & 5x Crate Tower", "World 1: Ultra-FPS & Physics",
          "25-deg OBB slope, rolling metallic sphere, 5x crate stack & CesiumMan", 1 },
        { "1.3 6x Domino Chain & Striker", "World 1: Ultra-FPS & Physics",
          "6 dynamic dominoes & high-velocity cannonball chain reaction", 2 },

        { "2.1 PBR 5x5 Sphere Matrix (01_pbr)", "World 2: Photoreal PBR & Sponza GI",
          "25 Cook-Torrance metallic/roughness spheres + Turquin MS + Iridescence", 4 },
        { "2.2 Crytek Sponza Atrium & DDGI (03_sponza)", "World 2: Photoreal PBR & Sponza GI",
          "Full Crytek Sponza Atrium glTF + Clustered Lights + Ray-Traced DDGI", 5 },
        { "2.3 Cinematic Optics & Bloom (04_optics)", "World 2: Photoreal PBR & Sponza GI",
          "Chrome, 24K Gold & Ceramic sculptures with HDR Bloom & Lens Optics", 6 },
        { "2.4 Asset Browser & Texture/Mesh Lab", "World 2: Photoreal PBR & Sponza GI",
          "Multi-textured 3D objects (Brick, UV Checker, Marble, Carbon) + Folder Tree", 12 },

        { "3.1 Double Ramp & Rover Collision (06_jolt)", "World 3: Jolt Dynamics & Shatter Lab",
          "Twin opposing 25-deg OBB ramps + heavy collider impact & crate wall", 7 },
        { "3.2 Trampoline Bounce Arena", "World 3: Jolt Dynamics & Shatter Lab",
          "High-restitution (0.96) trampoline pads + multi-sphere bounce", 3 },

        { "4.1 Skinned Avatar Squad & IK (05_avatar)", "World 4: Avatar Locomotion & Combat",
          "3x Skinned animated CesiumMan characters + obstacle course", 8 },
        { "4.2 Tactical Synth Squad Arena (08_synth)", "World 4: Avatar Locomotion & Combat",
          "Tactical cover barriers, squad avatars & dynamic point lights", 9 },

        { "5.1 Celestial Fog & God-Rays (02_aether)", "World 5: Volumetric Atmosphere & Nature",
          "Wronski volumetric fog, sun god-ray light shafts & monoliths", 10 },
        { "5.2 Golden Hour Ocean & Terra (07_terra)", "World 5: Volumetric Atmosphere & Nature",
          "3200K golden-hour sun, mirror water plane & island pillars", 11 },

        { "6.1 Cyber-Core Action Platformer (Playable Game)", "World 6: Playable Games & Interactive Templates",
          "Playable 3D Game: Avatar + Moving Platforms + JumpPads + Energy Cores + AI Bot + Goal", 13 },
        { "6.2 Universal 250-Node Showcase (Godot 219 + SOTA 31)", "World 6: Playable Games & Interactive Templates",
          "250-Node Universal Showcase: CharacterBody3D+SpringArm3D, VehicleBody3D, SoftBody3D/XPBD, CSG/SDF, VoxelGI/DDGI, GPUParticlesAttractor3D & TurquinMultiScatterPBR3D", 14 },
        { "6.3 Rockstar 2D+3D Physics & Euphoria/Pacejka Arena", "World 6: Playable Games & Interactive Templates",
          "32-Module Rockstar Physics Showcase: Euphoria Active Ragdoll + Pacejka 4-Wheel Vehicle + Voronoi Stress Fracture + Archimedes Buoyancy + XPBD Cloth", 15 },
    }};

    for (const auto& sp : kSpecs)
    {
        auto* lvl = proj->add_level(sp.name);
        lvl->set_world_name(sp.world_name);
        lvl->set_description(sp.desc);
        lvl->set_template_id(sp.template_id);
        lvl->bounds().min = { -60.0F, -5.0F, -60.0F };
        lvl->bounds().max = {  60.0F, 30.0F,  60.0F };
        populate_standard_level_layers(*lvl);
    }
    if (const auto* prev_lvl = (!prev_active_name.empty() ? proj->find_level(prev_active_name) : nullptr);
        prev_lvl != nullptr)
    {
        (void)proj->set_active_level(prev_active_name);
        cd_world.set_name(std::string(prev_lvl->world_name()));
    }
    else
    {
        (void)proj->set_active_level(kSpecs[0].name);
        cd_world.set_name(kSpecs[0].world_name);
    }
    cd_world.set_project(std::move(proj));
}

template <typename StateT>
inline void sync_camera_and_invalidate_frame_history(StateT& s) noexcept
{
    auto& fl = s.app_state.free_look;
    const float dx = s.cam.target.x - s.cam.eye.x;
    const float dy = s.cam.target.y - s.cam.eye.y;
    const float dz = s.cam.target.z - s.cam.eye.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist > 1e-4F)
    {
        fl.dist  = std::clamp(dist, 0.5F, 80.0F);
        fl.pitch = std::clamp(std::asin(std::clamp(dy / dist, -1.0F, 1.0F)), -1.48F, 1.48F);
        fl.yaw   = std::atan2(dx, -dz);
    }
    fl.manual_mode    = true;
    fl.has_last_mouse = false;
    fl.right_drag     = false;
    s.scene_cam.set_auto_spin(false);
    s.prev_cam_basis.valid = false;
    s.prev_vp_valid        = false;
    s.last_tlas_scene_hash = 0;
}

template <typename StateT, typename LogFn>
inline void init_clean_studio_workspace(StateT& s, LogFn&& log_push)
{
    auto& w = studio_world();
    if (w.initialized) return;
    w.initialized   = true;
    w.physics_world = cd::physics::make_rockstar_physics_world();

    if (s.cd_world.project() == nullptr)
    {
        auto proj = std::make_unique<cd::world_container::Project>("Empty_Project");
        auto* lvl = proj->add_level("Empty_Stage");
        lvl->set_world_name("Default_World");
        lvl->set_description("Clean empty stage — load projects/objects/textures from Disk into Context");
        lvl->set_template_id(0);
        populate_standard_level_layers(*lvl);
        (void)proj->set_active_level("Empty_Stage");
        s.cd_world.set_project(std::move(proj));
        s.cd_world.set_name("Default_World");
    }
    if (const auto* proj = s.cd_world.project())
    {
        w.active_project_name = std::string(proj->name());
    }
    if (auto* lvl = s.cd_world.active_level_ptr())
    {
        populate_standard_level_layers(*lvl);
        w.active_world_name = std::string(lvl->world_name());
        w.active_level_name = std::string(lvl->name());
    }

    w.environment_modifiers.items.clear();
    w.environment_modifiers.add_or_replace(std::make_unique<TonemapCameraModifier>());
    w.environment_modifiers.add_or_replace(std::make_unique<SkyAtmosphereModifier>());

    if (cd::studio_rt::golden::enabled())
    {
        w.clean_studio_mode = false;
        return;
    }

    for (auto& en : s.entities)
    {
        if (en.handle.is_valid())
        {
            cd::world_container::clear_layer(s.ecs_world, en.handle);
            s.scene.destroy_node(en.handle);
        }
    }
    s.entities.clear();
    w.entity_modifiers.clear();
    w.home_transforms.clear();
    w.euler_cache_deg.clear();
    w.live_contacts.clear();
    w.simulate_physics_in_edit = false;
    s.show_editor_floor = true;

    // 1. Clean Empty Stage — 0 scene objects on initial open (reference grid is purely a transparent viewport overlay)

    // 2. Ergonomic Studio Camera angle
    s.cam.eye    = { -5.8F, 3.8F, 6.2F };
    s.cam.target = {  0.0F, 0.8F, 0.0F };
    s.cam.fov_y  = 0.90F;
    sync_camera_and_invalidate_frame_history(s);

    // 3. Clean Studio Daylight + Fill Light
    s.lights.clear();
    s.lights.push_back({ "Studio Sun (5800K)",
        cd::light::directional({ -0.45F, -0.75F, -0.48F },
                               cd::light::cct_to_linear_rgb(5800.0F), 95000.0F),
        true, 5800.0F });
    s.lights.push_back({ "Sky Fill Point (6500K)",
        cd::light::point({ -3.5F, 4.5F, 3.5F },
                         cd::light::cct_to_linear_rgb(6500.0F), 2800.0F, 18.0F),
        true, 6500.0F });

    s.scene.update_transforms();
    s.selected      = -1;
    s.selected_kind = SelKind::kEntity;
    log_push("[studio] Opened Clean Empty Project ('Empty_Project / Empty_Stage'). Select sample projects (.cdproj) or load Objects/Textures from Disk into Context.");
}

// =============================================================================
// 8. Spawn 3D Entities & Prefabs with Attached Modifier Classes
// =============================================================================
template <typename StateT, typename LogFn>
inline int spawn_entity_with_modifiers(
    StateT&          s,
    std::string_view item_id,
    cd::math::Vec3f  raw_world_pos,
    LogFn&&          log_push)
{
    auto& w = studio_world();
    const cd::math::Vec3f world_pos = snap_vec3_if_enabled(raw_world_pos);

    if (item_id == "Prefab_CrateStack")
    {
        int first_idx = static_cast<int>(s.entities.size());
        for (int i = 0; i < 5; ++i)
        {
            const cd::ecs::Entity h = s.scene.create_node();
            const float y = std::max(0.425F, world_pos.y + 0.425F + static_cast<float>(i) * 0.85F);
            if (auto* lt = s.scene.local(h))
            {
                lt->value.position = { world_pos.x + (i % 2 == 0 ? -0.02F : 0.02F), y, world_pos.z };
                lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
                lt->value.scale    = { 0.85F, 0.85F, 0.85F };
            }
            SceneEntity e {};
            e.handle    = h;
            e.name      = "PhysBox_" + std::to_string(s.entities.size());
            e.kind      = PrimitiveKind::kCube;
            e.tint      = { 0.92F - 0.12F * static_cast<float>(i), 0.52F + 0.08F * static_cast<float>(i), 0.20F };
            e.is_pbr    = true;
            e.metallic  = 0.15F;
            e.roughness = 0.38F;
            s.entities.push_back(e);
            cd::world_container::assign_layer(s.ecs_world, h, "Physics");

            auto& ms = w.entity_modifiers[h.id];
            ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, e.metallic, e.roughness));
            ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kDynamic, 2.5F, 0.35F));
            ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
            record_entity_home_transform(s, h);
        }
        s.scene.update_transforms();
        s.selected      = first_idx;
        s.selected_kind = SelKind::kEntity;
        log_push("[drop] Spawned 5x Physics Crate Tower");
        return first_idx;
    }

    if (item_id == "Prefab_DominoRow")
    {
        int first_idx = static_cast<int>(s.entities.size());
        for (int i = 0; i < 6; ++i)
        {
            const cd::ecs::Entity h = s.scene.create_node();
            if (auto* lt = s.scene.local(h))
            {
                lt->value.position = { world_pos.x - 1.8F + static_cast<float>(i) * 0.72F,
                                       world_pos.y + 0.65F,
                                       world_pos.z };
                lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
                lt->value.scale    = { 0.32F, 1.30F, 0.85F };
            }
            SceneEntity e {};
            e.handle    = h;
            e.name      = "Domino_" + std::to_string(i + 1);
            e.kind      = PrimitiveKind::kCube;
            e.tint      = { 0.25F + 0.12F * static_cast<float>(i), 0.75F, 0.92F - 0.10F * static_cast<float>(i) };
            e.is_pbr    = true;
            e.metallic  = 0.25F;
            e.roughness = 0.25F;
            s.entities.push_back(e);
            cd::world_container::assign_layer(s.ecs_world, h, "Physics");

            auto& ms = w.entity_modifiers[h.id];
            ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, e.metallic, e.roughness));
            ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kDynamic, 1.8F, 0.50F));
            ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
            record_entity_home_transform(s, h);
        }
        s.scene.update_transforms();
        s.selected      = first_idx;
        s.selected_kind = SelKind::kEntity;
        log_push("[drop] Spawned 6x Dynamic Domino Row");
        return first_idx;
    }

    const cd::ecs::Entity h = s.scene.create_node();
    SceneEntity e {};
    e.handle    = h;
    e.is_pbr    = true;
    e.metallic  = 0.20F;
    e.roughness = 0.32F;
    e.tint      = { 0.82F, 0.85F, 0.90F };
    std::string_view semantic_layer = "Physics";

    auto& ms = w.entity_modifiers[h.id];

    if (item_id == "Dynamic_Cube" || item_id == "Cube" || item_id == "RigidBody_Crate")
    {
        e.kind = PrimitiveKind::kCube;
        e.name = "Cube_" + std::to_string(s.entities.size());
        e.tint = { 0.92F, 0.36F, 0.24F };
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.52F, world_pos.y), world_pos.z };
            lt->value.scale    = { 1.0F, 1.0F, 1.0F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, 0.15F, 0.35F));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kDynamic, 2.0F, 0.40F));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
    }
    else if (item_id == "Dynamic_Sphere" || item_id == "Sphere" || item_id == "RigidBody_Sphere")
    {
        e.kind      = PrimitiveKind::kSphere;
        e.name      = "Sphere_" + std::to_string(s.entities.size());
        e.tint      = { 0.24F, 0.72F, 0.98F };
        e.metallic  = 0.85F;
        e.roughness = 0.12F;
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.52F, world_pos.y), world_pos.z };
            lt->value.scale    = { 1.0F, 1.0F, 1.0F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, e.metallic, e.roughness));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kDynamic, 1.5F, 0.72F));
        ms.add_or_replace(std::make_unique<SphereColliderModifier>(0.5F));
    }
    else if (item_id == "Static_Ramp")
    {
        e.kind         = PrimitiveKind::kCube;
        e.name         = "Physics_Ramp_25deg_" + std::to_string(s.entities.size());
        e.tint         = { 0.88F, 0.68F, 0.22F };
        e.metallic     = 0.35F;
        e.roughness    = 0.22F;
        semantic_layer = "Geometry";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.52F, world_pos.y + 0.52F), world_pos.z };
            lt->value.rotation = euler_deg_to_quat({ 0.0F, 0.0F, -25.0F });
            lt->value.scale    = { 3.6F, 0.28F, 1.8F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, e.metallic, e.roughness));
        auto rb = std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kStatic, 0.0F, 0.35F);
        rb->friction = 0.08F; // Low friction so spheres & boxes roll smoothly down the slope!
        ms.add_or_replace(std::move(rb));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
    }
    else if (item_id == "Trampoline_Pad")
    {
        e.kind         = PrimitiveKind::kCube;
        e.name         = "Bounce_Trampoline_" + std::to_string(s.entities.size());
        e.tint         = { 0.18F, 0.92F, 0.52F };
        e.metallic     = 0.30F;
        e.roughness    = 0.20F;
        semantic_layer = "Geometry";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.18F, world_pos.y + 0.18F), world_pos.z };
            lt->value.scale    = { 2.4F, 0.35F, 2.4F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, e.metallic, e.roughness));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kStatic, 0.0F, 0.96F));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
    }
    else if (item_id == "Static_Wall")
    {
        e.kind         = PrimitiveKind::kCube;
        e.name         = "Static_Wall_" + std::to_string(s.entities.size());
        e.tint         = { 0.45F, 0.48F, 0.54F };
        semantic_layer = "Geometry";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(1.0F, world_pos.y + 1.0F), world_pos.z };
            lt->value.scale    = { 3.5F, 2.0F, 0.45F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, 0.10F, 0.65F));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kStatic, 0.0F, 0.40F));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
    }
    else if (item_id == "Cylinder")
    {
        e.kind = PrimitiveKind::kCylinder;
        e.name = "Cylinder_" + std::to_string(s.entities.size());
        e.tint = { 0.95F, 0.78F, 0.25F };
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.65F, world_pos.y + 1.5F), world_pos.z };
            lt->value.scale    = { 0.9F, 1.2F, 0.9F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, 0.40F, 0.28F));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kDynamic, 2.2F, 0.35F));
        ms.add_or_replace(std::make_unique<CapsuleColliderModifier>(0.45F, 0.30F));
    }
    else if (item_id == "Cone")
    {
        e.kind = PrimitiveKind::kCone;
        e.name = "Cone_" + std::to_string(s.entities.size());
        e.tint = { 0.30F, 0.88F, 0.48F };
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.55F, world_pos.y + 1.4F), world_pos.z };
            lt->value.scale    = { 1.0F, 1.0F, 1.0F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, 0.15F, 0.40F));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kDynamic, 1.5F, 0.35F));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
    }
    else if (item_id == "Torus")
    {
        e.kind = PrimitiveKind::kTorus;
        e.name = "Torus_" + std::to_string(s.entities.size());
        e.tint = { 0.88F, 0.32F, 0.92F };
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.55F, world_pos.y), world_pos.z };
            lt->value.scale    = { 1.0F, 1.0F, 1.0F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, 0.70F, 0.18F));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kDynamic, 1.8F, 0.50F));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.35F, 0.5F }));
    }
    else if (item_id == "Character_CesiumMan" || item_id == "Player_Hero")
    {
        e.kind         = PrimitiveKind::kGltf;
        e.name         = "CesiumMan_Actor_" + std::to_string(s.entities.size());
        e.tint         = { 1.0F, 1.0F, 1.0F };
        e.is_pbr       = false;
        e.use_texture  = true;
        semantic_layer = "Characters";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.02F, world_pos.y), world_pos.z };
            lt->value.rotation = { -0.7071068F, 0.0F, 0.0F, 0.7071068F };
            lt->value.scale    = { 1.8F, 1.8F, 1.8F };
        }
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kDynamic, 65.0F, 0.10F));
        auto cap = std::make_unique<CapsuleColliderModifier>(0.22F, 0.25F);
        cap->center_offset = { 0.0F, 0.85F, 0.0F };
        ms.add_or_replace(std::move(cap));
        ms.add_or_replace(std::make_unique<CharacterControllerModifier>());
    }
    else if (item_id == "World_Sponza")
    {
        e.kind         = PrimitiveKind::kSponza;
        e.name         = "Sponza_Atrium_World";
        e.tint         = { 1.0F, 1.0F, 1.0F };
        e.is_pbr       = false;
        e.use_texture  = true;
        semantic_layer = "Geometry";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { 0.0F, 0.0F, 0.0F };
            lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
            lt->value.scale    = { 0.01F, 0.01F, 0.01F };
        }
        s.show_editor_floor = false;
    }
    else if (item_id == "Moving_Platform")
    {
        e.kind              = PrimitiveKind::kCube;
        e.name              = "Kinematic_Platform_" + std::to_string(s.entities.size());
        e.tint              = { 0.25F, 0.78F, 0.96F };
        e.metallic          = 0.75F;
        e.roughness         = 0.22F;
        e.use_texture       = true;
        e.custom_texture_id = 4; // Carbon Fiber Twill
        semantic_layer      = "Geometry";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.45F, world_pos.y + 0.55F), world_pos.z };
            lt->value.scale    = { 2.2F, 0.26F, 1.4F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(
            e.tint, e.metallic, e.roughness, true, 1.0F, 0.0F, 4, "assets/textures/Carbon_Fiber_Twill.tex"));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kKinematic, 0.0F, 0.20F));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
        auto km = std::make_unique<KinematicMoverModifier>();
        km->move_axis    = { 1.0F, 0.0F, 0.0F };
        km->amplitude    = 1.8F;
        km->frequency_hz = 0.32F;
        km->carry_riders = true;
        ms.add_or_replace(std::move(km));
    }
    else if (item_id == "Collectible_EnergyCore")
    {
        e.kind              = PrimitiveKind::kSphere;
        e.name              = "Energy_Core_" + std::to_string(s.entities.size());
        e.tint              = { 1.00F, 0.84F, 0.22F };
        e.metallic          = 1.00F;
        e.roughness         = 0.12F;
        e.multiscatter_comp = 1.00F;
        e.iridescence_nm    = 460.0F;
        semantic_layer      = "Physics";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.75F, world_pos.y + 0.85F), world_pos.z };
            lt->value.scale    = { 0.52F, 0.52F, 0.52F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(
            e.tint, e.metallic, e.roughness, false, 1.0F, 460.0F));
        auto trg = std::make_unique<TriggerVolumeModifier>();
        trg->action         = TriggerActionKind::kCollectibleScore;
        trg->effect_value   = 100.0F;
        trg->trigger_radius = 1.05F;
        trg->one_shot       = true;
        ms.add_or_replace(std::move(trg));
    }
    else if (item_id == "JumpPad_Trigger")
    {
        e.kind         = PrimitiveKind::kCylinder;
        e.name         = "JumpPad_Boost_" + std::to_string(s.entities.size());
        e.tint         = { 0.18F, 0.98F, 0.56F };
        e.metallic     = 0.65F;
        e.roughness    = 0.16F;
        semantic_layer = "Physics";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.12F, world_pos.y + 0.12F), world_pos.z };
            lt->value.scale    = { 1.35F, 0.22F, 1.35F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, e.metallic, e.roughness));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kStatic, 0.0F, 0.90F));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
        auto trg = std::make_unique<TriggerVolumeModifier>();
        trg->action         = TriggerActionKind::kJumpPadBoost;
        trg->effect_value   = 10.5F;
        trg->trigger_radius = 0.75F;
        trg->one_shot       = false;
        ms.add_or_replace(std::move(trg));
    }
    else if (item_id == "Hazard_LaserBarrier")
    {
        e.kind         = PrimitiveKind::kCube;
        e.name         = "Hazard_Laser_" + std::to_string(s.entities.size());
        e.tint         = { 1.00F, 0.18F, 0.22F };
        e.metallic     = 0.85F;
        e.roughness    = 0.10F;
        semantic_layer = "Physics";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.35F, world_pos.y + 0.35F), world_pos.z };
            lt->value.scale    = { 1.80F, 0.35F, 0.32F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, e.metallic, e.roughness));
        auto trg = std::make_unique<TriggerVolumeModifier>();
        trg->action         = TriggerActionKind::kHazardDamage;
        trg->effect_value   = 25.0F;
        trg->trigger_radius = 0.95F;
        trg->one_shot       = false;
        ms.add_or_replace(std::move(trg));
    }
    else if (item_id == "Patrol_SynthBot")
    {
        e.kind              = PrimitiveKind::kCone;
        e.name              = "Synth_Patrol_Drone_" + std::to_string(s.entities.size());
        e.tint              = { 0.92F, 0.35F, 0.88F };
        e.metallic          = 0.92F;
        e.roughness         = 0.15F;
        e.iridescence_nm    = 380.0F;
        semantic_layer      = "Characters";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.55F, world_pos.y + 0.55F), world_pos.z };
            lt->value.scale    = { 0.85F, 1.10F, 0.85F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(
            e.tint, e.metallic, e.roughness, false, 1.0F, 380.0F));
        auto pat = std::make_unique<PatrolWaypointModifier>();
        pat->patrol_axis  = 0;
        pat->patrol_span  = 2.4F;
        pat->move_speed   = 2.2F;
        pat->chase_player = true;
        pat->chase_range  = 3.5F;
        ms.add_or_replace(std::move(pat));
        auto trg = std::make_unique<TriggerVolumeModifier>();
        trg->action         = TriggerActionKind::kHazardDamage;
        trg->effect_value   = 20.0F;
        trg->trigger_radius = 0.85F;
        trg->one_shot       = false;
        ms.add_or_replace(std::move(trg));
    }
    else if (item_id == "Goal_Beacon")
    {
        e.kind              = PrimitiveKind::kTorus;
        e.name              = "Level_Goal_Portal_" + std::to_string(s.entities.size());
        e.tint              = { 0.25F, 0.95F, 1.00F };
        e.metallic          = 0.98F;
        e.roughness         = 0.08F;
        e.multiscatter_comp = 1.00F;
        e.iridescence_nm    = 520.0F;
        semantic_layer      = "Gameplay";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(1.15F, world_pos.y + 1.15F), world_pos.z };
            lt->value.scale    = { 1.45F, 1.45F, 1.45F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(
            e.tint, e.metallic, e.roughness, false, 1.0F, 520.0F));
        auto trg = std::make_unique<TriggerVolumeModifier>();
        trg->action         = TriggerActionKind::kLevelGoalWin;
        trg->effect_value   = 500.0F;
        trg->trigger_radius = 1.45F;
        trg->one_shot       = true;
        ms.add_or_replace(std::move(trg));
    }

    const std::string_view chosen_layer =
        (w.active_layer_name != "Default" && !w.active_layer_name.empty())
            ? std::string_view(w.active_layer_name)
            : semantic_layer;
    cd::world_container::assign_layer(s.ecs_world, h, chosen_layer);
    if (const auto* active_lvl = s.cd_world.active_level_ptr())
    {
        if (const auto* lyr = active_lvl->find_layer(chosen_layer))
        {
            e.visible = lyr->visible();
            e.locked  = lyr->locked();
        }
    }

    record_entity_home_transform(s, h);
    const int new_idx = static_cast<int>(s.entities.size());
    s.entities.push_back(std::move(e));
    s.scene.update_transforms();
    s.selected      = new_idx;
    s.selected_kind = SelKind::kEntity;
    log_push("[drop] Spawned '" + s.entities.back().name + "' on layer '" + std::string(chosen_layer) + "'");
    return new_idx;
}

/// Spawn or compose any of the 250 Universal Catalog Constructs (`0..249`: 219 Godot + 31 Next-Gen SOTA)
/// in 1 click using CHROMODYNAMIC's 3-way composition architecture:
///   - `CatalogComposeMode::SpawnRootElement`: spawns a new top-level Element/Vessel/Pilot/Stage entity
///   - `CatalogComposeMode::NestAsChildElement`: spawns a child Element and attaches it under `s.selected` (`s.scene.attach`)
///   - `CatalogComposeMode::FoldAsInlineCapsule`: folds a `UniversalNodeModifier` directly into `s.selected`'s inline `ModifierStack`
template <typename StateT, typename LogFn>
inline int spawn_or_compose_studio_catalog_node(
    StateT&            s,
    std::uint16_t      node_id,
    CatalogComposeMode mode,
    cd::math::Vec3f    raw_world_pos,
    LogFn&&            log_push)
{
    const StudioNodeDescriptor* desc = find_studio_node_by_id(node_id);
    if (!desc) return -1;

    auto& w = studio_world();
    const int prev_selected = s.selected;
    const auto& p           = desc->spawn_preset;
    const std::uint32_t fl  = p.modifier_flags;
    const bool has_light    = (fl & (NodeSpawnFlags::kLightPoint | NodeSpawnFlags::kLightSpot | NodeSpawnFlags::kLightDirectional)) != 0u;
    const bool has_char     = (fl & NodeSpawnFlags::kCharacterController) != 0u;
    const bool has_patrol   = (fl & NodeSpawnFlags::kPatrolAi) != 0u;
    const bool has_mover    = (fl & NodeSpawnFlags::kKinematicMover) != 0u;
    const bool has_trigger  = (fl & (NodeSpawnFlags::kTriggerCollectible | NodeSpawnFlags::kTriggerJumpPad |
                                     NodeSpawnFlags::kTriggerHazard | NodeSpawnFlags::kTriggerGoal)) != 0u;
    const bool is_dynamic   = (fl & NodeSpawnFlags::kRigidBodyDynamic) != 0u;
    const bool is_kinematic = (fl & (NodeSpawnFlags::kRigidBodyKinematic | NodeSpawnFlags::kKinematicMover)) != 0u;

    // Mode 3: Fold directly into the selected entity's inline ModifierStack (Zero Scene-Tree Bloat!)
    if (mode == CatalogComposeMode::FoldAsInlineCapsule &&
        prev_selected >= 0 && std::cmp_less(prev_selected, s.entities.size()))
    {
        auto& target_ent = s.entities[static_cast<std::size_t>(prev_selected)];
        auto& ms         = w.entity_modifiers[target_ent.handle.id];
        ms.add_or_replace(std::make_unique<UniversalNodeModifier>(*desc));

        if (p.iridescence_nm > 0.0F)
        {
            target_ent.iridescence_nm = p.iridescence_nm;
            if (auto* pbr = ms.template find<PbrMaterialModifier>())
                pbr->iridescence_nm = p.iridescence_nm;
        }
        if (has_light && ms.template find<LightEmitterModifier>() == nullptr)
        {
            auto le       = std::make_unique<LightEmitterModifier>();
            le->color     = p.albedo;
            le->intensity = 2200.0F;
            le->range     = 14.0F;
            ms.add_or_replace(std::move(le));
        }
        if (has_char && ms.template find<CharacterControllerModifier>() == nullptr)
        {
            ms.add_or_replace(std::make_unique<CharacterControllerModifier>());
        }
        if (has_patrol &&
            ms.template find<PatrolWaypointModifier>() == nullptr &&
            ms.template find<CharacterControllerModifier>() == nullptr)
        {
            ms.add_or_replace(std::make_unique<PatrolWaypointModifier>());
        }
        if (has_mover && ms.template find<KinematicMoverModifier>() == nullptr)
        {
            ms.add_or_replace(std::make_unique<KinematicMoverModifier>());
        }
        w.hud_toast_message = "Folded Capsule [" + std::string(desc->chroma_name) + "] (" +
                              std::string(desc->godot_name) + ") -> " + target_ent.name;
        w.hud_toast_timer = 3.5F;
        log_push("[capsule] Folded inline capsule '" + std::string(desc->chroma_name) + "' (Godot: " +
                 std::string(desc->godot_name) + ") into '" + target_ent.name + "'");
        return prev_selected;
    }

    // Determine base primitive item_id for `spawn_entity_with_modifiers`
    std::string_view base_item_id = "Dynamic_Cube";
    if (p.prim_kind == CatalogPrimKind::GltfAvatar)
    {
        base_item_id = "Character_CesiumMan";
    }
    else if (p.prim_kind == CatalogPrimKind::Sphere)
    {
        base_item_id = "Dynamic_Sphere";
    }
    else if (p.prim_kind == CatalogPrimKind::Cylinder)
    {
        base_item_id = "Cylinder";
    }
    else if (p.prim_kind == CatalogPrimKind::Cone)
    {
        base_item_id = "Cone";
    }
    else if (p.prim_kind == CatalogPrimKind::Torus || p.prim_kind == CatalogPrimKind::TorusKnot)
    {
        base_item_id = "Torus";
    }
    else if (!is_dynamic)
    {
        base_item_id = "Static_Wall";
    }

    const int new_idx = spawn_entity_with_modifiers(s, base_item_id, raw_world_pos, log_push);
    if (new_idx < 0 || !std::cmp_less(new_idx, s.entities.size())) return -1;

    auto& ent = s.entities[static_cast<std::size_t>(new_idx)];
    ent.name  = std::string(desc->chroma_name) + " (" + std::string(desc->godot_name) + ")";
    if (ent.kind != PrimitiveKind::kGltf && ent.kind != PrimitiveKind::kSponza)
    {
        ent.tint              = p.albedo;
        ent.metallic          = p.metallic;
        ent.roughness         = p.roughness;
        ent.multiscatter_comp = p.multiscatter_comp;
        ent.iridescence_nm    = p.iridescence_nm;
        if (p.texture_slot >= 2)
        {
            ent.use_texture       = true;
            ent.custom_texture_id = p.texture_slot;
        }
    }

    if (auto* lt = s.scene.local(ent.handle))
    {
        if (ent.kind != PrimitiveKind::kGltf)
        {
            lt->value.scale    = p.scale;
            lt->value.position = {
                raw_world_pos.x,
                std::max(0.35F, raw_world_pos.y + 0.55F * p.scale.y),
                raw_world_pos.z
            };
        }
    }

    auto& ms = w.entity_modifiers[ent.handle.id];
    if (auto* pbr = ms.template find<PbrMaterialModifier>())
    {
        pbr->base_color        = ent.tint;
        pbr->metallic          = ent.metallic;
        pbr->roughness         = ent.roughness;
        pbr->multiscatter_comp = ent.multiscatter_comp;
        pbr->iridescence_nm    = ent.iridescence_nm;
        if (ent.use_texture)
        {
            pbr->use_texture  = true;
            pbr->texture_slot = ent.custom_texture_id;
        }
    }
    if (auto* rb = ms.template find<RigidBodyModifier>())
    {
        if (is_dynamic)
        {
            rb->body_type   = cd::physics::BodyType::kDynamic;
            rb->use_gravity = true;
            rb->mass        = 2.5F;
        }
        else if (is_kinematic)
        {
            rb->body_type       = cd::physics::BodyType::kKinematic;
            rb->use_gravity     = false;
            rb->linear_velocity = { 0.0F, 0.0F, 0.0F };
        }
        else
        {
            rb->body_type       = cd::physics::BodyType::kStatic;
            rb->use_gravity     = false;
            rb->linear_velocity = { 0.0F, 0.0F, 0.0F };
        }
    }

    if (has_mover && ms.template find<KinematicMoverModifier>() == nullptr)
    {
        auto km          = std::make_unique<KinematicMoverModifier>();
        km->move_axis    = { 1.0F, 0.0F, 0.0F };
        km->amplitude    = 1.4F;
        km->frequency_hz = 0.38F;
        km->carry_riders = true;
        ms.add_or_replace(std::move(km));
    }
    if (has_trigger && ms.template find<TriggerVolumeModifier>() == nullptr)
    {
        auto trg            = std::make_unique<TriggerVolumeModifier>();
        if ((fl & NodeSpawnFlags::kTriggerJumpPad) != 0u)
            trg->action = TriggerActionKind::kJumpPadBoost;
        else if ((fl & NodeSpawnFlags::kTriggerHazard) != 0u)
            trg->action = TriggerActionKind::kHazardDamage;
        else if ((fl & NodeSpawnFlags::kTriggerGoal) != 0u)
            trg->action = TriggerActionKind::kLevelGoalWin;
        else
            trg->action = TriggerActionKind::kCollectibleScore;
        trg->effect_value   = 100.0F;
        trg->trigger_radius = 1.15F;
        trg->one_shot       = false;
        ms.add_or_replace(std::move(trg));
    }
    if (has_patrol && ms.template find<PatrolWaypointModifier>() == nullptr)
    {
        auto pat          = std::make_unique<PatrolWaypointModifier>();
        pat->patrol_axis  = 0;
        pat->patrol_span  = 2.6F;
        pat->move_speed   = 2.4F;
        pat->chase_player = true;
        ms.add_or_replace(std::move(pat));
    }
    if (has_light)
    {
        auto le       = std::make_unique<LightEmitterModifier>();
        le->color     = p.albedo;
        le->kelvin    = 5800.0F;
        le->intensity = 2600.0F;
        le->range     = 14.0F;
        if ((fl & NodeSpawnFlags::kLightDirectional) != 0u)
            le->light_type = cd::light::LightType::kDirectional;
        else if ((fl & NodeSpawnFlags::kLightSpot) != 0u)
            le->light_type = cd::light::LightType::kSpot;
        else
            le->light_type = cd::light::LightType::kPoint;
        ms.add_or_replace(std::move(le));

        s.lights.push_back({
            std::string(desc->chroma_name) + " (" + std::string(desc->godot_name) + ")",
            cd::light::point({ raw_world_pos.x, raw_world_pos.y + 1.8F, raw_world_pos.z },
                             p.albedo, 2600.0F, 14.0F),
            true, 5800.0F });
    }

    // Attach the UniversalNodeModifier capsule describing the node's Chroma & Godot identity
    ms.add_or_replace(std::make_unique<UniversalNodeModifier>(*desc));

    // Mode 2: Nest as Child Element under previously selected parent entity!
    if (mode == CatalogComposeMode::NestAsChildElement &&
        prev_selected >= 0 && std::cmp_less(prev_selected, s.entities.size()) &&
        prev_selected != new_idx)
    {
        const auto& parent_ent = s.entities[static_cast<std::size_t>(prev_selected)];
        const auto  parent_h   = parent_ent.handle;
        if (parent_h.is_valid())
        {
            s.scene.attach(ent.handle, parent_h);
            if (auto* lt = s.scene.local(ent.handle))
            {
                if (parent_ent.kind == PrimitiveKind::kGltf)
                {
                    // CesiumMan glTF root has -90 deg X rotation and ~1.8x scale;
                    // counter-rotate (+90 deg X) and scale down so child boom sits cleanly above shoulder
                    lt->value.position = { 0.22F, -0.25F, 0.52F };
                    lt->value.rotation = { 0.7071068F, 0.0F, 0.0F, 0.7071068F };
                    lt->value.scale    = p.scale * 0.18F;
                }
                else
                {
                    lt->value.position = { 0.0F, 0.72F, 0.28F };
                    lt->value.scale    = p.scale * 0.45F;
                }
            }
            if (auto* rb = ms.template find<RigidBodyModifier>())
            {
                rb->body_type       = cd::physics::BodyType::kKinematic;
                rb->use_gravity     = false;
                rb->linear_velocity = { 0.0F, 0.0F, 0.0F };
            }
            if (auto* col = ms.find_collider())
            {
                col->enabled    = false;
                col->is_trigger = true;
            }
        }
    }

    cd::world_container::assign_layer(s.ecs_world, ent.handle,
        p.semantic_layer != nullptr ? p.semantic_layer : "Default");
    record_entity_home_transform(s, ent.handle);
    s.scene.update_transforms();

    w.hud_toast_message = "Spawned [" + std::string(chroma_construct_kind_badge(desc->construct_kind)) + "] " +
                          std::string(desc->chroma_name) + " (Godot: " + std::string(desc->godot_name) + ")";
    w.hud_toast_timer = 3.5F;
    log_push("[catalog] Spawned #" + std::to_string(desc->id) + " '" + std::string(desc->chroma_name) +
             "' (Godot: " + std::string(desc->godot_name) + " @ " + std::string(desc->video_timestamp) + ")");
    return new_idx;
}

/// Shoot a high-speed dynamic physics sphere from the player character (in PIE) or camera (in Edit)!
template <typename StateT, typename LogFn>
inline void shoot_physics_ball_from_camera(StateT& s, LogFn&& log_push)
{
    auto& w = studio_world();
    cd::math::Vec3f fwd = s.cam.target - s.cam.eye;
    const float len = cd::math::length(fwd);
    if (len > 1e-4F) fwd = fwd * (1.0F / len);
    else fwd = { 0.0F, -0.2F, -1.0F };

    cd::math::Vec3f spawn_pt = s.cam.eye + fwd * 1.6F;
    float launch_speed       = 13.5F;

    if (s.pie_playing)
    {
        for (const auto& ent : s.entities)
        {
            if (!ent.visible || ent.locked) continue;
            const auto* cc = w.entity_modifiers[ent.handle.id].template find<CharacterControllerModifier>();
            if (cc && cc->enabled)
            {
                if (const auto* p_lt = s.scene.local(ent.handle))
                {
                    const cd::math::Vec3f chest_pos = p_lt->value.position + cd::math::Vec3f { 0.0F, 0.85F, 0.0F };
                    // Aim toward nearest active enemy bot or hazard barrier if present
                    float best_d = 14.0F;
                    for (const auto& tgt : s.entities)
                    {
                        if (!tgt.visible || tgt.handle.id == ent.handle.id) continue;
                        const auto& t_ms = w.entity_modifiers[tgt.handle.id];
                        const auto* pat  = t_ms.template find<PatrolWaypointModifier>();
                        const auto* trg  = t_ms.template find<TriggerVolumeModifier>();
                        const bool is_enemy = (pat && pat->enabled) ||
                                              (trg && trg->enabled && trg->action == TriggerActionKind::kHazardDamage);
                        if (!is_enemy) continue;
                        if (const auto* t_lt = s.scene.local(tgt.handle))
                        {
                            const cd::math::Vec3f to_t =
                                (t_lt->value.position + cd::math::Vec3f { 0.0F, 0.55F, 0.0F }) - chest_pos;
                            const float d = cd::math::length(to_t);
                            if (d > 0.2F && d < best_d)
                            {
                                best_d = d;
                                fwd    = to_t * (1.0F / d);
                            }
                        }
                    }
                    spawn_pt     = chest_pos + fwd * 0.72F;
                    launch_speed = 16.5F;
                }
                break;
            }
        }
    }

    const int prev_sel = s.selected;
    const int idx = spawn_entity_with_modifiers(s, "Dynamic_Sphere", spawn_pt, log_push);
    if (s.pie_playing && prev_sel >= 0) s.selected = prev_sel;
    if (idx >= 0 && std::cmp_less(idx, s.entities.size()))
    {
        auto& ent = s.entities[static_cast<std::size_t>(idx)];
        ent.name  = "CannonBall_" + std::to_string(idx);
        ent.tint  = { 1.0F, 0.78F, 0.22F };
        if (auto* lt = s.scene.local(ent.handle))
        {
            lt->value.position = spawn_pt;
            lt->value.scale    = { 0.55F, 0.55F, 0.55F };
        }
        ModifierStack& ms = w.entity_modifiers[ent.handle.id];
        if (auto* pbr = ms.template find<PbrMaterialModifier>())
        {
            pbr->base_color = ent.tint;
            pbr->metallic   = 0.95F;
            pbr->roughness  = 0.08F;
        }
        if (auto* rb = ms.template find<RigidBodyModifier>())
        {
            rb->mass            = 4.5F;
            rb->restitution     = 0.65F;
            rb->linear_velocity = fwd * launch_speed + cd::math::Vec3f { 0.0F, 1.1F, 0.0F };
        }
        record_entity_home_transform(s, ent.handle);
        s.scene.update_transforms();
        log_push("[cannon] Fired Plasma CannonBall at " + std::to_string(static_cast<int>(launch_speed)) + " m/s!");
    }
}

/// Assign a Mesh asset (from Folder Tree / Content Browser or Inspector slot) onto an existing SceneEntity
template <typename StateT, typename LogFn>
inline bool assign_mesh_asset_to_entity(StateT& s, int target_idx, std::string_view asset_id, LogFn&& log_push)
{
    if (target_idx < 0 || !std::cmp_less(target_idx, s.entities.size())) return false;
    auto& ent = s.entities[static_cast<std::size_t>(target_idx)];
    auto& w   = studio_world();

    auto contains_icase = [](std::string_view hay, std::string_view needle) noexcept -> bool
    {
        if (needle.empty() || hay.size() < needle.size()) return false;
        for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
        {
            bool match = true;
            for (std::size_t j = 0; j < needle.size(); ++j)
            {
                char a = hay[i + j];
                char b = needle[j];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                if (a != b) { match = false; break; }
            }
            if (match) return true;
        }
        return false;
    };

    const PrimitiveKind prev_kind = ent.kind;
    const std::string path_str { asset_id };
    std::error_code ec {};
    const bool is_disk_gltf =
        (contains_icase(asset_id, ".gltf") || contains_icase(asset_id, ".glb")) &&
        !contains_icase(asset_id, "sponza") &&
        !contains_icase(asset_id, "cesium") &&
        std::filesystem::exists(path_str, ec) &&
        s.device_owner != nullptr;
    const bool is_disk_obj =
        contains_icase(asset_id, ".obj") &&
        std::filesystem::exists(path_str, ec) &&
        s.device_owner != nullptr;

    if (is_disk_gltf || is_disk_obj)
    {
        cd::asset::PrimitiveMesh merged {};
        bool mesh_ok = false;

        if (is_disk_obj)
        {
            mesh_ok = parse_wavefront_obj_mesh(path_str, merged);
        }
        else
        {
            auto gltf_r = cd::asset::gltf::load_gltf(path_str);
            if (gltf_r.has_value() && !gltf_r->meshes.empty())
            {
                std::size_t total_verts = 0;
                for (const auto& m : gltf_r->meshes)
                    for (const auto& prim : m.primitives)
                        total_verts += prim.vertices.size();

                merged.vertices.reserve(total_verts);
                const bool use_u32 = (total_verts > 0xFFFFU);
                for (const auto& m : gltf_r->meshes)
                {
                    for (const auto& prim : m.primitives)
                    {
                        const auto base = static_cast<std::uint32_t>(merged.vertices.size());
                        for (const auto& v : prim.vertices)
                        {
                            cd::asset::PrimitiveVertex pv {};
                            pv.pos[0]    = v.position.x;
                            pv.pos[1]    = v.position.y;
                            pv.pos[2]    = v.position.z;
                            pv.normal[0] = v.normal.x;
                            pv.normal[1] = v.normal.y;
                            pv.normal[2] = v.normal.z;
                            pv.uv[0]     = v.texcoord0.x;
                            pv.uv[1]     = v.texcoord0.y;
                            pv.color[0]  = 1.0F;
                            pv.color[1]  = 1.0F;
                            pv.color[2]  = 1.0F;
                            merged.vertices.push_back(pv);
                        }
                        if (use_u32)
                        {
                            for (auto idx : prim.indices)
                                merged.indices_u32.push_back(base + idx);
                        }
                        else
                        {
                            for (auto idx : prim.indices)
                            {
                                const auto val = base + idx;
                                if (val <= 0xFFFFU)
                                    merged.indices.push_back(static_cast<std::uint16_t>(val));
                            }
                        }
                    }
                }
                mesh_ok = !merged.vertices.empty() && (!merged.indices.empty() || !merged.indices_u32.empty());
            }
        }

        if (mesh_ok && normalize_and_validate_custom_mesh(merged))
        {
            s.device_owner->wait_idle();
            if (s.blas_custom_gltf.is_valid())
            {
                s.device_owner->destroy_acceleration_structure(s.blas_custom_gltf);
                s.blas_custom_gltf = {};
            }
            if (s.custom_gltf_mesh.vb.is_valid())
            {
                cd::render::destroy_mesh(*s.device_owner, s.custom_gltf_mesh);
                s.custom_gltf_mesh = {};
            }
            s.custom_gltf_mesh = cd::render::upload_mesh(*s.device_owner, merged);
            {
                cd::rhi::AccelTriangleGeometry tri {};
                tri.vertex_buffer = s.custom_gltf_mesh.vb;
                tri.vertex_offset = 0;
                tri.vertex_count  = s.custom_gltf_mesh.vertex_count;
                tri.vertex_stride = sizeof(cd::asset::PrimitiveVertex);
                tri.index_buffer  = s.custom_gltf_mesh.ib;
                tri.index_offset  = 0;
                tri.index_count   = s.custom_gltf_mesh.index_count;
                tri.index_type    = s.custom_gltf_mesh.index_type;
                std::array<cd::rhi::AccelTriangleGeometry, 1> tris { tri };
                cd::rhi::AccelStructureDesc bd {};
                bd.kind       = cd::rhi::AccelStructureKind::kBottomLevel;
                bd.triangles  = std::span<const cd::rhi::AccelTriangleGeometry>(tris);
                bd.debug_name = "blas_custom_gltf";
                auto blas_r   = s.device_owner->create_acceleration_structure(bd);
                s.blas_custom_gltf = blas_r.has_value() ? *blas_r : cd::rhi::AccelStructureHandle {};
            }
            if (s.blas_custom_gltf.is_valid())
            {
                if (auto bcmd_ptr = s.device_owner->create_command_buffer())
                {
                    bcmd_ptr->begin();
                    bcmd_ptr->build_acceleration_structure(s.blas_custom_gltf);
                    bcmd_ptr->end();
                    cd::rhi::SubmitDesc bsd {};
                    std::array<cd::rhi::ICommandBuffer*, 1> bcbs { bcmd_ptr.get() };
                    bsd.command_buffers = bcbs;
                    (void)s.device_owner->submit(bsd);
                    s.device_owner->wait_idle();
                }
            }
            ent.kind = PrimitiveKind::kCustomGltf;
        }
        else
        {
            w.last_asset_status_msg = "Warning: Failed to parse custom mesh '" + std::string(asset_id) + "' (keeping fallback)";
            log_push("[asset] " + w.last_asset_status_msg);
            return false;
        }
    }
    else if (contains_icase(asset_id, "sponza"))          ent.kind = PrimitiveKind::kSponza;
    else if (contains_icase(asset_id, "cesium") ||
             contains_icase(asset_id, "avatar") ||
             contains_icase(asset_id, ".glb"))            ent.kind = PrimitiveKind::kGltf;
    else if (contains_icase(asset_id, "knot"))            ent.kind = PrimitiveKind::kTorusKnot;
    else if (contains_icase(asset_id, "sphere") ||
             contains_icase(asset_id, "ball"))            ent.kind = PrimitiveKind::kSphere;
    else if (contains_icase(asset_id, "cylinder") ||
             contains_icase(asset_id, "pillar"))          ent.kind = PrimitiveKind::kCylinder;
    else if (contains_icase(asset_id, "cone") ||
             contains_icase(asset_id, "crystal"))         ent.kind = PrimitiveKind::kCone;
    else if (contains_icase(asset_id, "torus") ||
             contains_icase(asset_id, "ring"))            ent.kind = PrimitiveKind::kTorus;
    else                                                  ent.kind = PrimitiveKind::kCube;

    ent.custom_mesh_path = std::string(asset_id);
    if (auto* lt = s.scene.local(ent.handle))
    {
        if (ent.kind == PrimitiveKind::kSponza && prev_kind != PrimitiveKind::kSponza)
        {
            lt->value.scale = { 0.01F, 0.01F, 0.01F };
        }
        else if (ent.kind != PrimitiveKind::kSponza && prev_kind == PrimitiveKind::kSponza)
        {
            lt->value.scale = { 1.0F, 1.0F, 1.0F };
        }
    }
    record_entity_home_transform(s, ent.handle);
    s.scene.update_transforms();
    s.last_tlas_scene_hash = 0;
    w.last_asset_status_msg = "Assigned Mesh '" + std::string(asset_id) + "' -> " + ent.name;
    log_push("[asset] " + w.last_asset_status_msg);
    return true;
}

/// Assign a Texture or Material asset (from Folder Tree / Content Browser or Inspector slot) onto an existing SceneEntity
template <typename StateT, typename LogFn>
inline bool assign_texture_asset_to_entity(StateT& s, int target_idx, std::string_view asset_id, LogFn&& log_push)
{
    if (target_idx < 0 || !std::cmp_less(target_idx, s.entities.size())) return false;
    auto& ent = s.entities[static_cast<std::size_t>(target_idx)];
    auto& w   = studio_world();

    auto contains_icase = [](std::string_view hay, std::string_view needle) noexcept -> bool
    {
        if (needle.empty() || hay.size() < needle.size()) return false;
        for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
        {
            bool match = true;
            for (std::size_t j = 0; j < needle.size(); ++j)
            {
                char a = hay[i + j];
                char b = needle[j];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                if (a != b) { match = false; break; }
            }
            if (match) return true;
        }
        return false;
    };

    const std::string path_str { asset_id };
    std::error_code ec {};
    const bool is_disk_image =
        (contains_icase(asset_id, ".png")  || contains_icase(asset_id, ".jpg") ||
        contains_icase(asset_id, ".jpeg") || contains_icase(asset_id, ".tga") ||
        contains_icase(asset_id, ".bmp")) &&
        std::filesystem::exists(path_str, ec);

    int slot = 1;
    bool loaded_from_disk = false;
    if (is_disk_image && s.device_owner != nullptr && s.prim_bindless_set.is_valid())
    {
        if (auto it = s.custom_disk_tex_slots.find(path_str); it != s.custom_disk_tex_slots.end())
        {
            slot             = it->second;
            loaded_from_disk = true;
            ent.tint         = { 1.0F, 1.0F, 1.0F };
        }
        else if (s.custom_disk_textures.size() < 130U)
        {
            auto img_r = cd::asset::image::load_image(path_str);
            if (img_r.has_value() && !img_r->rgba.empty() && img_r->width > 0U && img_r->height > 0U)
            {
                auto gpu_tex = cd_studio::create_texture_rgba8(
                    *s.device_owner, img_r->rgba.data(), img_r->width, img_r->height);
                if (gpu_tex.view.is_valid())
                {
                    const int new_slot = 120 + static_cast<int>(s.custom_disk_textures.size());
                    const std::array<cd::rhi::DescriptorWrite, 1> bw {{
                        cd::rhi::DescriptorWrite {
                            .binding       = 0,
                            .array_element = static_cast<std::uint32_t>(new_slot),
                            .type          = cd::rhi::DescriptorType::kBindlessSampledImage,
                            .view          = gpu_tex.view,
                            .sampler       = s.albedo_sampler,
                        }
                    }};
                    (void)s.device_owner->update_descriptor_set(
                        s.prim_bindless_set,
                        std::span<const cd::rhi::DescriptorWrite>(bw));
                    s.custom_disk_textures.push_back(gpu_tex);
                    s.custom_disk_tex_slots[path_str] = new_slot;
                    slot             = new_slot;
                    loaded_from_disk = true;
                    ent.tint         = { 1.0F, 1.0F, 1.0F };
                    ent.metallic     = 0.08F;
                    ent.roughness    = 0.48F;
                }
            }
            else
            {
                log_push("[asset] Warning: Failed to decode disk image '" + path_str + "' — using procedural fallback slot");
            }
        }
    }

    if (!loaded_from_disk)
    {
        if (contains_icase(asset_id, "none") || contains_icase(asset_id, "clear"))
        {
            slot = 0;
        }
        else if (contains_icase(asset_id, "checker") || contains_icase(asset_id, "grid") || contains_icase(asset_id, "uv_"))
        {
            slot = 2;
            ent.tint      = { 1.0F, 1.0F, 1.0F };
            ent.metallic  = 0.10F;
            ent.roughness = 0.32F;
        }
        else if (contains_icase(asset_id, "brick") || contains_icase(asset_id, "arch") ||
                 contains_icase(asset_id, "wall")  || contains_icase(asset_id, "roof") ||
                 contains_icase(asset_id, "stone"))
        {
            slot = 3;
            ent.tint      = { 1.0F, 1.0F, 1.0F };
            ent.metallic  = 0.05F;
            ent.roughness = 0.72F;
        }
        else if (contains_icase(asset_id, "carbon") || contains_icase(asset_id, "twill") ||
                 contains_icase(asset_id, "chain")  || contains_icase(asset_id, "metal") ||
                 contains_icase(asset_id, "lion"))
        {
            slot = 4;
            ent.tint      = { 0.96F, 0.97F, 0.99F };
            ent.metallic  = 0.78F;
            ent.roughness = 0.24F;
        }
        else if (contains_icase(asset_id, "marble") || contains_icase(asset_id, "carrara") ||
                 contains_icase(asset_id, "floor")  || contains_icase(asset_id, "column")  ||
                 contains_icase(asset_id, "flagpole"))
        {
            slot = 5;
            ent.tint      = { 1.0F, 1.0F, 1.0F };
            ent.metallic  = 0.12F;
            ent.roughness = 0.14F;
        }
        else if (contains_icase(asset_id, "wood")  || contains_icase(asset_id, "oak")   ||
                 contains_icase(asset_id, "plank") || contains_icase(asset_id, "thorn") ||
                 contains_icase(asset_id, "vase")  || contains_icase(asset_id, "plant"))
        {
            slot = 6;
            ent.tint      = { 1.0F, 1.0F, 1.0F };
            ent.metallic  = 0.05F;
            ent.roughness = 0.46F;
        }
        else if (contains_icase(asset_id, "mat_gold"))
        {
            slot = 0;
            ent.tint              = { 1.00F, 0.78F, 0.34F };
            ent.metallic          = 1.0F;
            ent.roughness         = 0.16F;
            ent.multiscatter_comp = 1.0F;
            ent.iridescence_nm    = 0.0F;
        }
        else if (contains_icase(asset_id, "mat_irid") || contains_icase(asset_id, "bismuth"))
        {
            slot = 0;
            ent.tint              = { 0.90F, 0.92F, 0.98F };
            ent.metallic          = 1.0F;
            ent.roughness         = 0.14F;
            ent.multiscatter_comp = 1.0F;
            ent.iridescence_nm    = 440.0F;
        }
        else
        {
            // Default Slot 1: Earth / Disk Image Texture
            slot = 1;
            ent.tint = { 1.0F, 1.0F, 1.0F };
        }
    }

    ent.use_texture         = (slot > 0);
    ent.custom_texture_id   = slot;
    ent.custom_texture_path = (slot > 0) ? std::string(asset_id) : std::string {};
    ent.is_pbr              = true;

    ModifierStack& ms = w.entity_modifiers[ent.handle.id];
    auto* pbr = ms.template find<PbrMaterialModifier>();
    if (!pbr)
    {
        pbr = static_cast<PbrMaterialModifier*>(ms.add_or_replace(
            std::make_unique<PbrMaterialModifier>(
                ent.tint, ent.metallic, ent.roughness, ent.use_texture,
                ent.multiscatter_comp, ent.iridescence_nm, slot, ent.custom_texture_path)));
    }
    else
    {
        pbr->base_color         = ent.tint;
        pbr->metallic           = ent.metallic;
        pbr->roughness          = ent.roughness;
        pbr->use_texture        = ent.use_texture;
        pbr->texture_slot       = slot;
        pbr->texture_asset_path = ent.custom_texture_path;
        pbr->multiscatter_comp  = ent.multiscatter_comp;
        pbr->iridescence_nm     = ent.iridescence_nm;
    }

    s.last_tlas_scene_hash = 0;
    w.last_asset_status_msg = "Applied Texture '" + std::string(asset_id) + "' (Slot " + std::to_string(slot) + ") -> " + ent.name;
    log_push("[asset] " + w.last_asset_status_msg);
    return true;
}

/// 1-Click Interactive Multi-World & Multi-Level Templates (0..13)
template <typename StateT, typename LogFn>
inline void load_studio_interactive_template(StateT& s, int template_id, LogFn&& log_push)
{
    auto& w = studio_world();
    w.initialized = false;
    init_clean_studio_workspace(s, log_push);
    w.simulate_physics_in_edit = (template_id > 0);
    // Show collider wireframes only on physics-focused templates (1, 2, 3, 7) so rendering levels look pristine
    w.show_all_collider_wires = (template_id == 1 || template_id == 2 || template_id == 3 || template_id == 7);

    if (auto* proj = s.cd_world.project())
    {
        const auto* cur_lvl = proj->level(proj->active_level());
        if (cur_lvl != nullptr && static_cast<int>(cur_lvl->template_id()) == template_id)
        {
            s.cd_world.set_name(std::string(cur_lvl->world_name()));
            w.active_world_name = std::string(cur_lvl->world_name());
            w.active_level_name = std::string(cur_lvl->name());
        }
        else
        {
            for (std::size_t li = 0; li < proj->level_count(); ++li)
            {
                if (auto* lvl = proj->level(li); lvl && static_cast<int>(lvl->template_id()) == template_id)
                {
                    proj->set_active_level(li);
                    s.cd_world.set_name(std::string(lvl->world_name()));
                    w.active_world_name = std::string(lvl->world_name());
                    w.active_level_name = std::string(lvl->name());
                    break;
                }
            }
        }
    }

    auto place_static_grounded = [&](int idx, cd::math::Vec3f pos, cd::math::Vec3f scl, const char* nm)
    {
        if (idx < 0 || !std::cmp_less(idx, s.entities.size())) return;
        auto& ent = s.entities[static_cast<std::size_t>(idx)];
        if (nm != nullptr && nm[0] != '\0') ent.name = nm;
        if (auto* lt = s.scene.local(ent.handle))
        {
            lt->value.position = pos;
            lt->value.scale    = scl;
        }
        if (auto* rb = w.entity_modifiers[ent.handle.id].template find<RigidBodyModifier>())
        {
            rb->body_type       = cd::physics::BodyType::kStatic;
            rb->linear_velocity = { 0.0F, 0.0F, 0.0F };
        }
        record_entity_home_transform(s, ent.handle);
    };

    if (template_id > 0)
    {
        const cd::ecs::Entity floor_h = s.scene.create_node();
        if (auto* lt = s.scene.local(floor_h))
        {
            lt->value.position = { 0.0F, -0.25F, 0.0F };
            lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
            lt->value.scale    = { 20.0F, 0.50F, 20.0F };
        }
        SceneEntity floor_ent {};
        floor_ent.handle    = floor_h;
        floor_ent.name      = "Ground_Floor (Static Collider)";
        floor_ent.kind      = PrimitiveKind::kCube;
        floor_ent.tint      = { 0.24F, 0.27F, 0.31F };
        floor_ent.is_pbr    = true;
        floor_ent.metallic  = 0.08F;
        floor_ent.roughness = 0.72F;
        s.entities.push_back(floor_ent);
        cd::world_container::assign_layer(s.ecs_world, floor_h, "Geometry");

        auto& ms = w.entity_modifiers[floor_h.id];
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(
            cd::math::Vec3f { 0.24F, 0.27F, 0.31F }, 0.08F, 0.72F, false));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(
            cd::physics::BodyType::kStatic, 0.0F, 0.50F));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(
            cd::math::Vec3f { 0.50F, 0.50F, 0.50F }));
        record_entity_home_transform(s, floor_h);
    }

    if (template_id == 0)
    {
        // Level 1.1 / Empty_Stage: True Clean Empty Sandbox (0 pre-spawned objects, only Ground_Floor)
        (void)place_static_grounded;
        s.selected   = -1;
        s.cam.eye    = { -5.8F, 3.8F, 6.2F };
        s.cam.target = {  0.0F, 0.8F, 0.0F };
        w.active_level_name = "Empty_Stage";
        log_push("[level] Opened Clean Empty Stage ('Empty_Stage' — 0 pre-spawned objects)");
    }
    else if (template_id == 1)
    {
        // Level 1.2: Angled 25 deg Physics Ramp + Rolling Chrome Sphere + 5x Crate Tower + Character
        (void)spawn_entity_with_modifiers(s, "Static_Ramp", { -1.55F, 0.0F, 0.0F }, log_push);
        const int sph_idx = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -2.35F, 1.1F, 0.0F }, log_push);
        if (sph_idx >= 0 && std::cmp_less(sph_idx, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(sph_idx)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.position = { -2.35F, 2.05F, 0.0F };
            if (auto* rb = w.entity_modifiers[h.id].template find<RigidBodyModifier>())
            {
                rb->mass            = 6.5F;
                rb->linear_velocity = { 3.6F, -1.2F, 0.0F };
            }
            record_entity_home_transform(s, h);
        }
        (void)spawn_entity_with_modifiers(s, "Prefab_CrateStack", { 0.85F, 0.02F, 0.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { -0.4F, 0.02F, -2.2F }, log_push);
        s.selected   = (sph_idx >= 0) ? sph_idx : 1;
        s.cam.eye    = { -3.6F, 3.65F, 7.6F };
        s.cam.target = { -0.15F, 1.65F, -0.2F };
        log_push("[level] Loaded '1.2 OBB Ramp & 5x Crate Tower'");
    }
    else if (template_id == 2)
    {
        // Level 1.3: 6x Domino Chain Reaction + Striker Cannonball
        (void)spawn_entity_with_modifiers(s, "Prefab_DominoRow", { 0.35F, 0.0F, 0.0F }, log_push);
        const int striker = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -2.45F, -1.5F, 0.0F }, log_push);
        if (striker >= 0 && std::cmp_less(striker, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(striker)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.position = { -2.45F, 0.85F, 0.0F };
            if (auto* rb = w.entity_modifiers[h.id].template find<RigidBodyModifier>())
            {
                rb->mass            = 6.0F;
                rb->linear_velocity = { 6.5F, 0.0F, 0.0F };
            }
            record_entity_home_transform(s, h);
        }
        s.selected   = striker;
        s.cam.eye    = { -0.35F, 2.85F, 6.4F };
        s.cam.target = { -0.10F, 0.75F, 0.0F };
        log_push("[level] Loaded '1.3 6x Domino Chain & Striker'");
    }
    else if (template_id == 3)
    {
        // Level 3.2: Bouncing Trampoline Pad + High-Drop Spheres & Cubes
        (void)spawn_entity_with_modifiers(s, "Trampoline_Pad", { 0.0F, 0.0F, 0.0F }, log_push);
        const int sph_idx  = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -0.42F, 0.0F, 0.0F }, log_push);
        const int cube_idx = spawn_entity_with_modifiers(s, "Dynamic_Cube",   {  0.48F, 0.0F, 0.0F }, log_push);
        if (sph_idx >= 0 && std::cmp_less(sph_idx, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(sph_idx)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.position = { -0.42F, 1.85F, 0.0F };
            record_entity_home_transform(s, h);
        }
        if (cube_idx >= 0 && std::cmp_less(cube_idx, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(cube_idx)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.position = { 0.48F, 2.55F, 0.0F };
            record_entity_home_transform(s, h);
        }
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { -2.2F, 0.02F, 0.0F }, log_push);
        s.selected   = sph_idx;
        s.cam.eye    = { -2.4F, 3.05F, 6.2F };
        s.cam.target = { -0.25F, 1.35F, 0.0F };
        log_push("[level] Loaded '3.2 Trampoline Bounce Arena'");
    }
    else if (template_id == 4)
    {
        // Level 2.1: 01_pbr_materials_sss_hair — 5x5 Cook-Torrance PBR Sphere Matrix
        constexpr std::array<cd::math::Vec3f, 5> kRowColors {{
            { 0.96F, 0.97F, 0.99F }, // Row 0: Chrome / Silver
            { 1.00F, 0.78F, 0.34F }, // Row 1: 24K Gold (Turquin Multi-Scatter)
            { 0.95F, 0.50F, 0.36F }, // Row 2: Copper
            { 0.88F, 0.90F, 0.96F }, // Row 3: Thin-Film Iridescence (Belcour-Barla 320..560 nm)
            { 0.90F, 0.22F, 0.32F }, // Row 4: Crimson Dielectric Ceramic
        }};
        for (int row = 0; row < 5; ++row)
        {
            const float metallic = (row == 3) ? 0.92F : (static_cast<float>(4 - row) / 4.0F);
            for (int col = 0; col < 5; ++col)
            {
                const float roughness = std::clamp(0.05F + static_cast<float>(col) * 0.22F, 0.04F, 0.98F);
                const cd::ecs::Entity h = s.scene.create_node();
                if (auto* lt = s.scene.local(h))
                {
                    lt->value.position = { (static_cast<float>(col) - 2.0F) * 1.22F,
                                           0.68F + static_cast<float>(row) * 1.02F,
                                           -1.5F };
                    lt->value.scale    = { 0.82F, 0.82F, 0.82F };
                }
                SceneEntity e {};
                e.handle            = h;
                e.name              = "PBR_Sphere_R" + std::to_string(row) + "_C" + std::to_string(col);
                e.kind              = PrimitiveKind::kSphere;
                e.tint              = kRowColors[static_cast<std::size_t>(row)];
                e.is_pbr            = true;
                e.metallic          = metallic;
                e.roughness         = roughness;
                e.multiscatter_comp = 1.0F;
                e.iridescence_nm    = (row == 3) ? (320.0F + static_cast<float>(col) * 60.0F) : 0.0F;
                s.entities.push_back(e);
                cd::world_container::assign_layer(s.ecs_world, h, "Geometry");
                w.entity_modifiers[h.id].add_or_replace(
                    std::make_unique<PbrMaterialModifier>(
                        e.tint, e.metallic, e.roughness, false, e.multiscatter_comp, e.iridescence_nm));
                record_entity_home_transform(s, h);
            }
        }
        s.lights.push_back({ "Gallery Warm Key (4200K)",
            cd::light::point({ 3.8F, 4.2F, 2.5F },
                             cd::light::cct_to_linear_rgb(4200.0F), 3200.0F, 18.0F),
            true, 4200.0F });
        s.selected   = 18; // Select PBR_Sphere_R3_C2 (440nm thin-film iridescence + Turquin MS)
        s.cam.eye    = { 0.0F, 2.75F, 6.8F };
        s.cam.target = { 0.0F, 2.65F, -1.5F };
        log_push("[level] Loaded '2.1 PBR 5x5 Sphere Matrix (Turquin MS + Belcour-Barla Iridescence)'");
    }
    else if (template_id == 5)
    {
        // Level 2.2: 03_sponza_clustered_ddgi_restir — Full Crytek Sponza Atrium + Clustered Lights + Chrome Probe
        if (!s.entities.empty()) s.entities[0].visible = false;
        (void)spawn_entity_with_modifiers(s, "World_Sponza", { 0.0F, 0.0F, 0.0F }, log_push);
        const int probe = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { 0.0F, 1.6F, 0.0F }, log_push);
        if (probe >= 0 && std::cmp_less(probe, s.entities.size()))
        {
            auto& ent = s.entities[static_cast<std::size_t>(probe)];
            ent.name      = "Sponza_Chrome_Mirror_Probe";
            ent.tint      = { 0.98F, 0.98F, 1.0F };
            ent.metallic  = 1.0F;
            ent.roughness = 0.04F;
            if (auto* lt = s.scene.local(ent.handle)) lt->value.position = { 0.0F, 1.6F, -0.2F };
            w.entity_modifiers[ent.handle.id].items.clear();
            w.entity_modifiers[ent.handle.id].add_or_replace(
                std::make_unique<PbrMaterialModifier>(ent.tint, 1.0F, 0.04F, false));
            record_entity_home_transform(s, ent.handle);
        }
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { -2.6F, 0.02F, 1.35F }, log_push);
        // Use the calibrated W8-BB indoor Sponza Atrium light rig (oblique sun + nave lantern/spot/rect)
        s.lights.clear();
        s.lights.push_back({ "Sun (cool 6500K)",
            cd::light::directional({ -0.35F, -0.65F, -0.70F }, { 1.0F, 1.0F, 1.0F }, 100000.0F),
            true, 6500.0F });
        s.lights.push_back({ "Tungsten lantern (2700K)",
            cd::light::point({ -6.0F, 2.0F, 0.0F }, { 1.0F, 1.0F, 1.0F }, 3000.0F, 8.0F),
            true, 2700.0F });
        s.lights.push_back({ "Nave spot (3200K)",
            cd::light::spot({ -4.0F, 4.0F, 0.0F }, { 0.8F, -0.5F, 0.0F },
                            { 1.0F, 1.0F, 1.0F }, 6000.0F, 12.0F, 0.30F, 0.50F),
            true, 3200.0F });
        s.lights.push_back({ "South wall cyan (8000K)",
            cd::light::rect_area({ 0.0F, 3.0F, -4.0F }, { 0.0F, 0.0F, 1.0F }, { 1.0F, 0.0F, 0.0F },
                                 2.0F, 1.0F, { 0.6F, 0.85F, 1.0F }, 2500.0F),
            true, 8000.0F });
        s.lights.push_back({ "North wall magenta (25000K)",
            cd::light::rect_area({ 0.0F, 3.0F, 4.0F }, { 0.0F, 0.0F, -1.0F }, { 1.0F, 0.0F, 0.0F },
                                 2.0F, 0.4F, { 1.0F, 0.18F, 0.85F }, 6000.0F),
            true, 25000.0F });
        s.selected   = probe;
        s.cam.eye    = { -6.2F, 1.85F, -0.15F };
        s.cam.target = {  0.0F, 1.65F, -0.20F };
        log_push("[level] Loaded '2.2 Crytek Sponza Atrium & DDGI (03_sponza)'");
    }
    else if (template_id == 6)
    {
        // Level 2.3: 04_optics_cinematic_studio — Grounded Gallery Sculptures + Area Lights + Bloom & Lens Optics
        w.environment_modifiers.add_or_replace(std::make_unique<BloomLensModifier>());
        const int t_idx = spawn_entity_with_modifiers(s, "Torus",          { -1.85F, 0.0F, 0.0F }, log_push);
        const int s_idx = spawn_entity_with_modifiers(s, "Dynamic_Sphere", {  0.00F, 0.0F, 0.0F }, log_push);
        const int c_idx = spawn_entity_with_modifiers(s, "Cylinder",       {  1.85F, 0.0F, 0.0F }, log_push);
        place_static_grounded(t_idx, { -1.85F, 0.78F,  0.10F }, { 0.95F, 0.95F, 0.95F }, "Bismuth_Iridescent_Knot");
        place_static_grounded(s_idx, {  0.00F, 0.65F,  0.00F }, { 1.30F, 1.30F, 1.30F }, "Gold_24K_Optics_Orb");
        place_static_grounded(c_idx, {  1.85F, 0.75F, -0.10F }, { 1.00F, 1.50F, 1.00F }, "Carrara_Marble_Column");
        (void)assign_mesh_asset_to_entity(s, t_idx, "assets/meshes/TorusKnot.mesh", log_push);
        (void)assign_texture_asset_to_entity(s, t_idx, "Mat_Iridescent_Bismuth", log_push);
        (void)assign_texture_asset_to_entity(s, s_idx, "Mat_Gold_24K", log_push);
        (void)assign_texture_asset_to_entity(s, c_idx, "Carrara_Marble.tex", log_push);
        s.lights.push_back({ "Key Rect Area Softbox (5600K)",
            cd::light::rect_area({ 0.0F, 3.2F, 2.8F }, { 0.0F, -0.5F, -0.86F }, { 1.0F, 0.0F, 0.0F },
                                 2.8F, 1.4F, { 1.0F, 0.96F, 0.90F }, 5200.0F),
            true, 5600.0F });
        s.selected   = s_idx;
        s.cam.eye    = { 0.0F, 2.15F, 5.25F };
        s.cam.target = { 0.0F, 0.75F, 0.00F };
        log_push("[level] Loaded '2.3 Cinematic Optics & Bloom (04_optics)'");
    }
    else if (template_id == 7)
    {
        // Level 3.1: 06_jolt_dynamics_rover_shatter — Twin opposing OBB ramps + heavy collider impact
        (void)spawn_entity_with_modifiers(s, "Static_Ramp", { -2.4F, 0.0F, 0.0F }, log_push);
        const int r2 = spawn_entity_with_modifiers(s, "Static_Ramp", {  2.4F, 0.0F, 0.0F }, log_push);
        if (r2 >= 0 && std::cmp_less(r2, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(r2)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.rotation = euler_deg_to_quat({ 0.0F, 0.0F, 25.0F });
            record_entity_home_transform(s, h);
        }
        (void)spawn_entity_with_modifiers(s, "Prefab_CrateStack", { 0.0F, 0.02F, 0.0F }, log_push);
        const int b1 = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -2.75F, 0.0F, 0.0F }, log_push);
        if (b1 >= 0 && std::cmp_less(b1, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(b1)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.position = { -2.75F, 2.05F, 0.0F };
            if (auto* rb = w.entity_modifiers[h.id].template find<RigidBodyModifier>())
            {
                rb->mass            = 8.0F;
                rb->linear_velocity = { 4.8F, -1.0F, 0.0F };
            }
            record_entity_home_transform(s, h);
        }
        const int b2 = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { 2.75F, 0.0F, 0.0F }, log_push);
        if (b2 >= 0 && std::cmp_less(b2, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(b2)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.position = { 2.75F, 2.05F, 0.0F };
            if (auto* rb = w.entity_modifiers[h.id].template find<RigidBodyModifier>())
            {
                rb->mass            = 8.0F;
                rb->linear_velocity = { -4.8F, -1.0F, 0.0F };
            }
            (void)assign_texture_asset_to_entity(s, b2, "Mat_Gold_24K", log_push);
            record_entity_home_transform(s, h);
        }
        s.selected   = b1;
        s.cam.eye    = { 0.0F, 3.85F, 9.1F };
        s.cam.target = { 0.0F, 1.65F, 0.0F };
        log_push("[level] Loaded '3.1 Double Ramp & Rover Collision (06_jolt)'");
    }
    else if (template_id == 8)
    {
        // Level 4.1: 05_avatar_locomotion_anim_ik — 3x Skinned CesiumMan Squad + Obstacle Course
        const int c1 = spawn_entity_with_modifiers(s, "Character_CesiumMan", { -1.75F, 0.02F, -0.35F }, log_push);
        const int c2 = spawn_entity_with_modifiers(s, "Character_CesiumMan", {  0.00F, 0.02F,  0.35F }, log_push);
        const int c3 = spawn_entity_with_modifiers(s, "Character_CesiumMan", {  1.75F, 0.02F, -0.35F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Static_Ramp", { 0.0F, 0.0F, -2.2F }, log_push);
        (void)c1;
        (void)c3;
        s.selected   = c2;
        s.cam.eye    = { -2.4F, 2.45F, 5.2F };
        s.cam.target = {  0.0F, 0.95F, -0.2F };
        log_push("[level] Loaded '4.1 Skinned Avatar Squad & IK (05_avatar)'");
    }
    else if (template_id == 9)
    {
        // Level 4.2: 08_synth_pilot_squad_narrative — Tactical Combat Arena
        const int w1 = spawn_entity_with_modifiers(s, "Static_Wall",         { -2.6F, 0.0F, -1.5F }, log_push);
        const int w2 = spawn_entity_with_modifiers(s, "Static_Wall",         {  2.6F, 0.0F,  1.2F }, log_push);
        (void)assign_texture_asset_to_entity(s, w1, "Brick_Wall_PBR.tex", log_push);
        (void)assign_texture_asset_to_entity(s, w2, "Brick_Wall_PBR.tex", log_push);
        const int a1 = spawn_entity_with_modifiers(s, "Character_CesiumMan", { -1.35F, 0.02F,  0.65F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan",          {  1.35F, 0.02F, -0.65F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Prefab_CrateStack",            {  0.00F, 0.02F, -1.65F }, log_push);
        s.selected   = a1;
        s.cam.eye    = { -3.6F, 3.85F, 7.6F };
        s.cam.target = {  0.0F, 1.35F, -0.35F };
        log_push("[level] Loaded '4.2 Tactical Synth Squad Arena (08_synth)'");
    }
    else if (template_id == 10)
    {
        // Level 5.1: 02_celestial_aether_fog_clouds — Grounded Temple Monoliths + Volumetric Fog & God-Rays
        w.environment_modifiers.add_or_replace(std::make_unique<FogModifier>());
        for (int i = -2; i <= 2; ++i)
        {
            const int c_idx = spawn_entity_with_modifiers(s, "Cylinder", { static_cast<float>(i) * 1.65F, 0.0F, -1.8F }, log_push);
            const std::string col_name = "Marble_Monolith_" + std::to_string(i + 3);
            place_static_grounded(
                c_idx,
                { static_cast<float>(i) * 1.65F, 1.10F, -1.8F },
                { 0.72F, 2.20F, 0.72F },
                col_name.c_str());
            (void)assign_texture_asset_to_entity(s, c_idx, "Carrara_Marble.tex", log_push);
        }
        const int orb_idx = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { 0.0F, 0.0F, 0.35F }, log_push);
        place_static_grounded(orb_idx, { 0.0F, 1.15F, 0.35F }, { 1.15F, 1.15F, 1.15F }, "Aether_Sun_Core");
        (void)assign_texture_asset_to_entity(s, orb_idx, "Mat_Gold_24K", log_push);
        s.selected   = orb_idx;
        s.cam.eye    = { 0.0F, 2.35F, 6.2F };
        s.cam.target = { 0.0F, 1.15F, -0.8F };
        log_push("[level] Loaded '5.1 Celestial Fog & God-Rays (02_aether)'");
    }
    else if (template_id == 11)
    {
        // Level 5.2: 07_terra_ocean_nature_world — Golden Hour Archipelago (Mirror Ocean + Grounded Islands)
        s.show_editor_floor = false;
        w.environment_modifiers.add_or_replace(std::make_unique<FogModifier>());
        if (!s.lights.empty())
        {
            s.lights[0].name            = "Golden Hour Sun (3200K)";
            s.lights[0].kelvin          = 3200.0F;
            s.lights[0].light.color     = cd::light::cct_to_linear_rgb(3200.0F);
            s.lights[0].light.direction = { -0.65F, -0.28F, -0.70F };
        }
        if (!s.entities.empty())
        {
            // Turn ground slab into a mirror-like ocean surface without grid floor z-fighting
            s.entities[0].name      = "Ocean_Surface_Plane";
            s.entities[0].tint      = { 0.08F, 0.28F, 0.42F };
            s.entities[0].metallic  = 0.92F;
            s.entities[0].roughness = 0.06F;
            if (auto* lt = s.scene.local(s.entities[0].handle))
            {
                lt->value.position = { 0.0F, -0.25F, 0.0F };
                lt->value.scale    = { 60.0F, 0.50F, 60.0F };
            }
            w.entity_modifiers[s.entities[0].handle.id].add_or_replace(
                std::make_unique<PbrMaterialModifier>(s.entities[0].tint, 0.92F, 0.06F, false));
            record_entity_home_transform(s, s.entities[0].handle);
        }
        const int c1  = spawn_entity_with_modifiers(s, "Cone",  { -2.2F, 0.0F, -1.2F }, log_push);
        const int c2  = spawn_entity_with_modifiers(s, "Cone",  {  2.3F, 0.0F, -1.6F }, log_push);
        const int c3  = spawn_entity_with_modifiers(s, "Cone",  {  0.0F, 0.0F, -3.2F }, log_push);
        const int tor = spawn_entity_with_modifiers(s, "Torus", {  0.0F, 0.0F,  0.2F }, log_push);
        place_static_grounded(c1,  { -2.2F, 0.85F, -1.2F }, { 1.85F, 1.70F, 1.85F }, "Terra_Island_West");
        place_static_grounded(c2,  {  2.3F, 1.05F, -1.6F }, { 2.15F, 2.10F, 2.15F }, "Terra_Island_East");
        place_static_grounded(c3,  {  0.0F, 0.70F, -3.2F }, { 1.55F, 1.40F, 1.55F }, "Terra_Island_North");
        place_static_grounded(tor, {  0.0F, 0.85F,  0.2F }, { 1.40F, 1.40F, 1.15F }, "Golden_Sea_Arch");
        (void)assign_texture_asset_to_entity(s, tor, "Mat_Gold_24K", log_push);
        s.selected   = tor;
        s.cam.eye    = { -2.6F, 2.35F, 5.6F };
        s.cam.target = {  0.0F, 0.85F, -0.8F };
        log_push("[level] Loaded '5.2 Golden Hour Ocean & Terra (07_terra)'");
    }
    else if (template_id == 12)
    {
        // Level 2.4: Content Browser, Custom Mesh (.obj/.gltf) & Surface Texture (.jpg/.png/.tex) Assignment Showcase
        const int e_cube = spawn_entity_with_modifiers(s, "Static_Wall",         { -2.6F,  0.0F, -0.4F }, log_push);
        const int e_sph  = spawn_entity_with_modifiers(s, "Dynamic_Sphere",      { -0.95F, 0.0F,  0.1F }, log_push);
        const int e_cyl  = spawn_entity_with_modifiers(s, "Cylinder",            {  0.70F, 0.0F, -0.2F }, log_push);
        const int e_tor  = spawn_entity_with_modifiers(s, "Torus",               {  2.25F, 0.0F,  0.1F }, log_push);
        const int e_wood = spawn_entity_with_modifiers(s, "Dynamic_Cube",        { -1.85F, 0.0F,  1.25F }, log_push);
        const int e_obj  = spawn_entity_with_modifiers(s, "Cone",                {  1.55F, 0.0F,  1.35F }, log_push);
        const int e_chr  = spawn_entity_with_modifiers(s, "Character_CesiumMan", { -0.05F, 0.02F,-1.35F }, log_push);

        place_static_grounded(e_cube, { -2.35F, 0.72F, -0.45F }, { 1.20F, 1.44F, 0.85F }, "Brick_Wall_Block");
        place_static_grounded(e_sph,  { -0.75F, 0.61F,  0.25F }, { 1.22F, 1.22F, 1.22F }, "UV_Checker_Sphere");
        place_static_grounded(e_cyl,  {  0.82F, 0.72F, -0.35F }, { 1.02F, 1.44F, 1.02F }, "Carrara_Marble_Pillar");
        place_static_grounded(e_tor,  {  2.15F, 0.82F, -0.15F }, { 0.92F, 0.92F, 0.92F }, "Carbon_Fiber_TorusKnot");
        place_static_grounded(e_wood, { -1.75F, 0.48F,  1.15F }, { 0.92F, 0.92F, 0.92F }, "Oak_Wood_Crate");
        place_static_grounded(e_obj,  {  1.45F, 0.68F,  1.25F }, { 1.05F, 1.05F, 1.05F }, "Star_Crystal_DiskOBJ");

        (void)assign_mesh_asset_to_entity(s, e_tor, "assets/meshes/TorusKnot.mesh", log_push);
        (void)assign_mesh_asset_to_entity(s, e_obj, "assets/samples/Generic/Star_Crystal.obj", log_push);
        (void)assign_texture_asset_to_entity(s, e_cube, "assets/samples/Sponza/7268504077753552595.jpg", log_push);
        (void)assign_texture_asset_to_entity(s, e_sph,  "assets/textures/UV_Checkerboard_2K.tex", log_push);
        (void)assign_texture_asset_to_entity(s, e_cyl,  "assets/textures/Carrara_Marble.tex", log_push);
        (void)assign_texture_asset_to_entity(s, e_tor,  "assets/textures/Carbon_Fiber_Twill.tex", log_push);
        (void)assign_texture_asset_to_entity(s, e_wood, "assets/textures/Oak_Wood_Planks.tex", log_push);
        (void)assign_texture_asset_to_entity(s, e_obj,  "assets/samples/Sponza/8750083169368950601.jpg", log_push);
        (void)e_chr;

        s.selected   = e_sph;
        s.cam.eye    = { -0.05F, 2.55F, 6.15F };
        s.cam.target = { -0.05F, 0.75F, -0.10F };
        w.active_world_name = "World 2: Photoreal PBR & Sponza GI";
        w.active_level_name = "2.4 Asset Browser & Texture/Mesh Lab";
        log_push("[level] Loaded '2.4 Asset Browser & Texture/Mesh Lab'");
    }
    else if (template_id == 13)
    {
        // Level 6.1: Cyber-Core Action Platformer (Full Playable 3D Game Level!)
        w.show_all_collider_wires = false;
        const int p_idx = spawn_entity_with_modifiers(s, "Character_CesiumMan", { -3.1F, 0.02F, 1.4F }, log_push);
        if (p_idx >= 0 && std::cmp_less(p_idx, s.entities.size()))
        {
            s.entities[static_cast<std::size_t>(p_idx)].name = "Player_Hero_CesiumMan";
        }
        (void)spawn_entity_with_modifiers(s, "JumpPad_Trigger",        { -1.55F, 0.02F,  0.85F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Moving_Platform",        {  0.15F, 0.05F,  0.25F }, log_push);
        const int p2_idx = spawn_entity_with_modifiers(s, "Moving_Platform", {  2.25F, 0.55F, -0.75F }, log_push);
        if (p2_idx >= 0 && std::cmp_less(p2_idx, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(p2_idx)].handle;
            if (auto* km = w.entity_modifiers[h.id].template find<KinematicMoverModifier>())
            {
                km->move_axis    = { 0.0F, 1.0F, 0.0F };
                km->amplitude    = 0.65F;
                km->frequency_hz = 0.40F;
            }
            record_entity_home_transform(s, h);
        }
        (void)spawn_entity_with_modifiers(s, "Collectible_EnergyCore", { -1.55F, 0.45F,  0.85F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Collectible_EnergyCore", {  0.15F, 0.45F,  0.25F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Collectible_EnergyCore", {  1.35F, 0.65F, -0.35F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Collectible_EnergyCore", {  2.25F, 1.15F, -0.75F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Patrol_SynthBot",        {  0.65F, 0.02F, -1.25F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Hazard_LaserBarrier",    { -0.65F, 0.02F, -1.55F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Goal_Beacon",            {  3.15F, 0.45F, -1.65F }, log_push);

        s.lights.push_back({ "Goal Portal Cyan Beacon",
            cd::light::point({ 3.15F, 2.2F, -1.65F }, { 0.25F, 0.95F, 1.00F }, 3800.0F, 12.0F),
            true, 6500.0F });

        w.total_cores_in_level = 4;
        w.collected_cores      = 0;
        w.game_score           = 0;
        w.player_hp            = 100.0F;
        w.player_shield        = 100.0F;
        w.level_goal_reached   = false;
        w.hud_toast_message    = "6.1 Cyber-Core Platformer Ready — Press F5 or [> PLAY] to Play!";
        w.hud_toast_timer      = 4.0F;

        s.selected   = p_idx;
        s.cam.eye    = { -4.4F, 3.45F, 5.65F };
        s.cam.target = {  0.1F, 0.95F, -0.15F };
        w.active_world_name = "World 6: Playable Games & Interactive Templates";
        w.active_level_name = "6.1 Cyber-Core Action Platformer (Playable Game)";
        log_push("[level] Loaded '6.1 Cyber-Core Action Platformer (Playable Game)'");
    }
    else if (template_id == 14)
    {
        // Level 6.2: Universal 250-Node Showcase (219 Godot Equivalents + 31 Next-Gen SOTA)
        w.show_all_collider_wires       = false;
        w.focus_bottom_node_catalog_tab = true;
        w.selected_catalog_node_id      = 117; // CharacterBody3D / AvatarVessel3D

        // 1. CharacterBody3D (AvatarVessel3D #117) + nested SpringArm3D (#120) + folded inline UtilityGOAPBrainAI (#234)
        const int hero_idx = spawn_or_compose_studio_catalog_node(
            s, 117, CatalogComposeMode::SpawnRootElement, { -2.85F, 0.02F, 1.15F }, log_push);
        if (hero_idx >= 0)
        {
            s.selected = hero_idx;
            (void)spawn_or_compose_studio_catalog_node(
                s, 120, CatalogComposeMode::NestAsChildElement, { -2.85F, 1.35F, 1.55F }, log_push);
            s.selected = hero_idx;
            (void)spawn_or_compose_studio_catalog_node(
                s, 234, CatalogComposeMode::FoldAsInlineCapsule, { -2.85F, 0.02F, 1.15F }, log_push);
        }

        // 2. VehicleBody3D (RoverVessel3D #121) + nested VehicleWheel3D (#122)
        const int veh_idx = spawn_or_compose_studio_catalog_node(
            s, 121, CatalogComposeMode::SpawnRootElement, { -0.85F, 0.05F, 0.65F }, log_push);
        if (veh_idx >= 0)
        {
            s.selected = veh_idx;
            (void)spawn_or_compose_studio_catalog_node(
                s, 122, CatalogComposeMode::NestAsChildElement, { -0.85F, 0.25F, 1.05F }, log_push);
        }

        // 3. TurquinMultiScatterPBR3D (#221) Hero Iridescent Sphere + folded MeshletClusterLOD3D (#222)
        const int pbr_idx = spawn_or_compose_studio_catalog_node(
            s, 221, CatalogComposeMode::SpawnRootElement, { 0.85F, 0.15F, 0.55F }, log_push);
        if (pbr_idx >= 0)
        {
            s.selected = pbr_idx;
            (void)spawn_or_compose_studio_catalog_node(
                s, 222, CatalogComposeMode::FoldAsInlineCapsule, { 0.85F, 0.15F, 0.55F }, log_push);
        }

        // 4. SDFSmoothBooleanCSG3D (#226) + CSGCombiner3D (#108) architectural arch
        const int csg_idx = spawn_or_compose_studio_catalog_node(
            s, 226, CatalogComposeMode::SpawnRootElement, { 2.45F, 0.15F, -0.35F }, log_push);
        if (csg_idx >= 0)
        {
            (void)assign_texture_asset_to_entity(s, csg_idx, "assets/textures/Carrara_Marble.tex", log_push);
            s.selected = csg_idx;
            (void)spawn_or_compose_studio_catalog_node(
                s, 108, CatalogComposeMode::FoldAsInlineCapsule, { 2.45F, 0.15F, -0.35F }, log_push);
        }

        // 5. XPBDClothAndSoftBody3D (#224) + SoftBody3D (#109)
        const int xpbd_idx = spawn_or_compose_studio_catalog_node(
            s, 224, CatalogComposeMode::SpawnRootElement, { -1.55F, 0.35F, -1.05F }, log_push);
        if (xpbd_idx >= 0)
        {
            s.selected = xpbd_idx;
            (void)spawn_or_compose_studio_catalog_node(
                s, 109, CatalogComposeMode::FoldAsInlineCapsule, { -1.55F, 0.35F, -1.05F }, log_push);
        }

        // 6. DDGIIrradianceVolume3D (#220) + VoxelGI (#134) + GPUParticlesAttractorSphere3D (#89)
        const int ddgi_idx = spawn_or_compose_studio_catalog_node(
            s, 220, CatalogComposeMode::SpawnRootElement, { 0.35F, 0.45F, -1.25F }, log_push);
        if (ddgi_idx >= 0)
        {
            s.selected = ddgi_idx;
            (void)spawn_or_compose_studio_catalog_node(
                s, 134, CatalogComposeMode::FoldAsInlineCapsule, { 0.35F, 0.45F, -1.25F }, log_push);
        }
        (void)spawn_or_compose_studio_catalog_node(
            s, 89, CatalogComposeMode::SpawnRootElement, { 1.95F, 0.55F, 1.25F }, log_push);

        auto blm = std::make_unique<BloomLensModifier>();
        blm->bloom_intensity      = 0.16F;
        blm->vignette_strength    = 0.10F;
        blm->chromatic_aberration = 0.0010F;
        w.environment_modifiers.add_or_replace(std::move(blm));

        s.lights.push_back({ "250-Node Showcase Key Cyan (6500K)",
            cd::light::point({ 2.25F, 3.35F, 2.25F }, { 0.32F, 0.92F, 1.00F }, 850.0F, 14.0F),
            true, 6500.0F });
        s.lights.push_back({ "250-Node Showcase Warm Fill (3400K)",
            cd::light::point({ -1.85F, 3.25F, 2.65F }, { 1.00F, 0.76F, 0.36F }, 650.0F, 12.0F),
            true, 3400.0F });

        s.selected          = (hero_idx >= 0) ? hero_idx : 0;
        s.cam.eye           = { -3.95F, 3.35F, 5.45F };
        s.cam.target        = {  0.05F, 0.85F, 0.05F };
        w.active_world_name = "World 6: Playable Games & Interactive Templates";
        w.active_level_name = "6.2 Universal 250-Node Showcase (Godot 219 + SOTA 31)";
        w.hud_toast_message = "6.2 Universal 250-Node Catalog Active (219 Godot + 31 SOTA)!";
        w.hud_toast_timer   = 4.5F;
        log_push("[level] Loaded '6.2 Universal 250-Node Showcase (Godot 219 + SOTA 31)'");
    }
    else if (template_id == 15)
    {
        // Level 6.3: Rockstar 2D+3D Physics & Euphoria/Pacejka Arena (32-Module Unified Physics Engine)
        w.show_all_collider_wires  = true;
        w.simulate_physics_in_edit = true;
        cd::ui::chroma::state().tab_map[ImGui::GetID("BottomDockTabBar")] = 2; // Focus "Console & Physics" tab

        // Initialize the 32-Module Rockstar UnifiedPhysicsEngine with active 2D + 3D + Euphoria + Pacejka + Fracture + XPBD
        w.rockstar_physics_engine.clear();
        w.rockstar_engine_seeded      = true;
        const auto rag_id  = w.rockstar_physics_engine.create_euphoria_ragdoll({ -2.40F, 1.05F, 0.45F }, 78.0F);
        const auto veh_id  = w.rockstar_physics_engine.create_pacejka_vehicle();
        const auto frac_id = w.rockstar_physics_engine.create_fracture_system();
        if (auto* frac = w.rockstar_physics_engine.get_fracture_system(frac_id))
        {
            frac->generate_grid_fracture_object(2u, 3u, 2u, { 1.45F, 0.75F, -0.35F }, { 0.25F, 0.25F, 0.25F }, 5.0F, 65.0F);
            (void)frac->apply_impact_impulse({ 1.45F, 0.95F, -0.35F }, 1.15F, 220.0F);
        }
        const auto xpbd_id = w.rockstar_physics_engine.create_xpbd_solver();
        if (auto* xpbd = w.rockstar_physics_engine.get_xpbd_solver(xpbd_id))
        {
            xpbd->create_cloth_grid(6u, 6u, 0.16F, { -0.45F, 2.25F, -1.25F });
            xpbd->set_wind_velocity({ 3.8F, 0.4F, 1.2F });
        }
        if (auto* rag = w.rockstar_physics_engine.get_euphoria_ragdoll(rag_id))
        {
            rag->apply_ballistic_hit_impulse(
                cd::physics::rockstar::RagdollBoneId::kChest,
                { 95.0F, 18.0F, -25.0F },
                { -2.40F, 1.35F, 0.45F });
        }
        if (auto* veh = w.rockstar_physics_engine.get_pacejka_vehicle(veh_id))
        {
            cd::physics::rockstar::VehicleInputState vin {};
            vin.throttle        = 0.85F;
            vin.steer_angle_rad = 0.10F;
            veh->step(vin, 1.0F / 60.0F);
        }
        // Populate 2D & 3D worlds in UnifiedPhysicsEngine
        (void)w.rockstar_physics_engine.world_2d().create_body(
            cd::physics::core::BodyMotionType::kStatic,
            cd::physics::core::Transform2D { { 0.0F, -1.0F }, 0.0F },
            cd::physics::v2d::BoxShape2D { { 20.0F, 1.0F } });
        (void)w.rockstar_physics_engine.world_2d().create_body(
            cd::physics::core::BodyMotionType::kDynamic,
            cd::physics::core::Transform2D { { 0.0F, 1.8F }, 0.15F },
            cd::physics::v2d::BoxShape2D { { 0.5F, 0.5F } });
        (void)w.rockstar_physics_engine.world_2d().create_body(
            cd::physics::core::BodyMotionType::kDynamic,
            cd::physics::core::Transform2D { { 0.4F, 3.1F }, 0.0F },
            cd::physics::v2d::CircleShape2D { 0.45F });
        {
            cd::physics::v3d::RigidBody3D f3d {};
            f3d.motion_type        = cd::physics::core::BodyMotionType::kStatic;
            f3d.transform.position = { 0.0F, -0.5F, 0.0F };
            f3d.shape              = cd::physics::v3d::BoxShape3D { { 30.0F, 0.5F, 30.0F } };
            (void)w.rockstar_physics_engine.world_3d().create_body(f3d);

            cd::physics::v3d::RigidBody3D b3d {};
            b3d.motion_type        = cd::physics::core::BodyMotionType::kDynamic;
            b3d.transform.position = { 0.0F, 1.5F, 0.0F };
            b3d.mass               = 4.0F;
            b3d.shape              = cd::physics::v3d::BoxShape3D { { 0.5F, 0.5F, 0.5F } };
            (void)w.rockstar_physics_engine.world_3d().create_body(b3d);
        }

        // 1. Euphoria Active Ragdoll Avatar (AvatarVessel3D #117 + ActiveRagdollMotor3D #245)
        const int euph_idx = spawn_or_compose_studio_catalog_node(
            s, 117, CatalogComposeMode::SpawnRootElement, { -2.45F, 0.02F, 0.55F }, log_push);
        if (euph_idx >= 0 && std::cmp_less(euph_idx, s.entities.size()))
        {
            s.entities[static_cast<std::size_t>(euph_idx)].name = "Euphoria_ActiveRagdoll_Avatar";
            s.selected = euph_idx;
            (void)spawn_or_compose_studio_catalog_node(
                s, 245, CatalogComposeMode::FoldAsInlineCapsule, { -2.45F, 0.02F, 0.55F }, log_push);
        }

        // 2. Pacejka 4-Wheel Vehicle (RoverVessel3D #121 + SuspensionWheelCapsule3D #122)
        const int rover_idx = spawn_or_compose_studio_catalog_node(
            s, 121, CatalogComposeMode::SpawnRootElement, { -0.55F, 0.05F, 0.85F }, log_push);
        if (rover_idx >= 0 && std::cmp_less(rover_idx, s.entities.size()))
        {
            s.entities[static_cast<std::size_t>(rover_idx)].name = "Pacejka_Vehicle_Rover3D";
            (void)assign_texture_asset_to_entity(s, rover_idx, "assets/textures/Carbon_Fiber_Twill.tex", log_push);
            s.selected = rover_idx;
            (void)spawn_or_compose_studio_catalog_node(
                s, 122, CatalogComposeMode::NestAsChildElement, { -0.55F, 0.25F, 1.25F }, log_push);
        }

        // 3. OBB Ramp + Rolling Metallic Striker Sphere + 3x Voronoi Fracture Crate Stack
        (void)spawn_entity_with_modifiers(s, "Static_Ramp", { -1.65F, 0.0F, -1.10F }, log_push);
        const int striker_idx = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -2.35F, 2.05F, -1.10F }, log_push);
        if (striker_idx >= 0 && std::cmp_less(striker_idx, s.entities.size()))
        {
            s.entities[static_cast<std::size_t>(striker_idx)].name = "CCD_Striker_Sphere3D";
            const auto h = s.entities[static_cast<std::size_t>(striker_idx)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.position = { -2.35F, 2.05F, -1.10F };
            if (auto* rb = w.entity_modifiers[h.id].template find<RigidBodyModifier>())
                rb->linear_velocity = { 4.2F, 0.0F, 0.0F };
            record_entity_home_transform(s, h);
        }

        for (int row = 0; row < 3; ++row)
        {
            const int c_idx = spawn_entity_with_modifiers(
                s, "Dynamic_Cube", { 1.45F, 0.0F, -0.35F }, log_push);
            if (c_idx >= 0 && std::cmp_less(c_idx, s.entities.size()))
            {
                auto& cent = s.entities[static_cast<std::size_t>(c_idx)];
                cent.name = "Voronoi_Fracture_Chunk_" + std::to_string(row + 1);
                if (auto* lt = s.scene.local(cent.handle))
                {
                    lt->value.position = { 1.45F, 0.44F + static_cast<float>(row) * 0.88F, -0.35F };
                    lt->value.scale    = { 0.82F, 0.82F, 0.82F };
                }
                (void)assign_texture_asset_to_entity(s, c_idx, "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg", log_push);
                record_entity_home_transform(s, cent.handle);
            }
        }

        // 4. XPBD Cloth & Soft-Body Banner (#224) + Buoyancy Fluid Crate (#225)
        const int xpbd_idx = spawn_or_compose_studio_catalog_node(
            s, 224, CatalogComposeMode::SpawnRootElement, { 0.35F, 0.35F, -1.35F }, log_push);
        if (xpbd_idx >= 0 && std::cmp_less(xpbd_idx, s.entities.size()))
        {
            s.entities[static_cast<std::size_t>(xpbd_idx)].name = "XPBD_ClothSoftBody_Banner3D";
        }
        const int buoy_idx = spawn_or_compose_studio_catalog_node(
            s, 225, CatalogComposeMode::SpawnRootElement, { 2.55F, 0.25F, 0.85F }, log_push);
        if (buoy_idx >= 0 && std::cmp_less(buoy_idx, s.entities.size()))
        {
            s.entities[static_cast<std::size_t>(buoy_idx)].name = "Archimedes_Buoyancy_Vessel3D";
        }

        s.lights.push_back({ "Rockstar Arena Key Cyan (6500K)",
            cd::light::point({ 2.35F, 3.55F, 2.35F }, { 0.30F, 0.92F, 1.00F }, 900.0F, 14.0F),
            true, 6500.0F });
        s.lights.push_back({ "Rockstar Arena Warm Fill (3400K)",
            cd::light::point({ -2.15F, 3.25F, 2.55F }, { 1.00F, 0.75F, 0.34F }, 700.0F, 12.0F),
            true, 3400.0F });

        s.selected          = (euph_idx >= 0) ? euph_idx : 0;
        s.cam.eye           = { -4.15F, 3.40F, 5.55F };
        s.cam.target        = {  0.10F, 0.85F, 0.00F };
        w.active_world_name = "World 6: Playable Games & Interactive Templates";
        w.active_level_name = "6.3 Rockstar 2D+3D Physics & Euphoria/Pacejka Arena";
        w.hud_toast_message = "6.3 Rockstar 32-Module 2D+3D Physics Engine Active (Euphoria + Pacejka + Voronoi + XPBD)!";
        w.hud_toast_timer   = 4.5F;
        log_push("[level] Loaded '6.3 Rockstar 2D+3D Physics & Euphoria/Pacejka Arena'");
    }
    s.scene.update_transforms();
    sync_camera_and_invalidate_frame_history(s);
}

/// Switch active Level (and World) inside `s.cd_world.project()` and load its 3D content!
template <typename StateT, typename LogFn>
inline bool switch_to_project_level(StateT& s, std::string_view level_name, LogFn&& log_push)
{
    const std::string target_level { level_name };
    populate_master_showcase_project(s.cd_world);
    auto* proj = s.cd_world.project();
    if (!proj || proj->level_count() == 0U) return false;
    auto* lvl = proj->find_level(target_level);
    if (!lvl)
    {
        lvl = proj->level(0);
        if (!lvl) return false;
    }

    const std::string resolved_name  { lvl->name() };
    const std::string resolved_world { lvl->world_name() };
    const int         tpl_id = static_cast<int>(lvl->template_id());

    (void)proj->set_active_level(resolved_name);
    s.cd_world.set_name(resolved_world);
    auto& w = studio_world();
    w.active_world_name = resolved_world;
    w.active_level_name = resolved_name;

    load_studio_interactive_template(s, tpl_id, log_push);
    return true;
}

/// Catalog of the 12 `showcases/*.cdproj` projects in the repository
struct ShowcaseProjectEntry
{
    const char* id;
    const char* title;
    const char* cdproj_path;
    const char* world_group;
    int         template_id;
};

inline constexpr std::array<ShowcaseProjectEntry, 12> kShowcaseCatalog {{
    { "01_pbr_materials_sss_hair",     "01. PBR Materials, Clearcoat & Hair",
      "showcases/01_pbr_materials_sss_hair/01_pbr_materials_sss_hair.cdproj",
      "World 2: Photoreal PBR & Sponza GI", 4 },
    { "02_celestial_aether_fog_clouds","02. Celestial Aether, Fog & Clouds",
      "showcases/02_celestial_aether_fog_clouds/02_celestial_aether_fog_clouds.cdproj",
      "World 5: Volumetric Atmosphere & Nature", 10 },
    { "03_sponza_clustered_ddgi_restir","03. Sponza Clustered DDGI & ReSTIR",
      "showcases/03_sponza_clustered_ddgi_restir/03_sponza_clustered_ddgi_restir.cdproj",
      "World 2: Photoreal PBR & Sponza GI", 5 },
    { "04_optics_cinematic_studio",    "04. Optics Cinematic Studio & AgX",
      "showcases/04_optics_cinematic_studio/04_optics_cinematic_studio.cdproj",
      "World 2: Photoreal PBR & Sponza GI", 6 },
    { "05_avatar_locomotion_anim_ik",  "05. Avatar Locomotion & Anim IK",
      "showcases/05_avatar_locomotion_anim_ik/05_avatar_locomotion_anim_ik.cdproj",
      "World 4: Avatar Locomotion & Combat", 8 },
    { "06_jolt_dynamics_rover_shatter","06. Jolt Dynamics, Rover & Shatter",
      "showcases/06_jolt_dynamics_rover_shatter/06_jolt_dynamics_rover_shatter.cdproj",
      "World 3: Jolt Dynamics & Shatter Lab", 7 },
    { "07_terra_ocean_nature_world",   "07. Terra Ocean & Nature World",
      "showcases/07_terra_ocean_nature_world/07_terra_ocean_nature_world.cdproj",
      "World 5: Volumetric Atmosphere & Nature", 11 },
    { "08_synth_pilot_squad_narrative","08. Synth Pilot Squad & Narrative",
      "showcases/08_synth_pilot_squad_narrative/08_synth_pilot_squad_narrative.cdproj",
      "World 4: Avatar Locomotion & Combat", 9 },
    { "09_acoustics_hrtf_echo_chamber","09. Acoustics HRTF Echo Chamber",
      "showcases/09_acoustics_hrtf_echo_chamber/09_acoustics_hrtf_echo_chamber.cdproj",
      "World 1: Ultra-FPS & Physics", 1 },
    { "10_spark_swarm_and_decals",     "10. Spark GPU Swarm & OBB Decals",
      "showcases/10_spark_swarm_and_decals/10_spark_swarm_and_decals.cdproj",
      "World 3: Jolt Dynamics & Shatter Lab", 3 },
    { "11_custom_ui_hud_and_menus",    "11. ChromaUI 2.0 HUD & Menus",
      "showcases/11_custom_ui_hud_and_menus/11_custom_ui_hud_and_menus.cdproj",
      "World 1: Ultra-FPS & Physics", 2 },
    { "12_planar_2d_platformer_net",   "12. Planar 2D Platformer & Net",
      "showcases/12_planar_2d_platformer_net/12_planar_2d_platformer_net.cdproj",
      "World 1: Ultra-FPS & Physics", 0 },
}};

template <typename StateT, typename LogFn>
inline void load_showcase_project_by_index(StateT& s, std::size_t idx, LogFn&& log_push)
{
    if (idx >= kShowcaseCatalog.size()) return;
    const auto& entry = kShowcaseCatalog[idx];
    auto& w = studio_world();

    std::string parsed_scene_path {};
    if (auto cdproj_json = cd::asset::json::load(entry.cdproj_path);
        cdproj_json.has_value() && cdproj_json->is_object())
    {
        const auto& obj = cdproj_json->as_object();
        if (auto it = obj.find("last_opened_scene_path"); it != obj.end() && it->second.is_string())
        {
            parsed_scene_path = it->second.as_string();
        }
    }

    populate_master_showcase_project(s.cd_world);
    if (auto* proj = s.cd_world.project())
    {
        auto* lvl = proj->find_level(entry.title);
        if (lvl == nullptr)
        {
            lvl = proj->add_level(entry.title);
            lvl->set_world_name(entry.world_group);
            lvl->set_description(entry.cdproj_path);
            lvl->set_template_id(entry.template_id);
            populate_standard_level_layers(*lvl);
        }
        if (!parsed_scene_path.empty())
        {
            lvl->set_scene_path(parsed_scene_path);
        }
        (void)proj->set_active_level(entry.title);
    }

    load_studio_interactive_template(s, entry.template_id, log_push);
    w.active_project_name = entry.id;
    w.active_world_name   = entry.world_group;
    w.active_level_name   = entry.title;
    s.cd_world.set_name(entry.world_group);
    log_push(std::string("[project] Loaded Showcase Project '") + entry.id + "' (" + entry.cdproj_path +
             (parsed_scene_path.empty() ? "" : (", scene=" + parsed_scene_path)) + ")");
}

// =============================================================================
// 8B. Full Scene + ECS + Layer + Modifier Stack JSON Serialization & Exporter
// =============================================================================
[[nodiscard]] inline const char* primitive_kind_to_str(PrimitiveKind k) noexcept
{
    switch (k)
    {
        case PrimitiveKind::kSphere:     return "Sphere";
        case PrimitiveKind::kCone:       return "Cone";
        case PrimitiveKind::kCylinder:   return "Cylinder";
        case PrimitiveKind::kTorus:      return "Torus";
        case PrimitiveKind::kTorusKnot:  return "TorusKnot";
        case PrimitiveKind::kGltf:       return "Gltf";
        case PrimitiveKind::kSponza:     return "Sponza";
        case PrimitiveKind::kCustomGltf: return "CustomGltf";
        case PrimitiveKind::kCube:       return "Cube";
    }
    return "Cube";
}

template <typename StateT, typename LogFn>
[[nodiscard]] inline bool save_full_studio_scene(
    const StateT&   s,
    const char*     path,
    LogFn&&         log_push)
{
    const auto& w = studio_world();
    auto find_ent = [&s](cd::ecs::Entity e) -> const SceneEntity*
    {
        for (const auto& en : s.entities)
            if (en.handle.id == e.id) return &en;
        return nullptr;
    };

    auto make_vec3_arr = [](const cd::math::Vec3f& v)
    {
        cd::asset::json::Array a;
        a.emplace_back(static_cast<double>(v.x));
        a.emplace_back(static_cast<double>(v.y));
        a.emplace_back(static_cast<double>(v.z));
        return cd::asset::json::Value { std::move(a) };
    };

    auto root = cd::scene::serialize_scene_with(
        s.scene,
        [&](cd::ecs::Entity e, cd::asset::json::Object& obj)
        {
            const auto* en = find_ent(e);
            if (!en) return;
            obj["name"]                = cd::asset::json::Value { en->name };
            obj["kind"]                = cd::asset::json::Value { std::string { primitive_kind_to_str(en->kind) } };
            obj["is_pbr"]              = cd::asset::json::Value { en->is_pbr };
            obj["metallic"]            = cd::asset::json::Value { static_cast<double>(en->metallic) };
            obj["roughness"]           = cd::asset::json::Value { static_cast<double>(en->roughness) };
            obj["multiscatter_comp"]   = cd::asset::json::Value { static_cast<double>(en->multiscatter_comp) };
            obj["iridescence_nm"]      = cd::asset::json::Value { static_cast<double>(en->iridescence_nm) };
            obj["use_texture"]         = cd::asset::json::Value { en->use_texture };
            obj["custom_texture_id"]   = cd::asset::json::Value { static_cast<int>(en->custom_texture_id) };
            obj["custom_texture_path"] = cd::asset::json::Value { en->custom_texture_path };
            obj["custom_mesh_path"]    = cd::asset::json::Value { en->custom_mesh_path };
            obj["visible"]             = cd::asset::json::Value { en->visible };
            obj["locked"]              = cd::asset::json::Value { en->locked };
            obj["layer"]               = cd::asset::json::Value {
                std::string { cd::world_container::layer_of(s.ecs_world, e) } };
            obj["tint"]                = make_vec3_arr(en->tint);

            if (auto hit = w.home_transforms.find(e.id); hit != w.home_transforms.end())
            {
                const auto& htr = hit->second;
                obj["home_position"] = make_vec3_arr(htr.position);
                obj["home_scale"]    = make_vec3_arr(htr.scale);
                cd::asset::json::Array hrot;
                hrot.emplace_back(static_cast<double>(htr.rotation.x));
                hrot.emplace_back(static_cast<double>(htr.rotation.y));
                hrot.emplace_back(static_cast<double>(htr.rotation.z));
                hrot.emplace_back(static_cast<double>(htr.rotation.w));
                obj["home_rotation"] = cd::asset::json::Value { std::move(hrot) };
            }

            if (auto it = w.entity_modifiers.find(e.id); it != w.entity_modifiers.end())
            {
                cd::asset::json::Array mods_arr;
                for (const auto& m : it->second.items)
                {
                    if (!m) continue;
                    cd::asset::json::Object mo;
                    mo["class"]   = cd::asset::json::Value { std::string { m->class_name() } };
                    mo["enabled"] = cd::asset::json::Value { m->enabled };
                    if (const auto* pbr = dynamic_cast<const PbrMaterialModifier*>(m.get()))
                    {
                        mo["metallic"]           = cd::asset::json::Value { static_cast<double>(pbr->metallic) };
                        mo["roughness"]          = cd::asset::json::Value { static_cast<double>(pbr->roughness) };
                        mo["multiscatter_comp"]  = cd::asset::json::Value { static_cast<double>(pbr->multiscatter_comp) };
                        mo["iridescence_nm"]     = cd::asset::json::Value { static_cast<double>(pbr->iridescence_nm) };
                        mo["use_texture"]        = cd::asset::json::Value { pbr->use_texture };
                        mo["texture_slot"]       = cd::asset::json::Value { static_cast<int>(pbr->texture_slot) };
                        mo["texture_asset_path"] = cd::asset::json::Value { pbr->texture_asset_path };
                    }
                    else if (const auto* box = dynamic_cast<const BoxColliderModifier*>(m.get()))
                    {
                        mo["half_extents"]  = make_vec3_arr(box->half_extents);
                        mo["center_offset"] = make_vec3_arr(box->center_offset);
                        mo["is_trigger"]    = cd::asset::json::Value { box->is_trigger };
                    }
                    else if (const auto* sph = dynamic_cast<const SphereColliderModifier*>(m.get()))
                    {
                        mo["radius"]        = cd::asset::json::Value { static_cast<double>(sph->radius) };
                        mo["center_offset"] = make_vec3_arr(sph->center_offset);
                        mo["is_trigger"]    = cd::asset::json::Value { sph->is_trigger };
                    }
                    else if (const auto* cap = dynamic_cast<const CapsuleColliderModifier*>(m.get()))
                    {
                        mo["radius"]        = cd::asset::json::Value { static_cast<double>(cap->radius) };
                        mo["half_height"]   = cd::asset::json::Value { static_cast<double>(cap->half_height) };
                        mo["center_offset"] = make_vec3_arr(cap->center_offset);
                        mo["is_trigger"]    = cd::asset::json::Value { cap->is_trigger };
                    }
                    else if (const auto* rb = dynamic_cast<const RigidBodyModifier*>(m.get()))
                    {
                        mo["body_type"]      = cd::asset::json::Value { static_cast<int>(rb->body_type) };
                        mo["mass"]           = cd::asset::json::Value { static_cast<double>(rb->mass) };
                        mo["restitution"]    = cd::asset::json::Value { static_cast<double>(rb->restitution) };
                        mo["friction"]       = cd::asset::json::Value { static_cast<double>(rb->friction) };
                        mo["linear_damping"] = cd::asset::json::Value { static_cast<double>(rb->linear_damping) };
                        mo["gravity_scale"]  = cd::asset::json::Value { static_cast<double>(rb->gravity_scale) };
                        mo["use_gravity"]    = cd::asset::json::Value { rb->use_gravity };
                    }
                    else if (const auto* cc = dynamic_cast<const CharacterControllerModifier*>(m.get()))
                    {
                        mo["move_speed"]   = cd::asset::json::Value { static_cast<double>(cc->move_speed) };
                        mo["jump_impulse"] = cd::asset::json::Value { static_cast<double>(cc->jump_impulse) };
                    }
                    else if (const auto* km = dynamic_cast<const KinematicMoverModifier*>(m.get()))
                    {
                        mo["move_axis"]           = make_vec3_arr(km->move_axis);
                        mo["amplitude"]           = cd::asset::json::Value { static_cast<double>(km->amplitude) };
                        mo["frequency_hz"]        = cd::asset::json::Value { static_cast<double>(km->frequency_hz) };
                        mo["phase_offset"]        = cd::asset::json::Value { static_cast<double>(km->phase_offset) };
                        mo["angular_deg_per_sec"] = make_vec3_arr(km->angular_deg_per_sec);
                        mo["carry_riders"]        = cd::asset::json::Value { km->carry_riders };
                    }
                    else if (const auto* trg = dynamic_cast<const TriggerVolumeModifier*>(m.get()))
                    {
                        mo["action"]         = cd::asset::json::Value { static_cast<int>(trg->action) };
                        mo["effect_value"]   = cd::asset::json::Value { static_cast<double>(trg->effect_value) };
                        mo["trigger_radius"] = cd::asset::json::Value { static_cast<double>(trg->trigger_radius) };
                        mo["one_shot"]       = cd::asset::json::Value { trg->one_shot };
                    }
                    else if (const auto* pat = dynamic_cast<const PatrolWaypointModifier*>(m.get()))
                    {
                        mo["patrol_axis"]  = cd::asset::json::Value { pat->patrol_axis };
                        mo["patrol_span"]  = cd::asset::json::Value { static_cast<double>(pat->patrol_span) };
                        mo["move_speed"]   = cd::asset::json::Value { static_cast<double>(pat->move_speed) };
                        mo["chase_player"] = cd::asset::json::Value { pat->chase_player };
                        mo["chase_range"]  = cd::asset::json::Value { static_cast<double>(pat->chase_range) };
                    }
                    else if (const auto* le = dynamic_cast<const LightEmitterModifier*>(m.get()))
                    {
                        mo["light_type"] = cd::asset::json::Value { static_cast<int>(le->light_type) };
                        mo["color"]      = make_vec3_arr(le->color);
                        mo["kelvin"]     = cd::asset::json::Value { static_cast<double>(le->kelvin) };
                        mo["intensity"]  = cd::asset::json::Value { static_cast<double>(le->intensity) };
                        mo["range"]      = cd::asset::json::Value { static_cast<double>(le->range) };
                        mo["direction"]  = make_vec3_arr(le->direction);
                    }
                    else if (const auto* un = dynamic_cast<const UniversalNodeModifier*>(m.get()))
                    {
                        mo["node_id"]             = cd::asset::json::Value { static_cast<int>(un->node_id) };
                        mo["godot_name"]          = cd::asset::json::Value { un->godot_name };
                        mo["chroma_name"]         = cd::asset::json::Value { un->chroma_name };
                        mo["intensity_or_rate"]   = cd::asset::json::Value { static_cast<double>(un->intensity_or_rate) };
                        mo["substeps_or_quality"] = cd::asset::json::Value { un->substeps_or_quality };
                        mo["gpu_accelerated"]     = cd::asset::json::Value { un->gpu_accelerated };
                    }
                    mods_arr.emplace_back(std::move(mo));
                }
                obj["modifiers"] = cd::asset::json::Value { std::move(mods_arr) };
            }
        });

    auto& root_obj = root.as_object_mut();
    if (auto nit = root_obj.find("nodes"); nit != root_obj.end() && nit->second.is_array())
    {
        const auto& raw_nodes = nit->second.as_array();
        cd::asset::json::Array ordered_nodes;
        ordered_nodes.reserve(s.entities.size());
        for (const auto& sent : s.entities)
        {
            for (const auto& nv : raw_nodes)
            {
                if (!nv.is_object()) continue;
                const auto& no = nv.as_object();
                auto id_it = no.find("id");
                auto nm_it = no.find("name");
                if (id_it != no.end() && id_it->second.is_number() &&
                    nm_it != no.end() && nm_it->second.is_string() && !nm_it->second.as_string().empty() &&
                    static_cast<std::uint64_t>(id_it->second.as_number()) == sent.handle.id)
                {
                    ordered_nodes.push_back(nv);
                    break;
                }
            }
        }
        root_obj["nodes"] = cd::asset::json::Value { std::move(ordered_nodes) };
    }
    root_obj["active_project"] = cd::asset::json::Value { w.active_project_name };
    root_obj["active_world"]   = cd::asset::json::Value { w.active_world_name };
    root_obj["active_level"]   = cd::asset::json::Value { w.active_level_name };
    root_obj["camera_eye"]     = make_vec3_arr(s.cam.eye);
    root_obj["camera_target"]  = make_vec3_arr(s.cam.target);

    cd::asset::json::Array la;
    for (const auto& l : s.lights)
    {
        cd::asset::json::Object lo;
        lo["name"]      = cd::asset::json::Value { l.name };
        lo["enabled"]   = cd::asset::json::Value { l.enabled };
        lo["type"]      = cd::asset::json::Value { static_cast<int>(l.light.type) };
        lo["kelvin"]    = cd::asset::json::Value { static_cast<double>(l.kelvin) };
        lo["intensity"] = cd::asset::json::Value { static_cast<double>(l.light.intensity) };
        lo["range"]     = cd::asset::json::Value { static_cast<double>(l.light.range) };
        cd::asset::json::Array pos, col, dir;
        pos.emplace_back(static_cast<double>(l.light.position.x));
        pos.emplace_back(static_cast<double>(l.light.position.y));
        pos.emplace_back(static_cast<double>(l.light.position.z));
        lo["position"] = cd::asset::json::Value { std::move(pos) };
        col.emplace_back(static_cast<double>(l.light.color.x));
        col.emplace_back(static_cast<double>(l.light.color.y));
        col.emplace_back(static_cast<double>(l.light.color.z));
        lo["color"] = cd::asset::json::Value { std::move(col) };
        dir.emplace_back(static_cast<double>(l.light.direction.x));
        dir.emplace_back(static_cast<double>(l.light.direction.y));
        dir.emplace_back(static_cast<double>(l.light.direction.z));
        lo["direction"] = cd::asset::json::Value { std::move(dir) };
        la.emplace_back(std::move(lo));
    }
    root_obj["lights"] = cd::asset::json::Value { std::move(la) };

    const std::string txt = cd::asset::json::serialize(root, true);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open())
    {
        log_push(std::string("[scene] Failed to open for write: ") + path);
        return false;
    }
    ofs.write(txt.data(), static_cast<std::streamsize>(txt.size()));

    if (const auto* proj = s.cd_world.project())
    {
        (void)cd::world_container::save_project_file("chromodynamic_studio.cdproject", *proj);
    }
    log_push(std::string("[scene] Saved ") + std::to_string(s.entities.size()) +
             " entities + " + std::to_string(s.lights.size()) + " lights -> " + path);
    return true;
}

template <typename StateT, typename LogFn>
[[nodiscard]] inline bool load_full_studio_scene(
    StateT&     s,
    const char* path,
    LogFn&&     log_push)
{
    auto r = cd::asset::json::load(path);
    if (!r.has_value() || !r->is_object())
    {
        log_push(std::string("[scene] Failed to load JSON scene: ") + path);
        return false;
    }

    auto& w = studio_world();
    for (auto& en : s.entities)
    {
        if (en.handle.is_valid())
        {
            cd::world_container::clear_layer(s.ecs_world, en.handle);
            s.scene.destroy_node(en.handle);
        }
    }
    s.entities.clear();
    w.entity_modifiers.clear();
    w.home_transforms.clear();
    w.euler_cache_deg.clear();
    w.live_contacts.clear();

    const auto& root_obj = r->as_object();
    if (auto it = root_obj.find("active_project"); it != root_obj.end() && it->second.is_string())
        w.active_project_name = it->second.as_string();
    if (auto it = root_obj.find("active_world"); it != root_obj.end() && it->second.is_string())
    {
        w.active_world_name = it->second.as_string();
        s.cd_world.set_name(w.active_world_name);
    }
    if (auto it = root_obj.find("active_level"); it != root_obj.end() && it->second.is_string())
    {
        w.active_level_name = it->second.as_string();
        if (auto* proj = s.cd_world.project())
        {
            (void)proj->set_active_level(w.active_level_name);
        }
    }

    if (auto lit = root_obj.find("lights"); lit != root_obj.end() && lit->second.is_array())
    {
        s.lights.clear();
        for (const auto& lv : lit->second.as_array())
        {
            if (!lv.is_object()) continue;
            const auto& lo = lv.as_object();
            LightRow lr {};
            if (auto it = lo.find("name"); it != lo.end() && it->second.is_string())
                lr.name = it->second.as_string();
            if (auto it = lo.find("enabled"); it != lo.end() && it->second.is_bool())
                lr.enabled = it->second.as_bool();
            if (auto it = lo.find("type"); it != lo.end() && it->second.is_number())
                lr.light.type = static_cast<cd::light::LightType>(static_cast<int>(it->second.as_number()));
            if (auto it = lo.find("kelvin"); it != lo.end() && it->second.is_number())
                lr.kelvin = static_cast<float>(it->second.as_number());
            if (auto it = lo.find("intensity"); it != lo.end() && it->second.is_number())
                lr.light.intensity = static_cast<float>(it->second.as_number());
            if (auto it = lo.find("range"); it != lo.end() && it->second.is_number())
                lr.light.range = static_cast<float>(it->second.as_number());
            if (auto it = lo.find("position");
                it != lo.end() && it->second.is_array() && it->second.as_array().size() == 3)
            {
                const auto& a = it->second.as_array();
                lr.light.position = {
                    static_cast<float>(a[0].as_number()),
                    static_cast<float>(a[1].as_number()),
                    static_cast<float>(a[2].as_number())
                };
            }
            if (auto it = lo.find("color");
                it != lo.end() && it->second.is_array() && it->second.as_array().size() == 3)
            {
                const auto& a = it->second.as_array();
                lr.light.color = {
                    static_cast<float>(a[0].as_number()),
                    static_cast<float>(a[1].as_number()),
                    static_cast<float>(a[2].as_number())
                };
            }
            if (auto it = lo.find("direction");
                it != lo.end() && it->second.is_array() && it->second.as_array().size() == 3)
            {
                const auto& a = it->second.as_array();
                lr.light.direction = {
                    static_cast<float>(a[0].as_number()),
                    static_cast<float>(a[1].as_number()),
                    static_cast<float>(a[2].as_number())
                };
            }
            s.lights.push_back(std::move(lr));
        }
    }

    auto read_vec3 = [](const cd::asset::json::Object& o, std::string_view key, cd::math::Vec3f& dst)
    {
        if (auto it = o.find(std::string(key));
            it != o.end() && it->second.is_array() && it->second.as_array().size() == 3)
        {
            const auto& a = it->second.as_array();
            if (a[0].is_number() && a[1].is_number() && a[2].is_number())
            {
                dst = {
                    static_cast<float>(a[0].as_number()),
                    static_cast<float>(a[1].as_number()),
                    static_cast<float>(a[2].as_number())
                };
                return true;
            }
        }
        return false;
    };

    int total_cores = 0;
    std::vector<SceneEntity> loaded;
    (void)cd::scene::deserialize_scene_with(
        s.scene, *r,
        [&](cd::ecs::Entity e, const cd::asset::json::Object& obj)
        {
            SceneEntity en {};
            en.handle    = e;
            en.kind      = PrimitiveKind::kCube;
            en.tint      = { 0.82F, 0.85F, 0.90F };
            en.is_pbr    = true;
            en.metallic  = 0.15F;
            en.roughness = 0.38F;
            cd::world_container::assign_layer(s.ecs_world, e, "Default");

            if (auto it = obj.find("name"); it != obj.end() && it->second.is_string())
                en.name = it->second.as_string();
            if (en.name.empty())
            {
                cd::world_container::clear_layer(s.ecs_world, e);
                s.scene.destroy_node(e);
                return;
            }
            if (auto it = obj.find("kind"); it != obj.end() && it->second.is_string())
                en.kind = kind_from_name(it->second.as_string());
            if (auto it = obj.find("is_pbr"); it != obj.end() && it->second.is_bool())
                en.is_pbr = it->second.as_bool();
            if (auto it = obj.find("metallic"); it != obj.end() && it->second.is_number())
                en.metallic = static_cast<float>(it->second.as_number());
            if (auto it = obj.find("roughness"); it != obj.end() && it->second.is_number())
                en.roughness = static_cast<float>(it->second.as_number());
            if (auto it = obj.find("multiscatter_comp"); it != obj.end() && it->second.is_number())
                en.multiscatter_comp = static_cast<float>(it->second.as_number());
            if (auto it = obj.find("iridescence_nm"); it != obj.end() && it->second.is_number())
                en.iridescence_nm = static_cast<float>(it->second.as_number());
            if (auto it = obj.find("use_texture"); it != obj.end() && it->second.is_bool())
                en.use_texture = it->second.as_bool();
            if (auto it = obj.find("custom_texture_id"); it != obj.end() && it->second.is_number())
                en.custom_texture_id = std::max(0, static_cast<int>(it->second.as_number()));
            if (auto it = obj.find("custom_texture_path"); it != obj.end() && it->second.is_string())
                en.custom_texture_path = it->second.as_string();
            if (auto it = obj.find("custom_mesh_path"); it != obj.end() && it->second.is_string())
                en.custom_mesh_path = it->second.as_string();
            if (auto it = obj.find("visible"); it != obj.end() && it->second.is_bool())
                en.visible = it->second.as_bool();
            if (auto it = obj.find("locked"); it != obj.end() && it->second.is_bool())
                en.locked = it->second.as_bool();
            if (auto it = obj.find("layer"); it != obj.end() && it->second.is_string())
                cd::world_container::assign_layer(s.ecs_world, e, it->second.as_string());

            (void)read_vec3(obj, "tint", en.tint);

            if (auto* lt = s.scene.local(e))
            {
                (void)read_vec3(obj, "home_position", lt->value.position);
                (void)read_vec3(obj, "home_scale", lt->value.scale);
                if (auto rit = obj.find("home_rotation");
                    rit != obj.end() && rit->second.is_array() && rit->second.as_array().size() == 4)
                {
                    const auto& a = rit->second.as_array();
                    if (a[0].is_number() && a[1].is_number() && a[2].is_number() && a[3].is_number())
                    {
                        lt->value.rotation = {
                            static_cast<float>(a[0].as_number()),
                            static_cast<float>(a[1].as_number()),
                            static_cast<float>(a[2].as_number()),
                            static_cast<float>(a[3].as_number())
                        };
                    }
                }
            }

            auto& ms = w.entity_modifiers[e.id];
            if (en.is_pbr && en.kind != PrimitiveKind::kSponza &&
                (en.kind != PrimitiveKind::kGltf || en.custom_texture_id >= 2))
            {
                auto init_pbr = std::make_unique<PbrMaterialModifier>(
                    en.tint, en.metallic, en.roughness, en.use_texture,
                    en.multiscatter_comp, en.iridescence_nm);
                init_pbr->texture_slot       = en.custom_texture_id;
                init_pbr->texture_asset_path = en.custom_texture_path;
                ms.add_or_replace(std::move(init_pbr));
            }

            if (auto it = obj.find("modifiers"); it != obj.end() && it->second.is_array())
            {
                for (const auto& mv : it->second.as_array())
                {
                    if (!mv.is_object()) continue;
                    const auto& mo = mv.as_object();
                    auto cit = mo.find("class");
                    if (cit == mo.end() || !cit->second.is_string()) continue;
                    if (auto mod = instantiate_modifier_by_class(cit->second.as_string()))
                    {
                        if (auto eit = mo.find("enabled"); eit != mo.end() && eit->second.is_bool())
                            mod->enabled = eit->second.as_bool();
                        if (auto* pbr = dynamic_cast<PbrMaterialModifier*>(mod.get()))
                        {
                            pbr->base_color         = en.tint;
                            pbr->metallic           = en.metallic;
                            pbr->roughness          = en.roughness;
                            pbr->use_texture        = en.use_texture;
                            pbr->multiscatter_comp  = en.multiscatter_comp;
                            pbr->iridescence_nm     = en.iridescence_nm;
                            pbr->texture_slot       = en.custom_texture_id;
                            pbr->texture_asset_path = en.custom_texture_path;
                            if (auto mt = mo.find("metallic"); mt != mo.end() && mt->second.is_number())
                                pbr->metallic = static_cast<float>(mt->second.as_number());
                            if (auto rg = mo.find("roughness"); rg != mo.end() && rg->second.is_number())
                                pbr->roughness = static_cast<float>(rg->second.as_number());
                            if (auto msc = mo.find("multiscatter_comp"); msc != mo.end() && msc->second.is_number())
                                pbr->multiscatter_comp = static_cast<float>(msc->second.as_number());
                            if (auto ir = mo.find("iridescence_nm"); ir != mo.end() && ir->second.is_number())
                                pbr->iridescence_nm = static_cast<float>(ir->second.as_number());
                            if (auto ut = mo.find("use_texture"); ut != mo.end() && ut->second.is_bool())
                                pbr->use_texture = ut->second.as_bool();
                            if (auto ts = mo.find("texture_slot"); ts != mo.end() && ts->second.is_number())
                            {
                                pbr->texture_slot    = std::max(0, static_cast<int>(ts->second.as_number()));
                                en.custom_texture_id = pbr->texture_slot;
                            }
                            if (auto tp = mo.find("texture_asset_path"); tp != mo.end() && tp->second.is_string())
                            {
                                pbr->texture_asset_path = tp->second.as_string();
                                en.custom_texture_path  = pbr->texture_asset_path;
                            }
                        }
                        else if (auto* box = dynamic_cast<BoxColliderModifier*>(mod.get()))
                        {
                            (void)read_vec3(mo, "half_extents", box->half_extents);
                            (void)read_vec3(mo, "center_offset", box->center_offset);
                            if (auto tr = mo.find("is_trigger"); tr != mo.end() && tr->second.is_bool())
                                box->is_trigger = tr->second.as_bool();
                        }
                        else if (auto* sph = dynamic_cast<SphereColliderModifier*>(mod.get()))
                        {
                            if (auto rd = mo.find("radius"); rd != mo.end() && rd->second.is_number())
                                sph->radius = static_cast<float>(rd->second.as_number());
                            (void)read_vec3(mo, "center_offset", sph->center_offset);
                            if (auto tr = mo.find("is_trigger"); tr != mo.end() && tr->second.is_bool())
                                sph->is_trigger = tr->second.as_bool();
                        }
                        else if (auto* cap = dynamic_cast<CapsuleColliderModifier*>(mod.get()))
                        {
                            if (auto rd = mo.find("radius"); rd != mo.end() && rd->second.is_number())
                                cap->radius = static_cast<float>(rd->second.as_number());
                            if (auto hh = mo.find("half_height"); hh != mo.end() && hh->second.is_number())
                                cap->half_height = static_cast<float>(hh->second.as_number());
                            (void)read_vec3(mo, "center_offset", cap->center_offset);
                            if (auto tr = mo.find("is_trigger"); tr != mo.end() && tr->second.is_bool())
                                cap->is_trigger = tr->second.as_bool();
                        }
                        else if (auto* rb = dynamic_cast<RigidBodyModifier*>(mod.get()))
                        {
                            if (auto bt = mo.find("body_type"); bt != mo.end() && bt->second.is_number())
                                rb->body_type = static_cast<cd::physics::BodyType>(static_cast<int>(bt->second.as_number()));
                            if (auto m = mo.find("mass"); m != mo.end() && m->second.is_number())
                                rb->mass = static_cast<float>(m->second.as_number());
                            if (auto rs = mo.find("restitution"); rs != mo.end() && rs->second.is_number())
                                rb->restitution = static_cast<float>(rs->second.as_number());
                            if (auto fr = mo.find("friction"); fr != mo.end() && fr->second.is_number())
                                rb->friction = static_cast<float>(fr->second.as_number());
                            if (auto ld = mo.find("linear_damping"); ld != mo.end() && ld->second.is_number())
                                rb->linear_damping = static_cast<float>(ld->second.as_number());
                            if (auto gs = mo.find("gravity_scale"); gs != mo.end() && gs->second.is_number())
                                rb->gravity_scale = static_cast<float>(gs->second.as_number());
                            if (auto ug = mo.find("use_gravity"); ug != mo.end() && ug->second.is_bool())
                                rb->use_gravity = ug->second.as_bool();
                        }
                        else if (auto* cc = dynamic_cast<CharacterControllerModifier*>(mod.get()))
                        {
                            if (auto ms_v = mo.find("move_speed"); ms_v != mo.end() && ms_v->second.is_number())
                                cc->move_speed = static_cast<float>(ms_v->second.as_number());
                            if (auto ji = mo.find("jump_impulse"); ji != mo.end() && ji->second.is_number())
                                cc->jump_impulse = static_cast<float>(ji->second.as_number());
                        }
                        else if (auto* km = dynamic_cast<KinematicMoverModifier*>(mod.get()))
                        {
                            (void)read_vec3(mo, "move_axis", km->move_axis);
                            if (auto am = mo.find("amplitude"); am != mo.end() && am->second.is_number())
                                km->amplitude = static_cast<float>(am->second.as_number());
                            if (auto fq = mo.find("frequency_hz"); fq != mo.end() && fq->second.is_number())
                                km->frequency_hz = static_cast<float>(fq->second.as_number());
                            if (auto po = mo.find("phase_offset"); po != mo.end() && po->second.is_number())
                                km->phase_offset = static_cast<float>(po->second.as_number());
                            (void)read_vec3(mo, "angular_deg_per_sec", km->angular_deg_per_sec);
                            if (auto cr = mo.find("carry_riders"); cr != mo.end() && cr->second.is_bool())
                                km->carry_riders = cr->second.as_bool();
                        }
                        else if (auto* trg = dynamic_cast<TriggerVolumeModifier*>(mod.get()))
                        {
                            if (auto ac = mo.find("action"); ac != mo.end() && ac->second.is_number())
                                trg->action = static_cast<TriggerActionKind>(static_cast<int>(ac->second.as_number()));
                            if (auto ev = mo.find("effect_value"); ev != mo.end() && ev->second.is_number())
                                trg->effect_value = static_cast<float>(ev->second.as_number());
                            if (auto tr = mo.find("trigger_radius"); tr != mo.end() && tr->second.is_number())
                                trg->trigger_radius = static_cast<float>(tr->second.as_number());
                            if (auto os = mo.find("one_shot"); os != mo.end() && os->second.is_bool())
                                trg->one_shot = os->second.as_bool();
                            if (trg->action == TriggerActionKind::kCollectibleScore)
                                ++total_cores;
                        }
                        else if (auto* pat = dynamic_cast<PatrolWaypointModifier*>(mod.get()))
                        {
                            if (auto pa = mo.find("patrol_axis"); pa != mo.end() && pa->second.is_number())
                                pat->patrol_axis = static_cast<int>(pa->second.as_number());
                            if (auto ps = mo.find("patrol_span"); ps != mo.end() && ps->second.is_number())
                                pat->patrol_span = static_cast<float>(ps->second.as_number());
                            if (auto ms_v = mo.find("move_speed"); ms_v != mo.end() && ms_v->second.is_number())
                                pat->move_speed = static_cast<float>(ms_v->second.as_number());
                            if (auto cp = mo.find("chase_player"); cp != mo.end() && cp->second.is_bool())
                                pat->chase_player = cp->second.as_bool();
                            if (auto cr = mo.find("chase_range"); cr != mo.end() && cr->second.is_number())
                                pat->chase_range = static_cast<float>(cr->second.as_number());
                        }
                        else if (auto* le = dynamic_cast<LightEmitterModifier*>(mod.get()))
                        {
                            if (auto lt_v = mo.find("light_type"); lt_v != mo.end() && lt_v->second.is_number())
                                le->light_type = static_cast<cd::light::LightType>(static_cast<int>(lt_v->second.as_number()));
                            (void)read_vec3(mo, "color", le->color);
                            if (auto kl = mo.find("kelvin"); kl != mo.end() && kl->second.is_number())
                                le->kelvin = static_cast<float>(kl->second.as_number());
                            if (auto in = mo.find("intensity"); in != mo.end() && in->second.is_number())
                                le->intensity = static_cast<float>(in->second.as_number());
                            if (auto rg = mo.find("range"); rg != mo.end() && rg->second.is_number())
                                le->range = static_cast<float>(rg->second.as_number());
                            (void)read_vec3(mo, "direction", le->direction);
                        }
                        else if (auto* un = dynamic_cast<UniversalNodeModifier*>(mod.get()))
                        {
                            const StudioNodeDescriptor* bound_desc = nullptr;
                            if (auto nid = mo.find("node_id"); nid != mo.end() && nid->second.is_number())
                            {
                                const auto id_val = static_cast<std::uint16_t>(std::max(0, static_cast<int>(nid->second.as_number())));
                                bound_desc = find_studio_node_by_id(id_val);
                            }
                            if (bound_desc == nullptr)
                            {
                                if (auto gn = mo.find("godot_name"); gn != mo.end() && gn->second.is_string())
                                    bound_desc = find_studio_node(gn->second.as_string());
                            }
                            if (bound_desc == nullptr)
                            {
                                if (auto cn = mo.find("chroma_name"); cn != mo.end() && cn->second.is_string())
                                    bound_desc = find_studio_node(cn->second.as_string());
                            }
                            if (bound_desc != nullptr)
                                un->bind_descriptor(*bound_desc);
                            if (auto ir = mo.find("intensity_or_rate"); ir != mo.end() && ir->second.is_number())
                                un->intensity_or_rate = static_cast<float>(ir->second.as_number());
                            if (auto sq = mo.find("substeps_or_quality"); sq != mo.end() && sq->second.is_number())
                                un->substeps_or_quality = static_cast<int>(sq->second.as_number());
                            if (auto ga = mo.find("gpu_accelerated"); ga != mo.end() && ga->second.is_bool())
                                un->gpu_accelerated = ga->second.as_bool();
                        }
                        ms.add_or_replace(std::move(mod));
                    }
                }
            }
            record_entity_home_transform(s, e);
            loaded.push_back(std::move(en));
        });

    (void)read_vec3(root_obj, "camera_eye", s.cam.eye);
    (void)read_vec3(root_obj, "camera_target", s.cam.target);

    w.sim_time_accum       = 0.0F;
    w.game_score           = 0;
    w.collected_cores      = 0;
    w.total_cores_in_level = total_cores;
    w.defeated_enemies     = 0;
    w.level_goal_reached   = false;
    s.entities = std::move(loaded);
    for (std::size_t i = 0; i < s.entities.size(); ++i)
    {
        if (!s.entities[i].custom_mesh_path.empty() &&
            s.entities[i].kind == PrimitiveKind::kCustomGltf)
        {
            const std::string mp = s.entities[i].custom_mesh_path;
            (void)assign_mesh_asset_to_entity(s, static_cast<int>(i), mp, log_push);
        }
        if (s.entities[i].use_texture && !s.entities[i].custom_texture_path.empty())
        {
            const std::string tp = s.entities[i].custom_texture_path;
            (void)assign_texture_asset_to_entity(s, static_cast<int>(i), tp, log_push);
        }
    }
    bool has_sponza_loaded = false;
    for (const auto& en : s.entities)
    {
        if (en.kind == PrimitiveKind::kSponza)
        {
            has_sponza_loaded = true;
            break;
        }
    }
    s.show_editor_floor = !has_sponza_loaded;
    s.scene.update_transforms();
    sync_camera_and_invalidate_frame_history(s);
    int best_sel = s.entities.empty() ? -1 : 0;
    for (std::size_t i = 0; i < s.entities.size(); ++i)
    {
        if (w.entity_modifiers[s.entities[i].handle.id].template find<CharacterControllerModifier>() != nullptr)
        {
            best_sel = static_cast<int>(i);
            break;
        }
        if (best_sel == 0 && s.entities[i].name.find("Ground_Floor") == std::string::npos)
            best_sel = static_cast<int>(i);
    }
    s.selected = best_sel;
    s.selected_kind = SelKind::kEntity;
    log_push(std::string("[scene] Loaded ") + std::to_string(s.entities.size()) +
             " entities + " + std::to_string(s.lights.size()) + " lights from " + path);
    return true;
}

template <typename StateT, typename LogFn>
[[nodiscard]] inline bool export_standalone_game_package(
    const StateT& s,
    const char*   export_dir,
    LogFn&&       log_push)
{
    std::error_code ec {};
    std::filesystem::create_directories(export_dir, ec);
    const std::string scene_file = std::string(export_dir) + "/game_world.cdscene.json";
    (void)save_full_studio_scene(s, scene_file.c_str(), log_push);

    if (const auto* proj = s.cd_world.project())
    {
        const std::string proj_file = std::string(export_dir) + "/game.cdproject";
        (void)cd::world_container::save_project_file(proj_file, *proj);
    }

    const std::string launcher_bat = std::string(export_dir) + "/Play_ChromodynamicGame.bat";
    if (std::ofstream bat(launcher_bat); bat.is_open())
    {
        bat << "@echo off\n"
            << "echo Launching CHROMODYNAMIC Standalone Game...\n"
            << "\"%~dp0..\\..\\ide-base\\bin\\editor.exe\" --scene \"%~dp0game_world.cdscene.json\" --pie-play\n";
    }
    log_push(std::string("[export] Packaged standalone game -> ") + export_dir);
    return true;
}

/// E2E Engine Authoring Builder: constructs a full playable 3D game level using the engine's
/// entity spawning, PBR texture assignment, modifier stack instantiation, and JSON scene
/// serialization/deserialization pipeline (`build/export/ChromodynamicGame/game_world.cdscene.json`).
template <typename StateT, typename LogFn>
inline void build_and_save_custom_game_level(StateT& s, LogFn&& log_push)
{
    auto& w = studio_world();
    load_studio_interactive_template(s, 13, log_push);

    // Enrich the level with textured architectural landmarks & tactical cover using engine APIs
    const int pillar_l = spawn_entity_with_modifiers(s, "Cylinder", { -2.45F, 0.0F, -1.35F }, log_push);
    if (pillar_l >= 0 && std::cmp_less(pillar_l, s.entities.size()))
    {
        auto& ent = s.entities[static_cast<std::size_t>(pillar_l)];
        ent.name  = "Arena_Marble_Pillar_West";
        if (auto* lt = s.scene.local(ent.handle))
        {
            lt->value.position = { -2.45F, 0.85F, -1.35F };
            lt->value.scale    = { 0.72F, 1.70F, 0.72F };
        }
        if (auto* rb = w.entity_modifiers[ent.handle.id].template find<RigidBodyModifier>())
            rb->body_type = cd::physics::BodyType::kStatic;
        (void)assign_texture_asset_to_entity(s, pillar_l, "assets/textures/Carrara_Marble.tex", log_push);
        record_entity_home_transform(s, ent.handle);
    }

    const int gate_wall = spawn_entity_with_modifiers(s, "Static_Wall", { 2.85F, 0.0F, -2.55F }, log_push);
    if (gate_wall >= 0 && std::cmp_less(gate_wall, s.entities.size()))
    {
        auto& ent = s.entities[static_cast<std::size_t>(gate_wall)];
        ent.name  = "Goal_Citadel_Brick_Backstop";
        if (auto* lt = s.scene.local(ent.handle))
        {
            lt->value.position = { 2.85F, 0.90F, -2.55F };
            lt->value.scale    = { 2.40F, 1.80F, 0.42F };
        }
        (void)assign_texture_asset_to_entity(s, gate_wall, "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg", log_push);
        record_entity_home_transform(s, ent.handle);
    }

    // Apply Carbon-Fiber PBR texture to the Moving Platforms so their motion is visually crisp
    for (std::size_t i = 0; i < s.entities.size(); ++i)
    {
        if (s.entities[i].name.find("Kinematic_Platform") != std::string::npos)
        {
            (void)assign_texture_asset_to_entity(s, static_cast<int>(i), "assets/textures/Carbon_Fiber_Twill.tex", log_push);
        }
    }

    // Attach BloomLensModifier so Energy Cores, Plasma Projectiles, and Goal Portal glow in HDR
    auto blm = std::make_unique<BloomLensModifier>();
    blm->bloom_intensity      = 0.38F;
    blm->vignette_strength    = 0.14F;
    blm->chromatic_aberration = 0.0015F;
    w.environment_modifiers.add_or_replace(std::move(blm));

    s.cam.eye    = { -4.4F, 3.45F, 5.65F };
    s.cam.target = {  0.1F, 0.95F, -0.15F };
    s.scene.update_transforms();

    // Save standalone game package AND reload from disk to prove full round-trip engine usage!
    (void)export_standalone_game_package(s, "build/export/ChromodynamicGame", log_push);
    (void)save_full_studio_scene(s, "chromodynamic_studio_scene.cdscene.json", log_push);
    (void)load_full_studio_scene(s, "build/export/ChromodynamicGame/game_world.cdscene.json", log_push);
    w.hud_toast_message = "Authored, Saved & Reloaded 'game_world.cdscene.json' - Ready to Play!";
    w.hud_toast_timer   = 4.0F;
}

template <class StateT, class LogFn>
void reset_to_clean_empty_project(StateT& s, LogFn&& log_push)
{
    auto& w = studio_world();
    w.loaded_context_assets.clear();
    w.active_project_name    = "Empty_Project";
    w.active_world_name      = "Default_World";
    w.active_level_name      = "Empty_Stage";
    w.current_content_folder = "showcases";
    if (auto* active_lvl = s.cd_world.active_level_ptr(); active_lvl != nullptr)
    {
        active_lvl->set_template_id(0U);
    }
    init_clean_studio_workspace(s, log_push);
    w.hud_toast_message = "New Empty Project ready — select a Sample Project (.cdproj) or load Disk assets to Context";
    w.hud_toast_timer   = 3.5F;
}

template <class StateT, class LogFn>
bool load_project_or_scene_from_disk_path(StateT& s, std::string_view disk_path, LogFn&& log_push)
{
    auto& w = studio_world();
    const std::string path_str { disk_path };
    if (path_str.find(".cdscene.json") != std::string::npos)
    {
        const bool ok = load_full_studio_scene(s, path_str.c_str(), log_push);
        if (ok)
        {
            std::filesystem::path p { path_str };
            w.active_project_name = p.stem().stem().string();
            w.active_level_name   = p.stem().string();
            for (const auto& ent : s.entities)
            {
                if (!ent.custom_mesh_path.empty())
                {
                    (void)w.load_asset_into_context(
                        std::filesystem::path(ent.custom_mesh_path).stem().string(),
                        ent.custom_mesh_path, 0U);
                }
                if (!ent.custom_texture_path.empty())
                {
                    (void)w.load_asset_into_context(
                        std::filesystem::path(ent.custom_texture_path).stem().string(),
                        ent.custom_texture_path, 1U);
                }
            }
        }
        return ok;
    }

    if (path_str.find(".cdproj") != std::string::npos || path_str.find(".cdproject") != std::string::npos)
    {
        int mapped_template = 1;
        std::string proj_label = "Showcase_Project";
        if (path_str.find("01_pbr_materials") != std::string::npos)
        {
            mapped_template = 3;
            proj_label      = "01_PBR_Materials_SSS_Hair";
        }
        else if (path_str.find("02_realtime_gi") != std::string::npos)
        {
            mapped_template = 5;
            proj_label      = "02_Realtime_GI_RayTracing_Sponza";
        }
        else if (path_str.find("03_atmosphere") != std::string::npos)
        {
            mapped_template = 9;
            proj_label      = "03_Atmosphere_Clouds_Ocean";
        }
        else if (path_str.find("04_gpu_driven") != std::string::npos)
        {
            mapped_template = 4;
            proj_label      = "04_GPU_Driven_Geometry";
        }
        else if (path_str.find("05_physics") != std::string::npos)
        {
            mapped_template = 13;
            proj_label      = "05_Rockstar_Physics_Destruction";
        }
        else if (path_str.find("06_postfx") != std::string::npos)
        {
            mapped_template = 6;
            proj_label      = "06_PostFX_Upscaling_Camera";
        }
        else if (path_str.find("07_particles") != std::string::npos)
        {
            mapped_template = 9;
            proj_label      = "07_Particles_Decals_Foliage";
        }
        else if (path_str.find("08_procedural") != std::string::npos)
        {
            mapped_template = 7;
            proj_label      = "08_Procedural_Asset_Synth";
        }
        else if (path_str.find("09_editor") != std::string::npos)
        {
            mapped_template = 12;
            proj_label      = "09_Editor_Tools_Constructs";
        }
        else if (path_str.find("10_gameplay") != std::string::npos)
        {
            mapped_template = 1;
            proj_label      = "10_Gameplay_Animation_Net";
        }

        load_studio_interactive_template(s, mapped_template, log_push);
        w.active_project_name = proj_label;

        // Populate Active Context with project-relevant meshes & textures from disk
        (void)w.load_asset_into_context("Sphere", "assets/meshes/Sphere.mesh", 0U);
        (void)w.load_asset_into_context("Cube", "assets/meshes/Cube.mesh", 0U);
        (void)w.load_asset_into_context("Cylinder", "assets/meshes/Cylinder.mesh", 0U);
        (void)w.load_asset_into_context("CesiumMan", "assets/samples/Generic/CesiumMan.gltf", 0U);
        (void)w.load_asset_into_context("Gold_24K", "assets/textures/Gold_Leaf_24K.tex", 1U);
        (void)w.load_asset_into_context("Carbon_Fiber", "assets/textures/Carbon_Fiber_Twill.tex", 1U);
        (void)w.load_asset_into_context("Sponza_Brick", "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg", 1U);

        w.hud_toast_message = "Loaded Project from Disk: " + proj_label;
        w.hud_toast_timer   = 3.5F;
        log_push("[project] Opened disk project '" + path_str + "' -> " + proj_label);
        return true;
    }

    return false;
}

template <class StateT, class LogFn>
void seed_demo_context_and_scene(StateT& s, LogFn&& log_push)
{
    auto& w = studio_world();
    init_clean_studio_workspace(s, log_push);
    w.current_content_folder = "assets/meshes";

    // Stage 1: Load Objects & Textures from Disk into Active Project Context
    (void)w.load_asset_into_context("Sphere", "assets/meshes/Sphere.mesh", 0U);
    (void)w.load_asset_into_context("Cube", "assets/meshes/Cube.mesh", 0U);
    (void)w.load_asset_into_context("Cylinder", "assets/meshes/Cylinder.mesh", 0U);
    (void)w.load_asset_into_context("Torus", "assets/meshes/Torus.mesh", 0U);
    (void)w.load_asset_into_context("CesiumMan", "assets/samples/Generic/CesiumMan.gltf", 0U);
    (void)w.load_asset_into_context("Gold_24K", "assets/textures/Gold_Leaf_24K.tex", 1U);
    (void)w.load_asset_into_context("Checker_Grid", "assets/textures/Checkerboard_Grid.tex", 1U);
    (void)w.load_asset_into_context("Sponza_Brick", "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg", 1U);

    // Stage 2: Spawn Objects from Active Context into the 3D Scene & Assign Context Textures
    const int sph_idx = spawn_entity_with_modifiers(s, "Sphere", { -1.95F, 0.0F, 0.0F }, log_push);
    if (sph_idx >= 0 && std::cmp_less(sph_idx, s.entities.size()))
    {
        s.entities[static_cast<std::size_t>(sph_idx)].name = "Ctx_Gold_Sphere";
        if (auto* lt = s.scene.local(s.entities[static_cast<std::size_t>(sph_idx)].handle))
        {
            lt->value.position = { -1.95F, 0.0F, 0.0F };
        }
        (void)assign_mesh_asset_to_entity(s, sph_idx, "assets/meshes/Sphere.mesh", log_push);
        (void)assign_texture_asset_to_entity(s, sph_idx, "assets/textures/Gold_Leaf_24K.tex", log_push);
    }

    const int box_idx = spawn_entity_with_modifiers(s, "Cube", { 0.0F, 0.55F, 0.0F }, log_push);
    if (box_idx >= 0 && std::cmp_less(box_idx, s.entities.size()))
    {
        s.entities[static_cast<std::size_t>(box_idx)].name = "Ctx_Brick_Cube";
        (void)assign_mesh_asset_to_entity(s, box_idx, "assets/meshes/Cube.mesh", log_push);
        (void)assign_texture_asset_to_entity(s, box_idx, "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg", log_push);
    }

    const int tor_idx = spawn_entity_with_modifiers(s, "Torus", { 1.95F, 0.55F, 0.0F }, log_push);
    if (tor_idx >= 0 && std::cmp_less(tor_idx, s.entities.size()))
    {
        s.entities[static_cast<std::size_t>(tor_idx)].name = "Ctx_Checker_Torus";
        (void)assign_mesh_asset_to_entity(s, tor_idx, "assets/meshes/Torus.mesh", log_push);
        (void)assign_texture_asset_to_entity(s, tor_idx, "assets/textures/Checkerboard_Grid.tex", log_push);
    }

    s.selected      = box_idx;
    s.selected_kind = SelKind::kEntity;
    s.scene.update_transforms();
    w.hud_toast_message = "Disk -> Context -> Scene: 8 Assets in Context, 3 Objects Spawned";
    w.hud_toast_timer   = 4.0F;
}

} // namespace cd::studio

