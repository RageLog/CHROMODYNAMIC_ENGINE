// =============================================================================
// CHROMODYNAMIC — cd/app_common/PackageExporter.hpp
//
// Single canonical standalone `.cdpak` (`CDPK`) + `cd_player.exe` + runtime DLL
// game bundle exporter shared by `[2D World]`, `[3D World]`, and CLI `--export-standalone`.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace cd::app_common
{

struct PackageExportRequest
{
    std::string output_dir { "build/export/ChromodynamicGame" };
    std::string mode { "Pure2D" };  ///< "Pure2D", "Pure3D", or "Hybrid2D3D"
    std::string stage2d_json {};    ///< Serialized `.cd2d.json` payload (optional if Pure3D)
    std::string scene3d_json {};    ///< Serialized `.cdscene.json` payload (optional if Pure2D)
    std::string project_json {};    ///< Optional `.cdproject` metadata JSON
    bool        copy_player_binary { true };
};

struct PackageExportReport
{
    std::string pak_path {};
    std::size_t pak_size_bytes { 0U };
    bool        bundled_player_exe { false };
    std::size_t bundled_dll_count { 0U };
};

/// Build a self-contained `.cdpak` archive (`manifest.json` + stages/scenes) and optional portable `cd_player` bundle.
[[nodiscard]] cd::core::Result<PackageExportReport> export_standalone_cdpak_bundle(
    const PackageExportRequest& req);

}  // namespace cd::app_common
