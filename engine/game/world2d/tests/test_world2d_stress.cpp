// =============================================================================
// CHROMODYNAMIC: test_world2d_stress.cpp
// Milestone 4: Empirical Stress Testing Suite for cd::world2d & CommandBus2D.
// Covers:
// 1. Repeated Genre Template Switching (Action Platformer, RPG, Twin-Stick Arena)
// 2. CommandBus2D Undo / Redo across Template Loads, Stamps, and Flood-Fills
// 3. Dynamic Y-Sorting on Large Entity Sets (1,000+ entities, positive/negative Y, ties, mixed flags)
// 4. Projectile Pool Capacity, Zero-Allocation Recycling & Swarm Spawner Stress
// =============================================================================
#include <cd/asset/json/Json.hpp>
#include <cd/audio/IAudioBackend.hpp>
#include <cd/render2d/SortKey2D.hpp>
#include <cd/world2d/AudioCue2D.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/GenreTemplates2D.hpp>
#include <cd/world2d/ObjectPool2D.hpp>
#include <cd/world2d/ParticleSystem2D.hpp>
#include <cd/world2d/PlaytestRunner2D.hpp>
#include <cd/world2d/SpatialHash2D.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace
{

// =============================================================================
// 1. Repeated Genre Template Switching
// =============================================================================

TEST(World2DStressTest, RepeatedGenreTemplateSwitching_100Cycles_NoResourceLeak)
{
    cd::world2d::Stage2D stage {};

    for (int cycle = 0; cycle < 100; ++cycle)
    {
        // 1. Action Platformer
        cd::world2d::GenreTemplates2D::apply_genre_template(
            stage, cd::world2d::GenreTemplateKind2D::kActionPlatformer);
        EXPECT_EQ(stage.name(), "Neon_Cavern_Platformer_2D");
        EXPECT_FLOAT_EQ(stage.gravity_y(), -28.0F);
        EXPECT_FALSE(stage.is_y_sort_enabled());
        EXPECT_EQ(stage.parallax_layers().size(), 3U);
        EXPECT_GT(stage.entities().size(), 5U);
        EXPECT_GT(stage.tilemap().non_empty_tile_count(), 20U);

        // 2. Top-Down RPG Dungeon
        cd::world2d::GenreTemplates2D::apply_genre_template(
            stage, cd::world2d::GenreTemplateKind2D::kTopDownRpgDungeon);
        EXPECT_EQ(stage.name(), "TopDown_Rpg_Dungeon_2D");
        EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
        EXPECT_TRUE(stage.is_y_sort_enabled());
        EXPECT_EQ(stage.parallax_layers().size(), 1U);
        EXPECT_GT(stage.entities().size(), 8U);
        EXPECT_GT(stage.tilemap().non_empty_tile_count(), 50U);

        // 3. Twin-Stick Arena
        cd::world2d::GenreTemplates2D::apply_genre_template(
            stage, cd::world2d::GenreTemplateKind2D::kTwinStickArena);
        EXPECT_EQ(stage.name(), "TwinStick_Arena_2D");
        EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
        EXPECT_TRUE(stage.is_y_sort_enabled());
        EXPECT_TRUE(stage.swarm_spawner().active);
        EXPECT_EQ(stage.parallax_layers().size(), 1U);
        EXPECT_GT(stage.entities().size(), 8U);
        EXPECT_GT(stage.tilemap().non_empty_tile_count(), 50U);
    }
}

TEST(World2DStressTest, GenreSwitchingDuringPlayMode_RecoversCleanly)
{
    cd::world2d::Stage2D stage {};

    for (int i = 0; i < 10; ++i)
    {
        // Start platformer in play mode
        cd::world2d::GenreTemplates2D::apply_genre_template(
            stage, cd::world2d::GenreTemplateKind2D::kActionPlatformer);
        stage.enter_play();
        EXPECT_TRUE(stage.is_playing());

        cd::world2d::PlayerInput2D in {};
        in.move_x = 1.0F;
        stage.step_play(1.0F / 60.0F, in);

        // Directly apply RPG template while stage is still marked playing
        cd::world2d::GenreTemplates2D::apply_genre_template(
            stage, cd::world2d::GenreTemplateKind2D::kTopDownRpgDungeon);
        EXPECT_FALSE(stage.is_playing()); // clear() resets playing_

        stage.enter_play();
        EXPECT_TRUE(stage.is_playing());
        in.move_y = 1.0F;
        stage.step_play(1.0F / 60.0F, in);

        // Switch to Arena
        cd::world2d::GenreTemplates2D::apply_genre_template(
            stage, cd::world2d::GenreTemplateKind2D::kTwinStickArena);
        EXPECT_FALSE(stage.is_playing());

        stage.enter_play();
        EXPECT_TRUE(stage.is_playing());
        in.fire_pressed = true;
        stage.step_play(1.0F / 60.0F, in);
        stage.exit_play();
        EXPECT_FALSE(stage.is_playing());
    }
}

TEST(World2DStressTest, PlayInEditor_ExitPlay_PreservesAuthoringParallaxAndYSort)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::GenreTemplates2D::apply_genre_template(
        stage, cd::world2d::GenreTemplateKind2D::kActionPlatformer);
    ASSERT_EQ(stage.parallax_layers().size(), 3U);

    stage.enter_play();
    stage.step_play(0.1F, {});
    stage.exit_play();

    // Verify parallax layers were preserved after exiting play
    EXPECT_EQ(stage.parallax_layers().size(), 3U);

    // Test with RPG template and Y-sorting
    cd::world2d::GenreTemplates2D::apply_genre_template(
        stage, cd::world2d::GenreTemplateKind2D::kTopDownRpgDungeon);
    ASSERT_TRUE(stage.is_y_sort_enabled());
    ASSERT_EQ(stage.parallax_layers().size(), 1U);

    stage.enter_play();
    stage.step_play(0.1F, {});
    stage.exit_play();

    EXPECT_TRUE(stage.is_y_sort_enabled());
    EXPECT_EQ(stage.parallax_layers().size(), 1U);
}


