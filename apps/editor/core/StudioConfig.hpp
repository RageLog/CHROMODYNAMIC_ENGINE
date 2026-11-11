// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/core/StudioConfig.hpp
//
// Explicit value-type configuration replacing the 20 file-static g_studio_*
// globals previously scattered at the top of apps/editor/main.cpp (ADR-20261112).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace cd::studio
{

struct StudioConfig
{
    std::string     capture_png          {};
    std::uint32_t   capture_frame        { 8U };
    std::uint32_t   max_frames           { 0U };
    bool            start_pie            { false };
    bool            autopilot_e2e        { false };
    bool            force_project_manager{ false };
    bool            self_test_all        { false };
    std::string     capture_gameplay_dir {};
    bool            demo_all_phases      { false };
    bool            load_context_demo    { false };
    int             initial_template     { -1 };
    std::string     initial_scene        {};
    bool            start_2d_studio      { false };
    std::string     exec_2d_cmds         {};
    std::uint32_t   playtest_2d_frames   { 0U };
    std::string     telemetry_out_path   {};
    bool            has_cli_cam_eye      { false };
    cd::math::Vec3f cli_cam_eye          {};
    bool            has_cli_cam_target   { false };
    cd::math::Vec3f cli_cam_target       {};
    bool            headless_smoke       { false };
};

struct CliParseResult
{
    StudioConfig       config          {};
    std::optional<int> early_exit_code {};
};

/// Parses editor CLI flags, configures golden capture options, and executes
/// headless / schema-dump / standalone-export one-shot actions when requested.
[[nodiscard]] CliParseResult parse_and_run_early_cli(int argc, char** argv);

}  // namespace cd::studio
