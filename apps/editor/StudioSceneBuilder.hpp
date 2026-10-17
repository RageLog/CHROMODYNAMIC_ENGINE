// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioSceneBuilder.hpp
//
// Clean Studio workspace initializer, 3D Object & Prefab spawn catalog,
// physics cannonball spawner, and 1-click interactive physics playgrounds.
// =============================================================================
#pragma once

#include "StudioCollisionMath.hpp"

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
    add_tagged("Lights",     { 1.00F, 0.88F, 0.32F }, false);
    add_tagged("UI",         { 0.78F, 0.45F, 0.96F }, true);
}

inline void populate_master_showcase_project(cd::world_container::World& cd_world)
{
    if (cd_world.project() != nullptr && cd_world.project()->level_count() >= 5)
        return;

    auto proj = std::make_unique<cd::world_container::Project>("ChromodynamicMasterShowcase");
    struct LevelSpec
    {
        const char* name;
        const char* world_name;
        const char* desc;
        int         template_id;
    };
    constexpr std::array<LevelSpec, 13> kSpecs {{
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
    (void)proj->set_active_level(kSpecs[0].name);
    cd_world.set_name(kSpecs[0].world_name);
    cd_world.set_project(std::move(proj));
}

template <typename StateT, typename LogFn>
inline void init_clean_studio_workspace(StateT& s, LogFn&& log_push)
{
    auto& w = studio_world();
    if (w.initialized) return;
    w.initialized   = true;
    w.physics_world = cd::physics::make_builtin_physics_world();

    if (s.cd_world.project() == nullptr || s.cd_world.project()->level_count() == 0U)
    {
        populate_master_showcase_project(s.cd_world);
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
    s.show_editor_floor = true;

    // 1. Spawn the physical Ground Floor Collider Entity (`Ground_Floor (Static Collider)`)
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

    // 2. Ergonomic Studio Camera angle
    s.cam.eye    = { -5.8F, 3.8F, 6.2F };
    s.cam.target = {  0.0F, 0.8F, 0.0F };
    s.cam.fov_y  = 0.90F;
    s.app_state.free_look.manual_mode = true;

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
    s.selected      = 0;
    s.selected_kind = SelKind::kEntity;
    log_push("[studio] Ready: Multi-World & Multi-Level Showcase active! Switch Worlds/Levels in Top Bar or Outliner.");
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

    if (item_id == "Dynamic_Cube" || item_id == "Cube")
    {
        e.kind = PrimitiveKind::kCube;
        e.name = "Cube_" + std::to_string(s.entities.size());
        e.tint = { 0.92F, 0.36F, 0.24F };
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.55F, world_pos.y + 1.8F), world_pos.z };
            lt->value.scale    = { 1.0F, 1.0F, 1.0F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, 0.15F, 0.35F));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kDynamic, 2.0F, 0.40F));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.5F, 0.5F }));
    }
    else if (item_id == "Dynamic_Sphere" || item_id == "Sphere")
    {
        e.kind      = PrimitiveKind::kSphere;
        e.name      = "Sphere_" + std::to_string(s.entities.size());
        e.tint      = { 0.24F, 0.72F, 0.98F };
        e.metallic  = 0.85F;
        e.roughness = 0.12F;
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.55F, world_pos.y + 2.2F), world_pos.z };
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
            lt->value.position = { world_pos.x, std::max(0.95F, world_pos.y + 0.95F), world_pos.z };
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
            lt->value.position = { world_pos.x, std::max(0.55F, world_pos.y + 1.6F), world_pos.z };
            lt->value.scale    = { 1.0F, 1.0F, 1.0F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, 0.70F, 0.18F));
        ms.add_or_replace(std::make_unique<RigidBodyModifier>(cd::physics::BodyType::kDynamic, 1.8F, 0.50F));
        ms.add_or_replace(std::make_unique<BoxColliderModifier>(cd::math::Vec3f { 0.5F, 0.35F, 0.5F }));
    }
    else if (item_id == "Character_CesiumMan")
    {
        e.kind         = PrimitiveKind::kGltf;
        e.name         = "CesiumMan_Actor_" + std::to_string(s.entities.size());
        e.tint         = { 1.0F, 1.0F, 1.0F };
        e.use_texture  = true;
        semantic_layer = "Characters";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { world_pos.x, std::max(0.02F, world_pos.y), world_pos.z };
            lt->value.rotation = { -0.7071068F, 0.0F, 0.0F, 0.7071068F };
            lt->value.scale    = { 1.8F, 1.8F, 1.8F };
        }
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, 0.10F, 0.45F, true));
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
        e.use_texture  = true;
        semantic_layer = "Geometry";
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { 0.0F, 0.0F, 0.0F };
            lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
            lt->value.scale    = { 0.01F, 0.01F, 0.01F };
        }
        s.show_editor_floor = false;
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, 0.05F, 0.65F, true));
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

