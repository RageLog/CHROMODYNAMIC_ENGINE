// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/core/StudioConfig.cpp
// =============================================================================
#include "core/StudioConfig.hpp"

#include "Studio2DWorkspace.hpp"
#include "StudioCliOptions.hpp"
#include "StudioDdgiPipeline.hpp"
#include "core/StudioMcpServer.hpp"

#include <cd/app_common/FileTextIo.hpp>
#include <cd/schema/Schema.hpp>
#include <cd/ui/ChromaUi.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/GenreTemplates2D.hpp>
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

        if (arg == "--help" || arg == "-h" || arg == "/?")
        {
            std::printf(
                "CHROMODYNAMIC STUDIO (Vulkan 1.3 / C++23 Hybrid 2D+3D Engine)\n"
                "Usage: editor [options]\n"
                "  --project <path>          Open a .cdproj / .cd2d.json / .cdscene.json project\n"
                "  --2d-studio               Boot directly into [2D Studio] workspace\n"
                "  --genre <empty|platformer|rpg|arena|pong|break_the_bridges>\n"
                "  --save-project-2d <dir>   Save active 2D stage as a clean on-disk .cdproj folder\n"
                "  --export-standalone <dir> Package standalone .cdpak + executable game bundle\n"
                "  --exe-name <Name.exe>     Custom executable filename for --export-standalone\n"
                "  --mcp-server [stdio|tcp]  Start AI Model Context Protocol JSON-RPC 2.0 server\n"
                "  --dump-schema <path>      Export Draft 2020-12 JSON Schema for Entity2D & CommandBus2D\n"
                "  --exec-2d-commands <json> Execute deterministic CommandBus2D JSON batch\n"
                "  --playtest-2d <frames>    Run headless 60 Hz 2D physics/gameplay oracle\n"
                "  --self-test-all           Run full Studio E2E self-test suite\n"
                "  --capture-png <path>      Capture Vulkan swapchain screenshot to PNG\n"
                "  --frames <N>              Run N frames and exit cleanly\n");
            res.early_exit_code = 0;
            return res;
        }
        else if (arg == "--ddgi-force-on")
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
        else if ((arg == "--genre" || arg == "--genre-2d") && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.initial_genre_2d = argv[++i];
            cfg.start_2d_studio  = true;
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
        else if ((arg == "--project" || arg == "--open-project") && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.project_path = argv[++i];
        }
        else if (arg == "--save-project-2d" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.save_project_2d_dir = argv[++i];
        }
        else if (arg == "--export-standalone" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.export_standalone_dir = argv[++i];
        }
        else if ((arg == "--exe-name" || arg == "--executable-name") && i + 1 < argc && argv[i + 1] != nullptr)
        {
            cfg.export_exe_name = argv[++i];
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
        else if (arg == "--mcp-server")
        {
            cfg.mcp_transport = McpTransportKind::kStdio;
            if (i + 1 < argc && argv[i + 1] != nullptr && argv[i + 1][0] != '-')
            {
                const std::string_view val { argv[i + 1] };
                if (val == "stdio")
                {
                    cfg.mcp_transport = McpTransportKind::kStdio;
                    ++i;
                }
                else if (val == "tcp")
                {
                    cfg.mcp_transport = McpTransportKind::kTcp;
                    ++i;
                }
                else
                {
                    const int port = std::atoi(argv[i + 1]);
                    if (port > 0 && port <= 65535)
                    {
                        cfg.mcp_transport = McpTransportKind::kTcp;
                        cfg.mcp_port      = static_cast<std::uint16_t>(port);
                        ++i;
                    }
                }
            }
        }
        else if (arg == "--mcp-port" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            const int port = std::atoi(argv[++i]);
            if (port > 0 && port <= 65535)
            {
                cfg.mcp_port = static_cast<std::uint16_t>(port);
                if (cfg.mcp_transport == McpTransportKind::kDisabled)
                {
                    cfg.mcp_transport = McpTransportKind::kTcp;
                }
            }
        }
    }

    // Apply initial 2D genre or on-disk 2D project before one-shot save/export/headless paths
    std::string loaded_project_json {};
    if (!cfg.initial_genre_2d.empty())
    {
        auto& s2d = studio_2d();
        if (const auto gkind = cd::world2d::parse_genre_template_kind_2d(cfg.initial_genre_2d);
            gkind.has_value())
        {
            cd::world2d::GenreTemplates2D::apply_genre_template(s2d.stage, *gkind);
            s2d.active_genre_template_idx = static_cast<int>(*gkind);
            s2d.selected_entity_id        = s2d.stage.entities().empty() ? 0U : s2d.stage.entities().front().id;
            s2d.sync_lua_buffer_from_selected();
        }
    }
    if (!cfg.project_path.empty())
    {
        if (load_2d_project_or_stage_from_disk(cfg.project_path, &loaded_project_json))
        {
            cfg.start_2d_studio                    = true;
            cd::ui::chroma::state().workspace_mode = 3;
        }
    }

    if (!cfg.save_project_2d_dir.empty() || !cfg.export_standalone_dir.empty())
    {
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

        if (!cfg.save_project_2d_dir.empty())
        {
            if (!save_2d_project_folder_to_disk(cfg.save_project_2d_dir))
            {
                std::fprintf(stderr, "chromodynamic_studio: failed to save 2D project to %s\n",
                             cfg.save_project_2d_dir.c_str());
                res.early_exit_code = 2;
                return res;
            }
            std::printf("chromodynamic_studio: saved 2D project -> %s\n", cfg.save_project_2d_dir.c_str());
            if (cfg.export_standalone_dir.empty())
            {
                res.early_exit_code = 0;
                return res;
            }
        }

        if (!cfg.export_standalone_dir.empty())
        {
            const bool ok = export_2d_and_3d_standalone_bundle(
                cfg.export_standalone_dir.c_str(),
                [](std::string_view m) {
                    std::printf("%.*s\n", static_cast<int>(m.size()), m.data());
                },
                cfg.export_exe_name,
                loaded_project_json);
            res.early_exit_code = ok ? 0 : 2;
            return res;
        }
    }

    if (cfg.headless_smoke && cfg.capture_png.empty() &&
        cfg.capture_gameplay_dir.empty() && !cd::studio_rt::golden::enabled())
    {
        if (cfg.mcp_transport != McpTransportKind::kDisabled)
        {
            if (cfg.mcp_transport == McpTransportKind::kTcp)
            {
                std::fprintf(stderr,
                             "chromodynamic_studio: error: --headless currently requires stdio transport "
                             "(use --mcp-server stdio)\n");
                res.early_exit_code = 1;
                return res;
            }
            auto& s2d = studio_2d();
            const int mcp_code = run_studio_mcp_headless_loop(s2d.stage, s2d.command_bus);
            res.early_exit_code = mcp_code;
            return res;
        }

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
