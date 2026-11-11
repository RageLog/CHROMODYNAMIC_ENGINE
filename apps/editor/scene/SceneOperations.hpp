// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/scene/SceneOperations.hpp
//
// Valve Source 2 toolframework2-modeled concrete scene authoring subsystem:
//   * Clean workspace & project initialization
//   * Interactive template & showcase loading
//   * .cdscene.json serialization/deserialization & .cdpak export
//   * 3D primitive, light, camera, environment, and 250-construct spawning
//   * Duplicate, Delete, Focus, Snap-to-Ground, and Camera Bookmark operations
// =============================================================================
#pragma once

#include "core/StudioState.hpp"

#include <cd/math/Vector.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace cd::studio::scene_ops
{

void sync_camera_and_invalidate_history(StudioEngineState& s) noexcept;

void init_clean_workspace(StudioEngineState& s);

void reset_to_clean_empty_project(StudioEngineState& s, bool completely_blank = false);

void load_interactive_template(StudioEngineState& s, int template_id);

void load_showcase_project_by_index(StudioEngineState& s, std::size_t idx);

[[nodiscard]] bool switch_to_project_level(StudioEngineState& s, std::string_view level_name);

[[nodiscard]] bool load_project_or_scene_from_disk(StudioEngineState& s, std::string_view disk_path);

void seed_demo_context_and_scene(StudioEngineState& s);

void build_and_save_custom_game_level(StudioEngineState& s);

[[nodiscard]] bool save_scene(const StudioEngineState& s, const char* path);

[[nodiscard]] bool load_scene(StudioEngineState& s, const char* path);

[[nodiscard]] bool export_standalone_package(const StudioEngineState& s, const char* export_dir);

int spawn_entity_by_preset(StudioEngineState& s, std::string_view preset_id, cd::math::Vec3f world_pos);

int spawn_primitive_node(StudioEngineState& s,
                         const char* base_name,
                         PrimitiveKind kind,
                         cd::math::Vec3f tint,
                         bool is_pbr,
                         float metallic,
                         float roughness);

int spawn_ground_floor(StudioEngineState& s);

int spawn_volumetric_clouds(StudioEngineState& s);

int spawn_volumetric_fog(StudioEngineState& s);

int spawn_scene_camera(StudioEngineState& s, bool is_2d);

int spawn_character_vessel(StudioEngineState& s, bool is_2d);

int spawn_light_preset(StudioEngineState& s, int light_preset_kind);

void delete_light_at(StudioEngineState& s, std::size_t light_idx);

int spawn_catalog_construct(StudioEngineState& s,
                            std::uint16_t node_id,
                            cd::math::Vec3f world_pos,
                            bool attach_to_selected = false);

void duplicate_selected_node(StudioEngineState& s);

void delete_selected_node(StudioEngineState& s);

void focus_selected_node(StudioEngineState& s);

void snap_selected_to_ground(StudioEngineState& s);

void shoot_physics_ball(StudioEngineState& s);

void apply_camera_bookmark(StudioEngineState& s,
                           cd::math::Vec3f eye,
                           cd::math::Vec3f target,
                           const char* bookmark_name);

[[nodiscard]] bool open_os_file_dialog(const char* filter,
                                       std::string& out_path,
                                       const char* title = "Select File");

}  // namespace cd::studio::scene_ops