/// Shoot a high-speed dynamic physics sphere from the camera into the 3D scene!
template <typename StateT, typename LogFn>
inline void shoot_physics_ball_from_camera(StateT& s, LogFn&& log_push)
{
    cd::math::Vec3f fwd = s.cam.target - s.cam.eye;
    const float len = cd::math::length(fwd);
    if (len > 1e-4F) fwd = fwd * (1.0F / len);
    else fwd = { 0.0F, -0.2F, -1.0F };

    const cd::math::Vec3f spawn_pt = s.cam.eye + fwd * 1.6F;
    const int idx = spawn_entity_with_modifiers(s, "Dynamic_Sphere", spawn_pt, log_push);
    if (idx >= 0 && std::cmp_less(idx, s.entities.size()))
    {
        auto& ent = s.entities[static_cast<std::size_t>(idx)];
        ent.name  = "CannonBall_" + std::to_string(idx);
        ent.tint  = { 1.0F, 0.78F, 0.22F };
        if (auto* lt = s.scene.local(ent.handle))
        {
            lt->value.position = spawn_pt;
            lt->value.scale    = { 0.75F, 0.75F, 0.75F };
        }
        ModifierStack& ms = studio_world().entity_modifiers[ent.handle.id];
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
            rb->linear_velocity = fwd * 13.5F + cd::math::Vec3f { 0.0F, 1.5F, 0.0F };
        }
        record_entity_home_transform(s, ent.handle);
        s.scene.update_transforms();
        log_push("[cannon] Fired Physics CannonBall at 13.5 m/s!");
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
    if (contains_icase(asset_id, "sponza"))          ent.kind = PrimitiveKind::kSponza;
    else if (contains_icase(asset_id, "cesium") ||
             contains_icase(asset_id, "avatar") ||
             contains_icase(asset_id, ".glb"))       ent.kind = PrimitiveKind::kGltf;
    else if (contains_icase(asset_id, "sphere") ||
             contains_icase(asset_id, "ball"))       ent.kind = PrimitiveKind::kSphere;
    else if (contains_icase(asset_id, "cylinder") ||
             contains_icase(asset_id, "pillar"))     ent.kind = PrimitiveKind::kCylinder;
    else if (contains_icase(asset_id, "cone"))       ent.kind = PrimitiveKind::kCone;
    else if (contains_icase(asset_id, "torus") ||
             contains_icase(asset_id, "ring"))       ent.kind = PrimitiveKind::kTorus;
    else                                             ent.kind = PrimitiveKind::kCube;

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

    int slot = 1;
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

    w.last_asset_status_msg = "Applied Texture '" + std::string(asset_id) + "' (Slot " + std::to_string(slot) + ") -> " + ent.name;
    log_push("[asset] " + w.last_asset_status_msg);
    return true;
}

