// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/scene/SceneOperations.cpp
// =============================================================================
#include "scene/SceneOperations.hpp"

#include "StudioSceneBuilder.hpp"
#include "StudioToolbar.hpp"

namespace cd::studio::scene_ops
{

namespace
{
auto make_logger(StudioEngineState& s)
{
    return [&s](std::string msg) { s.push_log(std::move(msg)); };
}
}  // namespace

void sync_camera_and_invalidate_history(StudioEngineState& s) noexcept
{
    cd::studio::sync_camera_and_invalidate_frame_history(s);
}

void init_clean_workspace(StudioEngineState& s)
{
    cd::studio::init_clean_studio_workspace(s, make_logger(s));
}

void reset_to_clean_empty_project(StudioEngineState& s, bool completely_blank)
{
    cd::studio::reset_to_clean_empty_project(s, make_logger(s), completely_blank);
}

void load_interactive_template(StudioEngineState& s, int template_id)
{
    cd::studio::load_studio_interactive_template(s, template_id, make_logger(s));
}

void load_showcase_project_by_index(StudioEngineState& s, std::size_t idx)
{
    cd::studio::load_showcase_project_by_index(s, idx, make_logger(s));
}

bool switch_to_project_level(StudioEngineState& s, std::string_view level_name)
{
    return cd::studio::switch_to_project_level(s, level_name, make_logger(s));
}

bool load_project_or_scene_from_disk(StudioEngineState& s, std::string_view disk_path)
{
    return cd::studio::load_project_or_scene_from_disk_path(s, disk_path, make_logger(s));
}

void seed_demo_context_and_scene(StudioEngineState& s)
{
    cd::studio::seed_demo_context_and_scene(s, make_logger(s));
}

void build_and_save_custom_game_level(StudioEngineState& s)
{
    cd::studio::build_and_save_custom_game_level(s, make_logger(s));
}

bool save_scene(const StudioEngineState& s, const char* path)
{
    auto& mut_s = const_cast<StudioEngineState&>(s);
    return cd::studio::save_full_studio_scene(s, path, make_logger(mut_s));
}

bool load_scene(StudioEngineState& s, const char* path)
{
    return cd::studio::load_full_studio_scene(s, path, make_logger(s));
}

bool export_standalone_package(const StudioEngineState& s, const char* export_dir)
{
    auto& mut_s = const_cast<StudioEngineState&>(s);
    return cd::studio::export_standalone_game_package(s, export_dir, make_logger(mut_s));
}

int spawn_entity_by_preset(StudioEngineState& s, std::string_view preset_id, cd::math::Vec3f world_pos)
{
    return cd::studio::spawn_entity_with_modifiers(s, preset_id, world_pos, make_logger(s));
}

int spawn_primitive_node(StudioEngineState& s,
                         const char* base_name,
                         PrimitiveKind kind,
                         cd::math::Vec3f tint,
                         bool is_pbr,
                         float metallic,
                         float roughness)
{
    cd::studio::spawn_studio_entity(s, base_name, kind, tint, is_pbr, metallic, roughness, make_logger(s));
    return s.selected;
}

int spawn_ground_floor(StudioEngineState& s)
{
    return cd::studio::spawn_ground_floor_entity(s, make_logger(s));
}

int spawn_volumetric_clouds(StudioEngineState& s)
{
    return cd::studio::spawn_volumetric_clouds_entity(s, make_logger(s));
}

int spawn_volumetric_fog(StudioEngineState& s)
{
    return cd::studio::spawn_volumetric_fog_entity(s, make_logger(s));
}

int spawn_scene_camera(StudioEngineState& s, bool is_2d)
{
    return cd::studio::spawn_scene_camera_entity(s, is_2d, make_logger(s));
}

int spawn_character_vessel(StudioEngineState& s, bool is_2d)
{
    return cd::studio::spawn_empty_character_vessel(s, is_2d, make_logger(s));
}

int spawn_light_preset(StudioEngineState& s, int light_preset_kind)
{
    return cd::studio::spawn_studio_light_by_type(s, light_preset_kind, make_logger(s));
}

void delete_light_at(StudioEngineState& s, std::size_t light_idx)
{
    if (light_idx >= s.lights.size())
    {
        return;
    }
    auto& w = studio_world();
    const std::string nm = s.lights[light_idx].name;
    s.lights.erase(s.lights.begin() + static_cast<std::ptrdiff_t>(light_idx));
    s.push_log("[light] Deleted light: " + nm);
    if (s.selected_kind == SelKind::kLight)
    {
        if (std::cmp_equal(s.selected, light_idx))
        {
            s.selected = s.lights.empty() ? -1 : std::min(s.selected, static_cast<int>(s.lights.size()) - 1);
            if (s.selected < 0)
            {
                s.selected_kind = SelKind::kEntity;
            }
        }
        else if (std::cmp_greater(s.selected, light_idx))
        {
            --s.selected;
        }
    }
    if (s.lights.empty())
    {
        s.fx.clouds_coverage    = 0.0F;
        s.fx.volumetric_fog_on  = false;
        s.fx.fog_density        = 0.0F;
        s.fx.shafts_strength    = 0.0F;
        s.fx.aerial_perspective = 0.0F;
        if (auto* sky = w.environment_modifiers.find<SkyAtmosphereModifier>())
        {
            sky->enabled         = false;
            sky->clouds_coverage = 0.0F;
        }
    }
    s.last_tlas_scene_hash = 0;
}

int spawn_catalog_construct(StudioEngineState& s,
                            std::uint16_t node_id,
                            cd::math::Vec3f world_pos,
                            bool attach_to_selected)
{
    const auto mode = attach_to_selected
        ? CatalogComposeMode::NestAsChildElement
        : CatalogComposeMode::SpawnRootElement;
    return cd::studio::spawn_or_compose_studio_catalog_node(s, node_id, mode, world_pos, make_logger(s));
}

void duplicate_selected_node(StudioEngineState& s)
{
    cd::studio::duplicate_selected(s, make_logger(s));
}

void delete_selected_node(StudioEngineState& s)
{
    cd::studio::delete_selected(s, make_logger(s));
}

void focus_selected_node(StudioEngineState& s)
{
    cd::studio::focus_selected(s, make_logger(s));
}

void snap_selected_to_ground(StudioEngineState& s)
{
    cd::studio::snap_selected_entity_to_ground(s, make_logger(s));
}

void shoot_physics_ball(StudioEngineState& s)
{
    cd::studio::shoot_physics_ball_from_camera(s, make_logger(s));
}

void apply_camera_bookmark(StudioEngineState& s,
                           cd::math::Vec3f eye,
                           cd::math::Vec3f target,
                           const char* bookmark_name)
{
    cd::studio::apply_camera_bookmark(s, eye, target, bookmark_name, make_logger(s));
}

bool open_os_file_dialog(const char* filter, std::string& out_path, const char* title)
{
    return cd::studio::open_native_os_file_dialog(filter, out_path, title);
}

}  // namespace cd::studio::scene_ops
