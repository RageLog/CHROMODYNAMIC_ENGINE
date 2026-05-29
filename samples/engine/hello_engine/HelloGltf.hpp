// =============================================================================
// HelloGltf.hpp
// -----------------------------------------------------------------------------
// hello_engine-local glTF auto-load + baseColor swap. Lifted out of main()
// in Marathon Run 14 phase N16.
// =============================================================================
#pragma once

#include "HelloSkinned.hpp"

#include <cd/anim/Skeleton.hpp>
#include <cd/asset/Primitives.hpp>
#include <cd/asset/gltf/GltfLoader.hpp>
#include <cd/asset/gltf/SkinnedMeshBridge.hpp>
#include <cd/material/Material.hpp>
#include <cd/render/MeshUpload.hpp>
#include <cd/rhi/IDevice.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <utility>

namespace cd_sample {

/// Per-primitive sub-range of the merged gltf index buffer + its own
/// uploaded baseColor texture + alpha-test data.
/// Used for per-draw material dispatch and vegetation alpha-test.
struct GltfPrimRange
{
    std::uint32_t              index_offset  { 0 };    ///< first_index for draw_indexed
    std::uint32_t              index_count   { 0 };    ///< index count for this prim
    cd::rhi::TextureHandle     albedo_tex    {};        ///< uploaded RGBA8 texture (may be invalid)
    cd::rhi::TextureViewHandle albedo_view   {};        ///< view for albedo_tex
    bool                       has_texture   { false };
    float                      alpha_cutoff  { 0.0F }; ///< >0 enables alpha-test discard in shader
};

struct GltfLoadResult
{
    cd::render::GpuMesh              mesh        {};
    std::string                      loaded_name {};
    cd_sample::SkinnedRuntime        skinned     {};
    bool                             has_texture { false };
    std::vector<GltfPrimRange>       prim_ranges {};  ///< per-primitive sub-ranges + textures
};

struct AlbedoSlot
{
    cd::rhi::TextureHandle*     image_io;
    cd::rhi::TextureViewHandle* view_io;
    cd::rhi::SamplerHandle      sampler;
    std::function<std::pair<cd::rhi::TextureHandle, cd::rhi::TextureViewHandle>(const std::uint8_t*, std::uint32_t, std::uint32_t)> upload;
};

[[nodiscard]] inline GltfLoadResult
try_auto_load_gltf(cd::rhi::IDevice&                device,
                   AlbedoSlot                       albedo,
                   cd::material::MaterialInstance&  prim_inst)
{
    GltfLoadResult out {};

    // Sponza is placed first — it takes precedence when the ~50 MB asset pack is
    // present (see assets/samples/Sponza/README.md for download instructions).
    // CesiumMan and other assets remain as graceful fallbacks.
    const std::array<std::string, 28> kCandidates {
        // --- Sponza Atrium (first-priority scene) ---
        "assets/samples/Sponza/Sponza.gltf",
        "../../../../assets/samples/Sponza/Sponza.gltf",
        "C:/UserFiles/Project/CHROMODYNAMIC_ENGINE/assets/samples/Sponza/Sponza.gltf",
        // --- character / object fallbacks ---
        "CesiumMan.glb",
        "model.gltf",
        "assets/samples/CesiumMan.glb",
        "assets/samples/DamagedHelmet.glb",
        "assets/samples/FlightHelmet.gltf",
        "assets/samples/DamagedHelmet.gltf",
        "assets/samples/BoomBox.gltf",
        "assets/samples/Duck.gltf",
        "assets/samples/Suzanne.glb",
        "assets/samples/Fox.glb",
        "assets/samples/model.gltf",
        "../../../../assets/samples/CesiumMan.glb",
        "../../../../assets/samples/DamagedHelmet.glb",
        "../../../../assets/samples/DamagedHelmet.gltf",
        "../../../../assets/samples/Duck.gltf",
        "../../../../assets/samples/Suzanne.glb",
        "../../../../assets/samples/Fox.glb",
        "../../assets/samples/CesiumMan.glb",
        "../../assets/samples/DamagedHelmet.glb",
        "../../assets/samples/DamagedHelmet.gltf",
        "C:/UserFiles/Project/CHROMODYNAMIC_ENGINE/assets/samples/CesiumMan.glb",
        "C:/UserFiles/Project/CHROMODYNAMIC_ENGINE/assets/samples/DamagedHelmet.glb",
        // --- extra Sponza absolute path variants for robustness ---
        "C:/UserFiles/Project/CHROMODYNAMIC_ENGINE/assets/samples/Sponza/Sponza.gltf",
        "../../assets/samples/Sponza/Sponza.gltf",
        "../../../assets/samples/Sponza/Sponza.gltf",
    };

    for (const auto& p : kCandidates)
    {
        auto loaded = cd::asset::gltf::load_gltf(p);
        if (!loaded.has_value())
            continue;
        // Count total vertices across all primitives to choose u16 vs u32 path.
        std::size_t total_verts = 0;
        for (const auto& m : loaded->meshes)
            for (const auto& prim : m.primitives)
                total_verts += prim.vertices.size();

        cd::asset::PrimitiveMesh merged;
        merged.vertices.reserve(total_verts);
        const bool use_u32_indices = (total_verts > 0xFFFFU);

        // Per-primitive sub-ranges + textures for per-draw material dispatch.
        std::vector<GltfPrimRange> prim_ranges;

        for (const auto& m : loaded->meshes)
        {
            for (const auto& prim : m.primitives)
            {
                GltfPrimRange range {};
                range.index_offset = use_u32_indices
                    ? static_cast<std::uint32_t>(merged.indices_u32.size())
                    : static_cast<std::uint32_t>(merged.indices.size());

                const auto base = static_cast<std::uint32_t>(merged.vertices.size());
                for (const auto& v : prim.vertices)
                {
                    cd::asset::PrimitiveVertex pv {};
                    pv.pos[0] = v.position.x;
                    pv.pos[1] = v.position.y;
                    pv.pos[2] = v.position.z;
                    pv.normal[0] = v.normal.x;
                    pv.normal[1] = v.normal.y;
                    pv.normal[2] = v.normal.z;
                    pv.uv[0] = v.texcoord0.x;
                    pv.uv[1] = v.texcoord0.y;
                    pv.color[0] = 0.85F;
                    pv.color[1] = 0.82F;
                    pv.color[2] = 0.78F;
                    merged.vertices.push_back(pv);
                }
                if (use_u32_indices)
                {
                    for (auto idx : prim.indices)
                        merged.indices_u32.push_back(base + idx);
                    range.index_count = static_cast<std::uint32_t>(merged.indices_u32.size()) - range.index_offset;
                }
                else
                {
                    for (auto idx : prim.indices)
                    {
                        const auto val = base + idx;
                        if (val > 0xFFFFU)
                            continue;  // should not happen given total_verts check
                        merged.indices.push_back(static_cast<std::uint16_t>(val));
                    }
                    range.index_count = static_cast<std::uint32_t>(merged.indices.size()) - range.index_offset;
                }

                // Upload this primitive's baseColor texture and record alpha params.
                if (prim.material_index >= 0 &&
                    prim.material_index < static_cast<int>(loaded->materials.size()))
                {
                    const auto& mat = loaded->materials[static_cast<std::size_t>(prim.material_index)];
                    const int tex_idx = mat.base_color_texture;
                    if (tex_idx >= 0 && tex_idx < static_cast<int>(loaded->textures.size()))
                    {
                        const auto& gt = loaded->textures[static_cast<std::size_t>(tex_idx)];
                        if (!gt.rgba.empty() && gt.width > 0 && gt.height > 0)
                        {
                            auto [img, view] = albedo.upload(gt.rgba.data(), gt.width, gt.height);
                            if (img.is_valid())
                            {
                                range.albedo_tex   = img;
                                range.albedo_view  = view;
                                range.has_texture  = true;
                            }
                        }
                    }
                    // Alpha-test: set cutoff > 0 for MASK and BLEND materials.
                    // BLEND is treated as MASK with cutoff=0.5 for first cut
                    // (avoids sort-order issues; full OIT is M4 territory).
                    using AM = cd::asset::gltf::GltfAlphaMode;
                    if (mat.alpha_mode == AM::kMask)
                        range.alpha_cutoff = mat.alpha_cutoff;
                    else if (mat.alpha_mode == AM::kBlend)
                        range.alpha_cutoff = 0.5F;
                    // kOpaque leaves alpha_cutoff at 0.0 (no discard)
                }
                prim_ranges.push_back(range);
            }
        }
        const bool indices_ok = use_u32_indices ? !merged.indices_u32.empty() : !merged.indices.empty();
        if (merged.vertices.empty() || !indices_ok)
        {
            std::fprintf(stderr, "[gltf] %s parsed but contained no renderable geometry\n", p.c_str());
            continue;
        }
        out.mesh = cd::render::upload_mesh(device, merged);
        out.loaded_name = p;
        out.prim_ranges = std::move(prim_ranges);

        // Wire the first textured primitive into the global albedo slot so the
        // single-draw fallback path (CesiumMan / non-Sponza) still works.
        if (!loaded->skins.empty() && !loaded->animations.empty() && !loaded->meshes.empty() &&
            !loaded->meshes[0].primitives.empty() && !loaded->meshes[0].primitives[0].skin_vertices.empty())
        {
            auto bundle = cd::asset::gltf::to_skeleton_bundle(*loaded, 0);
            out.skinned.skeleton = std::move(bundle.skeleton);
            out.skinned.node_to_joint = std::move(bundle.node_to_joint);
            out.skinned.skin_joint_remap = std::move(bundle.skin_joint_remap);
            out.skinned.animation = loaded->animations[0];
            const auto& prim_src = loaded->meshes[0].primitives[0];
            out.skinned.base_positions.reserve(prim_src.vertices.size());
            out.skinned.base_normals.reserve(prim_src.vertices.size());
            out.skinned.base_uvs.reserve(prim_src.vertices.size());
            for (const auto& v : prim_src.vertices)
            {
                out.skinned.base_positions.push_back(v.position);
                out.skinned.base_normals.push_back(v.normal);
                out.skinned.base_uvs.push_back(v.texcoord0);
            }
            out.skinned.influences = prim_src.skin_vertices;
            out.skinned.pose = cd::anim::Pose::bind_pose(out.skinned.skeleton);
            out.skinned.deformed_scratch.resize(out.skinned.base_positions.size());
            out.skinned.valid = true;
            std::fprintf(
                stderr,
                "[skin] %zu vertices, %zu joints, %zu anim channels\n",
                out.skinned.base_positions.size(),
                out.skinned.skeleton.joint_count(),
                out.skinned.animation.channels.size()
            );
        }

        // Global albedo slot: pick the first primitive that has a valid texture.
        // For CesiumMan (single prim, single tex) this matches the old behaviour.
        // For Sponza the per-prim loop above carries each material's texture;
        // this first-tex write just ensures binding=4 is never left on the
        // 1x1 white default even for the single-draw fallback frame.
        if (!loaded->materials.empty() && !loaded->textures.empty())
        {
            const auto& mat = loaded->materials.front();
            const int tex_idx = mat.base_color_texture;
            if (tex_idx >= 0 && tex_idx < static_cast<int>(loaded->textures.size()))
            {
                const auto& gt = loaded->textures[static_cast<std::size_t>(tex_idx)];
                if (!gt.rgba.empty() && gt.width > 0 && gt.height > 0)
                {
                    auto [new_image, new_view] = albedo.upload(gt.rgba.data(), gt.width, gt.height);
                    if (new_image.is_valid())
                    {
                        if (albedo.view_io->is_valid())
                            device.destroy_texture_view(*albedo.view_io);
                        if (albedo.image_io->is_valid())
                            device.destroy_texture(*albedo.image_io);
                        *albedo.image_io = new_image;
                        *albedo.view_io = new_view;
                        std::array<cd::rhi::DescriptorWrite, 1> tw {
                            cd::rhi::DescriptorWrite { .binding = 4,
                                                       .array_element = 0,
                                                       .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                                       .view = new_view,
                                                       .sampler = albedo.sampler }
                        };
                        (void)prim_inst.update(tw);
                        out.has_texture = true;
                        std::fprintf(stderr, "[gltf] baseColor texture[0] loaded (%ux%u)\n", gt.width, gt.height);
                    }
                }
            }
        }
        // Count textured primitives for the log.
        {
            std::size_t tex_prims = 0;
            for (const auto& r : out.prim_ranges)
                if (r.has_texture) ++tex_prims;
            std::fprintf(stderr, "[gltf] %zu/%zu primitives have baseColor textures\n",
                         tex_prims, out.prim_ranges.size());
        }
        const std::size_t idx_count = use_u32_indices ? merged.indices_u32.size() : merged.indices.size();
        std::fprintf(
            stderr,
            "[gltf] loaded %s -- %zu verts, %zu indices (%s, textured=%d)\n",
            p.c_str(),
            merged.vertices.size(),
            idx_count,
            use_u32_indices ? "uint32" : "uint16",
            static_cast<int>(out.has_texture)
        );
        break;
    }
    if (out.loaded_name.empty())
    {
        std::fprintf(
            stderr,
            "[gltf] no asset found; drop a .gltf into ./assets/samples/ "
            "(e.g. Khronos DamagedHelmet) and re-launch.\n"
        );
    }
    return out;
}

} // namespace cd_sample
