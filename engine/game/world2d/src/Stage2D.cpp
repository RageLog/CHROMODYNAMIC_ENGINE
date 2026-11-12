// =============================================================================
// CHROMODYNAMIC: cd/world2d/Stage2D.cpp
// FAZ 35 (ADR-20261111): Unified Commercial 2D Stage Implementation.
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
        false, 0.0F, 13.0F, 1.0F,
        { "PropSprite", "PlayerHero", "DynamicCrate", "CollectibleGem", "JumpPad",
          "PatrolEnemy", "MovingPlatform", "GoalPortal", "PointLight2D", "ShadowOccluder2D",
          "KinematicPaddle", "AiOpponentPaddle", "BouncingBall", "DestructibleBlock" }));
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
    parallax_layers_.clear();
    projectile_pool_.clear();
    swarm_spawner_     = SwarmSpawnerConfig2D {};
    swarm_spawn_timer_ = 0.0F;
    fire_cooldown_     = 0.0F;
    ambient_fx_timer_  = 0.0F;
    arcade_ruleset_    = ArcadeRuleset2D::kStandard;
    y_sort_enabled_    = false;
    sync_bound_atlas_palette();
}

void Stage2D::bind_starter_atlas(std::uint32_t texture_slot, std::uint32_t tile_px)
{
    bound_atlas_slot_ = texture_slot;
    bound_atlas_      = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(std::max(16U, tile_px));
    sync_bound_atlas_palette();
}

void Stage2D::unbind_starter_atlas() noexcept
{
    bound_atlas_slot_ = 0xFFFFFFFFU;
    bound_atlas_      = {};
    const cd::render2d::Tilemap2D defaults {};
    for (const auto& p : defaults.palette())
    {
        tilemap_.register_palette_entry(p);
    }
}

void Stage2D::sync_bound_atlas_palette()
{
    if (!has_bound_atlas())
    {
        return;
    }

    const float inv_w = 1.0F / static_cast<float>(std::max(1U, bound_atlas_.width()));
    const float inv_h = 1.0F / static_cast<float>(std::max(1U, bound_atlas_.height()));
    const auto inset_uv = [inv_w, inv_h](const cd::render2d::UvRect2D& uv) noexcept -> cd::render2d::UvRect2D {
        const float eps_u = 0.25F * inv_w;
        const float eps_v = 0.25F * inv_h;
        return cd::render2d::UvRect2D {
            .u0 = uv.u0 + eps_u,
            .v0 = uv.v0 + eps_v,
            .u1 = uv.u1 - eps_u,
            .v1 = uv.v1 - eps_v,
        };
    };
    const auto region_uv = [this, &inset_uv](std::string_view name) noexcept -> cd::render2d::UvRect2D {
        if (const auto* r = bound_atlas_.find_region(name); r != nullptr)
        {
            return inset_uv(r->uv);
        }
        return cd::render2d::UvRect2D {};
    };

    const bool is_rpg =
        (stage_name_.find("Rpg") != std::string::npos || stage_name_.find("Dungeon") != std::string::npos);

    std::vector<cd::render2d::TilePaletteEntry2D> updated;
    updated.reserve(tilemap_.palette().size());
    for (const auto& src : tilemap_.palette())
    {
        cd::render2d::TilePaletteEntry2D entry = src;
        entry.texture_slot                     = bound_atlas_slot_;
        entry.fill_color                       = { 255U, 255U, 255U, 255U };

        if (entry.palette_id == 1U)
        {
            entry.has_autotile_uvs = true;
            entry.uv_rect          = region_uv("tile_grass");
            for (std::uint8_t mask = 0U; mask < 16U; ++mask)
            {
                const bool n = (mask & cd::render2d::autotile_bits::kNorth) != 0U;
                const bool s = (mask & cd::render2d::autotile_bits::kSouth) != 0U;
                const bool e = (mask & cd::render2d::autotile_bits::kEast) != 0U;
                const bool w = (mask & cd::render2d::autotile_bits::kWest) != 0U;
                if (!n)
                {
                    if (!w && e)
                    {
                        entry.autotile_uvs.at(mask) = region_uv("tile_grass_left");
                    }
                    else if (w && !e)
                    {
                        entry.autotile_uvs.at(mask) = region_uv("tile_grass_right");
                    }
                    else if (!w && !e)
                    {
                        entry.autotile_uvs.at(mask) = region_uv("tile_grass_island");
                    }
                    else
                    {
                        entry.autotile_uvs.at(mask) = region_uv("tile_grass");
                    }
                }
                else
                {
                    entry.autotile_uvs.at(mask) = (s && e && w) ? region_uv("tile_dirt") : region_uv("tile_stone");
                }
            }
        }
        else if (entry.palette_id == 2U)
        {
            entry.has_autotile_uvs = false;
            entry.uv_rect          = is_rpg ? region_uv("tile_stone") : region_uv("tile_platform_oneway");
        }
        else if (entry.palette_id == 3U)
        {
            entry.has_autotile_uvs = false;
            entry.uv_rect          = region_uv("tile_spikes_plasma");
            entry.emissive         = true;
        }
        else if (entry.palette_id == 4U)
        {
            entry.has_autotile_uvs = false;
            entry.uv_rect          = region_uv("tile_neon_conduit");
            entry.emissive         = true;
        }
        else if (entry.palette_id == 5U)
        {
            entry.has_autotile_uvs = false;
            const bool is_arena =
                (entry.name.find("Arena") != std::string::npos || stage_name_.find("Arena") != std::string::npos ||
                 stage_name_.find("Pong") != std::string::npos || arcade_ruleset_ == ArcadeRuleset2D::kPongCourt);
            entry.uv_rect = is_arena ? region_uv("tile_arena_floor") : region_uv("tile_dungeon_floor");
        }

        updated.push_back(std::move(entry));
    }

    for (auto& entry : updated)
    {
        tilemap_.register_palette_entry(std::move(entry));
    }
}

void Stage2D::sort_entities_by_foot_y()
{
    std::stable_sort(entities_.begin(), entities_.end(), compare_entities_by_foot_y);
}

