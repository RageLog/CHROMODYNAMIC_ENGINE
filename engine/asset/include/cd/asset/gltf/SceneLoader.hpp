// =============================================================================
// CHROMODYNAMIC — cd/asset/gltf/SceneLoader.hpp
//
// Generic, engine-ready glTF scene loader. Layer 1 of ADR-20260530:
//   .gltf / .glb → LoadedScene (POD, owning, ECS-ready).
//
// Layer 2 (cd::render::scene::ingest_gltf_scene) takes the LoadedScene and
// uploads meshes/textures to GPU + creates ECS entities. Sample/editor code
// only ever sees these two API calls — no per-asset enums, no hardcoded
// scale/rotation, no manual material binding.
//
// Why a separate header from GltfLoader.hpp:
//   GltfLoader returns `GltfScene` (the tinygltf-shaped flat intermediate).
//   SceneLoader returns `LoadedScene` (engine-shaped tree with bounds and
//   a deterministic world_xform). Old code that wants the flat form keeps
//   using load_gltf(); new generic code uses load_scene(). Both coexist.
// =============================================================================
#pragma once

#include <cd/asset/gltf/GltfLoader.hpp>
#include <cd/core/Result.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cd::asset::gltf
{

// ---- Generic scene types ---------------------------------------------------

/// Source coordinate system. glTF spec is Y-up; some Blender / 3DS exports
/// are Z-up. The loader heuristic looks at `asset.generator` plus the bbox
/// aspect to detect Z-up and apply an axis-convert as part of
/// `suggested_world_xform`.
enum class CoordSystem : std::uint8_t
{
    kYUp = 0,
    kZUp = 1,
};

/// One drawable primitive in the generic scene. Holds the engine-ready
/// vertex / index buffers PLUS per-primitive alpha + cutoff (lifted from
/// the material so the render-side material atlas can stay simple).
struct LoadedPrimitive
{
    cd::asset::gltf::GltfPrimitive       mesh {};
    std::uint32_t                        material_idx { 0U };
    cd::asset::gltf::GltfAlphaMode       alpha_mode { GltfAlphaMode::kOpaque };
    float                                alpha_cutoff { 0.5F };
};

/// A mesh = ordered list of primitives. Index into LoadedScene::meshes.
struct LoadedMesh
{
    std::string                          name;
    std::vector<LoadedPrimitive>         primitives;
};

/// A material with its PBR factors + texture indices (into LoadedScene::textures).
/// `base_color_tex` and friends are optional — `std::nullopt` means "no
/// texture, use the factor only".
struct LoadedMaterial
{
    std::string                          name;
    cd::math::Vec4f                      base_color_factor { 1.0F, 1.0F, 1.0F, 1.0F };
    std::optional<std::uint32_t>         base_color_tex;
    std::optional<std::uint32_t>         mr_tex;        ///< glTF packs: G=rough, B=metal
    std::optional<std::uint32_t>         normal_tex;
    std::optional<std::uint32_t>         emissive_tex;
    std::optional<std::uint32_t>         occlusion_tex;
    float                                metallic_factor { 1.0F };
    float                                roughness_factor { 1.0F };
    cd::math::Vec3f                      emissive_factor { 0.0F, 0.0F, 0.0F };
    cd::asset::gltf::GltfAlphaMode       alpha_mode { GltfAlphaMode::kOpaque };
    float                                alpha_cutoff { 0.5F };
    bool                                 double_sided { false };
};

/// A texture = decoded RGBA8 image + minimal sampler info. Reuses
/// GltfTexture so the loader doesn't have to translate raw bytes a second
/// time.
struct LoadedTexture
{
    cd::asset::gltf::GltfTexture         image {};
    // Future: wrap S/T, min/mag filter. tinygltf has these; v1 ignores.
};

/// One node of the scene tree. `mesh_idx` is the mesh this node draws (if
/// any); `skin_idx` ties it to a skin in LoadedScene::skins. `children` is
/// forward-only — no cycles (rejected at load).
struct LoadedNode
{
    std::string                          name;
    cd::math::Mat4f                      local_transform { cd::math::Mat4f::identity() };
    std::vector<std::uint32_t>           children;
    std::optional<std::uint32_t>         mesh_idx;
    std::optional<std::uint32_t>         skin_idx;
    cd::math::Vec3f                      bounds_local_min { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f                      bounds_local_max { 0.0F, 0.0F, 0.0F };
};

/// Generic, owning, ECS-ready scene. Everything needed to render ANY
/// glTF without per-asset code on the sample / editor side.
///
/// Invariants:
///   - All `*_idx` fields are valid indices (< vector.size()).
///   - `nodes[i].children` are forward-only (DAG = tree).
///   - `root_nodes` is non-empty when load succeeds.
///   - `bounds_world_*` is computed with `suggested_world_xform` applied.
struct LoadedScene
{
    std::vector<LoadedNode>              nodes;
    std::vector<std::uint32_t>           root_nodes;
    std::vector<LoadedMesh>              meshes;
    std::vector<LoadedMaterial>          materials;
    std::vector<LoadedTexture>           textures;
    std::vector<cd::asset::gltf::GltfSkin>      skins;
    std::vector<cd::asset::gltf::GltfAnimation> animations;

    /// Auto-detected from heuristic. glTF spec is Y-up; user overrides via
    /// IngestOptions.world_xform if the detection is wrong.
    CoordSystem                          source_coords { CoordSystem::kYUp };

    /// Engine-ready world transform: axis-convert × uniform_scale(target /
    /// max_bbox_edge). Default target extent is 5.0 m so any asset fits a
    /// "frame-selected" camera. Callers MAY override.
    cd::math::Mat4f                      suggested_world_xform { cd::math::Mat4f::identity() };

    /// World-space AABB AFTER applying suggested_world_xform.
    cd::math::Vec3f                      bounds_world_min { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f                      bounds_world_max { 0.0F, 0.0F, 0.0F };
};

// ---- API -------------------------------------------------------------------

/// Load a glTF file as a LoadedScene. Returns `std::expected`-style error.
/// The returned scene is fully owning — caller may free the source file
/// after this returns.
///
/// Error domain reuses `cd::asset::gltf::gltf_errors`:
///   kFileNotFound, kParseFailed, kUnsupportedAccessor, kInvalidArgument
///
/// Heuristics applied:
///   1. Auto-detect Z-up from bbox aspect (height/width > 1.8 and
///      bbox_min.z near zero → likely Z-up).
///   2. `suggested_world_xform` = axis_convert × scale(5.0 / max_edge).
///      max_edge clamped to [0.01, 1e6] to handle degenerate / huge assets.
[[nodiscard]] cd::core::Result<LoadedScene>
load_scene(std::string_view glb_or_gltf_path);

/// In-memory variant — same semantics as `load_scene`, no disk read.
/// `base_dir` resolves external texture / .bin URIs; pass {} to disallow.
[[nodiscard]] cd::core::Result<LoadedScene>
load_scene_from_memory(const std::uint8_t* bytes,
                       std::size_t          size,
                       std::string_view     base_dir = {});

}  // namespace cd::asset::gltf
