// =============================================================================
// HelloMeshes.hpp
// -----------------------------------------------------------------------------
// hello_engine-local procedural-primitive mesh + glTF auto-load + per-mesh
// BLAS aggregate.  Lifted out of main() in Marathon Run 16 (FINAL) phase
// NF1 so the boot path no longer threads 8 GpuMesh + 7 AccelStructureHandle
// locals through manual init order.
//
// Why one aggregate?  All eight CPU primitive meshes (cube / sphere / cone /
// cylinder / torus / floor / knot + optional glTF) feed identical pipelines
// downstream - per-frame draws look them up by PrimitiveKind via the caller
// mesh_for() switch, and the ray-query / planar-shadow / TLAS rebuild paths
// look up matching BLAS by the SAME selector.  Bundling means one factory
// call replaces ~110 lines of make_* / upload_mesh / build_blas boilerplate
// plus the one-shot AS-build command-buffer submit.
//
// Scope rule: this aggregate carries ONLY static boot-time mesh + BLAS
// resources.  It deliberately does NOT carry:
//   - PrimitiveKind (lives in main.cpp anonymous namespace; caller writes
//     the mesh_for / blas_for_kind switch lambdas over the public fields here)
//   - TLAS state (per-frame ring; see HelloTlasRing / HelloTlasRebuild)
//   - Skinned-runtime CPU LBS state (HelloSkinned / HelloSkinnedAnim)
//   - albedo / normal / MR textures + samplers (HelloIbl boot-graph output)
//
// Rendering behaviour: unchanged.  flatten_white() (post-procedural vertex
// colour reset to white) and the identical make_* parameter set match the
// pre-extract visuals exactly.  Identical exit codes preserved (16 for the
// one-shot AS-build cmd-buffer create failure).
// =============================================================================
#pragma once

#include "HelloGltf.hpp"
#include "HelloSkinned.hpp"

#include <cd/asset/Primitives.hpp>
#include <cd/material/Material.hpp>
#include <cd/render/MeshUpload.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/IDevice.hpp>


#include <array>
#include <cstdio>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd_sample {

/// Bundle of all static boot-time meshes + per-mesh BLAS.
struct HelloMeshes
{
    cd::render::GpuMesh cube  {};
    cd::render::GpuMesh sphere{};
    cd::render::GpuMesh cone  {};
    cd::render::GpuMesh cyl   {};
    cd::render::GpuMesh torus {};
    cd::render::GpuMesh floor {};
    cd::render::GpuMesh knot  {};
    /// Sponza Atrium (static scene, kSponza entity).
    cd::render::GpuMesh gltf  {};
    std::string         gltf_loaded_name {};
    /// Sponza has no skin — skinned is always invalid for this mesh.
    cd_sample::SkinnedRuntime skinned    {};
    bool                has_gltf_texture { false };
    /// Per-primitive sub-ranges + per-material textures for Sponza.
    std::vector<cd_sample::GltfPrimRange> gltf_prim_ranges {};

    /// CesiumMan animated character (kGltf entity).
    cd::render::GpuMesh gltf_cesium {};
    std::string         gltf_cesium_loaded_name {};
    cd_sample::SkinnedRuntime cesium_skinned {};
    bool                has_cesium_texture { false };

    cd::rhi::AccelStructureHandle blas_cube   {};
    cd::rhi::AccelStructureHandle blas_sphere {};
    cd::rhi::AccelStructureHandle blas_cone   {};
    cd::rhi::AccelStructureHandle blas_cyl    {};
    cd::rhi::AccelStructureHandle blas_torus  {};
    cd::rhi::AccelStructureHandle blas_floor  {};
    cd::rhi::AccelStructureHandle blas_gltf   {};   ///< Sponza BLAS
    cd::rhi::AccelStructureHandle blas_cesium {};   ///< CesiumMan BLAS

    int build_exit_code { 0 };  ///< non-zero on one-shot AS-build cmd-buffer failure
};

/// Build a BLAS for one GpuMesh.
[[nodiscard]] inline cd::rhi::AccelStructureHandle
build_mesh_blas(cd::rhi::IDevice&         device,
                const cd::render::GpuMesh& m,
                std::string_view           name)
{
    cd::rhi::AccelTriangleGeometry tri {};
    tri.vertex_buffer = m.vb;
    tri.vertex_offset = 0;
    tri.vertex_count  = m.vertex_count;
    tri.vertex_stride = sizeof(cd::asset::PrimitiveVertex);
    tri.index_buffer  = m.ib;
    tri.index_offset  = 0;
    tri.index_count   = m.index_count;
    tri.index_type    = m.index_type;
    std::array<cd::rhi::AccelTriangleGeometry, 1> tris { tri };
    cd::rhi::AccelStructureDesc bd {};
    bd.kind       = cd::rhi::AccelStructureKind::kBottomLevel;
    bd.triangles  = std::span<const cd::rhi::AccelTriangleGeometry>(tris);
    bd.debug_name = name;
    auto r = device.create_acceleration_structure(bd);
    return r.has_value() ? *r : cd::rhi::AccelStructureHandle {};
}