/// 1-Click Interactive Multi-World & Multi-Level Templates (0..12)
template <typename StateT, typename LogFn>
inline void load_studio_interactive_template(StateT& s, int template_id, LogFn&& log_push)
{
    auto& w = studio_world();
    w.initialized = false;
    init_clean_studio_workspace(s, log_push);
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

    if (template_id == 0)
    {
        // Level 1.1: Clean Empty Sandbox (Ultra-FPS baseline)
        s.cam.eye    = { -5.2F, 3.2F, 5.8F };
        s.cam.target = {  0.0F, 0.5F, 0.0F };
        log_push("[level] Loaded '1.1 Clean Empty Sandbox (Ultra-FPS)'");
    }
    else if (template_id == 1)
    {
        // Level 1.2: Angled 25 deg Physics Ramp + Rolling Chrome Sphere + 5x Crate Tower + Character
        (void)spawn_entity_with_modifiers(s, "Static_Ramp", { -1.55F, 0.15F, 0.0F }, log_push);
        const int sph_idx = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -2.35F, 1.1F, 0.0F }, log_push);
        if (sph_idx >= 0 && std::cmp_less(sph_idx, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(sph_idx)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.position = { -2.35F, 2.35F, 0.0F };
            if (auto* rb = w.entity_modifiers[h.id].template find<RigidBodyModifier>())
            {
                rb->mass            = 6.5F;
                rb->linear_velocity = { 3.6F, -1.2F, 0.0F };
            }
            record_entity_home_transform(s, h);
        }
        (void)spawn_entity_with_modifiers(s, "Prefab_CrateStack", { 0.85F, 0.02F, 0.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { -0.4F, 0.02F, -2.2F }, log_push);
        s.selected   = 1;
        s.cam.eye    = { -4.2F, 3.4F, 6.2F };
        s.cam.target = { -0.2F, 1.1F, -0.3F };
        log_push("[level] Loaded '1.2 OBB Ramp & 5x Crate Tower'");
    }
    else if (template_id == 2)
    {
        // Level 1.3: 6x Domino Chain Reaction + Striker Cannonball
        (void)spawn_entity_with_modifiers(s, "Prefab_DominoRow", { 0.4F, 0.0F, 0.0F }, log_push);
        const int striker = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -2.6F, -1.5F, 0.0F }, log_push);
        if (striker >= 0 && std::cmp_less(striker, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(striker)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.position = { -2.6F, 0.85F, 0.0F };
            if (auto* rb = w.entity_modifiers[h.id].template find<RigidBodyModifier>())
            {
                rb->mass            = 6.0F;
                rb->linear_velocity = { 6.5F, 0.0F, 0.0F };
            }
            record_entity_home_transform(s, h);
        }
        s.cam.eye    = { -1.8F, 2.8F, 5.4F };
        s.cam.target = { -0.2F, 0.8F, 0.0F };
        log_push("[level] Loaded '1.3 6x Domino Chain & Striker'");
    }
    else if (template_id == 3)
    {
        // Level 3.2: Bouncing Trampoline Pad + High-Drop Spheres & Cubes
        (void)spawn_entity_with_modifiers(s, "Trampoline_Pad", { 0.0F, 0.0F, 0.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -0.35F, 1.8F, 0.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Dynamic_Cube",   {  0.45F, 2.8F, 0.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { -2.5F, 0.02F, 0.0F }, log_push);
        s.cam.eye    = { -3.8F, 3.2F, 5.6F };
        s.cam.target = { -0.3F, 1.2F, 0.0F };
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
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { -3.8F, 0.02F, -0.4F }, log_push);
        s.lights.push_back({ "Courtyard Torch Warm (2400K)",
            cd::light::point({ -4.5F, 2.2F, 1.5F }, cd::light::cct_to_linear_rgb(2400.0F), 4200.0F, 14.0F),
            true, 2400.0F });
        s.lights.push_back({ "Archway Cyan Rect (7500K)",
            cd::light::rect_area({ 3.8F, 2.6F, -1.2F }, { -1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F },
                                 2.2F, 1.2F, cd::light::cct_to_linear_rgb(7500.0F), 3800.0F),
            true, 7500.0F });
        s.cam.eye    = { -8.5F, 2.2F, 1.2F };
        s.cam.target = {  0.0F, 1.8F, -0.2F };
        log_push("[level] Loaded '2.2 Crytek Sponza Atrium & DDGI (03_sponza)'");
    }
    else if (template_id == 6)
    {
        // Level 2.3: 04_optics_cinematic_studio — Sculptures + Area Lights + Bloom & Lens Optics
        w.environment_modifiers.add_or_replace(std::make_unique<BloomLensModifier>());
        const int t_idx = spawn_entity_with_modifiers(s, "Torus",          { -1.8F, 1.0F, 0.0F }, log_push);
        const int s_idx = spawn_entity_with_modifiers(s, "Dynamic_Sphere", {  0.0F, 1.0F, 0.0F }, log_push);
        const int c_idx = spawn_entity_with_modifiers(s, "Cylinder",       {  1.8F, 1.0F, 0.0F }, log_push);
        (void)assign_texture_asset_to_entity(s, t_idx, "Mat_Iridescent_Bismuth", log_push);
        (void)assign_texture_asset_to_entity(s, s_idx, "Mat_Gold_24K", log_push);
        (void)assign_texture_asset_to_entity(s, c_idx, "Carrara_Marble.tex", log_push);
        s.lights.push_back({ "Key Rect Area Softbox (5600K)",
            cd::light::rect_area({ 0.0F, 3.2F, 2.8F }, { 0.0F, -0.5F, -0.86F }, { 1.0F, 0.0F, 0.0F },
                                 2.8F, 1.4F, { 1.0F, 0.96F, 0.90F }, 5200.0F),
            true, 5600.0F });
        s.cam.eye    = { 0.0F, 2.2F, 5.2F };
        s.cam.target = { 0.0F, 1.0F, 0.0F };
        log_push("[level] Loaded '2.3 Cinematic Optics & Bloom (04_optics)'");
    }
    else if (template_id == 7)
    {
        // Level 3.1: 06_jolt_dynamics_rover_shatter — Twin opposing OBB ramps + heavy collider impact
        (void)spawn_entity_with_modifiers(s, "Static_Ramp", { -2.4F, 0.15F, 0.0F }, log_push);
        const int r2 = spawn_entity_with_modifiers(s, "Static_Ramp", {  2.4F, 0.15F, 0.0F }, log_push);
        if (r2 >= 0 && std::cmp_less(r2, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(r2)].handle;
            if (auto* lt = s.scene.local(h)) lt->value.rotation = euler_deg_to_quat({ 0.0F, 0.0F, 25.0F });
            record_entity_home_transform(s, h);
        }
        (void)spawn_entity_with_modifiers(s, "Prefab_CrateStack", { 0.0F, 0.02F, 0.0F }, log_push);
        const int b1 = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -3.2F, 2.6F, 0.0F }, log_push);
        if (b1 >= 0 && std::cmp_less(b1, s.entities.size()))
        {
            const auto h = s.entities[static_cast<std::size_t>(b1)].handle;
            if (auto* rb = w.entity_modifiers[h.id].template find<RigidBodyModifier>())
            {
                rb->mass = 8.0F;
                rb->linear_velocity = { 4.8F, -1.0F, 0.0F };
            }
        }
        s.cam.eye    = { 0.0F, 3.4F, 6.8F };
        s.cam.target = { 0.0F, 1.2F, 0.0F };
        log_push("[level] Loaded '3.1 Double Ramp & Rover Collision (06_jolt)'");
    }
    else if (template_id == 8)
    {
        // Level 4.1: 05_avatar_locomotion_anim_ik — 3x Skinned CesiumMan Squad + Obstacle Course
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { -1.8F, 0.02F,  0.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", {  0.0F, 0.02F, -1.2F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", {  1.8F, 0.02F,  0.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Static_Ramp",         {  0.0F, 0.15F,  2.2F }, log_push);
        s.cam.eye    = { -3.2F, 2.6F, 4.8F };
        s.cam.target = {  0.0F, 0.9F, 0.0F };
        log_push("[level] Loaded '4.1 Skinned Avatar Squad & IK (05_avatar)'");
    }
    else if (template_id == 9)
    {
        // Level 4.2: 08_synth_pilot_squad_narrative — Tactical Combat Arena
        const int w1 = spawn_entity_with_modifiers(s, "Static_Wall",         { -2.5F, 0.0F, -1.5F }, log_push);
        const int w2 = spawn_entity_with_modifiers(s, "Static_Wall",         {  2.5F, 0.0F,  1.5F }, log_push);
        (void)assign_texture_asset_to_entity(s, w1, "Brick_Wall_PBR.tex", log_push);
        (void)assign_texture_asset_to_entity(s, w2, "Brick_Wall_PBR.tex", log_push);
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { -1.2F, 0.02F, 0.5F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", {  1.2F, 0.02F,-0.5F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Prefab_CrateStack",   {  0.0F, 0.02F, 0.0F }, log_push);
        s.cam.eye    = { -4.5F, 3.6F, 5.8F };
        s.cam.target = {  0.0F, 1.0F, 0.0F };
        log_push("[level] Loaded '4.2 Tactical Synth Squad Arena (08_synth)'");
    }
    else if (template_id == 10)
    {
        // Level 5.1: 02_celestial_aether_fog_clouds — Volumetric Fog & God-Rays
        w.environment_modifiers.add_or_replace(std::make_unique<FogModifier>());
        for (int i = -2; i <= 2; ++i)
        {
            const int c_idx = spawn_entity_with_modifiers(s, "Cylinder", { static_cast<float>(i) * 2.2F, 0.8F, -2.0F }, log_push);
            (void)assign_texture_asset_to_entity(s, c_idx, "Carrara_Marble.tex", log_push);
        }
        (void)spawn_entity_with_modifiers(s, "Dynamic_Sphere", { 0.0F, 1.5F, 0.5F }, log_push);
        s.cam.eye    = { 0.0F, 2.4F, 6.2F };
        s.cam.target = { 0.0F, 1.2F, -1.0F };
        log_push("[level] Loaded '5.1 Celestial Fog & God-Rays (02_aether)'");
    }
    else if (template_id == 11)
    {
        // Level 5.2: 07_terra_ocean_nature_world — Golden Hour Archipelago
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
            // Turn ground slab into a mirror-like ocean surface
            s.entities[0].name      = "Ocean_Surface_Plane";
            s.entities[0].tint      = { 0.10F, 0.32F, 0.48F };
            s.entities[0].metallic  = 0.92F;
            s.entities[0].roughness = 0.06F;
            w.entity_modifiers[s.entities[0].handle.id].add_or_replace(
                std::make_unique<PbrMaterialModifier>(s.entities[0].tint, 0.92F, 0.06F, false));
        }
        (void)spawn_entity_with_modifiers(s, "Cone",     { -2.2F, 0.5F, -1.5F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Cone",     {  2.4F, 0.5F, -2.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Torus",    {  0.0F, 1.2F,  0.0F }, log_push);
        s.cam.eye    = { -4.2F, 2.6F, 5.4F };
        s.cam.target = {  0.0F, 0.9F, -0.8F };
        log_push("[level] Loaded '5.2 Golden Hour Ocean & Terra (07_terra)'");
    }
    else if (template_id == 12)
    {
        // Level 2.4: Content Browser, Custom Mesh & Surface Texture Assignment Showcase
        const int e_cube = spawn_entity_with_modifiers(s, "Static_Wall",    { -2.6F, 0.0F, -0.4F }, log_push);
        const int e_sph  = spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -0.95F, 0.0F, 0.1F }, log_push);
        const int e_cyl  = spawn_entity_with_modifiers(s, "Cylinder",       {  0.70F, 0.0F, -0.2F }, log_push);
        const int e_tor  = spawn_entity_with_modifiers(s, "Torus",          {  2.25F, 0.0F,  0.1F }, log_push);
        const int e_wood = spawn_entity_with_modifiers(s, "Dynamic_Cube",   { -1.85F, 0.0F,  1.25F }, log_push);
        const int e_chr  = spawn_entity_with_modifiers(s, "Character_CesiumMan", { 1.45F, 0.02F, 1.15F }, log_push);

        auto place_static = [&](int idx, cd::math::Vec3f pos, cd::math::Vec3f scl, const char* custom_name)
        {
            if (idx < 0 || !std::cmp_less(idx, s.entities.size())) return;
            auto& ent = s.entities[static_cast<std::size_t>(idx)];
            ent.name  = custom_name;
            if (auto* lt = s.scene.local(ent.handle))
            {
                lt->value.position = pos;
                lt->value.scale    = scl;
            }
            if (auto* rb = w.entity_modifiers[ent.handle.id].template find<RigidBodyModifier>())
            {
                rb->body_type = cd::physics::BodyType::kStatic;
            }
            record_entity_home_transform(s, ent.handle);
        };

        place_static(e_cube, { -2.65F, 0.72F, -0.55F }, { 1.25F, 1.44F, 0.85F }, "Brick_Wall_Block");
        place_static(e_sph,  { -0.95F, 0.78F,  0.05F }, { 1.22F, 1.22F, 1.22F }, "UV_Checker_Sphere");
        place_static(e_cyl,  {  0.68F, 0.78F, -0.35F }, { 1.05F, 1.45F, 1.05F }, "Carrara_Marble_Pillar");
        place_static(e_tor,  {  2.30F, 0.72F, -0.10F }, { 1.18F, 1.18F, 1.18F }, "Carbon_Fiber_Torus");
        place_static(e_wood, { -1.85F, 0.48F,  1.25F }, { 0.95F, 0.95F, 0.95F }, "Oak_Wood_Crate");

        (void)assign_texture_asset_to_entity(s, e_cube, "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg", log_push);
        (void)assign_texture_asset_to_entity(s, e_sph,  "assets/textures/UV_Checkerboard_2K.tex", log_push);
        (void)assign_texture_asset_to_entity(s, e_cyl,  "assets/textures/Carrara_Marble.tex", log_push);
        (void)assign_texture_asset_to_entity(s, e_tor,  "assets/textures/Carbon_Fiber_Twill.tex", log_push);
        (void)assign_texture_asset_to_entity(s, e_wood, "assets/textures/Oak_Wood_Planks.tex", log_push);
        (void)e_chr;

        s.selected   = e_sph;
        s.cam.eye    = { -0.2F, 2.55F, 5.35F };
        s.cam.target = { -0.1F, 0.75F, 0.0F };
        w.active_world_name = "World 2: Photoreal PBR & Sponza GI";
        w.active_level_name = "2.4 Asset Browser & Texture/Mesh Lab";
        log_push("[level] Loaded '2.4 Asset Browser & Texture/Mesh Lab'");
    }
    s.scene.update_transforms();
}