// =============================================================================
// 2. CommandBus2D Undo / Redo Stress Testing
// =============================================================================

TEST(World2DStressTest, CommandBus2D_TemplateLoad_UndoRedo_StatePreservation)
{
    cd::world2d::Stage2D      stage {};
    cd::world2d::CommandBus2D bus {};

    // 1. Initial platformer
    bus.load_genre_template(stage, cd::world2d::GenreTemplateKind2D::kActionPlatformer);
    EXPECT_EQ(stage.name(), "Neon_Cavern_Platformer_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), -28.0F);
    EXPECT_FALSE(stage.is_y_sort_enabled());
    const std::size_t platformer_parallax = stage.parallax_layers().size();
    EXPECT_EQ(platformer_parallax, 3U);

    // 2. Load TopDown RPG Dungeon
    bus.load_genre_template(stage, cd::world2d::GenreTemplateKind2D::kTopDownRpgDungeon);
    EXPECT_EQ(stage.name(), "TopDown_Rpg_Dungeon_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    EXPECT_TRUE(stage.is_y_sort_enabled());
    EXPECT_EQ(stage.parallax_layers().size(), 1U);

    // 3. Load TwinStick Arena
    bus.load_genre_template(stage, cd::world2d::GenreTemplateKind2D::kTwinStickArena);
    EXPECT_EQ(stage.name(), "TwinStick_Arena_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    EXPECT_TRUE(stage.is_y_sort_enabled());
    EXPECT_TRUE(stage.swarm_spawner().active);
    EXPECT_EQ(stage.parallax_layers().size(), 1U);

    // 4. Undo back to RPG
    ASSERT_TRUE(bus.undo(stage));
    EXPECT_EQ(stage.name(), "TopDown_Rpg_Dungeon_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    // Empirical verification: does stage JSON serialization preserve y_sort_enabled, parallax, spawner?
    EXPECT_TRUE(stage.is_y_sort_enabled());
    EXPECT_EQ(stage.parallax_layers().size(), 1U);

    // 5. Undo back to Platformer
    ASSERT_TRUE(bus.undo(stage));
    EXPECT_EQ(stage.name(), "Neon_Cavern_Platformer_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), -28.0F);
    EXPECT_FALSE(stage.is_y_sort_enabled());
    EXPECT_EQ(stage.parallax_layers().size(), 3U);

    // 6. Redo to RPG
    ASSERT_TRUE(bus.redo(stage));
    EXPECT_EQ(stage.name(), "TopDown_Rpg_Dungeon_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    EXPECT_TRUE(stage.is_y_sort_enabled());
    EXPECT_EQ(stage.parallax_layers().size(), 1U);

    // 7. Redo to Arena
    ASSERT_TRUE(bus.redo(stage));
    EXPECT_EQ(stage.name(), "TwinStick_Arena_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    EXPECT_TRUE(stage.is_y_sort_enabled());
    EXPECT_TRUE(stage.swarm_spawner().active);
    EXPECT_EQ(stage.parallax_layers().size(), 1U);
}

TEST(World2DStressTest, CommandBus2D_StampAndFloodFill_UndoRedo_TilemapExactness)
{
    cd::world2d::Stage2D      stage {};
    cd::world2d::CommandBus2D bus {};

    bus.load_genre_template(stage, cd::world2d::GenreTemplateKind2D::kTopDownRpgDungeon);
    const std::size_t base_tiles = stage.tilemap().non_empty_tile_count();

    // 1. Stamp a 4x4 block of steel tiles (palette ID 2) at (10, 10)
    const std::array<std::uint16_t, 16> stamp_pattern = {
        2U, 2U, 2U, 2U,
        2U, 2U, 2U, 2U,
        2U, 2U, 2U, 2U,
        2U, 2U, 2U, 2U,
    };
    const std::size_t stamped = bus.stamp(stage, 10, 10, 4, 4, stamp_pattern, true);
    EXPECT_EQ(stamped, 16U);
    const std::size_t post_stamp_tiles = stage.tilemap().non_empty_tile_count();

    // 2. Flood-fill from empty outer region with glowing cyber neon (palette ID 4)
    const std::size_t filled = bus.flood_fill(stage, -5, -5, 4U);
    EXPECT_GT(filled, 0U);
    const std::size_t post_fill_tiles = stage.tilemap().non_empty_tile_count();
    EXPECT_GT(post_fill_tiles, post_stamp_tiles);

    // 3. Undo Flood-Fill -> Tile count returns to post_stamp_tiles
    ASSERT_TRUE(bus.undo(stage));
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), post_stamp_tiles);

    // 4. Undo Stamp -> Tile count returns to base_tiles
    ASSERT_TRUE(bus.undo(stage));
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), base_tiles);

    // 5. Redo Stamp -> Tile count returns to post_stamp_tiles
    ASSERT_TRUE(bus.redo(stage));
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), post_stamp_tiles);

    // 6. Redo Flood-Fill -> Tile count returns to post_fill_tiles
    ASSERT_TRUE(bus.redo(stage));
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), post_fill_tiles);
}

// =============================================================================
// 3. Dynamic Y-Sorting Empirical Verification
// =============================================================================

TEST(World2DStressTest, DynamicYSorting_1500Entities_NegativeAndPositiveCoordinates_StrictMonotonicity)
{
    cd::world2d::Stage2D stage {};
    stage.clear();
    stage.set_y_sort_enabled(true);

    constexpr std::size_t kEntityCount = 1500U;
    std::mt19937          rng(1337U);
    std::uniform_real_distribution<float> y_dist(-500.0F, 500.0F);
    std::uniform_real_distribution<float> size_dist(0.5F, 4.0F);

    for (std::size_t i = 0U; i < kEntityCount; ++i)
    {
        cd::world2d::Entity2D ent {};
        ent.id       = static_cast<std::uint32_t>(i + 1U);
        ent.name     = "Entity_" + std::to_string(ent.id);
        ent.position = { static_cast<float>(i % 30), y_dist(rng) };
        ent.size     = { 1.0F, size_dist(rng) };
        ent.layer    = 100;
        ent.z_index  = 0;
        ent.y_sort   = true;
        (void)stage.add_entity(std::move(ent));
    }

    stage.sort_entities_by_foot_y();

    const auto ents = stage.entities();
    ASSERT_EQ(ents.size(), kEntityCount);

    // In descending order of foot Y: foot_y = position.y - size.y * 0.5f
    // Every consecutive pair must satisfy: foot_y[i] >= foot_y[i + 1]
    for (std::size_t i = 0U; i + 1U < ents.size(); ++i)
    {
        const float foot_cur  = ents[i].position.y - ents[i].size.y * 0.5F;
        const float foot_next = ents[i + 1U].position.y - ents[i + 1U].size.y * 0.5F;
        EXPECT_GE(foot_cur, foot_next)
            << "Inversion detected at index " << i << ": foot_cur=" << foot_cur
            << ", foot_next=" << foot_next;
    }
}

