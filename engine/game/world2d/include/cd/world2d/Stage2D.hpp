// =============================================================================
// CHROMODYNAMIC: cd/world2d/Stage2D.hpp
// FAZ 35 (ADR-20261111): Unified Commercial 2D Stage Container Bridging
// `cd::render2d`, `cd::physics::v2d` (Box2D v3 Soft-Step + CharacterController2D),
// Sandboxed Lua 5.4 Entity Behaviors (`cd::script::Engine`), `.cd2d.json` & `.cdpak`.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/v2d/CharacterController2D.hpp>
#include <cd/physics/v2d/PhysicsWorld2D.hpp>
#include <cd/render2d/ParallaxLayer2D.hpp>
#include <cd/render2d/Renderer2D.hpp>
#include <cd/render2d/TextureAtlas2D.hpp>
#include <cd/schema/Schema.hpp>
#include <cd/script/Engine.hpp>
#include <cd/world2d/AudioCue2D.hpp>
#include <cd/world2d/ObjectPool2D.hpp>
#include <cd/world2d/ParticleSystem2D.hpp>
#include <cd/world2d/SpatialHash2D.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::world2d
{

enum class EntityRole2D : std::uint8_t
{
    kPropSprite        = 0,
    kPlayerHero        = 1,
    kDynamicCrate      = 2,
    kCollectibleGem    = 3,
    kJumpPad           = 4,
    kPatrolEnemy       = 5,
    kMovingPlatform    = 6,
    kGoalPortal        = 7,
    kPointLight        = 8,
    kShadowOccluder    = 9,
    kKinematicPaddle   = 10,
    kAiOpponentPaddle  = 11,
    kBouncingBall      = 12,
    kDestructibleBlock = 13,
};

enum class ArcadeRuleset2D : std::uint8_t
{
    kStandard         = 0,
    kPongCourt        = 1,
    kBridgeDemolition = 2,
};

[[nodiscard]] constexpr std::string_view entity_role_name_2d(EntityRole2D role) noexcept
{
    switch (role)
    {
        case EntityRole2D::kPropSprite:        return "PropSprite";
        case EntityRole2D::kPlayerHero:        return "PlayerHero";
        case EntityRole2D::kDynamicCrate:      return "DynamicCrate";
        case EntityRole2D::kCollectibleGem:    return "CollectibleGem";
        case EntityRole2D::kJumpPad:           return "JumpPad";
        case EntityRole2D::kPatrolEnemy:       return "PatrolEnemy";
        case EntityRole2D::kMovingPlatform:    return "MovingPlatform";
        case EntityRole2D::kGoalPortal:        return "GoalPortal";
        case EntityRole2D::kPointLight:        return "PointLight2D";
        case EntityRole2D::kShadowOccluder:    return "ShadowOccluder2D";
        case EntityRole2D::kKinematicPaddle:   return "KinematicPaddle";
        case EntityRole2D::kAiOpponentPaddle:  return "AiOpponentPaddle";
        case EntityRole2D::kBouncingBall:      return "BouncingBall";
        case EntityRole2D::kDestructibleBlock: return "DestructibleBlock";
    }
    return "PropSprite";
}

struct Entity2D
{
    std::uint32_t   id { 1U };
    std::string     name { "Sprite2D" };
    EntityRole2D    role { EntityRole2D::kPropSprite };
    bool            visible { true };
    cd::math::Vec2f position { 0.0F, 2.0F };
    cd::math::Vec2f size { 1.0F, 1.0F };
    float           rotation_deg { 0.0F };
    cd::math::Vec4f color_rgba { 0.90F, 0.92F, 0.98F, 1.0F };
    std::int32_t    layer { 100 };
    std::int32_t    z_index { 0 };
    bool            y_sort { true };
    bool            flip_x { false };

    // Gameplay & Physics parameters
    float move_speed { 7.2F };
    float jump_speed { 13.8F };
    float patrol_span { 3.0F };
    float bounce_impulse { 17.5F };

    // Dynamic 2D Light parameters
    float light_intensity { 1.65F };
    float light_radius { 8.5F };
    bool  cast_shadows { true };

    // Sandboxed Lua 5.4 Behavior (`function on_tick(id, x, y, vx, vy, t, dt) return x, y, vx, vy end`)
    std::string lua_script {};
    std::string lua_last_error {};

    // Runtime-only state (rebuilt on `enter_play`, reset on `exit_play`)
    cd::math::Vec2f home_position { 0.0F, 2.0F };
    cd::math::Vec2f runtime_velocity { 0.0F, 0.0F };
    std::uint32_t   physics_body_id { 0U };
    bool            collected_or_defeated { false };
    float           anim_timer { 0.0F };
    std::uint32_t   anim_frame { 0U };
    float           patrol_dir { 1.0F };
};

struct PlayerInput2D
{
    float move_x { 0.0F };
    float move_y { 0.0F };
    bool  jump_pressed { false };
    bool  jump_held { false };
    bool  interact_pressed { false };
    bool  fire_pressed { false };
    bool  fire_held { false };
    float aim_angle_rad { 0.0F };
};

struct Projectile2D
{
    cd::math::Vec2f position { 0.0F, 0.0F };
    cd::math::Vec2f velocity { 0.0F, 0.0F };
    float           radius { 0.18F };
    float           lifetime_sec { 1.5F };
    float           age_sec { 0.0F };
    int             damage { 35 };
    bool            from_player { true };
    cd::math::Vec4f color_rgba { 0.25F, 0.95F, 1.0F, 1.0F };
};

struct SwarmSpawnerConfig2D
{
    float spawn_interval_sec { 1.8F };
    float spawn_radius { 16.0F };
    float enemy_speed { 3.2F };
    int   max_swarm_count { 40 };
    bool  active { false };
};

struct GameplayHudState2D
{
    int   player_hp { 100 };
    int   max_hp { 100 };
    int   score { 0 };
    int   opponent_score { 0 };
    int   rally_count { 0 };
    int   gems_collected { 0 };
    int   total_gems { 0 };
    int   enemies_defeated { 0 };
    bool  victory { false };
    bool  game_over { false };
    float sim_time_sec { 0.0F };
    std::uint32_t active_lua_vms { 0U };
    std::uint32_t lua_error_count { 0U };
};

/// Registers `Entity2D` and `CommandBus2D` schemas into `SchemaRegistry` for Auto-Inspector & AI `--dump-schema`.
[[nodiscard]] cd::schema::TypeSchema make_entity2d_type_schema();
void register_world2d_schemas(cd::schema::SchemaRegistry& registry);

class Stage2D
{
public:
    Stage2D();

    [[nodiscard]] std::string_view name() const noexcept { return stage_name_; }
    void set_name(std::string name) { stage_name_ = std::move(name); }

    [[nodiscard]] cd::render2d::Tilemap2D& tilemap() noexcept { return tilemap_; }
    [[nodiscard]] const cd::render2d::Tilemap2D& tilemap() const noexcept { return tilemap_; }

    [[nodiscard]] cd::render2d::Camera2D& camera() noexcept { return camera_; }
    [[nodiscard]] const cd::render2d::Camera2D& camera() const noexcept { return camera_; }

    [[nodiscard]] cd::math::Vec3f ambient_color() const noexcept { return ambient_color_; }
    void set_ambient_color(cd::math::Vec3f c) noexcept { ambient_color_ = c; }

    [[nodiscard]] float ambient_intensity() const noexcept { return ambient_intensity_; }
    void set_ambient_intensity(float v) noexcept { ambient_intensity_ = v; }

    [[nodiscard]] float gravity_y() const noexcept { return gravity_y_; }
    void set_gravity_y(float g) noexcept { gravity_y_ = g; }

    [[nodiscard]] ArcadeRuleset2D arcade_ruleset() const noexcept { return arcade_ruleset_; }
    void set_arcade_ruleset(ArcadeRuleset2D r) noexcept { arcade_ruleset_ = r; }

    [[nodiscard]] ParticleSystem2D& particle_system() noexcept { return particle_system_; }
    [[nodiscard]] const ParticleSystem2D& particle_system() const noexcept { return particle_system_; }
    [[nodiscard]] SpatialHash2D& spatial_hash() noexcept { return spatial_hash_; }
    [[nodiscard]] const SpatialHash2D& spatial_hash() const noexcept { return spatial_hash_; }
    [[nodiscard]] AudioCueSystem2D& audio_cues() noexcept { return audio_cues_; }
    [[nodiscard]] const AudioCueSystem2D& audio_cues() const noexcept { return audio_cues_; }

    [[nodiscard]] bool is_y_sort_enabled() const noexcept { return y_sort_enabled_; }
    void set_y_sort_enabled(bool enabled) noexcept { y_sort_enabled_ = enabled; }
    void sort_entities_by_foot_y();
    [[nodiscard]] static bool compare_entities_by_foot_y(const Entity2D& a, const Entity2D& b) noexcept;

    [[nodiscard]] std::vector<cd::render2d::ParallaxLayer2D>& parallax_layers() noexcept { return parallax_layers_; }
    [[nodiscard]] std::span<const cd::render2d::ParallaxLayer2D> parallax_layers() const noexcept { return parallax_layers_; }

    [[nodiscard]] ObjectPool2D<Projectile2D>& projectile_pool() noexcept { return projectile_pool_; }
    [[nodiscard]] const ObjectPool2D<Projectile2D>& projectile_pool() const noexcept { return projectile_pool_; }

    [[nodiscard]] SwarmSpawnerConfig2D& swarm_spawner() noexcept { return swarm_spawner_; }
    [[nodiscard]] const SwarmSpawnerConfig2D& swarm_spawner() const noexcept { return swarm_spawner_; }

    // ---- Entity Authoring ---------------------------------------------------
    std::uint32_t add_entity(Entity2D entity);
    bool remove_entity(std::uint32_t entity_id);
    [[nodiscard]] Entity2D* find_entity(std::uint32_t entity_id) noexcept;
    [[nodiscard]] const Entity2D* find_entity(std::uint32_t entity_id) const noexcept;
    [[nodiscard]] std::span<Entity2D> entities() noexcept { return entities_; }
    [[nodiscard]] std::span<const Entity2D> entities() const noexcept { return entities_; }

    void clear();

    /// Populates a rich playable commercial 2D showcase ("Neon Cavern Platformer 2D"):
    /// Wang auto-tiled terrain, one-way steel decks, hazard spikes, dynamic 2D point lights
    /// & soft shadow occluders, playable `CharacterController2D` hero, gems, jump pad,
    /// patrol drone, live Lua 5.4 moving platform, and goal portal.
    void build_starter_illuminated_platformer();

    // ---- Play-In-Editor (PIE) & Runtime Simulation --------------------------
    [[nodiscard]] bool is_playing() const noexcept { return playing_; }
    [[nodiscard]] const GameplayHudState2D& hud() const noexcept { return hud_; }
    [[nodiscard]] std::span<const cd::render2d::CollisionRect2D> baked_tile_colliders() const noexcept
    {
        return baked_colliders_;
    }

    void rebuild_baked_colliders();
    bool compile_entity_lua_script(std::uint32_t entity_id);

    void enter_play();
    void step_play(float dt, const PlayerInput2D& input);
    void exit_play();

    // ---- Rendering Bridge & GPU Pixel-Art Atlas Binding ---------------------
    void bind_starter_atlas(std::uint32_t texture_slot, std::uint32_t tile_px = 32U);
    void unbind_starter_atlas() noexcept;
    [[nodiscard]] bool has_bound_atlas() const noexcept
    {
        return bound_atlas_slot_ != 0xFFFFFFFFU && bound_atlas_.is_valid();
    }
    void sync_bound_atlas_palette();

    void render(
        cd::render2d::Canvas2D&                  out_canvas,
        const cd::render2d::ViewportRect2D&      viewport,
        const cd::render2d::Render2DDebugOptions& debug_opts = {}) const;

    // ---- Serialization (`.cd2d.json` & `.cdpak`) ----------------------------
    [[nodiscard]] std::string save_to_json_string(bool pretty = true) const;
    [[nodiscard]] cd::core::Result<void> load_from_json_string(std::string_view json_text);

    [[nodiscard]] cd::core::Result<void> save_to_file(std::string_view path) const;
    [[nodiscard]] cd::core::Result<void> load_from_file(std::string_view path);

    [[nodiscard]] std::vector<std::byte> build_standalone_pak_bytes() const;
    [[nodiscard]] cd::core::Result<void> load_from_pak_bytes(std::span<const std::byte> pak_bytes);

private:
    void build_lighting_environment(cd::render2d::LightingEnvironment2D& out_env) const;

    std::string                                    stage_name_ { "Neon_Cavern_2D" };
    cd::render2d::Tilemap2D                        tilemap_ {};
    cd::render2d::Camera2D                         camera_ {};
    cd::render2d::Renderer2D                       renderer_ {};
    cd::math::Vec3f                                ambient_color_ { 0.25F, 0.30F, 0.42F };
    float                                          ambient_intensity_ { 1.0F };
    float                                          gravity_y_ { -28.0F };
    std::uint32_t                                  next_entity_id_ { 1U };
    std::vector<Entity2D>                          entities_ {};
    std::vector<cd::render2d::CollisionRect2D>     baked_colliders_ {};

    // Runtime PIE state
    bool                                           playing_ { false };
    std::string                                    pre_play_snapshot_json_ {};
    cd::math::Vec2f                                pre_play_camera_pos_ { 8.0F, 4.5F };
    float                                          pre_play_camera_zoom_ { 1.0F };
    cd::physics::v2d::PhysicsWorld2D               physics_world_ {};
    cd::physics::v2d::CharacterController2D        char_controller_ {};
    std::unordered_map<std::uint32_t, std::unique_ptr<cd::script::Engine>> lua_vms_ {};
    GameplayHudState2D                             hud_ {};
    mutable ParticleSystem2D                       particle_system_ { 1024U };
    mutable SpatialHash2D                          spatial_hash_ { 2.0F };
    mutable AudioCueSystem2D                       audio_cues_ { nullptr };
    bool                                           y_sort_enabled_ { false };
    std::vector<cd::render2d::ParallaxLayer2D>     parallax_layers_ {};
    mutable ObjectPool2D<Projectile2D>             projectile_pool_ { 512U, false };
    SwarmSpawnerConfig2D                           swarm_spawner_ {};
    float                                          swarm_spawn_timer_ { 0.0F };
    float                                          fire_cooldown_ { 0.0F };
    float                                          ambient_fx_timer_ { 0.0F };
    ArcadeRuleset2D                                arcade_ruleset_ { ArcadeRuleset2D::kStandard };
    std::uint32_t                                  bound_atlas_slot_ { 0xFFFFFFFFU };
    cd::render2d::TextureAtlas2D                   bound_atlas_ {};
};

}  // namespace cd::world2d
