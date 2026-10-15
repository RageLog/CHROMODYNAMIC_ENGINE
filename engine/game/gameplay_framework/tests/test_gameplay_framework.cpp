// =============================================================================
// CHROMODYNAMIC — cd::gameplay_framework unit tests (FAZ 5 Wave 5.3)
// =============================================================================
#include <cd/gameplay_framework/VesselPilotDirector.hpp>

#include <gtest/gtest.h>

namespace
{

TEST(GameplayFrameworkWave53, StageDirectorSpawnsAvatarAtBeaconAndHumanPilotCommandsVessel)
{
    cd::element_core::Stage stage("OutpostArena");
    auto& beacon = stage.root().emplace_child<cd::gameplay_director::SpawnBeacon3D>("AlphaSpawn");
    beacon.team_tag = "Vanguard";
    beacon.priority = 10U;
    beacon.set_local_position(25.0F, 0.0F, -15.0F);

    auto& director = stage.root().emplace_child<cd::gameplay_director::StageDirector>("ArenaDirector");
    auto& ledger   = stage.root().emplace_child<cd::gameplay_director::WorldLedger>("ArenaLedger");
    auto& human    = stage.root().emplace_child<cd::gameplay_pilot::HumanPilot>("PlayerOne");
    human.record_inputs = true;

    auto* avatar = director.spawn_avatar_for_pilot(stage, human, "Vanguard");
    ASSERT_NE(avatar, nullptr);
    EXPECT_EQ(human.current_vessel(), avatar);
    EXPECT_EQ(avatar->active_pilot(), &human);
    EXPECT_FLOAT_EQ(avatar->world_position()[0], 25.0F);
    EXPECT_FLOAT_EQ(avatar->world_position()[2], -15.0F);
    EXPECT_NE(avatar->rigged_mesh(), nullptr);
    EXPECT_NE(avatar->spring_arm(), nullptr);
    EXPECT_NE(avatar->camera_lens(), nullptr);

    human.inject_move_and_look({ 1.0F, 0.0F, 0.0F }, 10.0F, -5.0F);
    EXPECT_EQ(human.recorded_frame_count(), 1U);
    stage.fixed_tick(1.0F);
    EXPECT_GT(avatar->world_position()[0], 25.0F);

    // Seamless Vessel swap: HumanPilot disembarks Avatar and embarks RoverVessel!
    auto& rover = stage.root().emplace_child<cd::gameplay_vessel::RoverVessel>("ArmoredRover");
    human.command_vessel(&rover);
    EXPECT_FALSE(avatar->is_occupied());
    EXPECT_EQ(rover.active_pilot(), &human);

    rover.set_vehicle_controls(1.0F, 15.0F, 0.0F);
    stage.fixed_tick(0.5F);
    EXPECT_GT(rover.forward_speed_mps, 0.0F);

    // SynthPilot AI takes over the unpossessed Avatar and steers toward objective
    auto& synth = stage.root().emplace_child<cd::gameplay_pilot::SynthPilot>("BotCompanion");
    synth.patrol_objective = { 100.0F, 0.0F, -15.0F };
    synth.command_vessel(avatar);
    stage.tick(0.1F);
    EXPECT_GT(avatar->pending_move_intent()[0], 0.9F);

    ledger.set_score("vanguard_tickets", 250);
    ledger.set_flag("outpost_captured", true);
    EXPECT_EQ(ledger.score("vanguard_tickets"), 250);
    EXPECT_TRUE(ledger.flag("outpost_captured"));
}

}  // namespace