TEST(World2DStressTest, DynamicYSorting_IdenticalFootYTies_PreservesStableOrder)
{
    cd::world2d::Stage2D stage {};
    stage.clear();
    stage.set_y_sort_enabled(true);

    constexpr std::size_t kTieCount = 500U;
    constexpr float       kCommonFootY = -12.5F;

    for (std::size_t i = 0U; i < kTieCount; ++i)
    {
        cd::world2d::Entity2D ent {};
        ent.id       = static_cast<std::uint32_t>(i + 100U);
        ent.name     = "TieEntity_" + std::to_string(ent.id);
        const float height = 2.0F + static_cast<float>(i % 5) * 0.5F;
        ent.position = { static_cast<float>(i), kCommonFootY + height * 0.5F };
        ent.size     = { 1.0F, height };
        ent.layer    = 100;
        ent.z_index  = 0;
        ent.y_sort   = true;
        (void)stage.add_entity(std::move(ent));
    }

    stage.sort_entities_by_foot_y();

    const auto ents = stage.entities();
    ASSERT_EQ(ents.size(), kTieCount);

    // Stable sort must preserve original relative insertion order when foot Y is identical
    for (std::size_t i = 0U; i < ents.size(); ++i)
    {
        EXPECT_EQ(ents[i].id, static_cast<std::uint32_t>(i + 100U))
            << "Stability violated at index " << i;
    }
}

TEST(World2DStressTest, DynamicYSorting_MixedYSortFlags_ViolatesStrictWeakOrderingTransitivity)
{
    cd::world2d::Entity2D a {};
    a.id = 1; a.layer = 100; a.y_sort = true; a.position = { 0.0F, 1.0F }; a.size = { 1.0F, 0.0F }; a.z_index = 0; // foot = 1.0

    cd::world2d::Entity2D b {};
    b.id = 2; b.layer = 100; b.y_sort = false; b.position = { 0.0F, 5.0F }; b.size = { 1.0F, 0.0F }; b.z_index = 10; // foot = 5.0

    cd::world2d::Entity2D c {};
    c.id = 3; c.layer = 100; c.y_sort = true; c.position = { 0.0F, 10.0F }; c.size = { 1.0F, 0.0F }; c.z_index = 20; // foot = 10.0

    const bool a_less_b = cd::world2d::Stage2D::compare_entities_by_foot_y(a, b);
    const bool b_less_c = cd::world2d::Stage2D::compare_entities_by_foot_y(b, c);
    const bool c_less_a = cd::world2d::Stage2D::compare_entities_by_foot_y(c, a);
    const bool a_less_c = cd::world2d::Stage2D::compare_entities_by_foot_y(a, c);

    // Check if transitivity holds (if a < b and b < c, then a < c must be true and c < a false)
    EXPECT_TRUE(a_less_b) << "a < b should be true via z_index (0 < 10)";
    EXPECT_TRUE(b_less_c) << "b < c should be true via z_index (10 < 20)";
    // If strict weak ordering holds, a < c must be true:
    EXPECT_TRUE(a_less_c) << "Strict weak ordering satisfied: a < b and b < c implies a < c!";
    EXPECT_FALSE(c_less_a) << "No cycle: c < a is false!";

    // Verify stage dynamic Y-sorting preserves transitive ordering
    cd::world2d::Stage2D stage {};
    stage.clear();
    stage.set_y_sort_enabled(true);
    stage.add_entity(c);
    stage.add_entity(a);
    stage.add_entity(b);
    stage.sort_entities_by_foot_y();

    const auto ents = stage.entities();
    ASSERT_EQ(ents.size(), 3U);
    EXPECT_EQ(ents[0].id, 1U); // a (z_index = 0)
    EXPECT_EQ(ents[1].id, 2U); // b (z_index = 10)
    EXPECT_EQ(ents[2].id, 3U); // c (z_index = 20)
}


TEST(World2DStressTest, DynamicYSorting_MakeSortKey2D_ClampingAndOrdering)
{
    // Test negative coordinates and boundary clamping in make_sort_key_2d
    const std::uint64_t key_far_north = cd::render2d::make_sort_key_2d(100U, 0, 5000.0F, true, 0U);
    const std::uint64_t key_north     = cd::render2d::make_sort_key_2d(100U, 0, 100.0F, true, 0U);
    const std::uint64_t key_zero      = cd::render2d::make_sort_key_2d(100U, 0, 0.0F, true, 0U);
    const std::uint64_t key_south     = cd::render2d::make_sort_key_2d(100U, 0, -100.0F, true, 0U);
    const std::uint64_t key_far_south = cd::render2d::make_sort_key_2d(100U, 0, -5000.0F, true, 0U);

    // Lower foot Y (closer to south/viewer) must produce LARGER sort key (drawn later / in front)
    EXPECT_LT(key_far_north, key_north);
    EXPECT_LT(key_north, key_zero);
    EXPECT_LT(key_zero, key_south);
    EXPECT_LT(key_south, key_far_south);
}

// =============================================================================
// 4. Projectile Pool Capacity, Recycling & Swarm Spawner Stress
// =============================================================================

