// =============================================================================
// CHROMODYNAMIC — apps/common/src/PackageExporter.cpp
// =============================================================================
#include <cd/app_common/PackageExporter.hpp>

#include <cd/app_common/FileTextIo.hpp>
#include <cd/asset/pak/Pak.hpp>

#include <array>
#include <filesystem>
#include <span>

namespace cd::app_common
{

cd::core::Result<PackageExportReport> export_standalone_cdpak_bundle(
    const PackageExportRequest& req)
{
    if (req.output_dir.empty())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kInvalidPath, "empty output_dir"));
    }

    std::error_code ec {};
    const std::filesystem::path out_dir(req.output_dir);
    std::filesystem::create_directories(out_dir, ec);

    cd::asset::pak::PakBuilder pak_builder;

    std::string manifest = "{\n"
                           "  \"engine\": \"CHROMODYNAMIC\",\n"
                           "  \"mode\": \"" + (req.mode.empty() ? std::string("Pure2D") : req.mode) + "\"";
    if (!req.stage2d_json.empty())
    {
        manifest += ",\n  \"entry_stage2d\": \"stages/main.cd2d.json\"";
    }
    if (!req.scene3d_json.empty())
    {
        manifest += ",\n  \"entry_scene3d\": \"scenes/game_world.cdscene.json\"";
    }
    manifest += "\n}\n";

    pak_builder.add("manifest.json", std::string_view { manifest });

    if (!req.stage2d_json.empty())
    {
        const auto s2d_path = (out_dir / "game_world.cd2d.json").string();
        if (auto wr = write_text_file_utf8(s2d_path, req.stage2d_json); !wr.has_value())
        {
            return std::unexpected(wr.error());
        }
        pak_builder.add("stages/main.cd2d.json", std::string_view { req.stage2d_json });
    }

    if (!req.scene3d_json.empty())
    {
        const auto s3d_path = (out_dir / "game_world.cdscene.json").string();
        if (auto wr = write_text_file_utf8(s3d_path, req.scene3d_json); !wr.has_value())
        {
            return std::unexpected(wr.error());
        }
        pak_builder.add("scenes/game_world.cdscene.json", std::string_view { req.scene3d_json });
    }

    if (!req.project_json.empty())
    {
        const auto proj_path = (out_dir / "game.cdproject").string();
        (void)write_text_file_utf8(proj_path, req.project_json);
        pak_builder.add("game.cdproject", std::string_view { req.project_json });
    }

    const auto pak_bytes = pak_builder.build();
    const std::string pak_path = (out_dir / "game.cdpak").string();
    if (auto pwr = write_binary_file(pak_path, std::span<const std::byte> { pak_bytes }); !pwr.has_value())
    {
        return std::unexpected(pwr.error());
    }

    PackageExportReport report {};
    report.pak_path       = pak_path;
    report.pak_size_bytes = pak_bytes.size();

    if (req.copy_player_binary)
    {
        const std::array<std::string_view, 4> candidate_bins {
            "build/ide-base/bin",
            "build/debug/bin",
            "bin",
            "."
        };
        for (const std::string_view bin_dir : candidate_bins)
        {
            const std::filesystem::path player_src = std::filesystem::path(bin_dir) / "cd_player.exe";
            if (!std::filesystem::exists(player_src, ec))
            {
                continue;
            }
            const std::filesystem::path player_dst = out_dir / "cd_player.exe";
            std::filesystem::copy_file(
                player_src,
                player_dst,
                std::filesystem::copy_options::overwrite_existing,
                ec);
            if (!ec)
            {
                report.bundled_player_exe = true;
                for (const auto& entry : std::filesystem::directory_iterator(bin_dir, ec))
                {
                    if (!entry.is_regular_file())
                    {
                        continue;
                    }
                    const std::string ext = entry.path().extension().string();
                    if (ext == ".dll" || ext == ".DLL")
                    {
                        std::filesystem::copy_file(
                            entry.path(),
                            out_dir / entry.path().filename(),
                            std::filesystem::copy_options::overwrite_existing,
                            ec);
                        if (!ec)
                        {
                            ++report.bundled_dll_count;
                        }
                    }
                }
                break;
            }
        }
    }

    const std::string launcher_bat = (out_dir / "Play_ChromodynamicGame.bat").string();
    const std::string bat_content =
        "@echo off\r\n"
        "echo Launching CHROMODYNAMIC Standalone Game (.cdpak)...\r\n"
        "if exist \"%~dp0cd_player.exe\" (\r\n"
        "    \"%~dp0cd_player.exe\" --pak \"%~dp0game.cdpak\"\r\n"
        ") else if exist \"%~dp0..\\..\\ide-base\\bin\\cd_player.exe\" (\r\n"
        "    \"%~dp0..\\..\\ide-base\\bin\\cd_player.exe\" --pak \"%~dp0game.cdpak\"\r\n"
        ") else (\r\n"
        "    echo cd_player.exe not found in bundle or build/ide-base/bin.\r\n"
        ")\r\n";
    (void)write_text_file_utf8(launcher_bat, bat_content);

    return report;
}

}  // namespace cd::app_common