/// Build every primitive mesh (CPU + GPU upload), run the glTF auto-load
/// chain, build one BLAS per mesh, and submit a transient one-shot
/// command buffer that triggers the actual AS builds.
/// prim_material is used to allocate per-prim descriptor sets for Sponza
/// so each textured primitive owns its own binding-4/8/9 slot and avoids
/// the Vulkan descriptor-aliasing hazard that caused all Sponza prims to
/// sample the last-written texture (vegetation/curtains showed wrong colors).
///
/// phase456: fallback_normal_view + fallback_mr_view supply the GLOBAL
/// procedural normal + MR maps so the per-prim descriptor is COMPLETE even
/// for prims that don't have their own normal/MR textures. (Vulkan requires
/// every binding in the layout to be written before bind.) Per-prim views
/// override the fallback when available.
[[nodiscard]] inline HelloMeshes
boot_meshes(cd::rhi::IDevice&                device,
            AlbedoSlot                       albedo,
            cd::material::MaterialInstance&  prim_inst,
            cd::material::Material&          prim_material,
            cd::rhi::TextureViewHandle       fallback_normal_view = {},
            cd::rhi::TextureViewHandle       fallback_mr_view     = {})
{
    HelloMeshes out {};

    auto cube_cpu       = cd::asset::make_cube();
    auto sphere_cpu_mut = cd::asset::make_sphere(18, 28);
    auto cone_cpu_mut   = cd::asset::make_cone(32);
    auto cyl_cpu_mut    = cd::asset::make_cylinder(32);
    auto torus_cpu_mut  = cd::asset::make_torus(0.45F, 0.18F, 16, 24);
    auto flatten_white = [](auto& mesh)
    {
        for (auto& v : mesh.vertices)
        {
            v.color[0] = 1.0F;
            v.color[1] = 1.0F;
            v.color[2] = 1.0F;
        }
    };
    flatten_white(cube_cpu);
    flatten_white(sphere_cpu_mut);
    flatten_white(cone_cpu_mut);
    flatten_white(cyl_cpu_mut);
    flatten_white(torus_cpu_mut);

    const auto floor_cpu = cd::asset::make_plane(1000.0F);

    out.cube   = cd::render::upload_mesh(device, cube_cpu);
    out.sphere = cd::render::upload_mesh(device, sphere_cpu_mut);
    out.cone   = cd::render::upload_mesh(device, cone_cpu_mut);
    out.cyl    = cd::render::upload_mesh(device, cyl_cpu_mut);
    out.torus  = cd::render::upload_mesh(device, torus_cpu_mut);
    out.floor  = cd::render::upload_mesh(device, floor_cpu);

    const auto knot_cpu = cd::asset::make_torus_knot(0.7F, 0.20F, 2, 3, 256, 24);
    out.knot = cd::render::upload_mesh(device, knot_cpu);

    // Load Sponza Atrium (static scene, kSponza entity).
    {
        auto loaded = cd_sample::try_auto_load_gltf(device, albedo, prim_inst);
        out.gltf             = std::move(loaded.mesh);
        out.gltf_loaded_name = std::move(loaded.loaded_name);
        out.skinned          = std::move(loaded.skinned);  // always invalid for Sponza
        out.gltf_prim_ranges = std::move(loaded.prim_ranges);
        if (loaded.has_texture)
            out.has_gltf_texture = true;

        // Per-prim descriptor sets: allocate one MaterialInstance per textured
        // prim so each draw binds its OWN descriptor set rather than sharing the
        // global prim_inst.  Sharing one VkDescriptorSet and calling
        // vkUpdateDescriptorSets between recorded draw calls is technically
        // undefined behaviour in Vulkan — the GPU sees only the last update for
        // ALL draws, defeating per-prim texture switching (vegetation/curtains
        // show wrong texture / no alpha discard).
        for (auto& pr : out.gltf_prim_ranges)
        {
            if (!pr.has_texture || !pr.albedo_view.is_valid())
                continue;
            auto inst_r = cd::material::MaterialInstance::create(device, prim_material);
            if (!inst_r.has_value())
            {
                std::fprintf(stderr,
                    "[gltf] per-prim MaterialInstance create failed — "
                    "falling back to shared descriptor (vegetation may be wrong)\n");
                continue;
            }
            // phase456: write bindings 4 (albedo), 8 (normal), 9 (MR) into
            // this prim's OWN descriptor set. Per-prim views override; the
            // global procedural fallbacks fill bindings the prim doesn't
            // author so Vulkan sees a complete descriptor at bind time.
            const cd::rhi::TextureViewHandle norm_v =
                pr.has_normal_map ? pr.normal_view : fallback_normal_view;
            const cd::rhi::TextureViewHandle mr_v =
                pr.has_mr_map ? pr.mr_view : fallback_mr_view;
            std::vector<cd::rhi::DescriptorWrite> tw;
            tw.reserve(3);
            tw.push_back(cd::rhi::DescriptorWrite {
                .binding       = 4,
                .array_element = 0,
                .type          = cd::rhi::DescriptorType::kCombinedImageSampler,
                .view          = pr.albedo_view,
                .sampler       = albedo.sampler });
            if (norm_v.is_valid())
            {
                tw.push_back(cd::rhi::DescriptorWrite {
                    .binding       = 8,
                    .array_element = 0,
                    .type          = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .view          = norm_v,
                    .sampler       = albedo.sampler });
            }
            if (mr_v.is_valid())
            {
                tw.push_back(cd::rhi::DescriptorWrite {
                    .binding       = 9,
                    .array_element = 0,
                    .type          = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .view          = mr_v,
                    .sampler       = albedo.sampler });
            }
            (void)inst_r->update(tw);
            pr.prim_inst = std::move(*inst_r);
        }
    }

    // Load CesiumMan animated character (kGltf entity).
    // Runs AFTER Sponza so both coexist independently.
    {
        auto loaded = cd_sample::try_load_cesiumman_gltf(device, albedo, prim_inst);
        out.gltf_cesium             = std::move(loaded.mesh);
        out.gltf_cesium_loaded_name = std::move(loaded.loaded_name);
        out.cesium_skinned          = std::move(loaded.skinned);
        // CesiumMan prim_ranges are empty (single merged draw); no storage needed.
        if (loaded.has_texture)
            out.has_cesium_texture = true;
    }

    out.blas_cube   = build_mesh_blas(device, out.cube,   "blas_cube");
    out.blas_sphere = build_mesh_blas(device, out.sphere, "blas_sphere");
    out.blas_cone   = build_mesh_blas(device, out.cone,   "blas_cone");
    out.blas_cyl    = build_mesh_blas(device, out.cyl,    "blas_cyl");
    out.blas_torus  = build_mesh_blas(device, out.torus,  "blas_torus");
    out.blas_floor  = build_mesh_blas(device, out.floor,  "blas_floor");
    out.blas_gltf   = out.gltf.vb.is_valid()
        ? build_mesh_blas(device, out.gltf,        "blas_gltf")
        : cd::rhi::AccelStructureHandle {};
    out.blas_cesium = out.gltf_cesium.vb.is_valid()
        ? build_mesh_blas(device, out.gltf_cesium, "blas_cesium")
        : cd::rhi::AccelStructureHandle {};

    {
        auto bcmd_ptr = device.create_command_buffer();
        if (bcmd_ptr == nullptr)
        {
            out.build_exit_code = 16;
            return out;
        }
        auto& bcmd = *bcmd_ptr;
        bcmd.begin();
        for (auto h : { out.blas_cube,
                        out.blas_sphere,
                        out.blas_cone,
                        out.blas_cyl,
                        out.blas_torus,
                        out.blas_floor,
                        out.blas_gltf,
                        out.blas_cesium })
        {
            if (h.is_valid())
                bcmd.build_acceleration_structure(h);
        }
        bcmd.end();
        cd::rhi::SubmitDesc bsd {};
        std::array<cd::rhi::ICommandBuffer*, 1> bcbs { &bcmd };
        bsd.command_buffers = bcbs;
        (void)device.submit(bsd);
        device.wait_idle();
    }

    return out;
}

