// =============================================================================
// HelloGenericGltf.hpp
// -----------------------------------------------------------------------------
// phase508-generic-gltf-path -- 3rd "drag-drop arbitrary glTF" load path for
// hello_engine.
//
//   USER-FACING PATH: drop any .gltf / .glb into
//
//       <repo>/assets/samples/Generic/             (preferred runtime path)
//       <repo>/samples/engine/hello_engine/assets/Generic/   (source mirror)
//
//   and the next launch will pick it up and ingest it through the brand-new
//   generic pipeline -- with NO code changes anywhere in this sample.
//
//   * Layer 1:  cd::asset::gltf::load_scene        -> LoadedScene
//                  (CPU-only intermediate; deterministic Y/Z-up detection;
//                   suggested_world_xform that auto-fits to a 5 m frame).
//
//   * Layer 2:  cd::render::scene::ingest_gltf_scene
//                  -> per-primitive cd::render::GpuMesh uploads
//                  -> per-texture  cd::render::GpuTexture2D uploads
//                  -> cd::ecs::Entity + cd::scene::LocalTransform per node
//                  -> wires the parent/child chain through Scene::attach()
//                  -> a synthetic root entity above LoadedScene::root_nodes
//                     so the caller has ONE handle to manipulate the asset.
//
// SCOPE (intentionally PARTIAL):
//   This phase ADDS the third load path alongside the legacy Sponza
//   (try_auto_load_gltf) and CesiumMan (try_load_cesiumman_gltf) paths
//   from HelloGltf.hpp. The legacy paths remain the load+render route
//   for those two specific assets -- they own their per-prim PBR material
//   plumbing (Sponza's 28-material per-draw descriptor sets, CesiumMan's
//   skin animation, the multi-geometry BLAS, etc.) and migrating those is
//   tracked separately as the full P1.3 work (multi-week).
//
//   What runs here END-TO-END:
//     - load_scene parses + validates the .gltf
//     - ingest_gltf_scene creates ECS entities, uploads GPU vertex / index /
//       texture buffers, and computes the world-space AABB
//     - The IngestResult is retained for shutdown so destroy_ingest_result
//       frees every handle cleanly
//     - A boot-time log line announces what was loaded for each file
//
//   What is DEFERRED to the full migration (P1.3 close-out):
//     - Visible rasterised draws of the generic ingest meshes. The shared
//       material pipeline (`HelloMaterials::prim`) uses a
//       cd::asset::PrimitiveVertex layout (pos + normal + uv + color, stride
//       44 B); the GpuMesh produced by ingest_gltf_scene uses a
//       cd::asset::gltf::GltfVertex layout (pos + normal + uv, stride 32 B).
//       Binding the ingest GpuMesh under the existing pipeline would
//       mis-read color from the next-vertex's position bytes. A dedicated
//       PSO with the 32 B layout (or a small CPU-side vertex re-pack) is
//       the natural follow-up.
//     - Dropping PrimitiveKind::kSponza, removing try_auto_load_gltf /
//       try_load_cesiumman_gltf, and routing those two assets through the
//       same generic path. That migration includes Sponza's per-prim
//       descriptor set chain (28 textures) and CesiumMan's skinned-runtime
//       hand-off, which are large bodies of code beyond this partial.
//
// What this header gives the call site:
//   * cd_sample::GenericIngest                  -- per-asset result bundle
//   * cd_sample::load_generic_gltf_scenes(...)  -- scans + ingests + logs
//   * cd_sample::destroy_generic_ingests(...)   -- frees every handle
//
// Lifted out of main() to keep the boot orchestrator readable.
// =============================================================================
#pragma once

#include <cd/asset/gltf/SceneLoader.hpp>
#include <cd/ecs/World.hpp>
#include <cd/render/scene/SceneIngest.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/scene/Scene.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cd_sample {

/// One ingested generic glTF -- carries the source path (for logs) plus the
/// fully-populated IngestResult so the shutdown path can free every GPU
/// handle. The IngestResult also owns the synthetic root entity, the
/// per-node entity list, and the world-space AABB the loader auto-computed.
struct GenericIngest
{
    std::string                          source_path {};
    cd::render::scene::IngestResult      result      {};
};

