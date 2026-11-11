// =============================================================================
// CHROMODYNAMIC — cd/world2d/Stage2D.cpp
// FAZ 35 (ADR-20261111) — Unified Commercial 2D Stage Implementation.
// =============================================================================
#include <cd/world2d/Stage2D.hpp>

#include <cd/asset/json/Json.hpp>
#include <cd/asset/pak/Pak.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cd::world2d
{

namespace
{

constexpr std::uint32_t kWorld2DDomain = 0x002C;

[[nodiscard]] cd::render2d::SpriteVisualStyle role_to_visual_style(EntityRole2D role) noexcept
{
    switch (role)
    {
        case EntityRole2D::kPlayerHero:     return cd::render2d::SpriteVisualStyle::kCharacterHero;
        case EntityRole2D::kDynamicCrate:   return cd::render2d::SpriteVisualStyle::kCrate;
        case EntityRole2D::kCollectibleGem: return cd::render2d::SpriteVisualStyle::kCollectibleGem;
        case EntityRole2D::kJumpPad:        return cd::render2d::SpriteVisualStyle::kJumpPad;
        case EntityRole2D::kPatrolEnemy:    return cd::render2d::SpriteVisualStyle::kPatrolDrone;
        case EntityRole2D::kMovingPlatform: return cd::render2d::SpriteVisualStyle::kMovingPlatform;
        case EntityRole2D::kGoalPortal:     return cd::render2d::SpriteVisualStyle::kGoalPortal;
        default:                            return cd::render2d::SpriteVisualStyle::kFlatQuad;
    }
}

[[nodiscard]] bool aabb_overlap_2d(
    cd::math::Vec2f ca, cd::math::Vec2f ha,
    cd::math::Vec2f cb, cd::math::Vec2f hb) noexcept
{
    return std::fabs(ca.x - cb.x) <= (ha.x + hb.x) &&
           std::fabs(ca.y - cb.y) <= (ha.y + hb.y);
}

}  // namespace

cd::schema::TypeSchema make_entity2d_type_schema()
{
    cd::schema::TypeSchema ts {};
    ts.type_name   = "Entity2D";
    ts.category    = "World2D";
    ts.description = "Unified 2D entity supporting Sprite, CharacterController2D, Physics2D, Dynamic Light & Lua 5.4";
    ts.version     = 1U;

    ts.fields.push_back(cd::schema::make_member_field(
        "name", "Name", cd::schema::FieldKind::kString,
        &Entity2D::name, std::string { "Sprite2D" }, "Display identifier in 2D Outliner"));
    ts.fields.push_back(cd::schema::make_member_field(
        "role", "Role", cd::schema::FieldKind::kEnum,
        &Entity2D::role, std::int32_t { 0 }, "Gameplay & rendering archetype",
        false, 0.0F, 9.0F, 1.0F,
        { "PropSprite", "PlayerHero", "DynamicCrate", "CollectibleGem", "JumpPad",
          "PatrolEnemy", "MovingPlatform", "GoalPortal", "PointLight2D", "ShadowOccluder2D" }));
    ts.fields.push_back(cd::schema::make_member_field(
        "visible", "Visible", cd::schema::FieldKind::kBool,
        &Entity2D::visible, true, "Render and simulate entity"));
    ts.fields.push_back(cd::schema::make_member_field(
        "position", "Position (XY)", cd::schema::FieldKind::kVec2f,
        &Entity2D::position, cd::math::Vec2f { 0.0F, 2.0F }, "World coordinates in meters/tiles (+Y up)"));
    ts.fields.push_back(cd::schema::make_member_field(
        "size", "Size (WH)", cd::schema::FieldKind::kVec2f,
        &Entity2D::size, cd::math::Vec2f { 1.0F, 1.0F }, "Width and height in world units",
        true, 0.1F, 64.0F, 0.1F));
    ts.fields.push_back(cd::schema::make_member_field(
        "rotation_deg", "Rotation (deg)", cd::schema::FieldKind::kFloat,
        &Entity2D::rotation_deg, 0.0F, "Counter-clockwise Z rotation in degrees",
        true, -360.0F, 360.0F, 1.0F));
    ts.fields.push_back(cd::schema::make_member_field(
        "color_rgba", "Color Tint", cd::schema::FieldKind::kColorRgba,
        &Entity2D::color_rgba, cd::math::Vec4f { 1.0F, 1.0F, 1.0F, 1.0F }, "Sprite or light RGBA color"));
    ts.fields.push_back(cd::schema::make_member_field(
        "layer", "Sort Layer", cd::schema::FieldKind::kInt32,
        &Entity2D::layer, std::int32_t { 100 }, "64-bit sort layer [0..255]",
        true, 0.0F, 255.0F, 1.0F));
    ts.fields.push_back(cd::schema::make_member_field(
        "z_index", "Z Bias", cd::schema::FieldKind::kInt32,
        &Entity2D::z_index, std::int32_t { 0 }, "Fine Z order within the same layer",
        true, -1000.0F, 1000.0F, 1.0F));
    ts.fields.push_back(cd::schema::make_member_field(
        "y_sort", "Top-Down Y-Sort", cd::schema::FieldKind::kBool,
        &Entity2D::y_sort, true, "Automatically order depth by foot Y coordinate"));
    ts.fields.push_back(cd::schema::make_member_field(
        "move_speed", "Move Speed", cd::schema::FieldKind::kFloat,
        &Entity2D::move_speed, 7.2F, "Horizontal movement speed (units/sec)",
        true, 0.0F, 40.0F, 0.2F));
    ts.fields.push_back(cd::schema::make_member_field(
        "jump_speed", "Jump Impulse", cd::schema::FieldKind::kFloat,
        &Entity2D::jump_speed, 13.8F, "Vertical jump velocity (units/sec)",
        true, 0.0F, 45.0F, 0.25F));
    ts.fields.push_back(cd::schema::make_member_field(
        "patrol_span", "Patrol / Travel Span", cd::schema::FieldKind::kFloat,
        &Entity2D::patrol_span, 3.0F, "Half-span distance for patrol drones & platforms",
        true, 0.0F, 30.0F, 0.25F));
    ts.fields.push_back(cd::schema::make_member_field(
        "bounce_impulse", "Bounce Impulse", cd::schema::FieldKind::kFloat,
        &Entity2D::bounce_impulse, 17.5F, "JumpPad launch velocity",
        true, 1.0F, 50.0F, 0.5F));
    ts.fields.push_back(cd::schema::make_member_field(
        "light_intensity", "Light Intensity", cd::schema::FieldKind::kFloat,
        &Entity2D::light_intensity, 1.65F, "PointLight2D luminous multiplier",
        true, 0.0F, 8.0F, 0.05F));
    ts.fields.push_back(cd::schema::make_member_field(
        "light_radius", "Light Radius", cd::schema::FieldKind::kFloat,
        &Entity2D::light_radius, 8.5F, "PointLight2D attenuation radius in world units",
        true, 0.5F, 40.0F, 0.25F));
    ts.fields.push_back(cd::schema::make_member_field(
        "cast_shadows", "Cast 2D Shadows", cd::schema::FieldKind::kBool,
        &Entity2D::cast_shadows, true, "Enable analytical soft-penumbra occlusion"));
    return ts;
}

void register_world2d_schemas(cd::schema::SchemaRegistry& registry)
{
    registry.register_type(make_entity2d_type_schema());

    registry.register_command(cd::schema::CommandSchema {
        .op_name    = "spawn_entity_2d",
        .summary    = "Spawn a new Entity2D (PlayerHero, CollectibleGem, JumpPad, PatrolEnemy, MovingPlatform, PointLight2D, etc.)",
        .reversible = true,
        .params     = {
            { "name", cd::schema::FieldKind::kString, true, "Entity display name" },
            { "role", cd::schema::FieldKind::kInt32, false, "EntityRole2D enum index (0..9)" },
            { "pos", cd::schema::FieldKind::kVec2f, false, "World [x, y] coordinates" },
            { "size", cd::schema::FieldKind::kVec2f, false, "World [width, height]" },
            { "color", cd::schema::FieldKind::kColorRgba, false, "Normalized [r, g, b, a]" },
        },
    });
    registry.register_command(cd::schema::CommandSchema {
        .op_name    = "delete_entity_2d",
        .summary    = "Delete an Entity2D by id",
        .reversible = true,
        .params     = {
            { "id", cd::schema::FieldKind::kInt32, true, "Target entity id" },
        },
    });
    registry.register_command(cd::schema::CommandSchema {
        .op_name    = "set_entity_field_2d",
        .summary    = "Modify any reflected Entity2D property through cd::schema validation",
        .reversible = true,
        .params     = {
            { "id", cd::schema::FieldKind::kInt32, true, "Target entity id" },
            { "field", cd::schema::FieldKind::kString, true, "Field name in Entity2D schema" },
            { "value", cd::schema::FieldKind::kString, true, "Typed JSON value matching field schema" },
        },
    });
    registry.register_command(cd::schema::CommandSchema {
        .op_name    = "paint_rect_2d",
        .summary    = "Paint an auto-tiled 2D tilemap rectangle and rebake greedy Box2D colliders",
        .reversible = true,
        .params     = {
            { "x0", cd::schema::FieldKind::kInt32, true, "Start grid X" },
            { "y0", cd::schema::FieldKind::kInt32, true, "Start grid Y" },
            { "x1", cd::schema::FieldKind::kInt32, true, "End grid X" },
            { "y1", cd::schema::FieldKind::kInt32, true, "End grid Y" },
            { "palette_id", cd::schema::FieldKind::kInt32, true, "Tile palette id (0=erase, 1=MossStone, 2=OneWayDeck, 3=Hazard, 4=Neon)" },
        },
    });
    registry.register_command(cd::schema::CommandSchema {
        .op_name    = "attach_lua_script_2d",
        .summary    = "Attach and sandbox-compile a live Lua 5.4 on_tick script on an Entity2D",
        .reversible = true,
        .params     = {
            { "id", cd::schema::FieldKind::kInt32, true, "Target entity id" },
            { "script", cd::schema::FieldKind::kString, true, "Lua 5.4 chunk defining on_tick(id, x, y, vx, vy, t, dt)" },
        },
    });
    registry.register_command(cd::schema::CommandSchema {
        .op_name    = "load_starter_platformer_2d",
        .summary    = "Populate the Neon Cavern 2D commercial showcase level",
        .reversible = true,
        .params     = {},
    });
}

Stage2D::Stage2D()
{
    build_starter_illuminated_platformer();
}

std::uint32_t Stage2D::add_entity(Entity2D entity)
{
    if (entity.id == 0U || find_entity(entity.id) != nullptr)
    {
        entity.id = next_entity_id_++;
    }
    else
    {
        next_entity_id_ = std::max(next_entity_id_, entity.id + 1U);
    }
    entity.home_position = entity.position;
    const std::uint32_t id = entity.id;
    entities_.push_back(std::move(entity));
    if (!entities_.back().lua_script.empty())
    {
        (void)compile_entity_lua_script(id);
    }
    return id;
}

bool Stage2D::remove_entity(std::uint32_t entity_id)
{
    for (auto it = entities_.begin(); it != entities_.end(); ++it)
    {
        if (it->id == entity_id)
        {
            lua_vms_.erase(entity_id);
            entities_.erase(it);
            return true;
        }
    }
    return false;
}

Entity2D* Stage2D::find_entity(std::uint32_t entity_id) noexcept
{
    for (auto& e : entities_)
    {
        if (e.id == entity_id)
        {
            return &e;
        }
    }
    return nullptr;
}

const Entity2D* Stage2D::find_entity(std::uint32_t entity_id) const noexcept
{
    for (const auto& e : entities_)
    {
        if (e.id == entity_id)
        {
            return &e;
        }
    }
    return nullptr;
}

void Stage2D::clear()
{
    playing_ = false;
    tilemap_.clear();
    entities_.clear();
    baked_colliders_.clear();
    lua_vms_.clear();
    next_entity_id_ = 1U;
    hud_            = GameplayHudState2D {};
}

void Stage2D::rebuild_baked_colliders()
{
    baked_colliders_ = tilemap_.bake_greedy_collision_rects();
}

bool Stage2D::compile_entity_lua_script(std::uint32_t entity_id)
{
    Entity2D* ent = find_entity(entity_id);
    if (ent == nullptr)
    {
        return false;
    }
    if (ent->lua_script.empty())
    {
        ent->lua_last_error.clear();
        lua_vms_.erase(entity_id);
        return true;
    }

    auto vm = std::make_unique<cd::script::Engine>();
    vm->set_instruction_cap(25000U);
    const auto res = vm->run_string(ent->lua_script);
    if (!res.has_value())
    {
        ent->lua_last_error = std::string { vm->last_error() };
        lua_vms_.erase(entity_id);
        return false;
    }

    ent->lua_last_error.clear();
    lua_vms_[entity_id] = std::move(vm);
    return true;
}

void Stage2D::build_starter_illuminated_platformer()
{
    clear();
    stage_name_        = "Neon_Cavern_Platformer_2D";
    ambient_color_     = { 0.24F, 0.29F, 0.42F };
    ambient_intensity_ = 1.0F;
    gravity_y_         = -28.0F;

    // 1. Paint Wang Auto-Tiled Multi-Tier Terrain
    tilemap_.paint_rect(-2, -1, 10, 0, 1U, true);   // Starting mossy ground
    tilemap_.paint_rect(11, -1, 12, -1, 3U, true);  // Hazard pit floor (easy jump or JumpPad)
    tilemap_.paint_rect(13, -1, 28, 0, 1U, true);   // East main ground
    tilemap_.paint_rect(6, 2, 9, 2, 2U, true);      // Elevated industrial steel deck
    tilemap_.paint_rect(18, 3, 21, 3, 2U, true);    // Upper steel catwalk
    tilemap_.paint_rect(23, 1, 27, 2, 4U, true);    // Glowing cyber neon citadel platform
    tilemap_.paint_rect(-3, -1, -3, 8, 1U, true);   // West boundary wall
    tilemap_.paint_rect(29, -1, 29, 8, 1U, true);   // East boundary wall
    rebuild_baked_colliders();

    // 2. Camera framing the action
    cd::render2d::Camera2DConfig cam_cfg {};
    cam_cfg.pixels_per_unit = 42.0F;
    cam_cfg.zoom            = 1.0F;
    camera_.set_config(cam_cfg);
    camera_.set_position({ 9.5F, 3.8F });

    // 3. Playable Cyber-Hero (`CharacterController2D` with coyote-time + jump-buffering)
    Entity2D hero {};
    hero.name       = "Hero_CyberRunner_2D";
    hero.role       = EntityRole2D::kPlayerHero;
    hero.position   = { 1.5F, 2.2F };
    hero.size       = { 0.85F, 1.35F };
    hero.color_rgba = { 0.25F, 0.78F, 1.0F, 1.0F };
    hero.move_speed = 7.6F;
    hero.jump_speed = 14.2F;
    hero.layer      = 120;
    (void)add_entity(std::move(hero));

    // 4. Collectible Gems along the optimal platforming arc
    const std::array<cd::math::Vec2f, 4> gem_coords { {
        { 4.5F, 2.0F },
        { 7.8F, 4.0F },
        { 14.5F, 5.4F },
        { 20.0F, 5.0F },
    } };
    for (std::size_t i = 0U; i < gem_coords.size(); ++i)
    {
        Entity2D gem {};
        gem.name       = "Energy_Gem_" + std::to_string(i + 1U);
        gem.role       = EntityRole2D::kCollectibleGem;
        gem.position   = gem_coords.at(i);
        gem.size       = { 0.62F, 0.62F };
        gem.color_rgba = { 0.35F, 1.0F, 0.72F, 1.0F };
        gem.layer      = 110;
        (void)add_entity(std::move(gem));
    }

    // 5. JumpPad before the plasma pit
    Entity2D pad {};
    pad.name           = "JumpPad_Launcher_2D";
    pad.role           = EntityRole2D::kJumpPad;
    pad.position       = { 10.0F, 1.35F };
    pad.size           = { 1.15F, 0.50F };
    pad.color_rgba     = { 0.20F, 0.95F, 0.78F, 1.0F };
    pad.bounce_impulse = 16.8F;
    pad.layer          = 105;
    (void)add_entity(std::move(pad));

    // 6. Live Lua 5.4 Moving Platform bridging the mid-air cavern!
    Entity2D lua_plat {};
    lua_plat.name        = "Lua_MovingBridge_2D";
    lua_plat.role        = EntityRole2D::kMovingPlatform;
    lua_plat.position    = { 14.5F, 3.6F };
    lua_plat.size        = { 2.8F, 0.50F };
    lua_plat.color_rgba  = { 0.32F, 0.62F, 0.96F, 1.0F };
    lua_plat.patrol_span = 2.2F;
    lua_plat.layer       = 95;
    lua_plat.lua_script  =
        "-- Live Sandboxed Lua 5.4 Platform Controller\n"
        "function on_tick(id, x, y, vx, vy, t, dt)\n"
        "  local target_x = 14.5 + math.sin(t * 1.8) * 1.8\n"
        "  local new_vx = (target_x - x) / math.max(dt, 0.001)\n"
        "  return target_x, y, new_vx, 0.0\n"
        "end\n";
    (void)add_entity(std::move(lua_plat));

    // 7. Autonomous Patrol Enemy Drone on East ground
    Entity2D drone {};
    drone.name        = "Synth_PatrolDrone_2D";
    drone.role        = EntityRole2D::kPatrolEnemy;
    drone.position    = { 17.5F, 1.85F };
    drone.size        = { 0.95F, 0.85F };
    drone.color_rgba  = { 1.0F, 0.34F, 0.44F, 1.0F };
    drone.move_speed  = 2.8F;
    drone.patrol_span = 2.5F;
    drone.layer       = 115;
    (void)add_entity(std::move(drone));

    // 8. Physics Crate stackable prop
    Entity2D crate {};
    crate.name       = "Physics_SupplyCrate_2D";
    crate.role       = EntityRole2D::kDynamicCrate;
    crate.position   = { 21.5F, 2.2F };
    crate.size       = { 0.90F, 0.90F };
    crate.color_rgba = { 0.92F, 0.68F, 0.30F, 1.0F };
    crate.layer      = 108;
    (void)add_entity(std::move(crate));

    // 9. Goal Portal atop the Neon Citadel
    Entity2D goal {};
    goal.name       = "Goal_CitadelPortal_2D";
    goal.role       = EntityRole2D::kGoalPortal;
    goal.position   = { 25.5F, 4.35F };
    goal.size       = { 1.35F, 2.10F };
    goal.color_rgba = { 0.30F, 0.92F, 1.0F, 1.0F };
    goal.layer      = 102;
    (void)add_entity(std::move(goal));

    // 10. Dynamic 2D Point Lights + Shadow Occluder Pillar
    Entity2D warm_lantern {};
    warm_lantern.name            = "Warm_CavernLantern_Light2D";
    warm_lantern.role            = EntityRole2D::kPointLight;
    warm_lantern.position        = { 5.2F, 4.8F };
    warm_lantern.size            = { 0.5F, 0.5F };
    warm_lantern.color_rgba      = { 1.0F, 0.78F, 0.42F, 1.0F };
    warm_lantern.light_intensity = 1.75F;
    warm_lantern.light_radius    = 10.5F;
    warm_lantern.cast_shadows    = true;
    (void)add_entity(std::move(warm_lantern));

    Entity2D cyan_beacon {};
    cyan_beacon.name            = "Cyan_CitadelBeacon_Light2D";
    cyan_beacon.role            = EntityRole2D::kPointLight;
    cyan_beacon.position        = { 24.8F, 5.8F };
    cyan_beacon.size            = { 0.5F, 0.5F };
    cyan_beacon.color_rgba      = { 0.25F, 0.92F, 1.0F, 1.0F };
    cyan_beacon.light_intensity = 1.95F;
    cyan_beacon.light_radius    = 12.0F;
    cyan_beacon.cast_shadows    = true;
    (void)add_entity(std::move(cyan_beacon));

    Entity2D occluder {};
    occluder.name       = "Stone_ShadowPillar_2D";
    occluder.role       = EntityRole2D::kShadowOccluder;
    occluder.position   = { 8.8F, 4.2F };
    occluder.size       = { 0.70F, 1.60F };
    occluder.color_rgba = { 0.30F, 0.36F, 0.48F, 1.0F };
    occluder.layer      = 85;
    (void)add_entity(std::move(occluder));
}

void Stage2D::enter_play()
{
    if (playing_)
    {
        exit_play();
    }

    pre_play_snapshot_json_ = save_to_json_string(false);
    pre_play_camera_pos_    = camera_.position();
    pre_play_camera_zoom_   = camera_.zoom();

    rebuild_baked_colliders();
    physics_world_ = cd::physics::v2d::PhysicsWorld2D {};
    physics_world_.set_gravity({ 0.0F, gravity_y_ });

    cd::physics::v2d::CharacterControllerConfig2D ccfg {};
    ccfg.max_slope_deg    = 48.0F;
    ccfg.snap_down_dist   = 0.18F;
    ccfg.skin_width       = 0.015F;
    ccfg.coyote_time_sec  = 0.14F;
    ccfg.jump_buffer_sec  = 0.14F;
    char_controller_      = cd::physics::v2d::CharacterController2D { ccfg };

    // 1. Add greedy-merged static tilemap colliders to PhysicsWorld2D
    for (const auto& rc : baked_colliders_)
    {
        if (rc.hazard)
        {
            continue;  // Hazard spikes are evaluated as damage triggers so player bounces cleanly
        }
        cd::physics::core::Transform2D xf {};
        xf.position = rc.center;
        cd::physics::v2d::BoxShape2D box { .half_extents = rc.half_extents };
        (void)physics_world_.create_body(cd::physics::core::BodyMotionType::kStatic, xf, box);
    }

    // 2. Add entity physics bodies & compile Lua scripts
    hud_ = GameplayHudState2D {};
    lua_vms_.clear();

    for (auto& ent : entities_)
    {
        ent.home_position         = ent.position;
        ent.runtime_velocity      = { 0.0F, 0.0F };
        ent.collected_or_defeated = false;
        ent.physics_body_id       = 0U;
        ent.anim_timer            = 0.0F;
        ent.anim_frame            = 0U;
        ent.patrol_dir            = 1.0F;

        if (ent.role == EntityRole2D::kCollectibleGem)
        {
            ++hud_.total_gems;
        }

        if (!ent.visible)
        {
            continue;
        }

        cd::physics::core::Transform2D xf {};
        xf.position     = ent.position;
        xf.rotation_rad = ent.rotation_deg * (3.14159265F / 180.0F);
        const cd::math::Vec2f half_ext {
            std::max(0.10F, ent.size.x * 0.5F),
            std::max(0.10F, ent.size.y * 0.5F),
        };

        if (ent.role == EntityRole2D::kPlayerHero)
        {
            const float radius = std::min(half_ext.x, half_ext.y * 0.48F);
            const float hh     = std::max(0.05F, half_ext.y - radius);
            cd::physics::v2d::CapsuleShape2D cap { .half_height = hh, .radius = radius };
            ent.physics_body_id = physics_world_.create_body(
                cd::physics::core::BodyMotionType::kKinematic, xf, cap);
        }
        else if (ent.role == EntityRole2D::kDynamicCrate)
        {
            cd::physics::v2d::BoxShape2D box { .half_extents = half_ext };
            ent.physics_body_id = physics_world_.create_body(
                cd::physics::core::BodyMotionType::kDynamic, xf, box);
        }
        else if (ent.role == EntityRole2D::kMovingPlatform || ent.role == EntityRole2D::kShadowOccluder)
        {
            cd::physics::v2d::BoxShape2D box { .half_extents = half_ext };
            ent.physics_body_id = physics_world_.create_body(
                cd::physics::core::BodyMotionType::kKinematic, xf, box);
        }

        if (!ent.lua_script.empty())
        {
            if (compile_entity_lua_script(ent.id))
            {
                ++hud_.active_lua_vms;
            }
            else
            {
                ++hud_.lua_error_count;
            }
        }
    }

    playing_ = true;
}

void Stage2D::step_play(float dt, const PlayerInput2D& input)
{
    if (!playing_ || dt <= 0.0F)
    {
        return;
    }
    const float clamped_dt = std::min(dt, 0.05F);
    hud_.sim_time_sec += clamped_dt;

    Entity2D* player_ent = nullptr;
    for (auto& e : entities_)
    {
        if (e.visible && e.role == EntityRole2D::kPlayerHero)
        {
            player_ent = &e;
            break;
        }
    }

    // 1. Update Moving Platforms & Sandboxed Lua 5.4 Scripts (carry player if standing on platform!)
    for (auto& ent : entities_)
    {
        if (!ent.visible || ent.collected_or_defeated)
        {
            continue;
        }
        const cd::math::Vec2f prev_pos = ent.position;

        if (!ent.lua_script.empty())
        {
            auto vm_it = lua_vms_.find(ent.id);
            if (vm_it != lua_vms_.end() && vm_it->second != nullptr)
            {
                const std::array<double, 7> args {
                    static_cast<double>(ent.id),
                    static_cast<double>(ent.position.x),
                    static_cast<double>(ent.position.y),
                    static_cast<double>(ent.runtime_velocity.x),
                    static_cast<double>(ent.runtime_velocity.y),
                    static_cast<double>(hud_.sim_time_sec),
                    static_cast<double>(clamped_dt),
                };
                std::vector<double> rets;
                const auto call_r = vm_it->second->call_global_numeric("on_tick", args, rets, 4U);
                if (call_r.has_value() && rets.size() >= 4U)
                {
                    ent.position.x         = static_cast<float>(rets.at(0));
                    ent.position.y         = static_cast<float>(rets.at(1));
                    ent.runtime_velocity.x = static_cast<float>(rets.at(2));
                    ent.runtime_velocity.y = static_cast<float>(rets.at(3));
                }
                else if (!call_r.has_value() && ent.lua_last_error.empty())
                {
                    ent.lua_last_error = std::string { vm_it->second->last_error() };
                    ++hud_.lua_error_count;
                }
            }
        }
        else if (ent.role == EntityRole2D::kMovingPlatform && ent.patrol_span > 0.01F)
        {
            ent.position.x = ent.home_position.x + std::sin(hud_.sim_time_sec * 1.6F) * ent.patrol_span;
        }
        else if (ent.role == EntityRole2D::kPatrolEnemy)
        {
            ent.position.x += ent.patrol_dir * ent.move_speed * clamped_dt;
            if (ent.position.x > ent.home_position.x + ent.patrol_span)
            {
                ent.position.x = ent.home_position.x + ent.patrol_span;
                ent.patrol_dir = -1.0F;
                ent.flip_x     = true;
            }
            else if (ent.position.x < ent.home_position.x - ent.patrol_span)
            {
                ent.position.x = ent.home_position.x - ent.patrol_span;
                ent.patrol_dir = 1.0F;
                ent.flip_x     = false;
            }
        }

        if (ent.physics_body_id != 0U &&
            (ent.role == EntityRole2D::kMovingPlatform || ent.role == EntityRole2D::kShadowOccluder))
        {
            if (auto* body = physics_world_.get_body(ent.physics_body_id))
            {
                body->transform.position = ent.position;
            }
            // Carry hero if standing on top of this kinematic platform
            if (player_ent != nullptr && ent.role == EntityRole2D::kMovingPlatform)
            {
                const float plat_top  = prev_pos.y + ent.size.y * 0.5F;
                const float hero_foot = player_ent->position.y - player_ent->size.y * 0.5F;
                if (std::fabs(player_ent->position.x - prev_pos.x) <= (ent.size.x * 0.5F + player_ent->size.x * 0.35F) &&
                    std::fabs(hero_foot - plat_top) <= 0.22F &&
                    player_ent->runtime_velocity.y <= 0.5F)
                {
                    const cd::math::Vec2f delta { ent.position.x - prev_pos.x, ent.position.y - prev_pos.y };
                    player_ent->position.x += delta.x;
                    player_ent->position.y += delta.y;
                    if (auto* pb = physics_world_.get_body(player_ent->physics_body_id))
                    {
                        pb->transform.position = player_ent->position;
                    }
                }
            }
        }
    }

    // 2. Step Dynamic RigidBodies (`PhysicsWorld2D` Box2D v3 Soft-Step solver)
    physics_world_.step(clamped_dt, 4U);
    for (auto& ent : entities_)
    {
        if (ent.role == EntityRole2D::kDynamicCrate && ent.physics_body_id != 0U)
        {
            if (const auto* b = physics_world_.get_body(ent.physics_body_id))
            {
                ent.position     = b->transform.position;
                ent.rotation_deg = b->transform.rotation_rad * (180.0F / 3.14159265F);
            }
        }
    }

    // 3. Step Playable CharacterController2D (`move_and_slide` + coyote-time + jump-buffering)
    if (player_ent != nullptr && player_ent->physics_body_id != 0U && !hud_.game_over)
    {
        if (input.jump_pressed)
        {
            char_controller_.request_jump();
        }

        cd::math::Vec2f desired_vel = player_ent->runtime_velocity;
        desired_vel.x               = std::clamp(input.move_x, -1.0F, 1.0F) * player_ent->move_speed;
        desired_vel.y += gravity_y_ * clamped_dt;

        // Variable jump height cut when jump button released early
        if (!input.jump_held && desired_vel.y > 4.0F)
        {
            desired_vel.y *= 0.88F;
        }

        if (char_controller_.try_consume_jump())
        {
            desired_vel.y = player_ent->jump_speed;
        }

        const cd::math::Vec2f solved_vel = char_controller_.move_and_slide(
            physics_world_, player_ent->physics_body_id, desired_vel, clamped_dt);
        player_ent->runtime_velocity = solved_vel;

        if (const auto* pb = physics_world_.get_body(player_ent->physics_body_id))
        {
            player_ent->position = pb->transform.position;
        }

        if (std::fabs(desired_vel.x) > 0.25F)
        {
            player_ent->flip_x = (desired_vel.x < 0.0F);
            player_ent->anim_timer += clamped_dt * 10.0F;
            player_ent->anim_frame = static_cast<std::uint32_t>(player_ent->anim_timer) % 4U;
        }
        else
        {
            player_ent->anim_frame = 0U;
        }

        // Check hazard tile overlaps
        const cd::math::Vec2f p_half { player_ent->size.x * 0.42F, player_ent->size.y * 0.46F };
        for (const auto& rc : baked_colliders_)
        {
            if (rc.hazard && aabb_overlap_2d(player_ent->position, p_half, rc.center, rc.half_extents))
            {
                hud_.player_hp                 = std::max(0, hud_.player_hp - 20);
                player_ent->runtime_velocity.y = player_ent->jump_speed * 0.82F;
                player_ent->position.y += 0.25F;
                if (auto* pb = physics_world_.get_body(player_ent->physics_body_id))
                {
                    pb->transform.position = player_ent->position;
                }
                camera_.add_trauma(0.45F);
                break;
            }
        }

        // Check gameplay triggers (Gems, JumpPads, Patrol Enemies, Goal Portal)
        for (auto& other : entities_)
        {
            if (&other == player_ent || !other.visible || other.collected_or_defeated)
            {
                continue;
            }
            const cd::math::Vec2f o_half { other.size.x * 0.5F, other.size.y * 0.5F };
            if (!aabb_overlap_2d(player_ent->position, p_half, other.position, o_half))
            {
                continue;
            }

            switch (other.role)
            {
                case EntityRole2D::kCollectibleGem:
                    other.collected_or_defeated = true;
                    ++hud_.gems_collected;
                    hud_.score += 250;
                    break;
                case EntityRole2D::kJumpPad:
                    player_ent->runtime_velocity.y = std::max(player_ent->runtime_velocity.y, other.bounce_impulse);
                    camera_.add_trauma(0.22F);
                    break;
                case EntityRole2D::kPatrolEnemy:
                    if (player_ent->position.y > other.position.y + other.size.y * 0.22F &&
                        player_ent->runtime_velocity.y <= 1.0F)
                    {
                        // Stomp enemy from above!
                        other.collected_or_defeated    = true;
                        ++hud_.enemies_defeated;
                        hud_.score += 500;
                        player_ent->runtime_velocity.y = player_ent->jump_speed * 0.78F;
                        camera_.add_trauma(0.30F);
                    }
                    else
                    {
                        hud_.player_hp                 = std::max(0, hud_.player_hp - 25);
                        const float push_dir           = (player_ent->position.x < other.position.x) ? -1.0F : 1.0F;
                        player_ent->position.x += push_dir * 0.55F;
                        player_ent->runtime_velocity.y = 8.5F;
                        if (auto* pb = physics_world_.get_body(player_ent->physics_body_id))
                        {
                            pb->transform.position = player_ent->position;
                        }
                        camera_.add_trauma(0.45F);
                    }
                    break;
                case EntityRole2D::kGoalPortal:
                    if (!hud_.victory)
                    {
                        hud_.victory = true;
                        hud_.score += 1000;
                    }
                    break;
                default:
                    break;
            }
        }

        if (hud_.player_hp <= 0 || player_ent->position.y < -12.0F)
        {
            hud_.game_over = true;
        }

        camera_.follow_target(player_ent->position, player_ent->runtime_velocity, clamped_dt);
    }
    else
    {
        camera_.step_shake(clamped_dt);
    }
}

void Stage2D::exit_play()
{
    if (!playing_)
    {
        return;
    }
    playing_ = false;
    lua_vms_.clear();
    if (!pre_play_snapshot_json_.empty())
    {
        (void)load_from_json_string(pre_play_snapshot_json_);
    }
    camera_.set_position(pre_play_camera_pos_);
    camera_.set_zoom(pre_play_camera_zoom_);
    pre_play_snapshot_json_.clear();
}

void Stage2D::build_lighting_environment(cd::render2d::LightingEnvironment2D& out_env) const
{
    out_env                   = cd::render2d::LightingEnvironment2D {};
    out_env.ambient_color     = ambient_color_;
    out_env.ambient_intensity = ambient_intensity_;
    out_env.enable_lighting   = true;

    for (const auto& ent : entities_)
    {
        if (!ent.visible || ent.collected_or_defeated)
        {
            continue;
        }
        if (ent.role == EntityRole2D::kPointLight)
        {
            out_env.point_lights.push_back(cd::render2d::PointLight2D {
                .position     = ent.position,
                .color        = { ent.color_rgba.x, ent.color_rgba.y, ent.color_rgba.z },
                .intensity    = ent.light_intensity,
                .radius       = ent.light_radius,
                .falloff_exp  = 1.55F,
                .z_height     = 1.4F,
                .cast_shadows = ent.cast_shadows,
                .enabled      = true,
            });
        }
        else if (ent.role == EntityRole2D::kGoalPortal)
        {
            out_env.point_lights.push_back(cd::render2d::PointLight2D {
                .position     = ent.position,
                .color        = { ent.color_rgba.x, ent.color_rgba.y, ent.color_rgba.z },
                .intensity    = 1.25F,
                .radius       = 6.5F,
                .falloff_exp  = 1.7F,
                .z_height     = 1.2F,
                .cast_shadows = false,
                .enabled      = true,
            });
        }
        else if (ent.role == EntityRole2D::kShadowOccluder)
        {
            out_env.occluders.push_back(cd::render2d::ShadowOccluder2D {
                .center       = ent.position,
                .half_extents = { ent.size.x * 0.5F, ent.size.y * 0.5F },
                .opacity      = 0.90F,
                .enabled      = true,
            });
        }
    }
}

void Stage2D::render(
    cd::render2d::Canvas2D&                   out_canvas,
    const cd::render2d::ViewportRect2D&       viewport,
    const cd::render2d::Render2DDebugOptions& debug_opts) const
{
    cd::render2d::LightingEnvironment2D light_env {};
    build_lighting_environment(light_env);

    std::vector<cd::render2d::Sprite2D> sprites;
    sprites.reserve(entities_.size());
    for (const auto& ent : entities_)
    {
        if (!ent.visible || ent.collected_or_defeated || ent.role == EntityRole2D::kPointLight)
        {
            continue;
        }
        cd::render2d::Sprite2D sp {};
        sp.position         = ent.position;
        sp.size             = ent.size;
        sp.rotation_rad     = ent.rotation_deg * (3.14159265F / 180.0F);
        sp.tint             = cd::render2d::ColorRgba8::from_floats(
            ent.color_rgba.x, ent.color_rgba.y, ent.color_rgba.z, ent.color_rgba.w);
        sp.layer            = static_cast<std::uint8_t>(std::clamp(ent.layer, 0, 255));
        sp.z_index          = static_cast<std::int16_t>(std::clamp(ent.z_index, -32000, 32000));
        sp.y_sort           = ent.y_sort;
        sp.flip_x           = ent.flip_x;
        sp.lit              = (ent.role != EntityRole2D::kCollectibleGem && ent.role != EntityRole2D::kGoalPortal);
        sp.style            = role_to_visual_style(ent.role);
        sp.anim_frame_index = ent.anim_frame;
        sprites.push_back(sp);
    }

    renderer_.render_scene(
        out_canvas, viewport, camera_, tilemap_, light_env, sprites, baked_colliders_, debug_opts);
}

std::string Stage2D::save_to_json_string(bool pretty) const
{
    namespace j = cd::asset::json;
    j::Object root;
    root["format"]            = "chromodynamic.stage2d.v1";
    root["stage_name"]        = stage_name_;
    root["gravity_y"]         = static_cast<double>(gravity_y_);
    root["ambient_intensity"] = static_cast<double>(ambient_intensity_);
    root["ambient_color"]     = j::Array {
        static_cast<double>(ambient_color_.x),
        static_cast<double>(ambient_color_.y),
        static_cast<double>(ambient_color_.z),
    };
    root["camera_pos"] = j::Array {
        static_cast<double>(camera_.position().x),
        static_cast<double>(camera_.position().y),
    };
    root["camera_zoom"] = static_cast<double>(camera_.zoom());

    j::Array tiles_arr;
    for (const auto& tr : tilemap_.export_tiles())
    {
        tiles_arr.push_back(j::Array {
            tr.gx,
            tr.gy,
            static_cast<int>(tr.palette_id),
        });
    }
    root["tiles"] = std::move(tiles_arr);

    j::Array ents_arr;
    for (const auto& e : entities_)
    {
        j::Object eo;
        eo["id"]              = static_cast<int>(e.id);
        eo["name"]            = e.name;
        eo["role"]            = static_cast<int>(e.role);
        eo["visible"]         = e.visible;
        eo["pos"]             = j::Array { static_cast<double>(e.position.x), static_cast<double>(e.position.y) };
        eo["size"]            = j::Array { static_cast<double>(e.size.x), static_cast<double>(e.size.y) };
        eo["rotation_deg"]    = static_cast<double>(e.rotation_deg);
        eo["color"]           = j::Array {
            static_cast<double>(e.color_rgba.x),
            static_cast<double>(e.color_rgba.y),
            static_cast<double>(e.color_rgba.z),
            static_cast<double>(e.color_rgba.w),
        };
        eo["layer"]           = e.layer;
        eo["z_index"]         = e.z_index;
        eo["y_sort"]          = e.y_sort;
        eo["flip_x"]          = e.flip_x;
        eo["move_speed"]      = static_cast<double>(e.move_speed);
        eo["jump_speed"]      = static_cast<double>(e.jump_speed);
        eo["patrol_span"]     = static_cast<double>(e.patrol_span);
        eo["bounce_impulse"]  = static_cast<double>(e.bounce_impulse);
        eo["light_intensity"] = static_cast<double>(e.light_intensity);
        eo["light_radius"]    = static_cast<double>(e.light_radius);
        eo["cast_shadows"]    = e.cast_shadows;
        eo["lua_script"]      = e.lua_script;
        ents_arr.push_back(std::move(eo));
    }
    root["entities"] = std::move(ents_arr);

    return j::serialize(j::Value { std::move(root) }, pretty);
}

cd::core::Result<void> Stage2D::load_from_json_string(std::string_view json_text)
{
    if (json_text.size() >= 3U &&
        static_cast<unsigned char>(json_text[0]) == 0xEFU &&
        static_cast<unsigned char>(json_text[1]) == 0xBBU &&
        static_cast<unsigned char>(json_text[2]) == 0xBFU)
    {
        json_text.remove_prefix(3U);
    }
    namespace j = cd::asset::json;
    auto parsed = j::parse(json_text);
    if (!parsed.has_value())
    {
        return std::unexpected(parsed.error());
    }
    const auto& root = *parsed;
    if (!root.is_object())
    {
        return cd::core::fail(kWorld2DDomain, 1U, "Stage2D root must be a JSON object");
    }

    clear();
    if (auto nm = root.at("stage_name"); nm.has_value() && (*nm)->is_string())
    {
        stage_name_ = (*nm)->as_string();
    }
    if (auto gv = root.at("gravity_y"); gv.has_value() && (*gv)->is_number())
    {
        gravity_y_ = static_cast<float>((*gv)->as_number());
    }
    if (auto ai = root.at("ambient_intensity"); ai.has_value() && (*ai)->is_number())
    {
        ambient_intensity_ = static_cast<float>((*ai)->as_number());
    }
    if (auto ac = root.at("ambient_color"); ac.has_value() && (*ac)->is_array() && (*ac)->as_array().size() >= 3U)
    {
        ambient_color_.x = static_cast<float>((*ac)->as_array().at(0).as_number());
        ambient_color_.y = static_cast<float>((*ac)->as_array().at(1).as_number());
        ambient_color_.z = static_cast<float>((*ac)->as_array().at(2).as_number());
    }
    if (auto cp = root.at("camera_pos"); cp.has_value() && (*cp)->is_array() && (*cp)->as_array().size() >= 2U)
    {
        camera_.set_position({
            static_cast<float>((*cp)->as_array().at(0).as_number()),
            static_cast<float>((*cp)->as_array().at(1).as_number()),
        });
    }
    if (auto cz = root.at("camera_zoom"); cz.has_value() && (*cz)->is_number())
    {
        camera_.set_zoom(static_cast<float>((*cz)->as_number()));
    }

    if (auto tiles = root.at("tiles"); tiles.has_value() && (*tiles)->is_array())
    {
        for (const auto& t : (*tiles)->as_array())
        {
            if (t.is_array() && t.as_array().size() >= 3U)
            {
                const int gx  = static_cast<int>(t.as_array().at(0).as_number());
                const int gy  = static_cast<int>(t.as_array().at(1).as_number());
                const auto id = static_cast<std::uint16_t>(std::max(0, static_cast<int>(t.as_array().at(2).as_number())));
                tilemap_.set_tile(gx, gy, id, false);
            }
        }
        tilemap_.rebuild_all_autotile_masks();
    }
    rebuild_baked_colliders();

    if (auto ents = root.at("entities"); ents.has_value() && (*ents)->is_array())
    {
        for (const auto& ev : (*ents)->as_array())
        {
            if (!ev.is_object()) continue;
            Entity2D e {};
            if (auto v = ev.at("id"); v.has_value() && (*v)->is_number())
                e.id = static_cast<std::uint32_t>(std::max(1, static_cast<int>((*v)->as_number())));
            if (auto v = ev.at("name"); v.has_value() && (*v)->is_string())
                e.name = (*v)->as_string();
            if (auto v = ev.at("role"); v.has_value() && (*v)->is_number())
                e.role = static_cast<EntityRole2D>(std::clamp(static_cast<int>((*v)->as_number()), 0, 9));
            if (auto v = ev.at("visible"); v.has_value() && (*v)->is_bool())
                e.visible = (*v)->as_bool();
            if (auto v = ev.at("pos"); v.has_value() && (*v)->is_array() && (*v)->as_array().size() >= 2U)
            {
                e.position.x = static_cast<float>((*v)->as_array().at(0).as_number());
                e.position.y = static_cast<float>((*v)->as_array().at(1).as_number());
            }
            if (auto v = ev.at("size"); v.has_value() && (*v)->is_array() && (*v)->as_array().size() >= 2U)
            {
                e.size.x = static_cast<float>((*v)->as_array().at(0).as_number());
                e.size.y = static_cast<float>((*v)->as_array().at(1).as_number());
            }
            if (auto v = ev.at("rotation_deg"); v.has_value() && (*v)->is_number())
                e.rotation_deg = static_cast<float>((*v)->as_number());
            if (auto v = ev.at("color"); v.has_value() && (*v)->is_array() && (*v)->as_array().size() >= 4U)
            {
                e.color_rgba.x = static_cast<float>((*v)->as_array().at(0).as_number());
                e.color_rgba.y = static_cast<float>((*v)->as_array().at(1).as_number());
                e.color_rgba.z = static_cast<float>((*v)->as_array().at(2).as_number());
                e.color_rgba.w = static_cast<float>((*v)->as_array().at(3).as_number());
            }
            if (auto v = ev.at("layer"); v.has_value() && (*v)->is_number())
                e.layer = static_cast<std::int32_t>((*v)->as_number());
            if (auto v = ev.at("z_index"); v.has_value() && (*v)->is_number())
                e.z_index = static_cast<std::int32_t>((*v)->as_number());
            if (auto v = ev.at("y_sort"); v.has_value() && (*v)->is_bool())
                e.y_sort = (*v)->as_bool();
            if (auto v = ev.at("flip_x"); v.has_value() && (*v)->is_bool())
                e.flip_x = (*v)->as_bool();
            if (auto v = ev.at("move_speed"); v.has_value() && (*v)->is_number())
                e.move_speed = static_cast<float>((*v)->as_number());
            if (auto v = ev.at("jump_speed"); v.has_value() && (*v)->is_number())
                e.jump_speed = static_cast<float>((*v)->as_number());
            if (auto v = ev.at("patrol_span"); v.has_value() && (*v)->is_number())
                e.patrol_span = static_cast<float>((*v)->as_number());
            if (auto v = ev.at("bounce_impulse"); v.has_value() && (*v)->is_number())
                e.bounce_impulse = static_cast<float>((*v)->as_number());
            if (auto v = ev.at("light_intensity"); v.has_value() && (*v)->is_number())
                e.light_intensity = static_cast<float>((*v)->as_number());
            if (auto v = ev.at("light_radius"); v.has_value() && (*v)->is_number())
                e.light_radius = static_cast<float>((*v)->as_number());
            if (auto v = ev.at("cast_shadows"); v.has_value() && (*v)->is_bool())
                e.cast_shadows = (*v)->as_bool();
            if (auto v = ev.at("lua_script"); v.has_value() && (*v)->is_string())
                e.lua_script = (*v)->as_string();

            (void)add_entity(std::move(e));
        }
    }

    return {};
}

cd::core::Result<void> Stage2D::save_to_file(std::string_view path) const
{
    const std::filesystem::path fs_path { path };
    std::error_code             ec {};
    if (fs_path.has_parent_path())
    {
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }
    std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return cd::core::fail(kWorld2DDomain, 2U, "cannot open Stage2D output file");
    }
    const std::string data = save_to_json_string(true);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out.good())
    {
        return cd::core::fail(kWorld2DDomain, 3U, "failed writing Stage2D JSON");
    }
    return {};
}

