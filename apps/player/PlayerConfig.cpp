// =============================================================================
// CHROMODYNAMIC — apps/player/PlayerConfig.cpp
// =============================================================================
#include "PlayerConfig.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace cd::player
{

PlayerConfig parse_player_cli(int argc, const char* const* argv)
{
    PlayerConfig cfg {};
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr)
        {
            continue;
        }
        const std::string_view arg { argv[i] };
        if (arg == "--showcase" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.showcase_slug = argv[++i];
        }
        else if (arg == "--pak" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.pak_path = argv[++i];
        }
        else if (arg == "--stage2d" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.stage2d_path = argv[++i];
        }
        else if (arg == "--autopilot" || arg == "--auto-play")
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
        else if (arg == "--capture-png" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.capture_png_path = argv[++i];
            cfg.force_windowed   = true;
        }
        else if (arg == "--smoke-frames" && i + 1 < argc && argv[i + 1] != nullptr)
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
        else if ((arg == "--frames" || arg == "--headless-frames") && i + 1 < argc && argv[i + 1] != nullptr)
        {
            const long v = std::strtol(argv[++i], nullptr, 10);
            if (v > 0)
            {
                cfg.frames_to_run = static_cast<std::uint32_t>(v);
            }
        }
    }

    // Portable double-click auto-discovery: if launched with no arguments inside an exported
    // game directory, automatically boot `game.cdpak` or `game_world.cd2d.json` next to `cd_player.exe`.
    if (argc <= 1 && cfg.pak_path.empty() && cfg.stage2d_path.empty() &&
        argv != nullptr && argv[0] != nullptr)
    {
        std::error_code ec {};
        const auto exe_dir  = std::filesystem::path(argv[0]).parent_path();
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