/// Tear down every static mesh GpuMesh + every BLAS handle in m.
inline void
destroy_meshes(cd::rhi::IDevice& device, HelloMeshes& m) noexcept
{
    cd::render::destroy_mesh(device, m.cube);
    cd::render::destroy_mesh(device, m.sphere);
    cd::render::destroy_mesh(device, m.cone);
    cd::render::destroy_mesh(device, m.cyl);
    cd::render::destroy_mesh(device, m.torus);
    cd::render::destroy_mesh(device, m.knot);
    cd::render::destroy_mesh(device, m.floor);
    for (auto h : { m.blas_cube,
                    m.blas_sphere,
                    m.blas_cone,
                    m.blas_cyl,
                    m.blas_torus,
                    m.blas_floor,
                    m.blas_gltf,
                    m.blas_cesium })
    {
        if (h.is_valid())
            device.destroy_acceleration_structure(h);
    }
    if (m.gltf.vb.is_valid())
        cd::render::destroy_mesh(device, m.gltf);
    // Destroy Sponza per-primitive textures.
    for (auto& pr : m.gltf_prim_ranges)
    {
        if (pr.albedo_view.is_valid())
            device.destroy_texture_view(pr.albedo_view);
        if (pr.albedo_tex.is_valid())
            device.destroy_texture(pr.albedo_tex);
        // phase456: per-prim normal + MR textures.
        if (pr.normal_view.is_valid())
            device.destroy_texture_view(pr.normal_view);
        if (pr.normal_tex.is_valid())
            device.destroy_texture(pr.normal_tex);
        if (pr.mr_view.is_valid())
            device.destroy_texture_view(pr.mr_view);
        if (pr.mr_tex.is_valid())
            device.destroy_texture(pr.mr_tex);
    }
    m.gltf_prim_ranges.clear();
    // Destroy CesiumMan mesh.
    if (m.gltf_cesium.vb.is_valid())
        cd::render::destroy_mesh(device, m.gltf_cesium);
}

} // namespace cd_sample
