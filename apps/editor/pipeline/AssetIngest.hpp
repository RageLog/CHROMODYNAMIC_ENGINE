// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/pipeline/AssetIngest.hpp
//
// Valve Source 2 resourcecompiler/resourcesystem-modeled asset ingestion and
// material/mesh binding subsystem for Studio.
// =============================================================================
#pragma once

#include "core/StudioState.hpp"

#include <string_view>

namespace cd::studio::pipeline
{

[[nodiscard]] bool import_or_assign_file(StudioEngineState& s,
                                         std::string_view os_file_path,
                                         bool assign_to_selected = false);

[[nodiscard]] bool assign_mesh_to_entity(StudioEngineState& s,
                                         int entity_idx,
                                         std::string_view asset_path);

[[nodiscard]] bool assign_texture_to_entity(StudioEngineState& s,
                                            int entity_idx,
                                            std::string_view asset_path);

void scan_and_ingest_generic_gltf_folder(StudioEngineState& s);

void destroy_ingested_assets(StudioEngineState& s) noexcept;

}  // namespace cd::studio::pipeline