TEST(World2DStressTest, ProjectilePool_RapidFiring_CapacityExhaustionAndRecycling)
{
    cd::world2d::ObjectPool2D<cd::world2d::Projectile2D> pool(512U, false);
    EXPECT_EQ(pool.capacity(), 512U);
    EXPECT_EQ(pool.active_count(), 0U);
    EXPECT_EQ(pool.available_count(), 512U);
    EXPECT_TRUE(pool.empty());
    EXPECT_FALSE(pool.full());

    // 1. Fill pool completely to 512 capacity
    std::vector<cd::world2d::ObjectPool2D<cd::world2d::Projectile2D>::HandleType> handles;
    handles.reserve(512U);

    for (std::size_t i = 0U; i < 512U; ++i)
    {
        auto h = pool.emplace(cd::world2d::Projectile2D {
            .position = { static_cast<float>(i), 0.0F },
            .velocity = { 10.0F, 0.0F },
            .damage   = static_cast<int>(i),
        });
        ASSERT_TRUE(h.has_value());
        EXPECT_TRUE(pool.is_valid(*h));
        handles.push_back(*h);
    }

    EXPECT_EQ(pool.active_count(), 512U);
    EXPECT_EQ(pool.available_count(), 0U);
    EXPECT_TRUE(pool.full());

    // 2. 513th acquire should gracefully return nullopt (no growth allowed)
    auto overflow_handle = pool.emplace(cd::world2d::Projectile2D {});
    EXPECT_FALSE(overflow_handle.has_value());
    EXPECT_EQ(pool.active_count(), 512U);
    EXPECT_EQ(pool.capacity(), 512U);

    // 3. Release every second projectile (256 released)
    for (std::size_t i = 0U; i < 512U; i += 2U)
    {
        EXPECT_TRUE(pool.release(handles[i]));
        EXPECT_FALSE(pool.is_valid(handles[i])); // Dead handle
    }
    EXPECT_EQ(pool.active_count(), 256U);
    EXPECT_EQ(pool.available_count(), 256U);
    EXPECT_FALSE(pool.full());

    // 4. Re-acquire 256 projectiles
    std::vector<cd::world2d::ObjectPool2D<cd::world2d::Projectile2D>::HandleType> new_handles;
    new_handles.reserve(256U);
    for (std::size_t i = 0U; i < 256U; ++i)
    {
        auto h = pool.emplace(cd::world2d::Projectile2D {
            .position = { -1.0F, static_cast<float>(i) },
            .velocity = { 0.0F, 5.0F },
            .damage   = 100,
        });
        ASSERT_TRUE(h.has_value());
        EXPECT_TRUE(pool.is_valid(*h));
        new_handles.push_back(*h);
    }
    EXPECT_EQ(pool.active_count(), 512U);
    EXPECT_TRUE(pool.full());

    // Verify ABA safety: old handles from step 3 must remain INVALID even though slot is reused
    for (std::size_t i = 0U; i < 512U; i += 2U)
    {
        EXPECT_FALSE(pool.is_valid(handles[i]))
            << "ABA violation: old handle at index " << i << " reported valid after slot reuse!";
    }

    // 5. Clean clear()
    pool.clear();
    EXPECT_EQ(pool.active_count(), 0U);
    EXPECT_EQ(pool.available_count(), 512U);
    EXPECT_TRUE(pool.empty());
}

TEST(World2DStressTest, TwinStickArena_1000FramesCombatSimulation_SwarmCapAndSpatialCollisions)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::GenreTemplates2D::apply_genre_template(
        stage, cd::world2d::GenreTemplateKind2D::kTwinStickArena);

    stage.enter_play();
    EXPECT_TRUE(stage.is_playing());
    const int max_swarm = stage.swarm_spawner().max_swarm_count;
    EXPECT_GT(max_swarm, 10);

    // Run 1000 simulation frames with rapid fire and circular aiming
    for (int frame = 0; frame < 1000; ++frame)
    {
        cd::world2d::PlayerInput2D in {};
        const float angle = static_cast<float>(frame) * 0.12F;
        in.move_x         = std::cos(angle) * 0.5F;
        in.move_y         = std::sin(angle) * 0.5F;
        in.fire_held      = true;
        in.aim_angle_rad  = angle * 2.0F;

        stage.step_play(1.0F / 60.0F, in);

        // Check projectile pool does not overflow capacity 512
        EXPECT_LE(stage.projectile_pool().active_count(), 512U);

        // Check active swarm count does not exceed max_swarm_count + tolerance
        int active_swarm = 0;
        for (const auto& ent : stage.entities())
        {
            if (ent.role == cd::world2d::EntityRole2D::kPatrolEnemy && !ent.collected_or_defeated)
            {
                ++active_swarm;
            }
        }
        EXPECT_LE(active_swarm, max_swarm + 4)
            << "Swarm spawner exceeded configured cap at frame " << frame;
    }

    EXPECT_GT(stage.hud().enemies_defeated, 0);
    EXPECT_GT(stage.hud().score, 0);
    stage.exit_play();
    EXPECT_FALSE(stage.is_playing());
}

// =============================================================================
// 5. Target D: AudioCue2D High-Throughput Burst & Pathological Waveforms
// =============================================================================

