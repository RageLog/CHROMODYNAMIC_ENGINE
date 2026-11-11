// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/pipeline/AssetIngest.cpp
// =============================================================================
#include "pipeline/AssetIngest.hpp"

#include "StudioSceneAssetIngest.hpp"
#include "StudioSceneBuilder.hpp"

namespace cd::studio::pipeline
{

namespace
{
auto make_logger(StudioEngineState& s)
{
    return [&s](std::string msg) { s.push_log(std::move(msg)); };
}
}  // namespace

bool import_or_assign_file(StudioEngineState& s,
                           std::string_view os_file_path,
                           bool assign_to_selected)
{
    return cd::studio::import_or_assign_os_file(
        s, std::string(os_file_path), assign_to_selected, make_logger(s));
}

bool assign_mesh_to_entity(StudioEngineState& s,
                           int entity_idx,
                           std::string_view asset_path)
{
    return cd::studio::assign_mesh_asset_to_entity(
        s, entity_idx, std::string(asset_path), make_logger(s));
}

bool assign_texture_to_entity(StudioEngineState& s,
                              int entity_idx,
                              std::string_view asset_path)
{
    return cd::studio::assign_texture_asset_to_entity(
        s, entity_idx, std::string(asset_path), make_logger(s));
}

void scan_and_ingest_generic_gltf_folder(StudioEngineState& s)
{
    if (!s.device_owner)
    {
        return;
    }
    s.generic_ingests = cd_studio::load_generic_gltf_scenes(
        *s.device_owner, s.ecs_world, s.scene,
        [&s](std::string msg) { s.push_log(std::move(msg)); });
}

void destroy_ingested_assets(StudioEngineState& s) noexcept
{
    if (!s.device_owner)
    {
        return;
    }
    cd_studio::destroy_generic_ingests(*s.device_owner, s.generic_ingests);
}

}  // namespace cd::studio::pipeline
