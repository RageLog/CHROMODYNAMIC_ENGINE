// =============================================================================
// CHROMODYNAMIC — cd/render/scene/SceneIngest.hpp
//
// Layer 2 of the generic glTF load pipeline (ADR-20260530 P1.2). Takes a
// `cd::asset::gltf::LoadedScene` (CPU-only intermediate) and lands it on
// the GPU + into the ECS:
//
//   * Each LoadedMesh.primitive → cd::render::GpuMesh (vb + ib upload).
//   * Each LoadedTexture        → cd::render::GpuTexture2D (RGBA8 upload).
//   * Each LoadedNode           → cd::ecs::Entity + cd::scene::LocalTransform,
//                                 wired into the cd::scene::Scene tree via
//                                 attach() for non-root nodes.
//
// The function is one-shot: it submits its own command buffers, waits for
// completion, and returns an `IngestResult` listing every entity and GPU
// resource it allocated. The caller takes ownership of everything in the
// result — call `destroy_*` on each handle at shutdown.
//
// Why a separate sub-library:
//   The umbrella `cd::render` doesn't depend on cd::ecs or cd::scene. This
//   ingest path needs both. Keeping it in its own static library avoids
//   forcing every render-tier consumer to drag the world+scene tree in.
// =============================================================================
#pragma once

#include <cd/asset/gltf/SceneLoader.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/render/MeshUpload.hpp>
#include <cd/render/TextureUpload.hpp>

#include <cstdint>
#include <vector>

namespace cd::ecs   { class World; }
namespace cd::rhi   { class IDevice; }
namespace cd::scene { class Scene;  }

namespace cd::render::scene
{

/// Caller-tunable ingestion knobs. Defaults match the "drag-drop a glTF
/// into the editor and have it look reasonable" goal described in the ADR.
struct IngestOptions
{
    /// Override the world transform applied to the asset's root. When
    /// `use_suggested_xform` is true (default) this value is ignored.
    cd::math::Mat4f world_xform { cd::math::Mat4f::identity() };

    /// Use the asset's `LoadedScene::suggested_world_xform` (default).
    /// Set false to take full control via `world_xform`.
    bool            use_suggested_xform { true };

    /// Create ECS entities for every LoadedNode. Set false to upload only
    /// GPU resources (callers that drive their own scene tree).
    bool            create_ecs_nodes { true };

    /// Upload every LoadedTexture to the GPU. Set false to skip texture
    /// uploads (useful for headless / depth-only paths).
    bool            upload_textures { true };
};

/// Output: handles for every GPU resource and ECS entity the ingest created.
struct IngestResult
{
    /// Synthetic wrapper root above the asset's actual roots. All
    /// LoadedScene::root_nodes are attached as children of this entity so
    /// the caller can manipulate the whole asset through one handle.
    /// Invalid when `IngestOptions::create_ecs_nodes == false`.
    cd::ecs::Entity                root_entity {};

    /// Index-aligned with `LoadedScene::nodes`. Each entry is the ECS
    /// entity backing that node. Empty when create_ecs_nodes is false.
    std::vector<cd::ecs::Entity>   node_entities;

    /// Index-aligned with `LoadedScene::meshes`. Each entry holds one
    /// GpuMesh per primitive of the source mesh.
    std::vector<std::vector<cd::render::GpuMesh>> gpu_meshes;

    /// Index-aligned with `LoadedScene::textures`. Empty when
    /// upload_textures is false.
    std::vector<cd::render::GpuTexture2D> gpu_textures;

    /// World-space AABB AFTER applying world_xform (= what got written into
    /// the bounds_world_* fields of every Renderable). Useful for the
    /// caller's "frame selected" camera helper.
    cd::math::Vec3f                bounds_world_min { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f                bounds_world_max { 0.0F, 0.0F, 0.0F };
};

/// Ingest a LoadedScene into device + world + scene. Returns an
/// IngestResult or a `cd::core::ErrorCode` on failure (currently: device
/// allocation error during mesh / texture upload).
///
/// Strong-exception guarantee: on failure, every GPU resource allocated
/// up to that point is destroyed and every ECS entity created up to that
/// point is destroyed. The caller's `world` / `scene` / `device` are
/// observed in the same state as before the call.
[[nodiscard]] cd::core::Result<IngestResult>
ingest_gltf_scene(cd::rhi::IDevice&                          device,
                  cd::ecs::World&                            world,
                  cd::scene::Scene&                          scene,
                  const cd::asset::gltf::LoadedScene&        src,
                  IngestOptions                              opts = {});

/// Release every GPU resource owned by an IngestResult. Safe to call
/// multiple times; entries are reset. Does NOT destroy ECS entities — the
/// caller is expected to call `Scene::destroy_node(result.root_entity)`
/// explicitly, mirroring create_node() symmetry.
void destroy_ingest_result(cd::rhi::IDevice& device, IngestResult& r) noexcept;

}  // namespace cd::render::scene