TEST(World2DStressTest, TargetD_AudioCue2D_50kBurst_NullBackend_ZeroAllocationAndTelemetry)
{
    cd::world2d::AudioCueSystem2D audio(nullptr);
    EXPECT_EQ(audio.backend(), nullptr);
    EXPECT_EQ(audio.total_cues_dispatched(), 0U);
    EXPECT_FALSE(audio.last_dispatched_cue().has_value());

    constexpr std::uint32_t kBurstCount = 50000U;
    constexpr std::array<cd::world2d::AudioCuePreset2D, 7> presets = {
        cd::world2d::AudioCuePreset2D::kJump,
        cd::world2d::AudioCuePreset2D::kCoin,
        cd::world2d::AudioCuePreset2D::kHit,
        cd::world2d::AudioCuePreset2D::kExplosion,
        cd::world2d::AudioCuePreset2D::kLaser,
        cd::world2d::AudioCuePreset2D::kPowerup,
        cd::world2d::AudioCuePreset2D::kBounce,
    };

    const auto t_start = std::chrono::steady_clock::now();

    for (std::uint32_t i = 0U; i < kBurstCount; ++i)
    {
        const auto preset = presets[i % presets.size()];
        const float vol = (i % 5 == 0) ? 1e6F : ((i % 5 == 1) ? -5.0F : ((i % 5 == 2) ? 0.0F : 1.0F));
        const cd::math::Vec2f emitter {
            static_cast<float>((i % 1000) - 500) * 20.0F,
            static_cast<float>((i % 700) - 350) * 15.0F,
        };

        if ((i & 1U) == 0U)
        {
            auto v = audio.play_cue(preset, vol);
            EXPECT_TRUE(v.is_null());
        }
        else
        {
            auto v = audio.play_cue_at(preset, emitter, vol);
            EXPECT_TRUE(v.is_null());
        }
    }

    const auto t_end = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

    EXPECT_EQ(audio.total_cues_dispatched(), kBurstCount);
    EXPECT_TRUE(audio.last_dispatched_cue().has_value());
    EXPECT_EQ(*audio.last_dispatched_cue(), presets[(kBurstCount - 1U) % presets.size()]);

    // High throughput verification: 50,000 cues should complete in < 250ms (no heap allocation overhead)
    EXPECT_LT(elapsed_ms, 250)
        << "50,000 null audio cue burst took " << elapsed_ms << " ms; expected < 250 ms";

    audio.reset_telemetry();
    EXPECT_EQ(audio.total_cues_dispatched(), 0U);
    EXPECT_FALSE(audio.last_dispatched_cue().has_value());
}

TEST(World2DStressTest, TargetD_AudioCue2D_ExtremeVolumeAndPanning_BoundaryAttenuations)
{
    cd::world2d::AudioCueSystem2D audio(nullptr);

    // 1. Extreme Volume Values
    const float kInf = std::numeric_limits<float>::infinity();
    const float kNaN = std::numeric_limits<float>::quiet_NaN();

    EXPECT_NO_THROW({
        audio.play_cue(cd::world2d::AudioCuePreset2D::kExplosion, 1e9F);
        audio.play_cue(cd::world2d::AudioCuePreset2D::kExplosion, -1e9F);
        audio.play_cue(cd::world2d::AudioCuePreset2D::kExplosion, kInf);
        audio.play_cue(cd::world2d::AudioCuePreset2D::kExplosion, kNaN);
    });
    EXPECT_EQ(audio.total_cues_dispatched(), 4U);

    // 2. Spatial Panning Clamping and Constant Power Law
    const cd::math::Vec2f listener { 0.0F, 0.0F };
    const cd::math::Vec2f far_left { -1e10F, 0.0F };
    const cd::math::Vec2f far_right { 1e10F, 0.0F };

    const auto pan_far_left = cd::world2d::compute_spatial_panning_2d(listener, 12.0F, far_left);
    EXPECT_FLOAT_EQ(pan_far_left.gain_left, 1.0F);
    EXPECT_FLOAT_EQ(pan_far_left.gain_right, 0.0F);

    const auto pan_far_right = cd::world2d::compute_spatial_panning_2d(listener, 12.0F, far_right);
    EXPECT_FLOAT_EQ(pan_far_right.gain_left, 0.0F);
    EXPECT_FLOAT_EQ(pan_far_right.gain_right, 1.0F);

    // Constant power pan L^2 + R^2 == 1.0f
    const auto pan_center = cd::audio::constant_power_pan(0.0F);
    const float power_center = pan_center.left * pan_center.left + pan_center.right * pan_center.right;
    EXPECT_NEAR(power_center, 1.0F, 1e-4F);

    const auto pan_stereo_left = cd::world2d::compute_2d_stereo_panning(listener, far_left);
    const float power_stereo_l = pan_stereo_left.left * pan_stereo_left.left + pan_stereo_left.right * pan_stereo_left.right;
    EXPECT_NEAR(power_stereo_l, 1.0F, 1e-4F);

    // Degenerate zero / negative stereo width clamping
    const auto pan_zero_width = cd::world2d::compute_spatial_panning_2d(listener, 0.0F, far_left);
    EXPECT_FLOAT_EQ(pan_zero_width.gain_left, 1.0F);
    EXPECT_FLOAT_EQ(pan_zero_width.gain_right, 0.0F);

    const auto pan_neg_width = cd::world2d::compute_spatial_panning_2d(listener, -10.0F, far_right);
    EXPECT_FLOAT_EQ(pan_neg_width.gain_left, 0.0F);
    EXPECT_FLOAT_EQ(pan_neg_width.gain_right, 1.0F);

    // 3. Distance Attenuation Boundaries
    cd::world2d::AudioSpatialParams2D params {};
    params.min_distance = 2.0F;
    params.max_distance = 20.0F;
    params.rolloff_factor = 1.0F;

    EXPECT_FLOAT_EQ(cd::world2d::compute_2d_distance_attenuation(listener, { 0.0F, 0.0F }, params), 1.0F);
    EXPECT_FLOAT_EQ(cd::world2d::compute_2d_distance_attenuation(listener, { 2.0F, 0.0F }, params), 1.0F);
    EXPECT_FLOAT_EQ(cd::world2d::compute_2d_distance_attenuation(listener, { 20.0F, 0.0F }, params), 0.0F);
    EXPECT_FLOAT_EQ(cd::world2d::compute_2d_distance_attenuation(listener, { 50.0F, 0.0F }, params), 0.0F);

    // Equal min and max distance: no division by zero crash
    params.min_distance = 10.0F;
    params.max_distance = 10.0F;
    EXPECT_FLOAT_EQ(cd::world2d::compute_2d_distance_attenuation(listener, { 5.0F, 0.0F }, params), 1.0F);
    EXPECT_FLOAT_EQ(cd::world2d::compute_2d_distance_attenuation(listener, { 15.0F, 0.0F }, params), 0.0F);
}

