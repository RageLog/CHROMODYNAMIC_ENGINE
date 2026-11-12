// =============================================================================
// CHROMODYNAMIC: cd/world2d/GenreTemplates2D.cpp
// Milestone 4: Commercial Switchable 2D Genre Templates:
// Action Platformer, Top-Down Y-Sorted RPG Dungeon, and Twin-Stick Arena.
// =============================================================================
#include <cd/world2d/GenreTemplates2D.hpp>

#include <cd/render2d/Camera2D.hpp>
#include <cd/render2d/ParallaxLayer2D.hpp>

#include <array>
#include <string>

namespace cd::world2d
{

void GenreTemplates2D::apply_genre_template(Stage2D& stage, GenreTemplateKind2D kind)
{
    switch (kind)
    {
        case GenreTemplateKind2D::kActionPlatformer:
            build_action_platformer(stage);
            break;
        case GenreTemplateKind2D::kTopDownRpgDungeon:
            build_top_down_rpg(stage);
            break;
        case GenreTemplateKind2D::kTwinStickArena:
            build_twin_stick_arena(stage);
            break;
        case GenreTemplateKind2D::kEmpty2D:
            build_empty_stage(stage);
            break;
        case GenreTemplateKind2D::kPongArcade:
            build_pong_arcade(stage);
            break;
        case GenreTemplateKind2D::kBreakTheBridges:
            build_break_the_bridges(stage);
            break;
    }
}

void GenreTemplates2D::build_empty_stage(Stage2D& stage)
{
    stage.clear();
    stage.set_name("Empty_2D_Project");
    stage.set_ambient_color({ 0.30F, 0.34F, 0.44F });
    stage.set_ambient_intensity(1.0F);
    stage.set_gravity_y(-28.0F);
    stage.set_y_sort_enabled(false);
    stage.set_arcade_ruleset(ArcadeRuleset2D::kStandard);

    cd::render2d::Camera2DConfig cam_cfg {};
    cam_cfg.pixels_per_unit = 42.0F;
    cam_cfg.zoom            = 1.0F;
    stage.camera().set_config(cam_cfg);
    stage.camera().set_position({ 0.0F, 2.0F });
    stage.sync_bound_atlas_palette();
}

void GenreTemplates2D::build_action_platformer(Stage2D& stage)
{
    stage.build_starter_illuminated_platformer();
    stage.set_y_sort_enabled(false);

    // Multi-tier parallax skyline: distant mountains, forest silhouettes, cyber skyline
    stage.parallax_layers().clear();

    cd::render2d::ParallaxLayer2D l0 {};
    l0.name                 = "Distant_Mountains";
    l0.scroll_factor        = { 0.15F, 0.05F };
    l0.auto_scroll_velocity = { 0.10F, 0.0F };
    l0.procedural_style     = cd::render2d::ParallaxProceduralStyle::kMountainPinnacle;
    l0.tint                 = cd::render2d::ColorRgba8 { 60U, 75U, 110U, 255U };
    stage.parallax_layers().push_back(l0);

    cd::render2d::ParallaxLayer2D l1 {};
    l1.name                 = "Forest_Silhouettes";
    l1.scroll_factor        = { 0.40F, 0.15F };
    l1.auto_scroll_velocity = { 0.25F, 0.0F };
    l1.procedural_style     = cd::render2d::ParallaxProceduralStyle::kForestSilhouettes;
    l1.tint                 = cd::render2d::ColorRgba8 { 40U, 90U, 80U, 255U };
    stage.parallax_layers().push_back(l1);

    cd::render2d::ParallaxLayer2D l2 {};
    l2.name             = "Cyber_Skyline";
    l2.scroll_factor    = { 0.70F, 0.30F };
    l2.procedural_style = cd::render2d::ParallaxProceduralStyle::kCitySkyline;
    l2.tint             = cd::render2d::ColorRgba8 { 90U, 50U, 120U, 255U };
    stage.parallax_layers().push_back(l2);
}

void GenreTemplates2D::build_top_down_rpg(Stage2D& stage)
{
    stage.clear();
    stage.set_name("TopDown_Rpg_Dungeon_2D");
    stage.set_ambient_color({ 0.12F, 0.14F, 0.22F });
    stage.set_ambient_intensity(0.85F);
    stage.set_gravity_y(0.0F);
    stage.set_y_sort_enabled(true);

    // 1. Register non-solid floor and paint 32x24 dungeon floor and perimeter stone walls
    stage.tilemap().register_palette_entry(cd::render2d::TilePaletteEntry2D {
        .palette_id        = 5U,
        .name              = "Dungeon Flagstone Floor",
        .fill_color        = { 42U, 48U, 64U, 255U },
        .top_cap_color     = { 56U, 64U, 84U, 255U },
        .edge_border_color = { 28U, 32U, 44U, 255U },
        .solid             = false,
        .one_way           = false,
        .hazard            = false,
        .emissive          = false,
    });
    stage.tilemap().paint_rect(0, 0, 31, 23, 5U, false);  // Non-solid floor
    stage.tilemap().paint_rect(0, 23, 31, 23, 2U, true);   // North wall
    stage.tilemap().paint_rect(0, 0, 31, 0, 2U, true);     // South wall
    stage.tilemap().paint_rect(0, 0, 0, 23, 2U, true);     // West wall
    stage.tilemap().paint_rect(31, 0, 31, 23, 2U, true);   // East wall

    // Interior pillars and chamber partitions
    stage.tilemap().paint_rect(6, 6, 8, 8, 2U, true);
    stage.tilemap().paint_rect(23, 6, 25, 8, 2U, true);
    stage.tilemap().paint_rect(6, 15, 8, 17, 2U, true);
    stage.tilemap().paint_rect(23, 15, 25, 17, 2U, true);
    stage.tilemap().paint_rect(12, 11, 19, 11, 2U, true);

    stage.rebuild_baked_colliders();

    // 2. Camera framing dungeon hall
    cd::render2d::Camera2DConfig cam_cfg {};
    cam_cfg.pixels_per_unit = 40.0F;
    cam_cfg.zoom            = 1.0F;
    stage.camera().set_config(cam_cfg);
    stage.camera().set_position({ 15.5F, 11.5F });

    // 3. Playable Hero Knight with 4/8-way movement and dynamic Y-sorting
    Entity2D hero {};
    hero.name       = "Hero_Knight_2D";
    hero.role       = EntityRole2D::kPlayerHero;
    hero.position   = { 4.0F, 4.0F };
    hero.size       = { 1.0F, 1.3F };
    hero.color_rgba = { 0.30F, 0.75F, 1.0F, 1.0F };
    hero.move_speed = 6.2F;
    hero.layer      = 100;
    hero.y_sort     = true;
    (void)stage.add_entity(std::move(hero));

    // 4. Torch 2D Point Lights with soft occluders
    const std::array<cd::math::Vec2f, 4> torch_positions = { {
        { 7.0F, 9.2F },
        { 24.0F, 9.2F },
        { 7.0F, 18.2F },
        { 24.0F, 18.2F },
    } };
    for (std::size_t i = 0U; i < torch_positions.size(); ++i)
    {
        Entity2D torch {};
        torch.name            = "Dungeon_Torch_Light_" + std::to_string(i + 1U);
        torch.role            = EntityRole2D::kPointLight;
        torch.position        = torch_positions[i];
        torch.size            = { 0.4F, 0.4F };
        torch.color_rgba      = { 1.0F, 0.62F, 0.22F, 1.0F };
        torch.light_intensity = 1.9F;
        torch.light_radius    = 8.5F;
        torch.cast_shadows    = true;
        (void)stage.add_entity(std::move(torch));
    }

    // Shadow occluders for dungeon pillars
    const std::array<cd::math::Vec2f, 4> pillar_centers = { {
        { 7.0F, 7.0F },
        { 24.0F, 7.0F },
        { 7.0F, 16.0F },
        { 24.0F, 16.0F },
    } };
    for (std::size_t i = 0U; i < pillar_centers.size(); ++i)
    {
        Entity2D occ {};
        occ.name       = "Stone_Pillar_Occluder_" + std::to_string(i + 1U);
        occ.role       = EntityRole2D::kShadowOccluder;
        occ.position   = pillar_centers[i];
        occ.size       = { 2.0F, 2.0F };
        occ.color_rgba = { 0.20F, 0.25F, 0.35F, 1.0F };
        occ.layer      = 90;
        occ.y_sort     = true;
        (void)stage.add_entity(std::move(occ));
    }

    // 5. Dungeon Chests and Keys
    Entity2D chest {};
    chest.name       = "Treasure_Chest_2D";
    chest.role       = EntityRole2D::kCollectibleGem;
    chest.position   = { 27.5F, 20.0F };
    chest.size       = { 1.1F, 0.9F };
    chest.color_rgba = { 1.0F, 0.82F, 0.25F, 1.0F };
    chest.layer      = 100;
    chest.y_sort     = true;
    (void)stage.add_entity(std::move(chest));

    Entity2D dungeon_key {};
    dungeon_key.name       = "Dungeon_Key_2D";
    dungeon_key.role       = EntityRole2D::kCollectibleGem;
    dungeon_key.position   = { 15.5F, 18.0F };
    dungeon_key.size       = { 0.7F, 0.7F };
    dungeon_key.color_rgba = { 0.40F, 1.0F, 0.80F, 1.0F };
    dungeon_key.layer      = 100;
    dungeon_key.y_sort     = true;
    (void)stage.add_entity(std::move(dungeon_key));

    // 6. Patrol Guards
    Entity2D guard1 {};
    guard1.name        = "Skeleton_Guard_East";
    guard1.role        = EntityRole2D::kPatrolEnemy;
    guard1.position    = { 18.0F, 5.0F };
    guard1.size        = { 1.0F, 1.2F };
    guard1.color_rgba  = { 0.90F, 0.30F, 0.35F, 1.0F };
    guard1.move_speed  = 2.4F;
    guard1.patrol_span = 3.5F;
    guard1.layer       = 100;
    guard1.y_sort      = true;
    (void)stage.add_entity(std::move(guard1));

    Entity2D guard2 {};
    guard2.name        = "Skeleton_Guard_North";
    guard2.role        = EntityRole2D::kPatrolEnemy;
    guard2.position    = { 8.0F, 20.0F };
    guard2.size        = { 1.0F, 1.2F };
    guard2.color_rgba  = { 0.90F, 0.30F, 0.35F, 1.0F };
    guard2.move_speed  = 2.0F;
    guard2.patrol_span = 2.8F;
    guard2.layer       = 100;
    guard2.y_sort      = true;
    (void)stage.add_entity(std::move(guard2));

    // 7. Dungeon Exit Portal
    Entity2D exit_portal {};
    exit_portal.name       = "Dungeon_Exit_Stairs";
    exit_portal.role       = EntityRole2D::kGoalPortal;
    exit_portal.position   = { 28.5F, 3.5F };
    exit_portal.size       = { 1.4F, 1.8F };
    exit_portal.color_rgba = { 0.30F, 0.90F, 1.0F, 1.0F };
    exit_portal.layer      = 95;
    exit_portal.y_sort     = true;
    (void)stage.add_entity(std::move(exit_portal));

    // 8. Parallax fog
    stage.parallax_layers().clear();
    cd::render2d::ParallaxLayer2D fog {};
    fog.name                 = "Dungeon_Rafters_Fog";
    fog.scroll_factor        = { 0.05F, 0.05F };
    fog.auto_scroll_velocity = { 0.15F, 0.08F };
    fog.procedural_style     = cd::render2d::ParallaxProceduralStyle::kCloudClusters;
    fog.tint                 = cd::render2d::ColorRgba8 { 25U, 30U, 45U, 120U };
    stage.parallax_layers().push_back(fog);
    stage.sync_bound_atlas_palette();
}

void GenreTemplates2D::build_twin_stick_arena(Stage2D& stage)
{
    stage.clear();
    stage.set_name("TwinStick_Arena_2D");
    stage.set_ambient_color({ 0.18F, 0.22F, 0.35F });
    stage.set_ambient_intensity(1.0F);
    stage.set_gravity_y(0.0F);
    stage.set_y_sort_enabled(true);

    // 1. Register non-solid arena floor and paint 36x28 arena perimeter walls
    stage.tilemap().register_palette_entry(cd::render2d::TilePaletteEntry2D {
        .palette_id        = 5U,
        .name              = "Cyber Arena Floor Deck",
        .fill_color        = { 26U, 34U, 50U, 255U },
        .top_cap_color     = { 38U, 50U, 72U, 255U },
        .edge_border_color = { 18U, 24U, 38U, 255U },
        .solid             = false,
        .one_way           = false,
        .hazard            = false,
        .emissive          = false,
    });
    stage.tilemap().paint_rect(0, 0, 35, 27, 5U, false);  // Non-solid floor
    stage.tilemap().paint_rect(0, 27, 35, 27, 4U, true);  // North boundary
    stage.tilemap().paint_rect(0, 0, 35, 0, 4U, true);    // South boundary
    stage.tilemap().paint_rect(0, 0, 0, 27, 4U, true);    // West boundary
    stage.tilemap().paint_rect(35, 0, 35, 27, 4U, true);  // East boundary

    // Corner bunkers
    stage.tilemap().paint_rect(4, 4, 6, 6, 2U, true);
    stage.tilemap().paint_rect(29, 4, 31, 6, 2U, true);
    stage.tilemap().paint_rect(4, 21, 6, 23, 2U, true);
    stage.tilemap().paint_rect(29, 21, 31, 23, 2U, true);

    stage.rebuild_baked_colliders();

    // 2. Camera centered on arena
    cd::render2d::Camera2DConfig cam_cfg {};
    cam_cfg.pixels_per_unit = 38.0F;
    cam_cfg.zoom            = 1.0F;
    stage.camera().set_config(cam_cfg);
    stage.camera().set_position({ 17.5F, 13.5F });

    // 3. Playable Commando Hero
    Entity2D hero {};
    hero.name       = "Arena_Commando_2D";
    hero.role       = EntityRole2D::kPlayerHero;
    hero.position   = { 17.5F, 13.5F };
    hero.size       = { 1.1F, 1.1F };
    hero.color_rgba = { 0.25F, 0.95F, 1.0F, 1.0F };
    hero.move_speed = 7.5F;
    hero.layer      = 100;
    hero.y_sort     = true;
    (void)stage.add_entity(std::move(hero));

    // 4. Configure Swarm Spawner
    auto& spawner              = stage.swarm_spawner();
    spawner.active             = true;
    spawner.spawn_interval_sec = 1.6F;
    spawner.spawn_radius       = 15.0F;
    spawner.enemy_speed        = 3.2F;
    spawner.max_swarm_count    = 45;

    // Initial swarm enemies
    const std::array<cd::math::Vec2f, 4> initial_enemies = { {
        { 9.0F, 9.0F },
        { 26.0F, 9.0F },
        { 9.0F, 18.0F },
        { 26.0F, 18.0F },
    } };
    for (std::size_t i = 0U; i < initial_enemies.size(); ++i)
    {
        Entity2D enemy {};
        enemy.name          = "Swarm_Drone_Initial_" + std::to_string(i + 1U);
        enemy.role          = EntityRole2D::kPatrolEnemy;
        enemy.position      = initial_enemies[i];
        enemy.home_position = initial_enemies[i];
        enemy.size          = { 0.95F, 0.95F };
        enemy.color_rgba    = { 1.0F, 0.35F, 0.25F, 1.0F };
        enemy.move_speed    = 2.8F;
        enemy.patrol_span   = 2.0F;
        enemy.layer         = 100;
        enemy.y_sort        = true;
        (void)stage.add_entity(std::move(enemy));
    }

    // 5. Dynamic Neon Lights in corners
    const std::array<cd::math::Vec2f, 4> light_pos = { {
        { 5.0F, 5.0F },
        { 30.0F, 5.0F },
        { 5.0F, 22.0F },
        { 30.0F, 22.0F },
    } };
    const std::array<cd::math::Vec4f, 4> light_cols = { {
        { 0.20F, 0.90F, 1.0F, 1.0F },
        { 1.0F, 0.20F, 0.80F, 1.0F },
        { 1.0F, 0.90F, 0.20F, 1.0F },
        { 0.30F, 1.0F, 0.50F, 1.0F },
    } };
    for (std::size_t i = 0U; i < light_pos.size(); ++i)
    {
        Entity2D light {};
        light.name            = "Neon_Corner_Light_" + std::to_string(i + 1U);
        light.role            = EntityRole2D::kPointLight;
        light.position        = light_pos[i];
        light.size            = { 0.5F, 0.5F };
        light.color_rgba      = light_cols[i];
        light.light_intensity = 1.8F;
        light.light_radius    = 12.0F;
        light.cast_shadows    = false;
        (void)stage.add_entity(std::move(light));
    }

    // 6. Deep cyber grid background
    stage.parallax_layers().clear();
    cd::render2d::ParallaxLayer2D grid {};
    grid.name                 = "Cyber_Arena_Grid";
    grid.scroll_factor        = { 0.10F, 0.10F };
    grid.auto_scroll_velocity = { 0.40F, 0.20F };
    grid.procedural_style     = cd::render2d::ParallaxProceduralStyle::kCitySkyline;
    grid.tint                 = cd::render2d::ColorRgba8 { 25U, 35U, 60U, 180U };
    stage.parallax_layers().push_back(grid);
    stage.sync_bound_atlas_palette();
}

void GenreTemplates2D::build_pong_arcade(Stage2D& stage)
{
    stage.clear();
    stage.set_name("Neon_Cyber_Pong_2D");
    stage.set_ambient_color({ 0.16F, 0.20F, 0.32F });
    stage.set_ambient_intensity(0.95F);
    stage.set_gravity_y(0.0F);
    stage.set_y_sort_enabled(false);
    stage.set_arcade_ruleset(ArcadeRuleset2D::kPongCourt);

    // 1. Cyber court floor & solid top/bottom neon bumper walls
    stage.tilemap().register_palette_entry(cd::render2d::TilePaletteEntry2D {
        .palette_id        = 5U,
        .name              = "Cyber Arena Floor Deck",
        .fill_color        = { 20U, 28U, 44U, 255U },
        .top_cap_color     = { 30U, 42U, 64U, 255U },
        .edge_border_color = { 14U, 20U, 32U, 255U },
        .solid             = false,
        .one_way           = false,
        .hazard            = false,
        .emissive          = false,
    });
    stage.tilemap().paint_rect(-13, -6, 12, 5, 5U, false);  // Non-solid dark cyber court
    stage.tilemap().paint_rect(-13, 6, 12, 6, 4U, true);    // Solid top neon bumper wall
    stage.tilemap().paint_rect(-13, -7, 12, -7, 4U, true);  // Solid bottom neon bumper wall
    stage.rebuild_baked_colliders();

    // 2. Fixed centered arcade camera
    cd::render2d::Camera2DConfig cam_cfg {};
    cam_cfg.pixels_per_unit = 44.0F;
    cam_cfg.zoom            = 1.0F;
    stage.camera().set_config(cam_cfg);
    stage.camera().set_position({ 0.0F, 0.0F });

    // 3. Left Player Paddle (W/S or Up/Down)
    Entity2D p1 {};
    p1.name        = "Player_Paddle_Left";
    p1.role        = EntityRole2D::kKinematicPaddle;
    p1.position    = { -11.0F, 0.0F };
    p1.size        = { 0.75F, 2.85F };
    p1.color_rgba  = { 0.22F, 0.94F, 1.0F, 1.0F };
    p1.move_speed  = 10.5F;
    p1.patrol_span = 4.45F;
    p1.layer       = 120;
    (void)stage.add_entity(std::move(p1));

    // 4. Right CPU Opponent Paddle (Autonomous Ball Tracking)
    Entity2D cpu {};
    cpu.name        = "CPU_Paddle_Right";
    cpu.role        = EntityRole2D::kAiOpponentPaddle;
    cpu.position    = { 11.0F, 0.0F };
    cpu.size        = { 0.75F, 2.85F };
    cpu.color_rgba  = { 1.0F, 0.28F, 0.62F, 1.0F };
    cpu.move_speed  = 7.2F;
    cpu.patrol_span = 4.45F;
    cpu.layer       = 120;
    (void)stage.add_entity(std::move(cpu));

    // 5. Balanced Arcade Energy Plasma Ball
    Entity2D ball {};
    ball.name         = "Pong_Plasma_Ball";
    ball.role         = EntityRole2D::kBouncingBall;
    ball.position     = { 0.0F, 0.0F };
    ball.size         = { 0.65F, 0.65F };
    ball.rotation_deg = 24.0F;
    ball.move_speed   = 9.2F;
    ball.color_rgba   = { 0.35F, 0.96F, 1.0F, 1.0F };
    ball.layer        = 130;
    (void)stage.add_entity(std::move(ball));

    // 6. Dynamic Court Rim Lights
    Entity2D left_glow {};
    left_glow.name            = "Cyan_Goal_Light";
    left_glow.role            = EntityRole2D::kPointLight;
    left_glow.position        = { -10.2F, 0.0F };
    left_glow.size            = { 0.5F, 0.5F };
    left_glow.color_rgba      = { 0.18F, 0.90F, 1.0F, 1.0F };
    left_glow.light_intensity = 1.65F;
    left_glow.light_radius    = 9.5F;
    left_glow.cast_shadows    = false;
    (void)stage.add_entity(std::move(left_glow));

    Entity2D right_glow {};
    right_glow.name            = "Magenta_Goal_Light";
    right_glow.role            = EntityRole2D::kPointLight;
    right_glow.position        = { 10.2F, 0.0F };
    right_glow.size            = { 0.5F, 0.5F };
    right_glow.color_rgba      = { 1.0F, 0.25F, 0.68F, 1.0F };
    right_glow.light_intensity = 1.65F;
    right_glow.light_radius    = 9.5F;
    right_glow.cast_shadows    = false;
    (void)stage.add_entity(std::move(right_glow));

    stage.sync_bound_atlas_palette();
}

void GenreTemplates2D::build_break_the_bridges(Stage2D& stage)
{
    stage.clear();
    stage.set_name("Break_The_Bridges_2D");
    stage.set_ambient_color({ 0.22F, 0.24F, 0.36F });
    stage.set_ambient_intensity(0.95F);
    stage.set_gravity_y(-24.0F);
    stage.set_y_sort_enabled(false);
    stage.set_arcade_ruleset(ArcadeRuleset2D::kBridgeDemolition);

    // 1. Canyon Cliff Bastions, Cavern Ceiling & Glowing Canyon Plasma River
    stage.tilemap().paint_rect(-13, -6, -10, 7, 1U, true);  // West cliff bastion
    stage.tilemap().paint_rect(9, -6, 12, 7, 1U, true);     // East cliff bastion
    stage.tilemap().paint_rect(-13, 8, 12, 8, 2U, true);    // Upper cavern ricochet arch
    stage.tilemap().paint_rect(-9, -6, 8, -6, 3U, true);    // Canyon plasma hazard river
    stage.rebuild_baked_colliders();

    // 2. Camera framing the 3 canyon bridges + lower striker defense rail
    cd::render2d::Camera2DConfig cam_cfg {};
    cam_cfg.pixels_per_unit = 42.0F;
    cam_cfg.zoom            = 1.0F;
    stage.camera().set_config(cam_cfg);
    stage.camera().set_position({ -0.5F, 1.35F });

    // 3. Player Mobile Demolition Striker & Plasma Cannon Rail
    Entity2D striker {};
    striker.name        = "Demolition_Striker_Rail";
    striker.role        = EntityRole2D::kKinematicPaddle;
    striker.position    = { -0.5F, -3.95F };
    striker.size        = { 3.15F, 0.68F };
    striker.color_rgba  = { 0.25F, 0.94F, 1.0F, 1.0F };
    striker.move_speed  = 10.5F;
    striker.patrol_span = 6.9F;
    striker.layer       = 125;
    (void)stage.add_entity(std::move(striker));

    // 4. Balanced Demolition Sphere
    Entity2D sphere {};
    sphere.name         = "Demolition_Sphere_2D";
    sphere.role         = EntityRole2D::kBouncingBall;
    sphere.position     = { -1.6F, -2.9F };
    sphere.size         = { 0.64F, 0.64F };
    sphere.rotation_deg = 64.0F;
    sphere.move_speed   = 8.4F;
    sphere.color_rgba   = { 0.40F, 0.96F, 1.0F, 1.0F };
    sphere.layer        = 130;
    (void)stage.add_entity(std::move(sphere));

    // 5. Three Multi-Span Destructible Bridges across the Canyon (-7.5 .. +6.5)
    const std::array<float, 3> bridge_heights { 1.0F, 3.45F, 5.90F };
    const std::array<const char*, 3> bridge_names { "Lower_Bridge_Span_", "Middle_Bridge_Span_", "Upper_Bridge_Span_" };
    const std::array<float, 6> span_xs { -7.5F, -4.7F, -1.9F, 0.9F, 3.7F, 6.5F };

    for (std::size_t tier = 0U; tier < bridge_heights.size(); ++tier)
    {
        for (std::size_t col = 0U; col < span_xs.size(); ++col)
        {
            Entity2D plank {};
            plank.name       = std::string(bridge_names.at(tier)) + std::to_string(col + 1U);
            plank.role       = EntityRole2D::kDestructibleBlock;
            plank.position   = { span_xs.at(col), bridge_heights.at(tier) };
            plank.size       = { 2.68F, 0.58F };
            plank.color_rgba = (tier == 0U) ? cd::math::Vec4f { 0.88F, 0.62F, 0.34F, 1.0F }
                             : ((tier == 1U) ? cd::math::Vec4f { 0.68F, 0.80F, 0.96F, 1.0F }
                                             : cd::math::Vec4f { 0.96F, 0.78F, 0.36F, 1.0F });
            plank.layer      = 105;
            (void)stage.add_entity(std::move(plank));
        }
    }

    // Load-bearing Keystone Bridge Support Piers (destroying a pier cascades planks above!)
    const std::array<cd::math::Vec2f, 4> pier_coords { {
        { -4.7F, -0.22F },
        {  3.7F, -0.22F },
        { -1.9F,  2.22F },
        {  0.9F,  2.22F },
    } };
    for (std::size_t i = 0U; i < pier_coords.size(); ++i)
    {
        Entity2D pier {};
        pier.name       = "Keystone_Bridge_Pier_" + std::to_string(i + 1U);
        pier.role       = EntityRole2D::kDestructibleBlock;
        pier.position   = pier_coords.at(i);
        pier.size       = { 0.88F, 1.72F };
        pier.color_rgba = { 0.95F, 0.82F, 0.44F, 1.0F };
        pier.layer      = 102;
        (void)stage.add_entity(std::move(pier));
    }

    // 6. Enemy Raiders Marching Across the 3 Bridges (plunge into chasm when bridges break!)
    struct BridgeRaiderSpec
    {
        const char*     name;
        cd::math::Vec2f pos;
        float           span;
        float           speed;
    };
    const std::array<BridgeRaiderSpec, 4> raiders { {
        { "Skeleton_Guard_West",   { -4.7F, 1.82F }, 2.6F, 1.55F },
        { "Skeleton_Guard_East",   {  3.7F, 1.82F }, 2.6F, 1.65F },
        { "Synth_Raider_MidSpan",  { -0.5F, 4.26F }, 5.2F, 2.05F },
        { "Skeleton_Guard_Upper",  { -0.5F, 6.72F }, 5.4F, 1.80F },
    } };
    for (const auto& r : raiders)
    {
        Entity2D enemy {};
        enemy.name        = r.name;
        enemy.role        = EntityRole2D::kPatrolEnemy;
        enemy.position    = r.pos;
        enemy.size        = { 0.95F, 1.05F };
        enemy.color_rgba  = { 1.0F, 0.36F, 0.42F, 1.0F };
        enemy.move_speed  = r.speed;
        enemy.patrol_span = r.span;
        enemy.layer       = 115;
        (void)stage.add_entity(std::move(enemy));
    }

    // 7. Canyon Cliff Torches & Molten River Glow
    const std::array<cd::math::Vec2f, 2> cliff_torches { {
        { -9.15F, 4.2F },
        {  8.15F, 4.2F },
    } };
    for (std::size_t i = 0U; i < cliff_torches.size(); ++i)
    {
        Entity2D torch {};
        torch.name            = "Cliff_Torch_Light_" + std::to_string(i + 1U);
        torch.role            = EntityRole2D::kPointLight;
        torch.position        = cliff_torches.at(i);
        torch.size            = { 0.5F, 0.5F };
        torch.color_rgba      = { 1.0F, 0.72F, 0.32F, 1.0F };
        torch.light_intensity = 1.85F;
        torch.light_radius    = 11.0F;
        torch.cast_shadows    = false;
        (void)stage.add_entity(std::move(torch));
    }

    Entity2D chasm_glow {};
    chasm_glow.name            = "Canyon_Lava_Glow";
    chasm_glow.role            = EntityRole2D::kPointLight;
    chasm_glow.position        = { -0.5F, -4.8F };
    chasm_glow.size            = { 0.5F, 0.5F };
    chasm_glow.color_rgba      = { 1.0F, 0.34F, 0.24F, 1.0F };
    chasm_glow.light_intensity = 1.60F;
    chasm_glow.light_radius    = 12.5F;
    chasm_glow.cast_shadows    = false;
    (void)stage.add_entity(std::move(chasm_glow));

    // 8. Mountain canyon parallax backdrop
    stage.parallax_layers().clear();
    cd::render2d::ParallaxLayer2D mtns {};
    mtns.name             = "Canyon_Peaks";
    mtns.scroll_factor    = { 0.15F, 0.05F };
    mtns.procedural_style = cd::render2d::ParallaxProceduralStyle::kMountainPinnacle;
    mtns.tint             = cd::render2d::ColorRgba8 { 52U, 64U, 96U, 210U };
    stage.parallax_layers().push_back(mtns);

    stage.sync_bound_atlas_palette();
}

}  // namespace cd::world2d
