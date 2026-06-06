// =============================================================================
// HelloGltf.hpp
// -----------------------------------------------------------------------------
// hello_engine-local glTF auto-load + baseColor swap. Lifted out of main()
// in Marathon Run 14 phase N16.
// =============================================================================
#pragma once

#include "HelloSkinned.hpp"
#include "HelloTextureAverage.hpp"

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
    bool                       double_sided  { false }; ///< glTF material doubleSided flag (info only; pipeline uses kNone cull)
    float                      alpha_cutoff  { 0.0F }; ///< >0 enables alpha-test discard in shader
    // phase448: per-prim PBR material factors PULLED FROM THE glTF FILE.
    // Sponza floor material says metallic_factor=0, roughness_factor=0.9;
    // chrome demo glTF would say metallic=1.0 rough=0.05 — and BOTH go
    // through the SAME shader path without per-asset sentinels. End of
    // "every new object needs more code" pain.
    float                      metallic           { 0.0F };  ///< glTF metallic_factor
    float                      roughness          { 0.9F };  ///< glTF roughness_factor (stone-ish default)
    float                      normal_strength    { 0.0F };  ///< glTF NormalTextureInfo.scale when has_normal_map; else 0 = use vertex normal
    // phase456: per-prim normal + metallic-roughness textures, mirroring
    // the per-prim albedo path. Without these, all Sponza prims sampled
    // the SHARED procedural Earth normal/MR maps (binding 8/9) at glTF
    // UVs — visually wrong (chrome highlights on stone, polygon facets on
    // vegetation). The per-prim MaterialInstance now carries bindings 4
    // (albedo), 8 (normal), and 9 (MR) all keyed to THIS prim's textures.
    cd::rhi::TextureHandle     normal_tex     {};
    cd::rhi::TextureViewHandle normal_view    {};
    bool                       has_normal_map { false };
    cd::rhi::TextureHandle     mr_tex         {};
    cd::rhi::TextureViewHandle mr_view        {};
    bool                       has_mr_map     { false };
    /// phase465-perprim: per-prim base color factor from the glTF material.
    /// Used by the RT-reflection per-(instance, geom) SSBO so each Sponza
    /// prim (vegetation green / fabric red / stone grey) reflects in its
    /// material colour instead of the single sandstone fallback.  When
    /// the prim has no factor (default {1,1,1,1}), the raster path's
    /// per-prim albedo TEXTURE drives the rasterised look; this RGB is
    /// the BEST-EFFORT representative tint for the reflection SSBO.
    std::array<float, 4>       base_color_factor { 1.0F, 1.0F, 1.0F, 1.0F };
    /// Per-prim descriptor set (valid when has_texture=true + prim_recreate called).
    /// Fixes Vulkan descriptor aliasing: each textured prim owns its own
    /// descriptor set with bindings 4/8/9 pointing at its own textures,
    /// so all prim draws see the right material at GPU execution time.
    cd::material::MaterialInstance prim_inst {};
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

// phase822-rt-chrome-sponza-tex-avg-extract:
// compute_texture_average_alpha_weighted lives in HelloTextureAverage.hpp
// (stdlib-only) so a unit test can include it without dragging in
// cd::rhi / Vulkan headers. See test_hello_engine_tex_avg.cpp.