/// Scan a small candidate list of "drop your .gltf here" directories and
/// ingest every .gltf / .glb file found via load_scene + ingest_gltf_scene.
///
/// Returns a vector with one entry per successfully-ingested file. Empty
/// vector when no Generic assets directory exists or contains files.
/// Each entry already represents:
///   * GPU vertex / index buffers uploaded (per LoadedPrimitive)
///   * GPU RGBA8 textures uploaded     (per LoadedTexture)
///   * ECS entities created + parent/child wired through Scene::attach()
///   * Root synthetic entity carrying suggested_world_xform
///
/// The function logs each ingest result via `log_push` so the boot panel /
/// console makes the asset discovery visible without inspecting the
/// returned vector.
///
/// Failure handling: any per-file failure (load_scene -> std::unexpected
/// or ingest_gltf_scene -> std::unexpected) is logged and the file is
/// skipped. Other candidate files keep being scanned -- no exception or
/// hard exit.
[[nodiscard]] inline std::vector<GenericIngest>
load_generic_gltf_scenes(cd::rhi::IDevice&                       device,
                         cd::ecs::World&                         world,
                         cd::scene::Scene&                       scene,
                         const std::function<void(std::string)>& log_push)
{
    std::vector<GenericIngest> out;

    // Mirror the same kind of candidate list `try_auto_load_gltf` uses --
    // try the binary-relative path first, then a few common
    // build-tree-to-repo offsets, then an absolute fallback. The directory
    // is OPTIONAL: when none of the candidates exists, we just return
    // empty and the rest of the sample keeps working with Sponza +
    // CesiumMan only.
    const std::array<std::string, 6> kCandidateDirs {
        "assets/samples/Generic",
        "../../../../assets/samples/Generic",
        "../../assets/samples/Generic",
        "../../../assets/samples/Generic",
        "C:/UserFiles/Project/CHROMODYNAMIC_ENGINE/assets/samples/Generic",
        "Generic",
    };

    std::filesystem::path chosen;
    for (const auto& cand : kCandidateDirs)
    {
        std::error_code ec;
        const std::filesystem::path p { cand };
        if (std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec))
        {
            chosen = p;
            break;
        }
    }
    if (chosen.empty())
    {
        log_push("[generic-gltf] no assets/samples/Generic/ directory found -- skipping");
        return out;
    }

    log_push(std::string { "[generic-gltf] scanning " } + chosen.string());

    // Stable iteration order across runs (filesystem iterators are not
    // ordered on all platforms). Sort by filename so repeated launches log
    // and ingest the same assets in the same order -- helps the user
    // correlate logs with files on disk.
    std::vector<std::filesystem::path> files;
    {
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator { chosen, ec };
             !ec && it != std::filesystem::directory_iterator {}; ++it)
        {
            if (!it->is_regular_file(ec))
                continue;
            const auto ext = it->path().extension().string();
            if (ext == ".gltf" || ext == ".glb" ||
                ext == ".GLTF" || ext == ".GLB")
            {
                files.push_back(it->path());
            }
        }
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.filename() < b.filename(); });

    if (files.empty())
    {
        log_push("[generic-gltf] directory exists but contains no .gltf / .glb files");
        return out;
    }

    for (const auto& file : files)
    {
        const std::string file_str = file.string();

        // ---- Layer 1: parse the file into the engine-shaped LoadedScene.
        auto loaded_r = cd::asset::gltf::load_scene(file_str);
        if (!loaded_r.has_value())
        {
            log_push(std::string { "[generic-gltf] load_scene FAILED for " } + file_str);
            std::fprintf(stderr, "[generic-gltf] load_scene FAILED for %s\n",
                         file_str.c_str());
            continue;
        }
        const auto& loaded = *loaded_r;

        // ---- Layer 2: GPU upload + ECS entity creation through the
        // generic ingest path. Default IngestOptions:
        //   use_suggested_xform = true   -> auto-fit to ~5 m frame
        //   create_ecs_nodes    = true   -> root + node_entities populated
        //   upload_textures     = true   -> RGBA8 textures hit the GPU
        // No per-asset code path -- exactly the design goal.
        auto ingest_r = cd::render::scene::ingest_gltf_scene(
            device, world, scene, loaded);
        if (!ingest_r.has_value())
        {
            log_push(std::string { "[generic-gltf] ingest_gltf_scene FAILED for " } + file_str);
            std::fprintf(stderr, "[generic-gltf] ingest_gltf_scene FAILED for %s\n",
                         file_str.c_str());
            continue;
        }

        GenericIngest g;
        g.source_path = file_str;
        g.result      = std::move(*ingest_r);

        // Summarise for the log so the user can see what landed without
        // walking the IngestResult fields manually.
        std::size_t mesh_primitives = 0;
        for (const auto& per_mesh : g.result.gpu_meshes)
            mesh_primitives += per_mesh.size();

        std::string summary = std::string { "[generic-gltf] loaded " } + file_str
            + " (" + std::to_string(g.result.node_entities.size()) + " nodes, "
            + std::to_string(mesh_primitives) + " primitives, "
            + std::to_string(g.result.gpu_textures.size()) + " textures)";
        log_push(summary);
        std::fprintf(stderr, "%s\n", summary.c_str());

        out.push_back(std::move(g));
    }

    return out;
}

/// Tear down every GPU resource owned by the generic ingest list. Safe to
/// call multiple times; each entry's IngestResult is reset to empty so a
/// subsequent call is a no-op. Mirrors the on_shutdown contract used by
/// the other GPU-resource holders in EngineState.
///
/// ECS entities created by the ingest stay alive: the convention from
/// SceneIngest.hpp is that the caller calls `Scene::destroy_node` on the
/// root entity if they want to remove the asset from the world. We do not
/// do that on shutdown because the whole World is torn down moments later
/// anyway, and `destroy_node` is not noexcept.
inline void destroy_generic_ingests(cd::rhi::IDevice&            device,
                                    std::vector<GenericIngest>&  ingests) noexcept
{
    for (auto& g : ingests)
        cd::render::scene::destroy_ingest_result(device, g.result);
    ingests.clear();
}

}  // namespace cd_sample
