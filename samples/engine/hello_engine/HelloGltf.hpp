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
#include <cd/asset_gltf/GltfLoader.hpp>
#include <cd/asset_gltf/SkinnedMeshBridge.hpp>
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

struct GltfLoadResult
{
    cd::render::GpuMesh       mesh        {};
    std::string               loaded_name {};
    cd_sample::SkinnedRuntime skinned     {};
    bool                      has_texture { false };
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

    const std::array<std::string, 22> kCandidates {
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
    };

    for (const auto& p : kCandidates)
    {
        auto loaded = cd::asset_gltf::load_gltf(p);
        if (!loaded.has_value())
            continue;
        cd::asset::PrimitiveMesh merged;
        for (const auto& m : loaded->meshes)
        {
            for (const auto& prim : m.primitives)
            {
                const auto base = static_cast<std::uint16_t>(merged.vertices.size());
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
                for (auto idx : prim.indices)
                {
                    if (base + idx > 0xFFFFU)
                        continue;
                    merged.indices.push_back(static_cast<std::uint16_t>(base + idx));
                }
            }
        }
        if (merged.vertices.empty() || merged.indices.empty())
        {
            std::fprintf(stderr, "[gltf] %s parsed but contained no renderable geometry\n", p.c_str());
            continue;
        }
        out.mesh = cd::render::upload_mesh(device, merged);
        out.loaded_name = p;

        if (!loaded->skins.empty() && !loaded->animations.empty() && !loaded->meshes.empty() &&
            !loaded->meshes[0].primitives.empty() && !loaded->meshes[0].primitives[0].skin_vertices.empty())
        {
            auto bundle = cd::asset_gltf::to_skeleton_bundle(*loaded, 0);
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
                        std::fprintf(stderr, "[gltf] baseColor texture loaded (%ux%u)\n", gt.width, gt.height);
                    }
                }
            }
        }
        std::fprintf(
            stderr,
            "[gltf] loaded %s -- %zu verts, %zu indices (textured=%d)\n",
            p.c_str(),
            merged.vertices.size(),
            merged.indices.size(),
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