/// Switch active Level (and World) inside `s.cd_world.project()` and load its 3D content!
template <typename StateT, typename LogFn>
inline bool switch_to_project_level(StateT& s, std::string_view level_name, LogFn&& log_push)
{
    populate_master_showcase_project(s.cd_world);
    auto* proj = s.cd_world.project();
    if (!proj) return false;
    auto* lvl = proj->find_level(level_name);
    if (!lvl) return false;

    proj->set_active_level(level_name);
    s.cd_world.set_name(std::string(lvl->world_name()));
    auto& w = studio_world();
    w.active_world_name = std::string(lvl->world_name());
    w.active_level_name = std::string(lvl->name());

    load_studio_interactive_template(s, static_cast<int>(lvl->template_id()), log_push);
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
        case PrimitiveKind::kSphere:   return "Sphere";
        case PrimitiveKind::kCone:     return "Cone";
        case PrimitiveKind::kCylinder: return "Cylinder";
        case PrimitiveKind::kTorus:    return "Torus";
        case PrimitiveKind::kGltf:     return "Gltf";
        case PrimitiveKind::kSponza:   return "Sponza";
        case PrimitiveKind::kCube:     return "Cube";
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

    auto root = cd::scene::serialize_scene_with(
        s.scene,
        [&](cd::ecs::Entity e, cd::asset::json::Object& obj)
        {
            const auto* en = find_ent(e);
            if (!en) return;
            obj["name"]              = cd::asset::json::Value { en->name };
            obj["kind"]              = cd::asset::json::Value { std::string { primitive_kind_to_str(en->kind) } };
            obj["is_pbr"]            = cd::asset::json::Value { en->is_pbr };
            obj["metallic"]          = cd::asset::json::Value { static_cast<double>(en->metallic) };
            obj["roughness"]         = cd::asset::json::Value { static_cast<double>(en->roughness) };
            obj["multiscatter_comp"] = cd::asset::json::Value { static_cast<double>(en->multiscatter_comp) };
            obj["iridescence_nm"]    = cd::asset::json::Value { static_cast<double>(en->iridescence_nm) };
            obj["use_texture"]       = cd::asset::json::Value { en->use_texture };
            obj["visible"]           = cd::asset::json::Value { en->visible };
            obj["locked"]            = cd::asset::json::Value { en->locked };
            obj["layer"]             = cd::asset::json::Value {
                std::string { cd::world_container::layer_of(s.ecs_world, e) } };

            cd::asset::json::Array tint;
            tint.emplace_back(static_cast<double>(en->tint.x));
            tint.emplace_back(static_cast<double>(en->tint.y));
            tint.emplace_back(static_cast<double>(en->tint.z));
            obj["tint"] = cd::asset::json::Value { std::move(tint) };

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
                        mo["metallic"]          = cd::asset::json::Value { static_cast<double>(pbr->metallic) };
                        mo["roughness"]         = cd::asset::json::Value { static_cast<double>(pbr->roughness) };
                        mo["multiscatter_comp"] = cd::asset::json::Value { static_cast<double>(pbr->multiscatter_comp) };
                        mo["iridescence_nm"]    = cd::asset::json::Value { static_cast<double>(pbr->iridescence_nm) };
                        mo["use_texture"]       = cd::asset::json::Value { pbr->use_texture };
                    }
                    else if (const auto* rb = dynamic_cast<const RigidBodyModifier*>(m.get()))
                    {
                        mo["body_type"]   = cd::asset::json::Value { static_cast<int>(rb->body_type) };
                        mo["mass"]        = cd::asset::json::Value { static_cast<double>(rb->mass) };
                        mo["restitution"] = cd::asset::json::Value { static_cast<double>(rb->restitution) };
                        mo["friction"]    = cd::asset::json::Value { static_cast<double>(rb->friction) };
                    }
                    mods_arr.emplace_back(std::move(mo));
                }
                obj["modifiers"] = cd::asset::json::Value { std::move(mods_arr) };
            }
        });

    auto& root_obj = root.as_object_mut();
    root_obj["active_project"] = cd::asset::json::Value { w.active_project_name };
    root_obj["active_world"]   = cd::asset::json::Value { w.active_world_name };
    root_obj["active_level"]   = cd::asset::json::Value { w.active_level_name };

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

            if (auto it = obj.find("name"); it != obj.end() && it->second.is_string())
                en.name = it->second.as_string();
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
            if (auto it = obj.find("visible"); it != obj.end() && it->second.is_bool())
                en.visible = it->second.as_bool();
            if (auto it = obj.find("locked"); it != obj.end() && it->second.is_bool())
                en.locked = it->second.as_bool();
            if (auto it = obj.find("layer"); it != obj.end() && it->second.is_string())
                cd::world_container::assign_layer(s.ecs_world, e, it->second.as_string());

            if (auto it = obj.find("tint");
                it != obj.end() && it->second.is_array() && it->second.as_array().size() == 3)
            {
                const auto& a = it->second.as_array();
                if (a[0].is_number() && a[1].is_number() && a[2].is_number())
                {
                    en.tint = {
                        static_cast<float>(a[0].as_number()),
                        static_cast<float>(a[1].as_number()),
                        static_cast<float>(a[2].as_number()),
                    };
                }
            }

            auto& ms = w.entity_modifiers[e.id];
            ms.add_or_replace(std::make_unique<PbrMaterialModifier>(
                en.tint, en.metallic, en.roughness, en.use_texture,
                en.multiscatter_comp, en.iridescence_nm));
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
                            pbr->base_color        = en.tint;
                            pbr->metallic          = en.metallic;
                            pbr->roughness         = en.roughness;
                            pbr->use_texture       = en.use_texture;
                            pbr->multiscatter_comp = en.multiscatter_comp;
                            pbr->iridescence_nm    = en.iridescence_nm;
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
                        }
                        ms.add_or_replace(std::move(mod));
                    }
                }
            }
            record_entity_home_transform(s, e);
            loaded.push_back(std::move(en));
        });

    s.entities = std::move(loaded);
    s.scene.update_transforms();
    s.selected = s.entities.empty() ? -1 : 0;
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
            << "\"%~dp0..\\..\\ide-base\\bin\\editor.exe\" --pie-play\n";
    }
    log_push(std::string("[export] Packaged standalone game -> ") + export_dir);
    return true;
}

} // namespace cd::studio
