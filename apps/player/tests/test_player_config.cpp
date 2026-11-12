// =============================================================================
// CHROMODYNAMIC - apps/player/tests/test_player_config.cpp
//
// Milestone 4 - Unit tests for cd_player CLI argument parsing, 2D genre presets,
// and portable bundle configuration parity.
// =============================================================================
#include "../PlayerConfig.hpp"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>

namespace cd::player::test
{

TEST(PlayerConfigTest, DefaultValues)
{
    const char* const argv[] = { "cd_player.exe" };
    const auto cfg = parse_player_cli(1, argv);

    EXPECT_FALSE(cfg.has_error);
    EXPECT_FALSE(cfg.show_help);
    EXPECT_EQ(cfg.stage_mode, "Pure3D");
    EXPECT_EQ(cfg.genre, "showcase");
    EXPECT_FALSE(cfg.is_2d_genre());
    EXPECT_FALSE(cfg.uses_stage2d());
    EXPECT_FALSE(cfg.autopilot_2d);
}

TEST(PlayerConfigTest, GenrePlatformerFlag)
{
    const char* const argv[] = { "cd_player.exe", "--genre", "platformer" };
    const auto cfg = parse_player_cli(3, argv);

    EXPECT_FALSE(cfg.has_error);
    EXPECT_EQ(cfg.genre, "platformer");
    EXPECT_EQ(cfg.stage_mode, "Pure2D");
    EXPECT_TRUE(cfg.is_2d_genre());
    EXPECT_TRUE(cfg.uses_stage2d());
}

TEST(PlayerConfigTest, GenreRpgAndArenaVariants)
{
    {
        const char* const argv[] = { "cd_player.exe", "--genre", "rpg" };
        const auto cfg = parse_player_cli(3, argv);
        EXPECT_TRUE(cfg.is_2d_genre());
        EXPECT_EQ(cfg.stage_mode, "Pure2D");
        EXPECT_TRUE(cfg.uses_stage2d());
    }
    {
        const char* const argv[] = { "cd_player.exe", "--genre", "arena" };
        const auto cfg = parse_player_cli(3, argv);
        EXPECT_TRUE(cfg.is_2d_genre());
        EXPECT_EQ(cfg.stage_mode, "Pure2D");
        EXPECT_TRUE(cfg.uses_stage2d());
    }
    {
        const char* const argv[] = { "cd_player.exe", "--genre", "action_platformer" };
        const auto cfg = parse_player_cli(3, argv);
        EXPECT_TRUE(cfg.is_2d_genre());
        EXPECT_EQ(cfg.stage_mode, "Pure2D");
    }
}

TEST(PlayerConfigTest, AutopilotFlag)
{
    const char* const argv[] = { "cd_player.exe", "--autopilot-2d" };
    const auto cfg = parse_player_cli(2, argv);

    EXPECT_FALSE(cfg.has_error);
    EXPECT_TRUE(cfg.autopilot_2d);
}

TEST(PlayerConfigTest, PakAndStage2dPathTriggersStage2d)
{
    {
        const char* const argv[] = { "cd_player.exe", "--pak", "stage.cdpak" };
        const auto cfg = parse_player_cli(3, argv);
        EXPECT_TRUE(cfg.uses_stage2d());
        EXPECT_EQ(cfg.pak_path, "stage.cdpak");
    }
    {
        const char* const argv[] = { "cd_player.exe", "--stage2d", "world.cd2d.json" };
        const auto cfg = parse_player_cli(3, argv);
        EXPECT_TRUE(cfg.uses_stage2d());
        EXPECT_EQ(cfg.stage2d_path, "world.cd2d.json");
    }
    {
        const char* const argv[] = { "cd_player.exe", "--stage-mode", "Pure2D" };
        const auto cfg = parse_player_cli(3, argv);
        EXPECT_TRUE(cfg.uses_stage2d());
    }
}

TEST(PlayerConfigTest, HelpAndErrorHandling)
{
    {
        const char* const argv[] = { "cd_player.exe", "--help" };
        const auto cfg = parse_player_cli(2, argv);
        EXPECT_TRUE(cfg.show_help);
    }
    {
        const char* const argv[] = { "cd_player.exe", "--unknown-flag" };
        const auto cfg = parse_player_cli(2, argv);
        EXPECT_TRUE(cfg.has_error);
        EXPECT_EQ(cfg.exit_code, 1);
    }
    {
        const char* const argv[] = { "cd_player.exe", "--genre" };
        const auto cfg = parse_player_cli(2, argv);
        EXPECT_TRUE(cfg.has_error);
        EXPECT_EQ(cfg.exit_code, 1);
    }
    {
        const char* const argv[] = { "cd_player.exe", "--genre", "invalid_name" };
        const auto cfg = parse_player_cli(3, argv);
        EXPECT_TRUE(cfg.has_error);
        EXPECT_EQ(cfg.exit_code, 1);
    }
    {
        const char* const argv[] = { "cd_player.exe", "--genre", "showcase" };
        const auto cfg = parse_player_cli(3, argv);
        EXPECT_FALSE(cfg.has_error);
        EXPECT_EQ(cfg.genre, "showcase");
        EXPECT_FALSE(cfg.is_2d_genre());
    }
}

TEST(PlayerConfigTest, PongBridgesEmptyGenresAndExportedExeAutoDiscovery)
{
    for (const char* g : { "empty", "pong", "break_the_bridges" })
    {
        const char* const argv[] = { "cd_player.exe", "--genre", g };
        const auto cfg = parse_player_cli(3, argv);
        EXPECT_FALSE(cfg.has_error);
        EXPECT_TRUE(cfg.is_2d_genre());
        EXPECT_EQ(cfg.stage_mode, "Pure2D");
        EXPECT_TRUE(cfg.uses_stage2d());
    }

    const auto tmp_dir = std::filesystem::temp_directory_path() / "cd_test_pong_autodiscover";
    std::error_code ec {};
    std::filesystem::create_directories(tmp_dir, ec);
    const auto pak_file = tmp_dir / "game.cdpak";
    {
        std::ofstream out(pak_file, std::ios::binary | std::ios::trunc);
        out << "CDPK";
    }
    const std::string fake_exe = (tmp_dir / "Pong.exe").string();
    const char* const argv[] = { fake_exe.c_str(), "--windowed", "--autopilot-2d", "--frames", "30" };
    const auto cfg = parse_player_cli(5, argv);
    EXPECT_FALSE(cfg.has_error);
    EXPECT_EQ(cfg.pak_path, pak_file.string());
    EXPECT_TRUE(cfg.uses_stage2d());
    EXPECT_TRUE(cfg.autopilot_2d);
    EXPECT_EQ(cfg.frames_to_run, 30U);

    std::filesystem::remove_all(tmp_dir, ec);
}

}  // namespace cd::player::test