TEST(World2DStressTest, TargetD_AudioCue2D_PathologicalWaveformSynthesis_NegativeDurationAndZeroSampleRate)
{
    // Test negative duration clamping
    cd::world2d::RetroSynthParams2D p_neg {};
    p_neg.duration_sec = -1.0F;
    p_neg.sample_rate = 48000U;

    auto samples_neg = cd::world2d::synthesize_waveform_samples(p_neg);
    EXPECT_FALSE(samples_neg.empty());
    for (float s : samples_neg)
    {
        EXPECT_GE(s, -1.0F);
        EXPECT_LE(s, 1.0F);
        EXPECT_FALSE(std::isnan(s));
        EXPECT_FALSE(std::isinf(s));
    }

    // Test zero sample rate fallback
    cd::world2d::RetroSynthParams2D p_sr0 {};
    p_sr0.duration_sec = 0.05F;
    p_sr0.sample_rate = 0U;

    auto samples_sr0 = cd::world2d::synthesize_waveform_samples(p_sr0, 0U);
    EXPECT_FALSE(samples_sr0.empty());
    for (float s : samples_sr0)
    {
        EXPECT_GE(s, -1.0F);
        EXPECT_LE(s, 1.0F);
        EXPECT_FALSE(std::isnan(s));
        EXPECT_FALSE(std::isinf(s));
    }

    // Test zero duration
    cd::world2d::RetroSynthParams2D p_dur0 {};
    p_dur0.duration_sec = 0.0F;
    auto samples_dur0 = cd::world2d::synthesize_waveform_samples(p_dur0);
    EXPECT_FALSE(samples_dur0.empty());

    // Test all 5 waveform types with extreme configurations
    const std::array<cd::world2d::WaveformType2D, 5> waveforms = {
        cd::world2d::WaveformType2D::kSquare,
        cd::world2d::WaveformType2D::kSine,
        cd::world2d::WaveformType2D::kTriangle,
        cd::world2d::WaveformType2D::kSawtooth,
        cd::world2d::WaveformType2D::kNoise,
    };

    for (const auto wave : waveforms)
    {
        cd::world2d::RetroSynthParams2D pw {};
        pw.wave = wave;
        pw.duration_sec = -0.5F;
        pw.sample_rate = 0U;
        pw.duty_cycle = -2.0F; // Clamped to [0.05, 0.95]
        pw.vibrato_depth_hz = 50.0F;
        pw.vibrato_freq_hz = 0.0F; // Should skip vibrato without division by zero
        auto smp = cd::world2d::synthesize_waveform_samples(pw);
        EXPECT_FALSE(smp.empty());
        for (float s : smp)
        {
            EXPECT_GE(s, -1.0F);
            EXPECT_LE(s, 1.0F);
            EXPECT_FALSE(std::isnan(s));
        }
    }
}

TEST(World2DStressTest, TargetD_AudioCue2D_PathologicalWaveformSynthesis_NaNAndExtremeNoiseInputs)
{
    // Test extreme noise_mix
    cd::world2d::RetroSynthParams2D p_noise {};
    p_noise.duration_sec = 0.05F;
    p_noise.noise_mix = 100.0F;
    auto samples_noise = cd::world2d::synthesize_waveform_samples(p_noise);
    EXPECT_FALSE(samples_noise.empty());
    for (float s : samples_noise)
    {
        EXPECT_GE(s, -1.0F);
        EXPECT_LE(s, 1.0F);
        EXPECT_FALSE(std::isnan(s));
    }

    // Test NaN frequency vulnerability identification (Square vs Sine)
    cd::world2d::RetroSynthParams2D p_nan_sq {};
    p_nan_sq.wave = cd::world2d::WaveformType2D::kSquare;
    p_nan_sq.start_freq_hz = std::numeric_limits<float>::quiet_NaN();
    p_nan_sq.duration_sec = 0.02F;
    auto smp_nan_sq = cd::world2d::synthesize_waveform_samples(p_nan_sq);
    EXPECT_FALSE(smp_nan_sq.empty());
    // For square wave, (NaN < duty) evaluates to false, producing -1.0f clamped
    for (float s : smp_nan_sq)
    {
        EXPECT_GE(s, -1.0F);
        EXPECT_LE(s, 1.0F);
    }

    cd::world2d::RetroSynthParams2D p_nan_sine {};
    p_nan_sine.wave = cd::world2d::WaveformType2D::kSine;
    p_nan_sine.start_freq_hz = std::numeric_limits<float>::quiet_NaN();
    p_nan_sine.duration_sec = 0.02F;
    auto smp_nan_sine = cd::world2d::synthesize_waveform_samples(p_nan_sine);
    EXPECT_FALSE(smp_nan_sine.empty());
    // Verified: NaN start frequency is sanitized; output samples are clean finite numbers (no NaN leakage)
    bool has_nan = false;
    for (float s : smp_nan_sine)
    {
        if (std::isnan(s)) { has_nan = true; break; }
    }
    EXPECT_FALSE(has_nan) << "Verified: NaN start frequency is rejected; samples are clean finite numbers";
}

TEST(World2DStressTest, TargetD_NullAudioBackend_LifecycleAndVoiceAccumulation)
{
    auto backend = cd::audio::make_null_audio_backend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->clip_count(), 0U);
    EXPECT_EQ(backend->voice_count(), 0U);

    {
        cd::world2d::AudioCueSystem2D audio(backend.get());
        EXPECT_EQ(audio.backend(), backend.get());
        // warm_up_presets was invoked in constructor
        EXPECT_EQ(backend->clip_count(), 7U);

        // Dispatch 1,000 cues
        for (std::uint32_t i = 0U; i < 1000U; ++i)
        {
            auto v = audio.play_cue(cd::world2d::AudioCuePreset2D::kJump);
            EXPECT_FALSE(v.is_null());
        }

        EXPECT_EQ(audio.total_cues_dispatched(), 1000U);
        // Empirical observation: NullAudioBackend retains voices until stop() or destroy_clip()
        EXPECT_EQ(backend->voice_count(), 1000U);
    }

    // AudioCueSystem2D destructor destroys all 7 clips, reaping all referencing voices
    EXPECT_EQ(backend->clip_count(), 0U);
    EXPECT_EQ(backend->voice_count(), 0U);
}