cd::core::Result<void> Stage2D::load_from_file(std::string_view path)
{
    std::ifstream in(std::filesystem::path { path }, std::ios::binary);
    if (!in.is_open())
    {
        return cd::core::fail(kWorld2DDomain, 4U, "cannot open Stage2D file");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return load_from_json_string(ss.str());
}

std::vector<std::byte> Stage2D::build_standalone_pak_bytes() const
{
    cd::asset::pak::PakBuilder builder;
    const std::string          manifest =
        "{\n  \"entry_stage2d\": \"stages/main.cd2d.json\",\n  \"engine\": \"CHROMODYNAMIC\",\n  \"mode\": \"Pure2D\"\n}\n";
    builder.add("manifest.json", std::string_view { manifest });
    builder.add("stages/main.cd2d.json", std::string_view { save_to_json_string(true) });
    return builder.build();
}

cd::core::Result<void> Stage2D::load_from_pak_bytes(std::span<const std::byte> pak_bytes)
{
    cd::asset::pak::Pak pak;
    auto                open_r = pak.open(pak_bytes.data(), pak_bytes.size());
    if (!open_r.has_value())
    {
        return std::unexpected(open_r.error());
    }
    auto blob = pak.find("stages/main.cd2d.json");
    if (!blob.has_value())
    {
        return cd::core::fail(kWorld2DDomain, 5U, "stages/main.cd2d.json not found inside .cdpak");
    }
    const std::string_view text {
        reinterpret_cast<const char*>(blob->bytes),
        blob->size,
    };
    return load_from_json_string(text);
}

}  // namespace cd::world2d