// Internal helper: parse a loaded GltfScene into a GltfLoadResult.
// Shared by both try_auto_load_gltf (Sponza) and try_load_cesiumman_gltf.
[[nodiscard]] inline GltfLoadResult
parse_gltf_result(cd::rhi::IDevice&                  device,
                  AlbedoSlot                         albedo,
                  cd::material::MaterialInstance&    prim_inst,
                  cd::asset::gltf::GltfScene&        loaded,
                  std::string_view                   path)
{
    GltfLoadResult out {};

    // Count total vertices across all primitives to choose u16 vs u32 path.
    std::size_t total_verts = 0;
    for (const auto& m : loaded.meshes)
        for (const auto& prim : m.primitives)
            total_verts += prim.vertices.size();

    cd::asset::PrimitiveMesh merged;
    merged.vertices.reserve(total_verts);
    const bool use_u32_indices = (total_verts > 0xFFFFU);

    std::vector<GltfPrimRange> prim_ranges;

    for (const auto& m : loaded.meshes)
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
                        continue;
                    merged.indices.push_back(static_cast<std::uint16_t>(val));
                }
                range.index_count = static_cast<std::uint32_t>(merged.indices.size()) - range.index_offset;
            }

            if (prim.material_index >= 0 &&
                prim.material_index < static_cast<int>(loaded.materials.size()))
            {
                const auto& mat = loaded.materials[static_cast<std::size_t>(prim.material_index)];
                const int tex_idx = mat.base_color_texture;
                // phase796-rt-chrome-sponza-real-prim-color:
                // Compute the texture-average colour for this prim's albedo
                // texture so the RT reflection SSBO (HelloTlasRebuild) shows
                // the prim's ACTUAL visual colour — not the (1,1,1) factor
                // most authored Sponza materials carry. Without this every
                // Sponza prim collapses to a uniform tint in chrome
                // reflections and the curtains / vegetation / wall shapes
                // become invisible on the mirror sphere.
                //
                // We sample on a sparse grid (stride 16 px) so each prim
                // adds <0.5 ms to load. The result is alpha-weighted so
                // transparent border pixels of cutout textures (vegetation,
                // fabric) don't drag the mean towards black. If the texture
                // is missing or unloadable, the fallback is the glTF
                // base_color_factor (which is the legacy phase465 path).
                std::optional<std::array<float, 4>> avg_texture_color {};
                if (tex_idx >= 0 && tex_idx < static_cast<int>(loaded.textures.size()))
                {
                    const auto& gt = loaded.textures[static_cast<std::size_t>(tex_idx)];
                    if (!gt.rgba.empty() && gt.width > 0 && gt.height > 0)
                    {
                        auto [img, view] = albedo.upload(gt.rgba.data(), gt.width, gt.height);
                        if (img.is_valid())
                        {
                            range.albedo_tex   = img;
                            range.albedo_view  = view;
                            range.has_texture  = true;
                        }
                        // phase822: delegate to the extracted free function
                        // (testable via test_hello_engine_tex_avg.cpp).
                        avg_texture_color = compute_texture_average_alpha_weighted(
                            std::span<const std::uint8_t> { gt.rgba.data(), gt.rgba.size() },
                            gt.width, gt.height);
                    }
                }
                // phase456: per-prim normal texture upload. We reuse the
                // SAME upload functor as albedo (RGBA8 generic 2D image
                // path). The shader treats this as a tangent-space normal
                // map via the normal_strength weight in fx_params4[2].
                if (mat.normal_texture >= 0 &&
                    mat.normal_texture < static_cast<int>(loaded.textures.size()))
                {
                    const auto& gt = loaded.textures[static_cast<std::size_t>(mat.normal_texture)];
                    if (!gt.rgba.empty() && gt.width > 0 && gt.height > 0)
                    {
                        auto [img, view] = albedo.upload(gt.rgba.data(), gt.width, gt.height);
                        if (img.is_valid())
                        {
                            range.normal_tex     = img;
                            range.normal_view    = view;
                            range.has_normal_map = true;
                        }
                    }
                }
                // phase456: per-prim metallic-roughness texture upload.
                // glTF packs ARM-style: G=rough, B=metal (A unused).
                if (mat.metallic_roughness_texture >= 0 &&
                    mat.metallic_roughness_texture < static_cast<int>(loaded.textures.size()))
                {
                    const auto& gt = loaded.textures[static_cast<std::size_t>(mat.metallic_roughness_texture)];
                    if (!gt.rgba.empty() && gt.width > 0 && gt.height > 0)
                    {
                        auto [img, view] = albedo.upload(gt.rgba.data(), gt.width, gt.height);
                        if (img.is_valid())
                        {
                            range.mr_tex     = img;
                            range.mr_view    = view;
                            range.has_mr_map = true;
                        }
                    }
                }
                range.double_sided = mat.double_sided;
                using AM = cd::asset::gltf::GltfAlphaMode;
                if (mat.alpha_mode == AM::kMask)
                    range.alpha_cutoff = mat.alpha_cutoff;
                else if (mat.alpha_mode == AM::kBlend)
                    // BLEND treated as MASK with a permissive cutoff so thin
                    // semi-transparent areas (alpha 0.1-0.49) survive the test.
                    range.alpha_cutoff = 0.1F;
                // phase448: pull PBR factors directly from the glTF material.
                // Sponza's stone says ~(0, 0.9); chrome would say ~(1, 0.05).
                // The shader reads these via fx_params4 — no per-asset hack.
                range.metallic        = mat.metallic_factor;
                range.roughness       = mat.roughness_factor;
                // phase465-perprim: capture the glTF base color factor so
                // per-prim RT reflections show the right material colour.
                // phase796-rt-chrome-sponza-real-prim-color (phase823 refactor):
                // when the texture-average colour is valid, store the PRODUCT
                // of (factor * texture-average) — same per-channel composition
                // the raster path uses (texture sampled * factor) so a
                // (1,1,1) factor returns the texture's actual dominant colour.
                // Without this every (1,1,1)-factor prim collapsed to white
                // on the RT side and lost the colour separation between
                // curtain panels, vegetation, sandstone walls etc. The
                // optional-nullopt branch falls through to the unmodified
                // factor, matching the legacy phase465 path.
                range.base_color_factor =
                    fold_texture_avg_into_factor(mat.base_color_factor,
                                                 avg_texture_color);
                // phase456: now that we ACTUALLY upload per-prim normal
                // textures and bind them as the prim's own binding-8
                // descriptor, we can finally enable the shader's
                // tangent-space normal sample path. normal_strength
                // gates the perturbation: 0 = vertex normal, positive
                // value = glTF NormalTextureInfo.scale.
                range.normal_strength = range.has_normal_map ? mat.normal_scale : 0.0F;
            }
            prim_ranges.push_back(std::move(range));
        }
    }
    const bool indices_ok = use_u32_indices ? !merged.indices_u32.empty() : !merged.indices.empty();
    if (merged.vertices.empty() || !indices_ok)
    {
        std::fprintf(stderr, "[gltf] %.*s parsed but contained no renderable geometry\n",
                     static_cast<int>(path.size()), path.data());
        return out;
    }
    out.mesh = cd::render::upload_mesh(device, merged);
    out.loaded_name = std::string { path };
    out.prim_ranges = std::move(prim_ranges);

    // Skinning data
    if (!loaded.skins.empty() && !loaded.animations.empty() && !loaded.meshes.empty() &&
        !loaded.meshes[0].primitives.empty() && !loaded.meshes[0].primitives[0].skin_vertices.empty())
    {
        auto bundle = cd::asset::gltf::to_skeleton_bundle(loaded, 0);
        out.skinned.skeleton = std::move(bundle.skeleton);
        out.skinned.node_to_joint = std::move(bundle.node_to_joint);
        out.skinned.skin_joint_remap = std::move(bundle.skin_joint_remap);
        out.skinned.animation = loaded.animations[0];
        const auto& prim_src = loaded.meshes[0].primitives[0];
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

    // Global albedo slot: first textured material -> binding=4 default.
    if (!loaded.materials.empty() && !loaded.textures.empty())
    {
        const auto& mat = loaded.materials.front();
        const int tex_idx = mat.base_color_texture;
        if (tex_idx >= 0 && tex_idx < static_cast<int>(loaded.textures.size()))
        {
            const auto& gt = loaded.textures[static_cast<std::size_t>(tex_idx)];
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
        "[gltf] loaded %.*s -- %zu verts, %zu indices (%s, textured=%d)\n",
        static_cast<int>(path.size()), path.data(),
        merged.vertices.size(),
        idx_count,
        use_u32_indices ? "uint32" : "uint16",
        static_cast<int>(out.has_texture)
    );
    return out;
}

[[nodiscard]] inline GltfLoadResult
try_auto_load_gltf(cd::rhi::IDevice&                device,
                   AlbedoSlot                       albedo,
                   cd::material::MaterialInstance&  prim_inst)
{
    GltfLoadResult out {};

    // Sponza-only candidates — called first at boot to load the atrium scene.
    const std::array<std::string, 6> kCandidates {
        "assets/samples/Sponza/Sponza.gltf",
        "../../../../assets/samples/Sponza/Sponza.gltf",
        "C:/UserFiles/Project/CHROMODYNAMIC_ENGINE/assets/samples/Sponza/Sponza.gltf",
        "../../assets/samples/Sponza/Sponza.gltf",
        "../../../assets/samples/Sponza/Sponza.gltf",
        "Sponza/Sponza.gltf",
    };

    for (const auto& p : kCandidates)
    {
        auto loaded = cd::asset::gltf::load_gltf(p);
        if (!loaded.has_value())
            continue;
        auto result = parse_gltf_result(device, albedo, prim_inst, *loaded, p);
        if (!result.loaded_name.empty())
            return result;
    }
    if (out.loaded_name.empty())
    {
        std::fprintf(stderr,
            "[gltf] Sponza not found; drop Sponza.gltf into assets/samples/Sponza/ and re-launch.\n");
    }
    return out;
}

/// Load CesiumMan (animated character) separately from the Sponza scene.
/// Tries CesiumMan-specific candidates only; never loads Sponza here.
/// Returns an empty GltfLoadResult when no asset is found.
[[nodiscard]] inline GltfLoadResult
try_load_cesiumman_gltf(cd::rhi::IDevice&                device,
                        AlbedoSlot                       albedo,
                        cd::material::MaterialInstance&  prim_inst)
{
    GltfLoadResult out {};

    const std::array<std::string, 10> kCandidates {
        "assets/samples/CesiumMan.glb",
        "../../../../assets/samples/CesiumMan.glb",
        "C:/UserFiles/Project/CHROMODYNAMIC_ENGINE/assets/samples/CesiumMan.glb",
        "../../assets/samples/CesiumMan.glb",
        "CesiumMan.glb",
        // Generic animated fallbacks when CesiumMan not present:
        "assets/samples/Fox.glb",
        "../../../../assets/samples/Fox.glb",
        "../../assets/samples/Fox.glb",
        "assets/samples/model.gltf",
        "model.gltf",
    };

    for (const auto& p : kCandidates)
    {
        auto loaded = cd::asset::gltf::load_gltf(p);
        if (!loaded.has_value())
            continue;
        auto result = parse_gltf_result(device, albedo, prim_inst, *loaded, p);
        if (!result.loaded_name.empty())
        {
            std::fprintf(stderr, "[gltf] character loaded: %s\n", p.c_str());
            return result;
        }
    }
    std::fprintf(stderr,
        "[gltf] CesiumMan not found — character slot will be empty.\n"
        "       Drop CesiumMan.glb into assets/samples/ and re-launch.\n");
    return out;
}

} // namespace cd_sample