TEST(World2DStressTest, TargetD_NullAudioBackend_5000ContinuousDispatches_CappedAt1024AndLruEviction)
{
    auto backend = cd::audio::make_null_audio_backend();
    ASSERT_NE(backend, nullptr);
    cd::world2d::AudioCueSystem2D audio(backend.get());

    std::vector<cd::audio::VoiceHandle> handles;
    handles.reserve(5000U);

    // Dispatch 5,000 cues continuously
    for (std::uint32_t i = 0U; i < 5000U; ++i)
    {
        auto v = audio.play_cue(cd::world2d::AudioCuePreset2D::kJump);
        EXPECT_FALSE(v.is_null());
        handles.push_back(v);
        // Invariant: active voice count must NEVER exceed 1024 at any point
        EXPECT_LE(backend->voice_count(), 1024U);
    }

    EXPECT_EQ(audio.total_cues_dispatched(), 5000U);
    // After 5,000 continuous dispatches, voice count must be exactly capped to 1024
    EXPECT_EQ(backend->voice_count(), 1024U);

    // Verify oldest voice LRU eviction:
    // The earliest 3,976 voices (indices 0..3975) must have been evicted and are no longer playing
    for (std::size_t i = 0U; i < 3976U; i += 100U)
    {
        EXPECT_FALSE(backend->is_playing(handles[i]));
    }
    EXPECT_FALSE(backend->is_playing(handles[3975U]));

    // The most recent 1,024 voices (indices 3976..4999) must be actively retained
    EXPECT_TRUE(backend->is_playing(handles[3976U]));
    for (std::size_t i = 3976U; i < 5000U; i += 100U)
    {
        EXPECT_TRUE(backend->is_playing(handles[i]));
    }
    EXPECT_TRUE(backend->is_playing(handles[4999U]));
}

// =============================================================================
// 6. Target E: CommandBus2D & JSON-RPC Recursive Depth and Undo/Redo Stress
// =============================================================================

TEST(World2DStressTest, TargetE_JsonParser_RecursiveNestingDepthLimit_StackExhaustionGuard)
{
    // 1. Boundary: Nesting depth 64 should succeed
    std::string valid_depth_64;
    for (int i = 0; i < 64; ++i) valid_depth_64 += "[";
    valid_depth_64 += "1";
    for (int i = 0; i < 64; ++i) valid_depth_64 += "]";

    auto res_64 = cd::asset::json::parse(valid_depth_64, 64);
    EXPECT_TRUE(res_64.has_value()) << "Depth 64 should be permitted by max_depth=64";

    // 2. Exceeding depth 64: Nesting depth 65 must return kDepthLimit
    std::string depth_65;
    for (int i = 0; i < 65; ++i) depth_65 += "[";
    depth_65 += "1";
    for (int i = 0; i < 65; ++i) depth_65 += "]";

    auto res_65 = cd::asset::json::parse(depth_65, 64);
    ASSERT_FALSE(res_65.has_value());
    EXPECT_EQ(res_65.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kDepthLimit));
    EXPECT_EQ(std::string_view(res_65.error().message), "nesting exceeds max_depth");

    // 3. Deep recursion attack (depth 100, 1,000, 10,000): Must not overflow C++ thread stack
    const std::array<int, 3> attack_depths = { 100, 1000, 10000 };
    for (int d : attack_depths)
    {
        // Deeply nested arrays
        std::string attack_arr;
        attack_arr.reserve(static_cast<std::size_t>(d * 2 + 10));
        for (int i = 0; i < d; ++i) attack_arr += "[";
        attack_arr += "\"leaf\"";
        for (int i = 0; i < d; ++i) attack_arr += "]";

        auto res_arr = cd::asset::json::parse(attack_arr);
        ASSERT_FALSE(res_arr.has_value());
        EXPECT_EQ(res_arr.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kDepthLimit));

        // Deeply nested objects
        std::string attack_obj;
        attack_obj.reserve(static_cast<std::size_t>(d * 6 + 10));
        for (int i = 0; i < d; ++i) attack_obj += "{\"k\":";
        attack_obj += "null";
        for (int i = 0; i < d; ++i) attack_obj += "}";

        auto res_obj = cd::asset::json::parse(attack_obj);
        ASSERT_FALSE(res_obj.has_value());
        EXPECT_EQ(res_obj.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kDepthLimit));
    }

    // 4. Pathological unclosed deep nesting
    std::string unclosed_deep(1000, '[');
    auto res_unclosed = cd::asset::json::parse(unclosed_deep);
    ASSERT_FALSE(res_unclosed.has_value());
    EXPECT_EQ(res_unclosed.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kDepthLimit));
}

