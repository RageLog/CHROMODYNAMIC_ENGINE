// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/core/StudioConfig.cpp
// =============================================================================
#include "core/StudioConfig.hpp"

#include "Studio2DWorkspace.hpp"
#include "StudioCliOptions.hpp"
#include "StudioDdgiPipeline.hpp"

#include <cd/app_common/FileTextIo.hpp>
#include <cd/schema/Schema.hpp>
#include <cd/ui/ChromaUi.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/PlaytestRunner2D.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace cd::studio
{

CliParseResult parse_and_run_early_cli(int argc, char** argv)
{
    CliParseResult res {};
    StudioConfig&  cfg = res.config;

    (void)cd::studio_rt::golden::parse(argc, argv);

    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr) continue;
        const std::string_view arg { argv[i] };

        if (arg == "--ddgi-force-on")
        {
            cd_studio::g_ddgi_force_on = true;
        }
        else if (arg == "--pie-play")
        {
            cfg.start_pie = true;
        }
        else if (arg == "--autopilot" || arg == "--auto-play" || arg == "--play-game")
        {
            cfg.autopilot_e2e = true;
        }
        else if (arg == "--capture-gameplay-dir" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.autopilot_e2e        = true;
            cfg.capture_gameplay_dir = argv[++i];
            if (cfg.max_frames == 0U) cfg.max_frames = 110U;
        }
        else if (arg == "--demo-all-phases")
        {
            cfg.demo_all_phases = true;
        }
        else if (arg == "--load-context-demo")
        {
            cfg.load_context_demo = true;
        }
        else if (arg == "--project-manager")
        {
            cfg.force_project_manager = true;
        }
        else if (arg == "--self-test-all" || arg == "--self-test")
        {
            cfg.self_test_all = true;
            if (cfg.max_frames == 0U) cfg.max_frames = 12U;
        }
        else if ((arg == "--capture-window-png" || arg == "--studio-capture-png" ||
                  arg == "--capture-png" || arg == "--screenshot") &&
                 i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.capture_png = argv[++i];
        }
        else if ((arg == "--frames" || arg == "--max-frames" || arg == "--frame-count" ||
                  arg == "--studio-capture-frame" || arg == "--capture-frame") &&
                 i + 1 < argc && argv[i + 1] != nullptr)
        {
            const int f = std::atoi(argv[++i]);
            if (f > 0)
            {
                cfg.capture_frame = static_cast<std::uint32_t>(f);
                cfg.max_frames    = static_cast<std::uint32_t>(f) + 4U;
            }
        }
        else if (arg == "--ui-mode" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            const int m = std::atoi(argv[++i]);
            cd::ui::chroma::state().workspace_mode = std::clamp(m, 0, 3);
            if (cd::ui::chroma::state().workspace_mode == 3)
            {
                cfg.start_2d_studio = true;
            }
        }
        else if (arg == "--2d-studio" || arg == "--studio-2d")
        {
            cfg.start_2d_studio = true;
            cd::ui::chroma::state().workspace_mode = 3;
        }
        else if (arg == "--dump-schema" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            const char* out_path = argv[++i];
            cd::schema::SchemaRegistry reg;
            cd::world2d::register_world2d_schemas(reg);
            if (!cd::app_common::write_text_file_utf8(out_path, reg.export_json_schema_draft2020()))
            {
                std::fprintf(stderr, "chromodynamic_studio: failed to write schema to %s\n", out_path);
                res.early_exit_code = 2;
                return res;
            }
            std::printf("chromodynamic_studio: exported Draft 2020-12 JSON Schema -> %s\n", out_path);
            res.early_exit_code = 0;
            return res;
        }
        else if (arg == "--exec-2d-commands" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.start_2d_studio = true;
            cfg.exec_2d_cmds    = argv[++i];
        }
        else if (arg == "--playtest-2d" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.start_2d_studio    = true;
            const int pf           = std::atoi(argv[++i]);
            cfg.playtest_2d_frames = static_cast<std::uint32_t>(std::max(1, pf));
        }
        else if (arg == "--telemetry-out" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.telemetry_out_path = argv[++i];
        }
        else if (arg == "--export-standalone" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            const char* exp_dir = argv[++i];
            if (!cfg.exec_2d_cmds.empty())
            {
                std::string payload = cfg.exec_2d_cmds;
                if (auto file_txt = cd::app_common::read_text_file_utf8(cfg.exec_2d_cmds);
                    file_txt.has_value())
                {
                    payload = std::move(*file_txt);
                }
                auto& s2d = studio_2d();
                (void)s2d.command_bus.execute_json(s2d.stage, payload);
            }
            const bool ok = export_2d_and_3d_standalone_bundle(
                exp_dir,
                [](std::string_view m) {
                    std::printf("%.*s\n", static_cast<int>(m.size()), m.data());
                });
            res.early_exit_code = ok ? 0 : 2;
            return res;
        }
        else if (arg == "--theme" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            const int t = std::atoi(argv[++i]);
            ImGui::ApplyTheme(t);
        }
        else if ((arg == "--template" || arg == "--studio-template" ||
                  arg == "--level" || arg == "--studio-level") &&
                 i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.initial_template = std::clamp(std::atoi(argv[++i]), 0, 20);
        }
        else if ((arg == "--scene" || arg == "--load-scene") &&
                 i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.initial_scene = argv[++i];
        }
        else if (arg == "--cam-eye" && i + 3 < argc &&
                 argv[i + 1] != nullptr && argv[i + 2] != nullptr && argv[i + 3] != nullptr)
        {
            cfg.has_cli_cam_eye = true;
            cfg.cli_cam_eye.x   = static_cast<float>(std::atof(argv[++i]));
            cfg.cli_cam_eye.y   = static_cast<float>(std::atof(argv[++i]));
            cfg.cli_cam_eye.z   = static_cast<float>(std::atof(argv[++i]));
        }
        else if (arg == "--cam-target" && i + 3 < argc &&
                 argv[i + 1] != nullptr && argv[i + 2] != nullptr && argv[i + 3] != nullptr)
        {
            cfg.has_cli_cam_target = true;
            cfg.cli_cam_target.x   = static_cast<float>(std::atof(argv[++i]));
            cfg.cli_cam_target.y   = static_cast<float>(std::atof(argv[++i]));
            cfg.cli_cam_target.z   = static_cast<float>(std::atof(argv[++i]));
        }
        else if (arg == "--headless" || arg == "--null")
        {
            cfg.headless_smoke = true;
        }
    }

    if (cfg.headless_smoke && cfg.capture_png.empty() &&
        cfg.capture_gameplay_dir.empty() && !cd::studio_rt::golden::enabled())
    {
        if (cfg.start_2d_studio)
        {
            auto& s2d = studio_2d();
            if (!cfg.exec_2d_cmds.empty())
            {
                std::string payload = cfg.exec_2d_cmds;
                if (auto file_txt = cd::app_common::read_text_file_utf8(cfg.exec_2d_cmds);
                    file_txt.has_value())
                {
                    payload = std::move(*file_txt);
                }
                const auto cr = s2d.command_bus.execute_json(s2d.stage, payload);
                if (!cr.ok)
                {
                    std::fprintf(stderr, "chromodynamic_studio: CommandBus2D error: %s\n",
                                 cr.error_message.c_str());
                    res.early_exit_code = 2;
                    return res;
                }
            }
            if (cfg.playtest_2d_frames > 0U)
            {
                const auto rep = cd::world2d::run_playtest_2d(
                    s2d.stage, cfg.playtest_2d_frames, true, true);
                if (!cfg.telemetry_out_path.empty())
                {
                    (void)cd::app_common::write_text_file_utf8(
                        cfg.telemetry_out_path, rep.to_json_string());
                }
                std::printf(
                    "chromodynamic_studio: headless 2D playtest pass (frames=%u, gems=%d/%d, victory=%d)\n",
                    rep.frames_simulated, rep.gems_collected, rep.total_gems, rep.victory ? 1 : 0);
                res.early_exit_code = 0;
                return res;
            }
        }
        std::printf("chromodynamic_studio: headless smoke pass (PIE=%d)\n",
                    cfg.start_pie ? 1 : 0);
        res.early_exit_code = 0;
        return res;
    }

    return res;
}

}  // namespace cd::studio