bool Stage2D::compare_entities_by_foot_y(const Entity2D& a, const Entity2D& b) noexcept
{
    if (a.layer != b.layer)
    {
        return a.layer < b.layer;
    }
    if (a.z_index != b.z_index)
    {
        return a.z_index < b.z_index;
    }
    if (a.y_sort != b.y_sort)
    {
        return a.y_sort < b.y_sort;
    }
    if (a.y_sort)
    {
        const float foot_a = a.position.y - a.size.y * 0.5F;
        const float foot_b = b.position.y - b.size.y * 0.5F;
        if (foot_a > foot_b)
        {
            return true;
        }
        if (foot_a < foot_b)
        {
            return false;
        }
    }
    return false;
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

    sync_bound_atlas_palette();
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

        if (ent.role == EntityRole2D::kCollectibleGem || ent.role == EntityRole2D::kDestructibleBlock)
        {
            ++hud_.total_gems;
        }

        if (!ent.visible)
        {
            continue;
        }

        if (ent.role == EntityRole2D::kBouncingBall)
        {
            const float spd = (ent.move_speed > 0.2F) ? ent.move_speed : 13.5F;
            const float rad = ent.rotation_deg * (3.14159265F / 180.0F);
            ent.runtime_velocity = { std::cos(rad) * spd, std::sin(rad) * spd };
            if (arcade_ruleset_ == ArcadeRuleset2D::kPongCourt && std::fabs(ent.runtime_velocity.x) < 4.0F)
            {
                ent.runtime_velocity.x = (ent.runtime_velocity.x >= 0.0F ? 1.0F : -1.0F) * spd * 0.85F;
                ent.runtime_velocity.y = spd * 0.45F;
            }
            else if (arcade_ruleset_ == ArcadeRuleset2D::kBridgeDemolition && std::fabs(ent.runtime_velocity.y) < 4.0F)
            {
                ent.runtime_velocity.x = spd * 0.42F;
                ent.runtime_velocity.y = spd * 0.90F;
            }
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
        else if (ent.role == EntityRole2D::kMovingPlatform || ent.role == EntityRole2D::kShadowOccluder ||
                 ent.role == EntityRole2D::kKinematicPaddle || ent.role == EntityRole2D::kAiOpponentPaddle ||
                 ent.role == EntityRole2D::kDestructibleBlock)
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

    spatial_hash_.clear();
    for (const auto& ent : entities_)
    {
        if (ent.visible && !ent.collected_or_defeated)
        {
            spatial_hash_.insert(
                ent.id,
                Aabb2D {
                    ent.position - ent.size * 0.5F,
                    ent.position + ent.size * 0.5F
                });
        }
    }
    for (std::size_t i = 0U; i < baked_colliders_.size(); ++i)
    {
        const auto& c = baked_colliders_[i];
        spatial_hash_.insert(
            0x80000000U | static_cast<std::uint32_t>(i),
            Aabb2D { c.center - c.half_extents, c.center + c.half_extents });
    }
    particle_system_.clear();
    audio_cues_.reset_telemetry();
    if (arcade_ruleset_ == ArcadeRuleset2D::kPongCourt)
    {
        hud_.total_gems = 5;
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
    particle_system_.update(clamped_dt);

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
        else if (ent.role == EntityRole2D::kKinematicPaddle && !hud_.victory && !hud_.game_over)
        {
            if (ent.size.y >= ent.size.x)
            {
                const float axis = (std::fabs(input.move_y) > 0.01F) ? input.move_y : input.move_x;
                ent.runtime_velocity = { 0.0F, std::clamp(axis, -1.0F, 1.0F) * ent.move_speed };
                ent.position.y = std::clamp(
                    ent.position.y + ent.runtime_velocity.y * clamped_dt,
                    ent.home_position.y - ent.patrol_span,
                    ent.home_position.y + ent.patrol_span);
            }
            else
            {
                ent.runtime_velocity = { std::clamp(input.move_x, -1.0F, 1.0F) * ent.move_speed, 0.0F };
                ent.position.x = std::clamp(
                    ent.position.x + ent.runtime_velocity.x * clamped_dt,
                    ent.home_position.x - ent.patrol_span,
                    ent.home_position.x + ent.patrol_span);
            }
        }
        else if (ent.role == EntityRole2D::kAiOpponentPaddle && !hud_.victory && !hud_.game_over)
        {
            const Entity2D* target_ball = nullptr;
            float best_score            = 1.0e9F;
            for (const auto& b : entities_)
            {
                if (!b.visible || b.collected_or_defeated || b.role != EntityRole2D::kBouncingBall)
                {
                    continue;
                }
                const float dist_x   = std::fabs(b.position.x - ent.position.x);
                const bool  incoming = ((ent.position.x > 0.0F && b.runtime_velocity.x > 0.0F) ||
                                        (ent.position.x < 0.0F && b.runtime_velocity.x < 0.0F));
                const float weight   = incoming ? dist_x : (dist_x + 50.0F);
                if (weight < best_score)
                {
                    best_score  = weight;
                    target_ball = &b;
                }
            }
            if (target_ball != nullptr)
            {
                if (ent.size.y >= ent.size.x)
                {
                    const float dy   = target_ball->position.y - ent.position.y;
                    const float step = std::clamp(dy * 5.5F, -ent.move_speed, ent.move_speed);
                    ent.runtime_velocity = { 0.0F, step };
                    ent.position.y = std::clamp(
                        ent.position.y + step * clamped_dt,
                        ent.home_position.y - ent.patrol_span,
                        ent.home_position.y + ent.patrol_span);
                }
                else
                {
                    const float dx   = target_ball->position.x - ent.position.x;
                    const float step = std::clamp(dx * 5.5F, -ent.move_speed, ent.move_speed);
                    ent.runtime_velocity = { step, 0.0F };
                    ent.position.x = std::clamp(
                        ent.position.x + step * clamped_dt,
                        ent.home_position.x - ent.patrol_span,
                        ent.home_position.x + ent.patrol_span);
                }
            }
        }
        else if (ent.role == EntityRole2D::kPatrolEnemy)
        {
            bool has_bridge_support = true;
            if (arcade_ruleset_ == ArcadeRuleset2D::kBridgeDemolition && gravity_y_ < -0.1F)
            {
                has_bridge_support  = false;
                const float foot_y  = ent.position.y - ent.size.y * 0.5F;
                const cd::math::Vec2f probe_c { ent.position.x, foot_y - 0.14F };
                const cd::math::Vec2f probe_h { ent.size.x * 0.35F, 0.22F };
                for (const auto& rc : baked_colliders_)
                {
                    if (!rc.hazard && aabb_overlap_2d(probe_c, probe_h, rc.center, rc.half_extents))
                    {
                        has_bridge_support = true;
                        break;
                    }
                }
                if (!has_bridge_support)
                {
                    for (const auto& blk : entities_)
                    {
                        if (blk.visible && !blk.collected_or_defeated && blk.role == EntityRole2D::kDestructibleBlock &&
                            aabb_overlap_2d(probe_c, probe_h, blk.position, blk.size * 0.5F))
                        {
                            has_bridge_support = true;
                            break;
                        }
                    }
                }
            }

            if (has_bridge_support)
            {
                ent.runtime_velocity.y = 0.0F;
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
                ent.anim_timer += clamped_dt * 6.0F;
                ent.anim_frame = static_cast<std::uint32_t>(ent.anim_timer) % 4U;
            }
            else
            {
                // Bridge beneath enemy shattered! Tumble into the glowing canyon abyss!
                ent.runtime_velocity.y += gravity_y_ * clamped_dt;
                ent.position.y += ent.runtime_velocity.y * clamped_dt;
                ent.position.x += ent.patrol_dir * (ent.move_speed * 0.35F) * clamped_dt;
                ent.rotation_deg += 280.0F * clamped_dt;

                bool hit_abyss = (ent.position.y < -6.2F);
                if (!hit_abyss)
                {
                    const cd::math::Vec2f eh = ent.size * 0.45F;
                    for (const auto& rc : baked_colliders_)
                    {
                        if (rc.hazard && aabb_overlap_2d(ent.position, eh, rc.center, rc.half_extents))
                        {
                            hit_abyss = true;
                            break;
                        }
                    }
                }
                if (hit_abyss)
                {
                    ent.collected_or_defeated = true;
                    ++hud_.enemies_defeated;
                    hud_.score += 500;
                    particle_system_.spawn_burst(ent.position, 24U, ParticleEmitterPresets2D::enemy_defeat());
                    audio_cues_.play_cue_at(AudioCuePreset2D::kExplosion, ent.position);
                    camera_.add_trauma(0.26F);
                }
            }
        }

        if (ent.physics_body_id != 0U &&
            (ent.role == EntityRole2D::kMovingPlatform || ent.role == EntityRole2D::kShadowOccluder ||
             ent.role == EntityRole2D::kKinematicPaddle || ent.role == EntityRole2D::kAiOpponentPaddle))
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
        if (std::abs(gravity_y_) < 0.01F)
        {
            // Zero gravity: 4/8-way movement (Top-down RPG & Twin-Stick Arena)
            cd::math::Vec2f move_dir { input.move_x, input.move_y };
            const float len = std::sqrt(move_dir.x * move_dir.x + move_dir.y * move_dir.y);
            if (len > 1.0e-4F)
            {
                move_dir.x /= len;
                move_dir.y /= len;
                player_ent->runtime_velocity = move_dir * player_ent->move_speed;
                player_ent->flip_x           = (move_dir.x < -0.1F);
                player_ent->anim_timer += clamped_dt * 8.0F;
                player_ent->anim_frame = static_cast<std::uint32_t>(player_ent->anim_timer) % 4U;
            }
            else
            {
                player_ent->runtime_velocity = { 0.0F, 0.0F };
                player_ent->anim_frame       = 0U;
            }
            const cd::math::Vec2f solved_vel = char_controller_.move_and_slide(
                physics_world_, player_ent->physics_body_id, player_ent->runtime_velocity, clamped_dt);
            player_ent->runtime_velocity = solved_vel;
            if (const auto* pb = physics_world_.get_body(player_ent->physics_body_id))
            {
                player_ent->position = pb->transform.position;
            }
        }
        else
        {
            // Platformer side-scrolling with gravity & jump / wall-jump
            if (input.jump_pressed)
            {
                if (char_controller_.on_wall() && !char_controller_.on_floor())
                {
                    const cd::math::Vec2f wn = char_controller_.wall_normal();
                    player_ent->runtime_velocity.x = wn.x * player_ent->move_speed * 1.25F;
                    player_ent->runtime_velocity.y = player_ent->jump_speed * 0.92F;
                    audio_cues_.play_cue_at(AudioCuePreset2D::kJump, player_ent->position);
                    particle_system_.spawn_burst(
                        player_ent->position + wn * (player_ent->size.x * 0.45F),
                        10U,
                        ParticleEmitterPresets2D::jump_dust());
                    camera_.add_trauma(0.18F);
                }
                else
                {
                    char_controller_.request_jump();
                }
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
                audio_cues_.play_cue_at(AudioCuePreset2D::kJump, player_ent->position);
                particle_system_.spawn_burst(
                    player_ent->position - cd::math::Vec2f { 0.0F, player_ent->size.y * 0.45F },
                    8U,
                    ParticleEmitterPresets2D::jump_dust());
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
                    audio_cues_.play_cue_at(AudioCuePreset2D::kCoin, other.position);
                    particle_system_.spawn_burst(other.position, 16U, ParticleEmitterPresets2D::sparkle_burst());
                    break;
                case EntityRole2D::kJumpPad:
                    player_ent->runtime_velocity.y = std::max(player_ent->runtime_velocity.y, other.bounce_impulse);
                    camera_.add_trauma(0.22F);
                    audio_cues_.play_cue_at(AudioCuePreset2D::kBounce, other.position);
                    particle_system_.spawn_burst(other.position, 12U, ParticleEmitterPresets2D::jump_dust());
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
                        audio_cues_.play_cue_at(AudioCuePreset2D::kExplosion, other.position);
                        particle_system_.spawn_burst(other.position, 20U, ParticleEmitterPresets2D::enemy_defeat());
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
                        audio_cues_.play_cue_at(AudioCuePreset2D::kHit, player_ent->position);
                        particle_system_.spawn_burst(player_ent->position, 8U, ParticleEmitterPresets2D::hit_impact());
                    }
                    break;
                case EntityRole2D::kGoalPortal:
                    if (!hud_.victory)
                    {
                        hud_.victory = true;
                        hud_.score += 1000;
                        audio_cues_.play_cue_at(AudioCuePreset2D::kPowerup, other.position);
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

    // Helper to shatter a DestructibleBlock (and cascade unsupported bridge planks resting on a pier)
    auto shatter_destructible_block = [&](Entity2D& block) {
        if (block.collected_or_defeated)
        {
            return;
        }
        block.collected_or_defeated = true;
        ++hud_.gems_collected;
        hud_.score += 200;
        if (block.physics_body_id != 0U)
        {
            if (auto* b = physics_world_.get_body(block.physics_body_id))
            {
                b->transform.position = { 0.0F, -9999.0F };
            }
        }
        spatial_hash_.remove(block.id);
        particle_system_.spawn_burst(block.position, 16U, ParticleEmitterPresets2D::enemy_defeat());
        audio_cues_.play_cue_at(AudioCuePreset2D::kExplosion, block.position);
        camera_.add_trauma(0.16F);

        // Structural keystone pier cascade: destroying a vertical bridge pier collapses planks resting above it!
        if (block.size.y > block.size.x * 1.10F)
        {
            const float pier_top = block.position.y + block.size.y * 0.5F;
            for (auto& span : entities_)
            {
                if (span.visible && !span.collected_or_defeated && span.role == EntityRole2D::kDestructibleBlock &&
                    span.size.x >= span.size.y)
                {
                    const float span_bot = span.position.y - span.size.y * 0.5F;
                    if (std::fabs(span.position.x - block.position.x) <= (block.size.x * 0.5F + span.size.x * 0.72F) &&
                        span_bot >= block.position.y - 0.25F &&
                        span_bot <= pier_top + 0.85F)
                    {
                        span.collected_or_defeated = true;
                        ++hud_.gems_collected;
                        hud_.score += 250;
                        if (span.physics_body_id != 0U)
                        {
                            if (auto* sb = physics_world_.get_body(span.physics_body_id))
                            {
                                sb->transform.position = { 0.0F, -9999.0F };
                            }
                        }
                        spatial_hash_.remove(span.id);
                        particle_system_.spawn_burst(span.position, 14U, ParticleEmitterPresets2D::enemy_defeat());
                    }
                }
            }
        }

        if (arcade_ruleset_ == ArcadeRuleset2D::kBridgeDemolition && hud_.total_gems > 0 &&
            hud_.gems_collected >= hud_.total_gems && !hud_.victory)
        {
            hud_.victory = true;
            hud_.score += 2000;
            audio_cues_.play_cue_at(AudioCuePreset2D::kPowerup, block.position);
        }
    };

    // 4. Twin-Stick Weapon & Bridge Demolition Striker Cannon Firing
    fire_cooldown_ = std::max(0.0F, fire_cooldown_ - clamped_dt);
    if ((input.fire_pressed || (input.fire_held && fire_cooldown_ <= 0.0F)) && player_ent != nullptr && !hud_.game_over)
    {
        fire_cooldown_ = 0.12F;
        const cd::math::Vec2f dir { std::cos(input.aim_angle_rad), std::sin(input.aim_angle_rad) };
        (void)projectile_pool_.emplace(Projectile2D {
            .position     = player_ent->position + dir * 0.6F,
            .velocity     = dir * 22.0F,
            .radius       = 0.18F,
            .lifetime_sec = 1.4F,
            .damage       = 35,
            .from_player  = true,
            .color_rgba   = { 0.20F, 0.95F, 1.0F, 1.0F },
        });
        audio_cues_.play_cue_at(AudioCuePreset2D::kLaser, player_ent->position);
        camera_.add_trauma(0.06F);
    }
    else if (arcade_ruleset_ == ArcadeRuleset2D::kBridgeDemolition &&
             (input.fire_pressed || input.jump_pressed || (input.fire_held && fire_cooldown_ <= 0.0F)) &&
             fire_cooldown_ <= 0.0F && !hud_.victory && !hud_.game_over)
    {
        for (const auto& striker : entities_)
        {
            if (striker.visible && striker.role == EntityRole2D::kKinematicPaddle && striker.size.x > striker.size.y)
            {
                fire_cooldown_ = 0.34F;
                (void)projectile_pool_.emplace(Projectile2D {
                    .position     = striker.position + cd::math::Vec2f { 0.0F, striker.size.y * 0.72F },
                    .velocity     = { striker.runtime_velocity.x * 0.18F, 19.5F },
                    .radius       = 0.20F,
                    .lifetime_sec = 1.35F,
                    .damage       = 50,
                    .from_player  = true,
                    .color_rgba   = { 1.0F, 0.76F, 0.24F, 1.0F },
                });
                audio_cues_.play_cue_at(AudioCuePreset2D::kLaser, striker.position);
                camera_.add_trauma(0.05F);
                break;
            }
        }
    }

    // 4b. Elastic Arcade Bouncing Ball (Pong & Break the Bridges)
    if (!hud_.victory && !hud_.game_over)
    {
        const Entity2D* horizontal_striker = nullptr;
        for (const auto& e : entities_)
        {
            if (e.visible && e.role == EntityRole2D::kKinematicPaddle && e.size.x > e.size.y)
            {
                horizontal_striker = &e;
                break;
            }
        }

        for (auto& ball : entities_)
        {
            if (!ball.visible || ball.collected_or_defeated || ball.role != EntityRole2D::kBouncingBall)
            {
                continue;
            }
            const float base_spd = (ball.move_speed > 0.5F) ? ball.move_speed : 13.5F;
            const float sub_dt   = clamped_dt * 0.25F;
            const cd::math::Vec2f bh = ball.size * 0.5F;

            for (int step_i = 0; step_i < 4 && !hud_.victory && !hud_.game_over; ++step_i)
            {
                ball.position += ball.runtime_velocity * sub_dt;

                // Bounce off solid non-hazard tilemap walls
                for (const auto& rc : baked_colliders_)
                {
                    if (rc.hazard || !aabb_overlap_2d(ball.position, bh, rc.center, rc.half_extents))
                    {
                        continue;
                    }
                    const float ox = (bh.x + rc.half_extents.x) - std::fabs(ball.position.x - rc.center.x);
                    const float oy = (bh.y + rc.half_extents.y) - std::fabs(ball.position.y - rc.center.y);
                    if (ox < oy)
                    {
                        ball.position.x += (ball.position.x >= rc.center.x) ? ox : -ox;
                        ball.runtime_velocity.x = (ball.position.x >= rc.center.x)
                            ? std::fabs(ball.runtime_velocity.x)
                            : -std::fabs(ball.runtime_velocity.x);
                    }
                    else
                    {
                        ball.position.y += (ball.position.y >= rc.center.y) ? oy : -oy;
                        ball.runtime_velocity.y = (ball.position.y >= rc.center.y)
                            ? std::fabs(ball.runtime_velocity.y)
                            : -std::fabs(ball.runtime_velocity.y);
                    }
                    audio_cues_.play_cue_at(AudioCuePreset2D::kBounce, ball.position);
                    particle_system_.spawn_burst(ball.position, 5U, ParticleEmitterPresets2D::jump_dust());
                }

                // Bounce off Player / AI Paddles with arcade spin control
                for (const auto& pad : entities_)
                {
                    if (!pad.visible || pad.collected_or_defeated ||
                        (pad.role != EntityRole2D::kKinematicPaddle && pad.role != EntityRole2D::kAiOpponentPaddle))
                    {
                        continue;
                    }
                    const cd::math::Vec2f ph = pad.size * 0.5F;
                    if (!aabb_overlap_2d(ball.position, bh, pad.position, ph))
                    {
                        continue;
                    }
                    const float cur_spd = std::clamp(
                        std::sqrt(ball.runtime_velocity.x * ball.runtime_velocity.x +
                                  ball.runtime_velocity.y * ball.runtime_velocity.y) * 1.035F,
                        base_spd,
                        base_spd * 1.65F);

                    if (pad.size.y >= pad.size.x)
                    {
                        const float dir_x = (ball.position.x >= pad.position.x) ? 1.0F : -1.0F;
                        const float rel_y = std::clamp((ball.position.y - pad.position.y) / std::max(0.2F, ph.y), -0.92F, 0.92F);
                        const float ang   = rel_y * 0.96F; // up to ~55 deg
                        ball.position.x   = pad.position.x + dir_x * (ph.x + bh.x + 0.02F);
                        ball.runtime_velocity = { dir_x * std::cos(ang) * cur_spd, std::sin(ang) * cur_spd };
                    }
                    else
                    {
                        const float rel_x = std::clamp((ball.position.x - pad.position.x) / std::max(0.2F, ph.x), -0.92F, 0.92F);
                        const float ang   = rel_x * 1.02F; // up to ~58 deg from +Y
                        ball.position.y   = pad.position.y + ph.y + bh.y + 0.02F;
                        ball.runtime_velocity = { std::sin(ang) * cur_spd, std::cos(ang) * cur_spd };
                    }
                    ++hud_.rally_count;
                    hud_.score += 25;
                    audio_cues_.play_cue_at(AudioCuePreset2D::kBounce, ball.position);
                    particle_system_.spawn_burst(ball.position, 10U, ParticleEmitterPresets2D::sparkle_burst());
                    camera_.add_trauma(0.08F);
                }

                // Shatter Destructible Bridge Segments & Piers on impact
                for (auto& blk : entities_)
                {
                    if (!blk.visible || blk.collected_or_defeated || blk.role != EntityRole2D::kDestructibleBlock)
                    {
                        continue;
                    }
                    const cd::math::Vec2f kh = blk.size * 0.5F;
                    if (!aabb_overlap_2d(ball.position, bh, blk.position, kh))
                    {
                        continue;
                    }
                    const float ox = (bh.x + kh.x) - std::fabs(ball.position.x - blk.position.x);
                    const float oy = (bh.y + kh.y) - std::fabs(ball.position.y - blk.position.y);
                    if (ox < oy)
                    {
                        ball.position.x += (ball.position.x >= blk.position.x) ? ox : -ox;
                        ball.runtime_velocity.x = -ball.runtime_velocity.x;
                    }
                    else
                    {
                        ball.position.y += (ball.position.y >= blk.position.y) ? oy : -oy;
                        ball.runtime_velocity.y = -ball.runtime_velocity.y;
                    }
                    shatter_destructible_block(blk);
                    break;
                }

                // Court Goal / Canyon Abyss checks
                if (arcade_ruleset_ == ArcadeRuleset2D::kPongCourt)
                {
                    if (ball.position.x > 13.2F)
                    {
                        ++hud_.gems_collected;
                        hud_.score += 500;
                        hud_.rally_count = 0;
                        particle_system_.spawn_burst(ball.position, 22U, ParticleEmitterPresets2D::sparkle_burst());
                        audio_cues_.play_cue_at(AudioCuePreset2D::kCoin, ball.position);
                        camera_.add_trauma(0.25F);
                        if (hud_.gems_collected >= 5)
                        {
                            hud_.victory = true;
                        }
                        else
                        {
                            ball.position         = ball.home_position;
                            ball.runtime_velocity = { -base_spd * 0.88F, base_spd * 0.42F };
                        }
                        break;
                    }
                    if (ball.position.x < -13.2F)
                    {
                        ++hud_.opponent_score;
                        hud_.rally_count = 0;
                        particle_system_.spawn_burst(ball.position, 18U, ParticleEmitterPresets2D::hit_impact());
                        audio_cues_.play_cue_at(AudioCuePreset2D::kHit, ball.position);
                        camera_.add_trauma(0.28F);
                        if (hud_.opponent_score >= 5)
                        {
                            hud_.game_over = true;
                        }
                        else
                        {
                            ball.position         = ball.home_position;
                            ball.runtime_velocity = { base_spd * 0.88F, -base_spd * 0.42F };
                        }
                        break;
                    }
                }
                else if (arcade_ruleset_ == ArcadeRuleset2D::kBridgeDemolition && ball.position.y < -5.6F)
                {
                    hud_.player_hp   = std::max(0, hud_.player_hp - 20);
                    hud_.rally_count = 0;
                    particle_system_.spawn_burst(ball.position, 16U, ParticleEmitterPresets2D::hit_impact());
                    audio_cues_.play_cue_at(AudioCuePreset2D::kHit, ball.position);
                    camera_.add_trauma(0.26F);
                    if (hud_.player_hp <= 0)
                    {
                        hud_.game_over = true;
                    }
                    else
                    {
                        const cd::math::Vec2f spawn_p = (horizontal_striker != nullptr)
                            ? (horizontal_striker->position + cd::math::Vec2f { 0.0F, horizontal_striker->size.y * 0.85F + 0.35F })
                            : ball.home_position;
                        ball.position         = spawn_p;
                        ball.runtime_velocity = { base_spd * 0.38F, base_spd * 0.92F };
                    }
                    break;
                }
            }
        }
    }

    // 5. Update Projectiles and Collide via SpatialHash2D
    std::vector<Entity2D> spawned_gems {};
    projectile_pool_.for_each_active([&](Projectile2D& proj, auto handle) {
        proj.position += proj.velocity * clamped_dt;
        proj.age_sec  += clamped_dt;
        if (proj.age_sec >= proj.lifetime_sec)
        {
            projectile_pool_.release(handle);
            return;
        }
        const auto hits = spatial_hash_.query_circle(proj.position, proj.radius);
        for (std::uint32_t hit_id : hits)
        {
            if (hit_id >= 0x80000000U)
            {
                particle_system_.spawn_burst(proj.position, 6U, ParticleEmitterPresets2D::hit_impact());
                projectile_pool_.release(handle);
                return;
            }
            Entity2D* ent = find_entity(hit_id);
            if (ent != nullptr && ent->role == EntityRole2D::kDestructibleBlock && !ent->collected_or_defeated)
            {
                shatter_destructible_block(*ent);
                projectile_pool_.release(handle);
                return;
            }
            if (ent != nullptr && ent->role == EntityRole2D::kPatrolEnemy && !ent->collected_or_defeated)
            {
                ent->collected_or_defeated = true;
                ++hud_.enemies_defeated;
                hud_.score += 150;
                particle_system_.spawn_burst(ent->position, 16U, ParticleEmitterPresets2D::enemy_defeat());
                audio_cues_.play_cue_at(AudioCuePreset2D::kExplosion, ent->position);

                if (arcade_ruleset_ == ArcadeRuleset2D::kStandard)
                {
                    Entity2D gem {};
                    gem.id         = next_entity_id_++;
                    gem.name       = "XP_Gem_Drop";
                    gem.role       = EntityRole2D::kCollectibleGem;
                    gem.position   = ent->position;
                    gem.size       = { 0.45F, 0.45F };
                    gem.color_rgba = { 0.25F, 1.0F, 0.65F, 1.0F };
                    gem.layer      = 100;
                    gem.y_sort     = true;
                    spawned_gems.push_back(std::move(gem));
                    ++hud_.total_gems;
                }

                projectile_pool_.release(handle);
                return;
            }
        }
    });
    for (auto& gem : spawned_gems)
    {
        spatial_hash_.insert(
            gem.id,
            Aabb2D {
                gem.position - gem.size * 0.5F,
                gem.position + gem.size * 0.5F
            });
        entities_.push_back(std::move(gem));
    }

    // 6. Swarm Spawner & Enemy Homing in Arena Mode
    if (swarm_spawner_.active && player_ent != nullptr && !hud_.game_over)
    {
        swarm_spawn_timer_ += clamped_dt;
        if (swarm_spawn_timer_ >= swarm_spawner_.spawn_interval_sec)
        {
            swarm_spawn_timer_ = 0.0F;
            int current_swarm = 0;
            for (const auto& e : entities_)
            {
                if (e.role == EntityRole2D::kPatrolEnemy && !e.collected_or_defeated)
                {
                    ++current_swarm;
                }
            }
            if (current_swarm < swarm_spawner_.max_swarm_count)
            {
                const float angle = static_cast<float>(hud_.sim_time_sec * 2.3F);
                const cd::math::Vec2f spawn_pos {
                    player_ent->position.x + std::cos(angle) * swarm_spawner_.spawn_radius,
                    player_ent->position.y + std::sin(angle) * swarm_spawner_.spawn_radius
                };
                Entity2D enemy {};
                enemy.id            = next_entity_id_++;
                enemy.name          = "Swarm_Drone_" + std::to_string(enemy.id);
                enemy.role          = EntityRole2D::kPatrolEnemy;
                enemy.position      = spawn_pos;
                enemy.home_position = spawn_pos;
                enemy.size          = { 0.9F, 0.9F };
                enemy.color_rgba    = { 1.0F, 0.35F, 0.25F, 1.0F };
                enemy.move_speed    = swarm_spawner_.enemy_speed;
                enemy.layer         = 100;
                enemy.y_sort        = true;
                spatial_hash_.insert(
                    enemy.id,
                    Aabb2D {
                        enemy.position - enemy.size * 0.5F,
                        enemy.position + enemy.size * 0.5F
                    });
                entities_.push_back(std::move(enemy));
            }
        }

        for (auto& ent : entities_)
        {
            if (ent.role == EntityRole2D::kPatrolEnemy && !ent.collected_or_defeated && ent.visible)
            {
                cd::math::Vec2f to_player = player_ent->position - ent.position;
                const float d = std::sqrt(to_player.x * to_player.x + to_player.y * to_player.y);
                if (d > 0.1F)
                {
                    to_player.x /= d;
                    to_player.y /= d;
                    ent.position += to_player * ent.move_speed * clamped_dt;
                    ent.flip_x = (to_player.x < 0.0F);
                }
            }
        }
    }

    // 7. XP Gem Magnet Attraction (Top-down & Arena)
    if (std::abs(gravity_y_) < 0.01F && player_ent != nullptr)
    {
        constexpr float kMagnetRadius = 3.5F;
        for (auto& gem : entities_)
        {
            if (gem.role == EntityRole2D::kCollectibleGem && !gem.collected_or_defeated && gem.visible)
            {
                cd::math::Vec2f diff = player_ent->position - gem.position;
                const float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
                if (dist < kMagnetRadius && dist > 0.05F)
                {
                    const float pull_speed = 7.5F * (1.0F - dist / kMagnetRadius) + 2.0F;
                    gem.position += (diff / dist) * pull_speed * clamped_dt;
                }
            }
        }
    }

    // 8. Update Moving Entity Bounds in SpatialHash2D
    for (const auto& ent : entities_)
    {
        if (ent.visible && !ent.collected_or_defeated &&
            (ent.role == EntityRole2D::kPatrolEnemy || ent.role == EntityRole2D::kPlayerHero))
        {
            spatial_hash_.update(
                ent.id,
                Aabb2D {
                    ent.position - ent.size * 0.5F,
                    ent.position + ent.size * 0.5F
                });
        }
    }

    // 9. Ambient World-Space Embers on Torches, Portals & Projectiles
    ambient_fx_timer_ += clamped_dt;
    if (ambient_fx_timer_ >= 0.09F)
    {
        ambient_fx_timer_ = 0.0F;
        ParticleEmitterConfig2D ember_cfg {};
        ember_cfg.shape           = ParticleEmitterShape2D::kCircle;
        ember_cfg.style           = ParticleRenderStyle2D::kRadialGlow;
        ember_cfg.shape_radius    = 0.18F;
        ember_cfg.lifetime_min    = 0.35F;
        ember_cfg.lifetime_max    = 0.75F;
        ember_cfg.velocity_min    = { -0.45F, 0.75F };
        ember_cfg.velocity_max    = { 0.45F, 2.10F };
        ember_cfg.gravity         = { 0.0F, 1.2F };
        ember_cfg.drag            = 0.6F;
        ember_cfg.start_size_min  = { 0.14F, 0.14F };
        ember_cfg.start_size_max  = { 0.24F, 0.24F };
        ember_cfg.end_size_min    = { 0.02F, 0.02F };
        ember_cfg.end_size_max    = { 0.05F, 0.05F };

        for (const auto& ent : entities_)
        {
            if (!ent.visible || ent.collected_or_defeated)
            {
                continue;
            }
            if (ent.role == EntityRole2D::kPointLight)
            {
                ember_cfg.start_color = { ent.color_rgba.x, ent.color_rgba.y, ent.color_rgba.z, 0.85F };
                ember_cfg.end_color   = { ent.color_rgba.x, ent.color_rgba.y * 0.5F, ent.color_rgba.z * 0.3F, 0.0F };
                (void)particle_system_.emit_particle(ent.position + cd::math::Vec2f { 0.0F, 0.18F }, ember_cfg);
            }
            else if (ent.role == EntityRole2D::kGoalPortal)
            {
                ember_cfg.start_color = { 0.35F, 0.95F, 1.0F, 0.85F };
                ember_cfg.end_color   = { 0.65F, 0.25F, 1.0F, 0.0F };
                (void)particle_system_.emit_particle(ent.position, ember_cfg);
            }
        }
    }

    // 10. Dynamic Y-Sorting
    if (y_sort_enabled_)
    {
        sort_entities_by_foot_y();
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
        else if (ent.role == EntityRole2D::kBouncingBall)
        {
            out_env.point_lights.push_back(cd::render2d::PointLight2D {
                .position     = ent.position,
                .color        = { ent.color_rgba.x, ent.color_rgba.y, ent.color_rgba.z },
                .intensity    = 1.55F,
                .radius       = 7.2F,
                .falloff_exp  = 1.5F,
                .z_height     = 1.3F,
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

    const bool use_atlas = has_bound_atlas();
    const float inv_w    = use_atlas ? (1.0F / static_cast<float>(std::max(1U, bound_atlas_.width()))) : 0.0F;
    const float inv_h    = use_atlas ? (1.0F / static_cast<float>(std::max(1U, bound_atlas_.height()))) : 0.0F;
    const auto atlas_uv  = [this, use_atlas, inv_w, inv_h](std::string_view name) noexcept -> cd::render2d::UvRect2D {
        if (use_atlas)
        {
            if (const auto* r = bound_atlas_.find_region(name); r != nullptr)
            {
                const float eps_u = 0.25F * inv_w;
                const float eps_v = 0.25F * inv_h;
                return cd::render2d::UvRect2D {
                    .u0 = r->uv.u0 + eps_u,
                    .v0 = r->uv.v0 + eps_v,
                    .u1 = r->uv.u1 - eps_u,
                    .v1 = r->uv.v1 - eps_v,
                };
            }
        }
        return cd::render2d::UvRect2D {};
    };

    std::vector<cd::render2d::Sprite2D> sprites;
    sprites.reserve(entities_.size());
    for (const auto& ent : entities_)
    {
        if (!ent.visible || ent.collected_or_defeated)
        {
            continue;
        }
        if (ent.role == EntityRole2D::kPointLight)
        {
            if (use_atlas &&
                (ent.name.find("Torch") != std::string::npos || ent.name.find("Lantern") != std::string::npos))
            {
                cd::render2d::Sprite2D brazier {};
                brazier.position     = ent.position;
                brazier.size         = { 0.82F, 0.92F };
                brazier.tint         = { 255U, 255U, 255U, 255U };
                brazier.texture_slot = bound_atlas_slot_;
                brazier.uv           = atlas_uv("torch_brazier");
                brazier.layer        = 96U;
                brazier.y_sort       = y_sort_enabled_;
                brazier.lit          = false;
                sprites.push_back(brazier);
            }
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
        sp.y_sort           = ent.y_sort || y_sort_enabled_;
        sp.flip_x           = ent.flip_x;
        sp.lit              = (ent.role != EntityRole2D::kCollectibleGem && ent.role != EntityRole2D::kGoalPortal &&
                               ent.role != EntityRole2D::kBouncingBall && ent.role != EntityRole2D::kKinematicPaddle &&
                               ent.role != EntityRole2D::kAiOpponentPaddle);
        sp.style            = role_to_visual_style(ent.role);
        sp.anim_frame_index = ent.anim_frame;

        if (use_atlas)
        {
            const std::uint8_t alpha = static_cast<std::uint8_t>(
                std::clamp(static_cast<int>(std::lround(ent.color_rgba.w * 255.0F)), 0, 255));
            sp.texture_slot = bound_atlas_slot_;
            sp.tint         = { 255U, 255U, 255U, alpha };

            switch (ent.role)
            {
                case EntityRole2D::kPlayerHero:
                    if (std::abs(ent.runtime_velocity.y) > 1.5F && std::abs(gravity_y_) > 0.1F)
                    {
                        sp.uv = atlas_uv("hero_jump");
                    }
                    else if ((ent.anim_frame % 4U) == 1U)
                    {
                        sp.uv = atlas_uv("hero_walk_0");
                    }
                    else if ((ent.anim_frame % 4U) == 3U)
                    {
                        sp.uv = atlas_uv("hero_walk_1");
                    }
                    else
                    {
                        sp.uv = atlas_uv("hero_idle");
                    }
                    break;
                case EntityRole2D::kCollectibleGem:
                    if (ent.name.find("Chest") != std::string::npos)
                    {
                        sp.uv = atlas_uv("chest_gold");
                    }
                    else if (ent.name.find("Key") != std::string::npos)
                    {
                        sp.uv = atlas_uv("key_golden");
                    }
                    else if (ent.color_rgba.x > 0.80F && ent.color_rgba.y < 0.50F)
                    {
                        sp.uv = atlas_uv("gem_ruby");
                    }
                    else if (ent.color_rgba.x > 0.85F && ent.color_rgba.y > 0.75F)
                    {
                        sp.uv = atlas_uv("coin_gold");
                    }
                    else
                    {
                        sp.uv = atlas_uv("gem_emerald");
                    }
                    break;
                case EntityRole2D::kPatrolEnemy:
                    if (ent.name.find("Skeleton") != std::string::npos || ent.name.find("Guard") != std::string::npos)
                    {
                        sp.uv = atlas_uv("skeleton_guard");
                    }
                    else
                    {
                        sp.uv = atlas_uv("enemy_drone");
                    }
                    break;
                case EntityRole2D::kDynamicCrate:
                    sp.uv = atlas_uv("crate_wood");
                    break;
                case EntityRole2D::kJumpPad:
                    sp.uv  = atlas_uv("jumppad_coil");
                    sp.lit = false;
                    break;
                case EntityRole2D::kMovingPlatform:
                    sp.uv = atlas_uv("platform_hover");
                    break;
                case EntityRole2D::kGoalPortal:
                    sp.uv  = atlas_uv("portal_swirl");
                    sp.lit = false;
                    break;
                case EntityRole2D::kShadowOccluder:
                    sp.uv = atlas_uv("stone_pillar");
                    break;
                case EntityRole2D::kKinematicPaddle:
                    sp.uv = (ent.size.y >= ent.size.x) ? atlas_uv("paddle_cyan") : atlas_uv("paddle_striker");
                    break;
                case EntityRole2D::kAiOpponentPaddle:
                    sp.uv = atlas_uv("paddle_magenta");
                    break;
                case EntityRole2D::kBouncingBall:
                    sp.uv = atlas_uv("ball_energy");
                    break;
                case EntityRole2D::kDestructibleBlock:
                    sp.uv = (ent.size.y > ent.size.x * 1.10F) ? atlas_uv("bridge_pillar") : atlas_uv("bridge_plank");
                    break;
                case EntityRole2D::kPropSprite:
                case EntityRole2D::kPointLight:
                    if (ent.name.find("Tree") != std::string::npos || ent.name.find("Pine") != std::string::npos)
                    {
                        sp.uv = atlas_uv("tree_pine");
                    }
                    else if (ent.name.find("Mage") != std::string::npos)
                    {
                        sp.uv = atlas_uv("mage_companion");
                    }
                    else
                    {
                        sp.uv = atlas_uv("crate_wood");
                    }
                    break;
            }
        }
        sprites.push_back(sp);
    }

    renderer_.render_scene(
        out_canvas, viewport, camera_, tilemap_, light_env, sprites, baked_colliders_, debug_opts, parallax_layers_);

    // Pong Court Center Dashed Divider Line
    if (arcade_ruleset_ == ArcadeRuleset2D::kPongCourt)
    {
        const float px_scale         = camera_.effective_scale_px_per_unit();
        const std::uint64_t line_key = cd::render2d::make_sort_key_2d(45U, 0, 0.0F, false, 0U);
        for (int seg = -6; seg <= 6; ++seg)
        {
            const cd::math::Vec2f sp_pos = camera_.world_to_screen({ 0.0F, static_cast<float>(seg) * 0.95F }, viewport);
            const float w_px = std::max(3.0F, 0.14F * px_scale);
            const float h_px = std::max(8.0F, 0.48F * px_scale);
            out_canvas.add_solid_rect(
                sp_pos.x - w_px * 0.5F,
                sp_pos.y - h_px * 0.5F,
                w_px,
                h_px,
                { 86U, 198U, 255U, 145U },
                line_key);
        }
    }

    // Subtle additive-feel radial bloom halos behind glowing entities when using pixel-art atlas
    if (use_atlas)
    {
        const float px_scale         = camera_.effective_scale_px_per_unit();
        const std::uint64_t halo_key = cd::render2d::make_sort_key_2d(92U, 0, 0.0F, false, 0U);
        for (const auto& ent : entities_)
        {
            if (!ent.visible || ent.collected_or_defeated)
            {
                continue;
            }
            if (ent.role == EntityRole2D::kGoalPortal || ent.role == EntityRole2D::kPointLight ||
                ent.role == EntityRole2D::kJumpPad || ent.role == EntityRole2D::kBouncingBall)
            {
                const cd::math::Vec2f sp_pos = camera_.world_to_screen(ent.position, viewport);
                const float radius_px = (ent.role == EntityRole2D::kGoalPortal) ? (1.35F * px_scale)
                                      : ((ent.role == EntityRole2D::kPointLight) ? (1.65F * px_scale) : (0.95F * px_scale));
                const float alpha     = (ent.role == EntityRole2D::kPointLight) ? 0.22F
                                      : ((ent.role == EntityRole2D::kBouncingBall) ? 0.42F : 0.30F);
                out_canvas.add_radial_glow(
                    sp_pos.x,
                    sp_pos.y,
                    radius_px,
                    cd::render2d::ColorRgba8::from_floats(ent.color_rgba.x, ent.color_rgba.y, ent.color_rgba.z, alpha),
                    16,
                    halo_key);
            }
        }
    }

    const std::uint64_t proj_key = cd::render2d::make_sort_key_2d(140U, 0, 0.0F, false, 0U);
    const cd::render2d::UvRect2D proj_uv = atlas_uv("projectile_plasma");
    projectile_pool_.for_each_active([&](const Projectile2D& proj, auto) {
        const cd::math::Vec2f sp = camera_.world_to_screen(proj.position, viewport);
        const float r_px = std::max(3.0F, proj.radius * camera_.effective_scale_px_per_unit());
        out_canvas.add_radial_glow(
            sp.x, sp.y, r_px * 2.4F,
            cd::render2d::ColorRgba8::from_floats(proj.color_rgba.x, proj.color_rgba.y, proj.color_rgba.z, 0.45F),
            12, proj_key);
        if (use_atlas)
        {
            const float d_px = r_px * 2.8F;
            out_canvas.add_textured_rect(
                sp.x - d_px * 0.5F, sp.y - d_px * 0.5F, d_px, d_px,
                bound_atlas_slot_, proj_uv, { 255U, 255U, 255U, 255U }, proj_key + 1ULL);
        }
        else
        {
            out_canvas.add_solid_rect(
                sp.x - r_px, sp.y - r_px, r_px * 2.0F, r_px * 2.0F,
                cd::render2d::ColorRgba8::from_floats(proj.color_rgba.x, proj.color_rgba.y, proj.color_rgba.z, 1.0F),
                proj_key + 1ULL);
        }
    });

    particle_system_.emit_to_canvas(out_canvas, camera_, viewport, 150U, 0);
    out_canvas.sort_commands();
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
    root["camera_zoom"]    = static_cast<double>(camera_.zoom());
    root["y_sort_enabled"] = y_sort_enabled_;
    root["arcade_ruleset"] = static_cast<int>(arcade_ruleset_);

    j::Object spawner_obj;
    spawner_obj["active"]             = swarm_spawner_.active;
    spawner_obj["spawn_interval_sec"] = static_cast<double>(swarm_spawner_.spawn_interval_sec);
    spawner_obj["spawn_radius"]       = static_cast<double>(swarm_spawner_.spawn_radius);
    spawner_obj["enemy_speed"]        = static_cast<double>(swarm_spawner_.enemy_speed);
    spawner_obj["max_swarm_count"]    = swarm_spawner_.max_swarm_count;
    root["swarm_spawner"]             = std::move(spawner_obj);

    j::Array parallax_arr;
    for (const auto& layer : parallax_layers_)
    {
        j::Object lo;
        lo["name"]                 = layer.name;
        lo["scroll_factor"]        = j::Array {
            static_cast<double>(layer.scroll_factor.x),
            static_cast<double>(layer.scroll_factor.y),
        };
        lo["auto_scroll_velocity"] = j::Array {
            static_cast<double>(layer.auto_scroll_velocity.x),
            static_cast<double>(layer.auto_scroll_velocity.y),
        };
        lo["procedural_style"]     = static_cast<int>(layer.procedural_style);
        lo["visible"]              = layer.visible;
        lo["z_order"]              = static_cast<int>(layer.z_order);
        lo["tint"]                 = j::Array {
            static_cast<int>(layer.tint.r),
            static_cast<int>(layer.tint.g),
            static_cast<int>(layer.tint.b),
            static_cast<int>(layer.tint.a),
        };
        lo["layer"]                = static_cast<int>(layer.layer);
        lo["repeat_x"]             = layer.repeat_x;
        lo["repeat_y"]             = layer.repeat_y;
        lo["tile_size"]            = j::Array {
            static_cast<double>(layer.tile_size.x),
            static_cast<double>(layer.tile_size.y),
        };
        lo["texture_slot"]         = static_cast<int>(layer.texture_slot);
        parallax_arr.push_back(std::move(lo));
    }
    root["parallax_layers"] = std::move(parallax_arr);

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

    if (auto ys = root.at("y_sort_enabled"); ys.has_value() && (*ys)->is_bool())
    {
        y_sort_enabled_ = (*ys)->as_bool();
    }
    if (auto ar = root.at("arcade_ruleset"); ar.has_value() && (*ar)->is_number())
    {
        arcade_ruleset_ = static_cast<ArcadeRuleset2D>(std::clamp(static_cast<int>((*ar)->as_number()), 0, 2));
    }

    if (auto sp = root.at("swarm_spawner"); sp.has_value() && (*sp)->is_object())
    {
        const auto* so = *sp;
        if (auto v = so->at("active"); v.has_value() && (*v)->is_bool())
        {
            swarm_spawner_.active = (*v)->as_bool();
        }
        if (auto v = so->at("spawn_interval_sec"); v.has_value() && (*v)->is_number())
        {
            swarm_spawner_.spawn_interval_sec = static_cast<float>((*v)->as_number());
        }
        if (auto v = so->at("spawn_radius"); v.has_value() && (*v)->is_number())
        {
            swarm_spawner_.spawn_radius = static_cast<float>((*v)->as_number());
        }
        if (auto v = so->at("enemy_speed"); v.has_value() && (*v)->is_number())
        {
            swarm_spawner_.enemy_speed = static_cast<float>((*v)->as_number());
        }
        if (auto v = so->at("max_swarm_count"); v.has_value() && (*v)->is_number())
        {
            swarm_spawner_.max_swarm_count = static_cast<int>((*v)->as_number());
        }
    }

    if (auto pl = root.at("parallax_layers"); pl.has_value() && (*pl)->is_array())
    {
        parallax_layers_.clear();
        for (const auto& lv : (*pl)->as_array())
        {
            if (!lv.is_object())
            {
                continue;
            }
            cd::render2d::ParallaxLayer2D l {};
            if (auto v = lv.at("name"); v.has_value() && (*v)->is_string())
            {
                l.name = (*v)->as_string();
            }
            if (auto v = lv.at("scroll_factor"); v.has_value() && (*v)->is_array() && (*v)->as_array().size() >= 2U)
            {
                l.scroll_factor.x = static_cast<float>((*v)->as_array().at(0).as_number());
                l.scroll_factor.y = static_cast<float>((*v)->as_array().at(1).as_number());
            }
            if (auto v = lv.at("auto_scroll_velocity"); v.has_value() && (*v)->is_array() && (*v)->as_array().size() >= 2U)
            {
                l.auto_scroll_velocity.x = static_cast<float>((*v)->as_array().at(0).as_number());
                l.auto_scroll_velocity.y = static_cast<float>((*v)->as_array().at(1).as_number());
            }
            if (auto v = lv.at("procedural_style"); v.has_value())
            {
                if ((*v)->is_number())
                {
                    l.procedural_style = static_cast<cd::render2d::ParallaxProceduralStyle>(
                        std::clamp(static_cast<int>((*v)->as_number()), 0, 4));
                }
                else if ((*v)->is_string())
                {
                    const auto& s = (*v)->as_string();
                    if (s == "CitySkyline" || s == "kCitySkyline")
                    {
                        l.procedural_style = cd::render2d::ParallaxProceduralStyle::kCitySkyline;
                    }
                    else if (s == "MountainPinnacle" || s == "kMountainPinnacle")
                    {
                        l.procedural_style = cd::render2d::ParallaxProceduralStyle::kMountainPinnacle;
                    }
                    else if (s == "ForestSilhouettes" || s == "kForestSilhouettes")
                    {
                        l.procedural_style = cd::render2d::ParallaxProceduralStyle::kForestSilhouettes;
                    }
                    else if (s == "CloudClusters" || s == "kCloudClusters")
                    {
                        l.procedural_style = cd::render2d::ParallaxProceduralStyle::kCloudClusters;
                    }
                    else
                    {
                        l.procedural_style = cd::render2d::ParallaxProceduralStyle::kNone;
                    }
                }
            }
            if (auto v = lv.at("visible"); v.has_value() && (*v)->is_bool())
            {
                l.visible = (*v)->as_bool();
            }
            if (auto v = lv.at("z_order"); v.has_value() && (*v)->is_number())
            {
                l.z_order = static_cast<std::int16_t>((*v)->as_number());
                l.z_index = l.z_order;
            }
            if (auto v = lv.at("tint"); v.has_value() && (*v)->is_array() && (*v)->as_array().size() >= 4U)
            {
                const auto& ta = (*v)->as_array();
                const double r = ta.at(0).as_number();
                const double g = ta.at(1).as_number();
                const double b = ta.at(2).as_number();
                const double a = ta.at(3).as_number();
                if (r <= 1.0 && g <= 1.0 && b <= 1.0 && a <= 1.0 && (r > 0.0 || g > 0.0 || b > 0.0 || a > 0.0))
                {
                    l.tint = cd::render2d::ColorRgba8::from_floats(
                        static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a));
                }
                else
                {
                    l.tint.r = static_cast<std::uint8_t>(std::clamp(static_cast<int>(r), 0, 255));
                    l.tint.g = static_cast<std::uint8_t>(std::clamp(static_cast<int>(g), 0, 255));
                    l.tint.b = static_cast<std::uint8_t>(std::clamp(static_cast<int>(b), 0, 255));
                    l.tint.a = static_cast<std::uint8_t>(std::clamp(static_cast<int>(a), 0, 255));
                }
            }
            if (auto v = lv.at("layer"); v.has_value() && (*v)->is_number())
            {
                l.layer = static_cast<std::uint8_t>(std::clamp(static_cast<int>((*v)->as_number()), 0, 255));
            }
            if (auto v = lv.at("repeat_x"); v.has_value() && (*v)->is_bool())
            {
                l.repeat_x = (*v)->as_bool();
            }
            if (auto v = lv.at("repeat_y"); v.has_value() && (*v)->is_bool())
            {
                l.repeat_y = (*v)->as_bool();
            }
            if (auto v = lv.at("tile_size"); v.has_value() && (*v)->is_array() && (*v)->as_array().size() >= 2U)
            {
                l.tile_size.x = static_cast<float>((*v)->as_array().at(0).as_number());
                l.tile_size.y = static_cast<float>((*v)->as_array().at(1).as_number());
            }
            if (auto v = lv.at("texture_slot"); v.has_value() && (*v)->is_number())
            {
                l.texture_slot = static_cast<std::uint32_t>((*v)->as_number());
            }
            parallax_layers_.push_back(std::move(l));
        }
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
                e.role = static_cast<EntityRole2D>(std::clamp(static_cast<int>((*v)->as_number()), 0, 13));
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

    if (tilemap_.find_palette(5U) == nullptr)
    {
        const bool is_arena = (stage_name_.find("Arena") != std::string::npos ||
                               stage_name_.find("Pong") != std::string::npos ||
                               arcade_ruleset_ == ArcadeRuleset2D::kPongCourt);
        tilemap_.register_palette_entry(cd::render2d::TilePaletteEntry2D {
            .palette_id        = 5U,
            .name              = is_arena ? "Cyber Arena Floor Deck" : "Dungeon Flagstone Floor",
            .fill_color        = is_arena ? cd::render2d::ColorRgba8 { 26U, 34U, 50U, 255U }
                                          : cd::render2d::ColorRgba8 { 42U, 48U, 64U, 255U },
            .top_cap_color     = is_arena ? cd::render2d::ColorRgba8 { 38U, 50U, 72U, 255U }
                                          : cd::render2d::ColorRgba8 { 56U, 64U, 84U, 255U },
            .edge_border_color = is_arena ? cd::render2d::ColorRgba8 { 18U, 24U, 38U, 255U }
                                          : cd::render2d::ColorRgba8 { 28U, 32U, 44U, 255U },
            .solid             = false,
            .one_way           = false,
            .hazard            = false,
            .emissive          = false,
        });
    }
    sync_bound_atlas_palette();

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
