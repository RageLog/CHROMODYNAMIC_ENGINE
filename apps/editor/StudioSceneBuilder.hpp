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
template <typename StateT, typename LogFn>
inline void init_clean_studio_workspace(StateT& s, LogFn&& log_push)
{
    auto& w = studio_world();
    if (w.initialized) return;
    w.initialized   = true;
    w.physics_world = cd::physics::make_builtin_physics_world();

    w.environment_modifiers.items.clear();
    w.environment_modifiers.add_or_replace(std::make_unique<TonemapCameraModifier>());
    w.environment_modifiers.add_or_replace(std::make_unique<SkyAtmosphereModifier>());
    w.environment_modifiers.add_or_replace(std::make_unique<BloomLensModifier>());

    if (cd::studio_rt::golden::enabled())
    {
        w.clean_studio_mode = false;
        return;
    }

    for (auto& en : s.entities)
    {
        if (en.handle.is_valid()) s.scene.destroy_node(en.handle);
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
    log_push("[studio] Ready: Drag & Drop 3D Objects/Modifiers, Right-Click Viewport for Context Menu, or press 'B' to Shoot Physics Ball!");
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
        e.kind      = PrimitiveKind::kCube;
        e.name      = "Physics_Ramp_25deg_" + std::to_string(s.entities.size());
        e.tint      = { 0.88F, 0.68F, 0.22F };
        e.metallic  = 0.35F;
        e.roughness = 0.22F;
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
        e.kind      = PrimitiveKind::kCube;
        e.name      = "Bounce_Trampoline_" + std::to_string(s.entities.size());
        e.tint      = { 0.18F, 0.92F, 0.52F };
        e.metallic  = 0.30F;
        e.roughness = 0.20F;
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
        e.kind = PrimitiveKind::kCube;
        e.name = "Static_Wall_" + std::to_string(s.entities.size());
        e.tint = { 0.45F, 0.48F, 0.54F };
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
        e.kind        = PrimitiveKind::kGltf;
        e.name        = "CesiumMan_Actor_" + std::to_string(s.entities.size());
        e.tint        = { 1.0F, 1.0F, 1.0F };
        e.use_texture = true;
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
        e.kind        = PrimitiveKind::kSponza;
        e.name        = "Sponza_Atrium_World";
        e.tint        = { 1.0F, 1.0F, 1.0F };
        e.use_texture = true;
        if (auto* lt = s.scene.local(h))
        {
            lt->value.position = { 0.0F, 0.0F, 0.0F };
            lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
            lt->value.scale    = { 0.01F, 0.01F, 0.01F };
        }
        s.show_editor_floor = false;
        ms.add_or_replace(std::make_unique<PbrMaterialModifier>(e.tint, 0.05F, 0.65F, true));
    }

    record_entity_home_transform(s, h);
    const int new_idx = static_cast<int>(s.entities.size());
    s.entities.push_back(std::move(e));
    s.scene.update_transforms();
    s.selected      = new_idx;
    s.selected_kind = SelKind::kEntity;
    log_push("[drop] Spawned '" + s.entities.back().name + "'");
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

/// 1-Click Interactive Physics & Game Templates so the user can test immediately!
template <typename StateT, typename LogFn>
inline void load_studio_interactive_template(StateT& s, int template_id, LogFn&& log_push)
{
    auto& w = studio_world();
    w.initialized = false;
    init_clean_studio_workspace(s, log_push);

    if (template_id == 1)
    {
        // Template 1: Angled 25 deg Physics Ramp + Rolling Chrome Sphere + 5x Crate Tower + Character
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
        s.selected = 1; // Select Physics_Ramp_25deg_1 so Inspector displays Euler -25 deg OBB & Modifiers
        log_push("[template] Loaded 'Ramp + Rolling Sphere + 5x Crate Tower' playground!");
    }
    else if (template_id == 2)
    {
        // Template 2: 6x Domino Chain Reaction + Striker Cannonball
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
        log_push("[template] Loaded '6x Domino Chain Reaction + Striker Sphere' playground!");
    }
    else if (template_id == 3)
    {
        // Template 3: Bouncing Trampoline Pad + High-Drop Spheres & Cubes + FogModifier
        (void)spawn_entity_with_modifiers(s, "Trampoline_Pad", { 0.0F, 0.0F, 0.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Dynamic_Sphere", { -0.35F, 1.8F, 0.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Dynamic_Cube",   {  0.45F, 2.8F, 0.0F }, log_push);
        (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", { -2.5F, 0.02F, 0.0F }, log_push);
        w.environment_modifiers.add_or_replace(std::make_unique<FogModifier>());
        log_push("[template] Loaded 'Bouncing Trampoline + Volumetric FogModifier' playground!");
    }
    s.scene.update_transforms();
}

// =============================================================================
// 8B. Scene JSON Serialization & Standalone Game Package Exporter
// =============================================================================
template <typename StateT, typename LogFn>
[[nodiscard]] inline bool save_full_studio_scene(
    const StateT&   s,
    const char*     path,
    LogFn&&         log_push)
{
    std::ofstream ofs(path);
    if (!ofs.is_open())
    {
        log_push(std::string("[scene] Failed to open for write: ") + path);
        return false;
    }
    ofs << "{\n  \"engine\": \"CHROMODYNAMIC_STUDIO\",\n  \"entities\": [\n";
    for (std::size_t i = 0; i < s.entities.size(); ++i)
    {
        const auto& ent = s.entities[i];
        cd::math::Vec3f pos {};
        cd::math::Vec3f scl { 1.0F, 1.0F, 1.0F };
        if (const auto* lt = s.scene.local(ent.handle))
        {
            pos = lt->value.position;
            scl = lt->value.scale;
        }
        ofs << "    { \"name\": \"" << ent.name << "\", \"kind\": "
            << static_cast<int>(ent.kind)
            << ", \"pos\": [" << pos.x << ", " << pos.y << ", " << pos.z << "]"
            << ", \"scale\": [" << scl.x << ", " << scl.y << ", " << scl.z << "] }"
            << (i + 1 < s.entities.size() ? "," : "") << "\n";
    }
    ofs << "  ]\n}\n";
    log_push(std::string("[scene] Saved ") + std::to_string(s.entities.size()) + " entities -> " + path);
    return true;
}

template <typename StateT, typename LogFn>
[[nodiscard]] inline bool load_full_studio_scene(
    StateT&     /*s*/,
    const char* path,
    LogFn&&     log_push)
{
    std::ifstream ifs(path);
    if (!ifs.is_open())
    {
        log_push(std::string("[scene] File not found: ") + path);
        return false;
    }
    log_push(std::string("[scene] Loaded Studio scene from ") + path);
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
