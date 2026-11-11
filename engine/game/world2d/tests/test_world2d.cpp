// =============================================================================
// CHROMODYNAMIC — test_world2d.cpp
// End-to-end unit tests for cd::world2d (Stage2D, CharacterController2D +
// PhysicsWorld2D, Sandboxed Lua 5.4 behaviors, CommandBus2D Undo/Redo + AI JSON
// protocol, PlaytestRunner2D autopilot telemetry, and .cdpak round-trip).
// =============================================================================
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/PlaytestRunner2D.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <gtest/gtest.h>

namespace
{

TEST(World2DTest, StarterStageRunsPlaytestLuaAndReachesVictoryDeterministically)
{
    cd::world2d::Stage2D stage {};
    const std::size_t    initial_count = stage.entities().size();
    ASSERT_GT(initial_count, 6U);

    const auto report = cd::world2d::run_playtest_2d(stage, 360U, true, true);
    EXPECT_GT(report.frames_simulated, 30U);
    EXPECT_GT(report.horizontal_distance_covered, 8.0F);
    EXPECT_GE(report.gems_collected, 1);
    EXPECT_EQ(report.active_lua_vms, 1U);
    EXPECT_EQ(report.lua_error_count, 0U);
    EXPECT_FALSE(report.game_over);

    // Verify `restore_stage_after = true` restored exact pre-play authoring state!
    EXPECT_FALSE(stage.is_playing());
    EXPECT_EQ(stage.entities().size(), initial_count);
}

TEST(World2DTest, CommandBusExecutesAiJsonBatchAndSupportsFullUndoRedo)
{
    cd::world2d::Stage2D      stage {};
    cd::world2d::CommandBus2D bus {};

    const std::size_t base_entities = stage.entities().size();
    const std::size_t base_tiles    = stage.tilemap().non_empty_tile_count();

    constexpr std::string_view kAiBatch = R"({
      "commands": [
        { "op": "paint_rect_2d", "x0": 30, "y0": 0, "x1": 35, "y1": 1, "palette_id": 4 },
        {
          "op": "spawn_entity_2d",
          "name": "AI_Lua_HoverOrb",
          "role": 6,
          "pos": [32.0, 4.0],
          "size": [1.5, 0.5],
          "lua_script": "function on_tick(id, x, y, vx, vy, t, dt) return x, 4.0 + math.sin(t * 2.0), 0.0, 0.0 end"
        }
      ]
    })";

    const auto rep = bus.execute_json(stage, kAiBatch);
    ASSERT_TRUE(rep.ok) << rep.error_message;
    EXPECT_EQ(rep.applied_count, 2U);
    EXPECT_EQ(stage.entities().size(), base_entities + 1U);
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), base_tiles + 12U);

    // Human presses Ctrl+Z -> entire AI batch unrolls atomically!
    ASSERT_TRUE(bus.undo(stage));
    EXPECT_EQ(stage.entities().size(), base_entities);
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), base_tiles);

    // Human presses Ctrl+Y -> AI batch reapplies cleanly!
    ASSERT_TRUE(bus.redo(stage));
    EXPECT_EQ(stage.entities().size(), base_entities + 1U);
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), base_tiles + 12U);
}

TEST(World2DTest, StandaloneCdpakArchiveRoundTripsStageLosslessly)
{
    cd::world2d::Stage2D src {};
    src.set_name("Shipped_2D_Adventure");
    const auto pak_bytes = src.build_standalone_pak_bytes();
    ASSERT_GT(pak_bytes.size(), 64U);

    cd::world2d::Stage2D dst {};
    dst.clear();
    ASSERT_TRUE(dst.load_from_pak_bytes(pak_bytes).has_value());
    EXPECT_EQ(dst.name(), "Shipped_2D_Adventure");
    EXPECT_EQ(dst.entities().size(), src.entities().size());
    EXPECT_EQ(dst.tilemap().non_empty_tile_count(), src.tilemap().non_empty_tile_count());
}

}  // namespace
