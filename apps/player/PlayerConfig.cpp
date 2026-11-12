// =============================================================================
// CHROMODYNAMIC - apps/player/PlayerConfig.cpp
// =============================================================================
#include "PlayerConfig.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace cd::player
{

PlayerConfig parse_player_cli(int argc, const char* const* argv)
{
    PlayerConfig cfg {};
    bool explicit_stage_or_showcase = false;
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr)
        {
            continue;
        }
        const std::string_view arg { argv[i] };
        if (arg == "--help" || arg == "-h" || arg == "/?" || arg == "help")
        {
            std::printf("CHROMODYNAMIC Standalone Stage Player (cd_player)\n");
            std::printf("Usage: cd_player.exe [options]\n");
            std::printf("  --stage-mode <Pure2D|HD2D|Pure3D>  Stage rendering mode (default: Pure3D)\n");
            std::printf("  --genre <empty|platformer|rpg|arena|pong|break_the_bridges> 2D genre preset\n");
            std::printf("  --autopilot-2d                     Enable simulated autopilot player movement\n");
            std::printf("  --frames <N>                       Run for N frames and exit\n");
            std::printf("  --null | --headless                Run with NullDevice mock driver\n");
            std::printf("  --windowed                         Force physical windowed display\n");
            std::printf("  --capture-png <path>               Capture swapchain PNG readback\n");
            std::printf("  --all-showcases                    Run smoke pass over all showcases\n");
            std::printf("  --help, -h, /?                     Display this help text\n");
            std::fflush(stdout);
            cfg.show_help = true;
            cfg.force_headless = true;
            return cfg;
        }
        else if (arg == "--showcase")
        {
            if (i + 1 < argc && argv[i + 1] != nullptr)
            {
                cfg.showcase_slug          = argv[++i];
                explicit_stage_or_showcase = true;
            }
            else
            {
                std::fprintf(stderr, "[cd_player] Error: %.*s requires an argument.\n",
                             static_cast<int>(arg.size()), arg.data());
                std::fflush(stderr);
                cfg.has_error = true;
                cfg.exit_code = 1;
                return cfg;
            }
        }
        else if (arg == "--pak")
        {
            if (i + 1 < argc && argv[i + 1] != nullptr)
            {
                cfg.pak_path               = argv[++i];
                explicit_stage_or_showcase = true;
            }
            else
            {
                std::fprintf(stderr, "[cd_player] Error: %.*s requires an argument.\n",
                             static_cast<int>(arg.size()), arg.data());
                std::fflush(stderr);
                cfg.has_error = true;
                cfg.exit_code = 1;
                return cfg;
            }
        }
        else if (arg == "--stage2d")
        {
            if (i + 1 < argc && argv[i + 1] != nullptr)
            {
                cfg.stage2d_path           = argv[++i];
                explicit_stage_or_showcase = true;
            }
            else
            {
                std::fprintf(stderr, "[cd_player] Error: %.*s requires an argument.\n",
                             static_cast<int>(arg.size()), arg.data());
                std::fflush(stderr);
                cfg.has_error = true;
                cfg.exit_code = 1;
                return cfg;
            }
        }
        else if (arg == "--stage-mode")
        {
            if (i + 1 < argc && argv[i + 1] != nullptr)
            {
                cfg.stage_mode             = argv[++i];
                explicit_stage_or_showcase = true;
            }
            else
            {
                std::fprintf(stderr, "[cd_player] Error: %.*s requires an argument.\n",
                             static_cast<int>(arg.size()), arg.data());
                std::fflush(stderr);
                cfg.has_error = true;
                cfg.exit_code = 1;
                return cfg;
            }
        }
        else if (arg == "--genre")
        {
            if (i + 1 < argc && argv[i + 1] != nullptr)
            {
                cfg.genre                  = argv[++i];
                explicit_stage_or_showcase = true;
                if (cfg.is_2d_genre())
                {
                    cfg.stage_mode = "Pure2D";
                }
                else if (cfg.genre != "showcase")
                {
                    std::fprintf(
                        stderr,
                        "[cd_player] Error: Unknown 2D genre preset: '%s'. Valid options: empty, platformer, rpg, arena, pong, break_the_bridges.\n",
                        cfg.genre.c_str());
                    std::fflush(stderr);
                    cfg.has_error = true;
                    cfg.exit_code = 1;
                    return cfg;
                }
            }
            else
            {
                std::fprintf(stderr, "[cd_player] Error: %.*s requires an argument.\n",
                             static_cast<int>(arg.size()), arg.data());
                std::fflush(stderr);
                cfg.has_error = true;
                cfg.exit_code = 1;
                return cfg;
            }
        }
        else if (arg == "--autopilot" || arg == "--auto-play" || arg == "--autopilot-2d")
        {
            cfg.autopilot_2d = true;
        }
        else if (arg == "--all-showcases")
        {
            cfg.run_all_showcases = true;
            cfg.force_headless    = true;
        }
        else if (arg == "--null" || arg == "--headless")
        {
            cfg.force_headless = true;
        }
        else if (arg == "--windowed")
        {
            cfg.force_windowed = true;
        }
        else if (arg == "--capture-png")
        {
            if (i + 1 < argc && argv[i + 1] != nullptr)
            {
                cfg.capture_png_path = argv[++i];
                cfg.force_windowed   = true;
            }
            else
            {
                std::fprintf(stderr, "[cd_player] Error: %.*s requires an argument.\n",
                             static_cast<int>(arg.size()), arg.data());
                std::fflush(stderr);
                cfg.has_error = true;
                cfg.exit_code = 1;
                return cfg;
            }
        }
        else if (arg == "--smoke-frames")
        {
            if (i + 1 < argc && argv[i + 1] != nullptr)
            {
                const long v = std::strtol(argv[++i], nullptr, 10);
                if (v > 0)
                {
                    cfg.frames_to_run = static_cast<std::uint32_t>(v);
                }
                if (!cfg.force_windowed)
                {
                    cfg.force_headless = true;
                }
            }
            else
            {
                std::fprintf(stderr, "[cd_player] Error: %.*s requires an argument.\n",
                             static_cast<int>(arg.size()), arg.data());
                std::fflush(stderr);
                cfg.has_error = true;
                cfg.exit_code = 1;
                return cfg;
            }
        }
        else if (arg == "--frames" || arg == "--headless-frames")
        {
            if (i + 1 < argc && argv[i + 1] != nullptr)
            {
                const long v = std::strtol(argv[++i], nullptr, 10);
                if (v > 0)
                {
                    cfg.frames_to_run = static_cast<std::uint32_t>(v);
                }
            }
            else
            {
                std::fprintf(stderr, "[cd_player] Error: %.*s requires an argument.\n",
                             static_cast<int>(arg.size()), arg.data());
                std::fflush(stderr);
                cfg.has_error = true;
                cfg.exit_code = 1;
                return cfg;
            }
        }
        else if (arg.starts_with("-"))
        {
            std::fprintf(stderr, "[cd_player] Error: Unrecognized command-line flag: %.*s\n",
                         static_cast<int>(arg.size()), arg.data());
            std::fprintf(stderr, "Try 'cd_player.exe --help' for available options.\n");
            std::fflush(stderr);
            cfg.has_error = true;
            cfg.exit_code = 1;
            return cfg;
        }
    }

    // Portable standalone game auto-discovery: if launched inside an exported game directory
    // (either double-clicked with zero args OR invoked with runtime flags like --capture-png),
    // automatically boot `game.cdpak` or `game_world.cd2d.json` next to the executable.
    if (!explicit_stage_or_showcase && !cfg.run_all_showcases &&
        cfg.pak_path.empty() && cfg.stage2d_path.empty() &&
        argv != nullptr && argv[0] != nullptr)
    {
        std::error_code ec {};
        std::filesystem::path exe_dir = std::filesystem::path(argv[0]).parent_path();
        if (exe_dir.empty())
        {
            exe_dir = std::filesystem::current_path(ec);
        }
        const auto cand_pak = exe_dir / "game.cdpak";
        const auto cand_s2d = exe_dir / "game_world.cd2d.json";
        if (std::filesystem::exists(cand_pak, ec))
        {
            cfg.pak_path = cand_pak.string();
        }
        else if (std::filesystem::exists(cand_s2d, ec))
        {
            cfg.stage2d_path = cand_s2d.string();
        }
    }

    return cfg;
}

}  // namespace cd::player