TEST(World2DStressTest, TargetE_CommandBus2D_1000MalformedCommands_ZeroStatePollution)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::CommandBus2D bus {};

    // Initialize with a known baseline template
    bus.load_genre_template(stage, cd::world2d::GenreTemplateKind2D::kActionPlatformer);
    bus.clear_history();

    const std::string baseline_json = stage.save_to_json_string(false);
    const std::size_t base_entities = stage.entities().size();
    const std::size_t base_tiles = stage.tilemap().non_empty_tile_count();
    const float base_gravity = stage.gravity_y();
    const std::string_view base_name = stage.name();

    EXPECT_GT(base_entities, 5U);
    EXPECT_GT(base_tiles, 20U);
    EXPECT_EQ(bus.undo_depth(), 0U);
    EXPECT_EQ(bus.redo_depth(), 0U);

    // Blast 1,000 distinct malformed/invalid commands
    for (int i = 0; i < 1000; ++i)
    {
        std::string malformed;
        switch (i % 10)
        {
            case 0:
                // Syntax error
                malformed = "{ \"op\": \"spawn_entity_2d\", unclosed syntax ...";
                break;
            case 1:
                // Non-object root
                malformed = "[\"string_not_command_object\", 12345]";
                break;
            case 2:
                // Missing 'op' field
                malformed = "{\"name\": \"LostSprite\", \"pos\": [10, 20]}";
                break;
            case 3:
                // Unknown 'op' name
                malformed = "{\"op\": \"destroy_world_and_erase_everything_dangerous\"}";
                break;
            case 4:
                // Delete non-existent entity ID
                malformed = "{\"op\": \"delete_entity_2d\", \"id\": 9999999}";
                break;
            case 5:
                // Set field on non-existent entity
                malformed = "{\"op\": \"set_entity_field_2d\", \"id\": 888888, \"field\": \"visible\", \"value\": false}";
                break;
            case 6:
                // Set invalid schema field
                malformed = "{\"op\": \"set_entity_field_2d\", \"id\": 1, \"field\": \"totally_fake_field_name\", \"value\": 42}";
                break;
            case 7:
                // Schema type mismatch (string for Vec2f position)
                malformed = "{\"op\": \"set_entity_field_2d\", \"id\": 1, \"field\": \"position\", \"value\": \"not_a_vec2\"}";
                break;
            case 8:
                // Attach invalid Lua syntax
                malformed = "{\"op\": \"attach_lua_script_2d\", \"id\": 1, \"script\": \"function broken_lua( syntax error ) end\"}";
                break;
            case 9:
                // Unknown genre template
                malformed = "{\"op\": \"load_genre_template_2d\", \"genre\": \"non_existent_genre_99\"}";
                break;
        }

        const auto rep = bus.execute_json(stage, malformed);
        EXPECT_FALSE(rep.ok) << "Malformed command " << i << " unexpectedly succeeded: " << malformed;
        EXPECT_FALSE(rep.error_message.empty());
    }

    // Crucial verification: bus undo/redo stack remained 100% untainted
    EXPECT_EQ(bus.undo_depth(), 0U);
    EXPECT_EQ(bus.redo_depth(), 0U);

    // Crucial verification: stage state remained 100% identical
    EXPECT_EQ(stage.entities().size(), base_entities);
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), base_tiles);
    EXPECT_FLOAT_EQ(stage.gravity_y(), base_gravity);
    EXPECT_EQ(stage.name(), base_name);
    EXPECT_EQ(stage.save_to_json_string(false), baseline_json);
}

TEST(World2DStressTest, TargetE_CommandBus2D_1000ConsecutiveUndoRedoCycles_EmptyStackResilience)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::CommandBus2D bus {};

    EXPECT_EQ(bus.undo_depth(), 0U);
    EXPECT_EQ(bus.redo_depth(), 0U);

    // 1,000 consecutive undo calls on empty history: must all return false without crashing
    for (int i = 0; i < 1000; ++i)
    {
        EXPECT_FALSE(bus.undo(stage));
    }

    // 1,000 consecutive redo calls on empty history: must all return false without crashing
    for (int i = 0; i < 1000; ++i)
    {
        EXPECT_FALSE(bus.redo(stage));
    }

    EXPECT_EQ(bus.undo_depth(), 0U);
    EXPECT_EQ(bus.redo_depth(), 0U);
}

TEST(World2DStressTest, TargetE_CommandBus2D_ValidMutations_InterleavedWith1000Errors_PreservesExactHistory)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::CommandBus2D bus {};

    const std::string init_json = stage.save_to_json_string(false);

    // 1. Execute 5 valid commands
    const auto r1 = bus.execute_json(stage, "{\"op\": \"spawn_entity_2d\", \"name\": \"ValidEntity_1\", \"pos\": [1.0, 2.0]}");
    ASSERT_TRUE(r1.ok);
    const auto r2 = bus.execute_json(stage, "{\"op\": \"spawn_entity_2d\", \"name\": \"ValidEntity_2\", \"pos\": [3.0, 4.0]}");
    ASSERT_TRUE(r2.ok);
    const auto r3 = bus.execute_json(stage, "{\"op\": \"paint_rect_2d\", \"x0\": 0, \"y0\": 0, \"x1\": 3, \"y1\": 3, \"palette_id\": 2}");
    ASSERT_TRUE(r3.ok);
    const auto r4 = bus.execute_json(stage, "{\"op\": \"spawn_entity_2d\", \"name\": \"ValidEntity_3\", \"pos\": [5.0, 6.0]}");
    ASSERT_TRUE(r4.ok);
    const auto r5 = bus.execute_json(stage, "{\"op\": \"paint_tile_2d\", \"gx\": 10, \"gy\": 10, \"palette_id\": 1}");
    ASSERT_TRUE(r5.ok);

    EXPECT_EQ(bus.undo_depth(), 5U);
    EXPECT_EQ(bus.redo_depth(), 0U);
    const std::string valid_json = stage.save_to_json_string(false);

    // 2. Blast with 1,000 malformed commands
    for (int i = 0; i < 1000; ++i)
    {
        const std::string bad_cmd = "{\"op\": \"unknown_bad_command_" + std::to_string(i) + "\"}";
        const auto rep = bus.execute_json(stage, bad_cmd);
        EXPECT_FALSE(rep.ok);
    }

    // Verify undo depth is still 5 and stage is unchanged
    EXPECT_EQ(bus.undo_depth(), 5U);
    EXPECT_EQ(bus.redo_depth(), 0U);
    EXPECT_EQ(stage.save_to_json_string(false), valid_json);

    // 3. Perform 1,000 undo calls: exactly 5 succeed, 995 safely return false
    std::size_t undos_succeeded = 0;
    for (int i = 0; i < 1000; ++i)
    {
        if (bus.undo(stage))
        {
            ++undos_succeeded;
        }
    }
    EXPECT_EQ(undos_succeeded, 5U);
    EXPECT_EQ(bus.undo_depth(), 0U);
    EXPECT_EQ(bus.redo_depth(), 5U);
    EXPECT_EQ(stage.save_to_json_string(false), init_json);

    // 4. Perform 1,000 redo calls: exactly 5 succeed, 995 safely return false
    std::size_t redos_succeeded = 0;
    for (int i = 0; i < 1000; ++i)
    {
        if (bus.redo(stage))
        {
            ++redos_succeeded;
        }
    }
    EXPECT_EQ(redos_succeeded, 5U);
    EXPECT_EQ(bus.undo_depth(), 5U);
    EXPECT_EQ(bus.redo_depth(), 0U);
    EXPECT_EQ(stage.save_to_json_string(false), valid_json);
}

}  // namespace
