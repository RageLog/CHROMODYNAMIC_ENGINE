// =============================================================================
// CHROMODYNAMIC — HelloBootBindless.hpp
// phase1124 (on_boot extraction, batch 2): the W8-BE host-side bindless
// wiring block lifted verbatim out of HelloEngineApp::on_boot():
//   * bindings 11/12 (Sponza VB/IB) + 14/15 (CesiumMan VB/IB) on the
//     SHARED prim_inst set, with the documented non-null fallbacks
//   * the DEDICATED 256-slot bindless albedo set (phase864) including
//     the cesium slot-base capture (phase890) and the PBR-sphere
//     slot-255 seed (phase1000)
//   * per-prim global-binding sync (phase848) for both glTF range sets
//   * sponza/cesium representative albedos + W8-BE geom metadata tables
//
// Under the chrome-probe golden net: any behavioural drift here shows up
// as a pixel diff in fixture #5 (and the w8be layout unit tests).
// =============================================================================
#pragma once

#include "HelloMeshes.hpp"        // GltfPrimRange + sync_perprim_global_bindings
#include "HelloRayQuery.hpp"      // kBindlessAlbedoSlotNone
#include "HelloTlasRebuild.hpp"   // W8BEGeomMeta

#include <cd/math/Vector.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace cd_sample
{

/// phase1124: boot-time W8-BE bindless descriptor wiring + metadata
/// tables. `light_ubo_bytes` / `inst_mat_bytes` are the byte sizes the
/// caller allocated for the multi-light UBO and the instance-material
/// SSBO (they live as on_boot locals / cd::hello_engine constants).
template <typename StateT, typename DeviceT>
inline void wire_w8be_bindless_and_meta(StateT& s, DeviceT& device,
                                        std::uint32_t light_ubo_bytes,
                                        std::uint32_t inst_mat_bytes)
{
    // phase843-W8-BE-rt-bindless-texture-sampling: host-side wiring for
    // ray-side per-prim albedo texture sampling.
    //
    //   * Binding 11 ← Sponza vertex buffer (storage)
    //   * Binding 12 ← Sponza index buffer  (storage)
    //   * Binding 13 ← bindless sampler2D array; per-prim albedo views
    //                   land in slot i for prim_ranges[i].
    //
    // When Sponza is not loaded, the prim_inst descriptor still needs
    // SOMETHING in bindings 11/12 because the layout demands non-null
    // storage-buffer writes. We default-point them at the procedural
    // sphere mesh's VB/IB so the descriptor is valid; the shader
    // never actually reads them in that case because the sentinel
    // path (tex_slot == 0xFFFFFFFFu) is always taken when no Sponza
    // per-prim metadata is written into the SSBO.
    if (s.meshes.gltf.vb.is_valid() && s.meshes.gltf.ib.is_valid())
    {
        // phase864-bindless-dedicated-set: write bindings 11 + 12
        // (Sponza VB / IB SSBOs) to the SHARED prim_inst set, but
        // write the bindless slots to the DEDICATED bindless set
        // (allocated from materials.prim_bindless_layout) on its own
        // binding 0. The shared set no longer carries binding 13.
        // phase888-non-sponza-bindless-shader: also wire bindings
        // 14/15 with CesiumMan VB/IB so the shader can read the
        // cesium mesh's per-vertex UVs when mesh_id == 1 in the
        // per-prim SSBO row. If CesiumMan isn't loaded, point at
        // Sponza's VB/IB as a non-null fallback (the shader gates
        // on mesh_id and never reaches the cesium read in that case).
        const auto cesium_vb_to_write = s.meshes.gltf_cesium.vb.is_valid()
                                            ? s.meshes.gltf_cesium.vb
                                            : s.meshes.gltf.vb;
        const auto cesium_ib_to_write = s.meshes.gltf_cesium.ib.is_valid()
                                            ? s.meshes.gltf_cesium.ib
                                            : s.meshes.gltf.ib;
        const cd::rhi::DescriptorWrite dw_shared[4] = {
            { .binding = 11, .array_element = 0,
              .type    = cd::rhi::DescriptorType::kStorageBuffer,
              .buffer  = s.meshes.gltf.vb },
            { .binding = 12, .array_element = 0,
              .type    = cd::rhi::DescriptorType::kStorageBuffer,
              .buffer  = s.meshes.gltf.ib },
            { .binding = 14, .array_element = 0,
              .type    = cd::rhi::DescriptorType::kStorageBuffer,
              .buffer  = cesium_vb_to_write },
            { .binding = 15, .array_element = 0,
              .type    = cd::rhi::DescriptorType::kStorageBuffer,
              .buffer  = cesium_ib_to_write },
        };
        (void)s.prim_inst.update(std::span<const cd::rhi::DescriptorWrite>(dw_shared));

        // Allocate the dedicated bindless descriptor set + populate
        // all 256 slots (Sponza prim albedos + fallback).
        if (auto bs_r = device.allocate_descriptor_set(
                s.materials.prim_bindless_layout); bs_r.has_value())
        {
            s.prim_bindless_set = *bs_r;
            std::vector<cd::rhi::DescriptorWrite> dw_b {};
            dw_b.reserve(256u);
            std::uint32_t slot_idx = 0u;
            for (const auto& pr : s.meshes.gltf_prim_ranges)
            {
                const cd::rhi::TextureViewHandle view_to_write =
                    pr.has_texture ? pr.albedo_view : s.albedo_tex.view;
                dw_b.push_back(cd::rhi::DescriptorWrite {
                    .binding       = 0,
                    .array_element = slot_idx,
                    .type          = cd::rhi::DescriptorType::kBindlessSampledImage,
                    .view          = view_to_write,
                    .sampler       = s.albedo_sampler,
                });
                ++slot_idx;
            }
            // phase890-cesium-bindless-textures: cesium per-prim
            // albedos follow Sponza in the slot array. The slot
            // INDEX where CesiumMan starts is captured in
            // s.cesium_bindless_slot_base so the per-frame SSBO
            // fill can stamp the right tex_slot on cesium hits.
            s.cesium_bindless_slot_base = slot_idx;
            for (const auto& pr : s.meshes.gltf_cesium_prim_ranges)
            {
                if (slot_idx >= 256u) break;
                const cd::rhi::TextureViewHandle view_to_write =
                    pr.has_texture ? pr.albedo_view : s.albedo_tex.view;
                dw_b.push_back(cd::rhi::DescriptorWrite {
                    .binding       = 0,
                    .array_element = slot_idx,
                    .type          = cd::rhi::DescriptorType::kBindlessSampledImage,
                    .view          = view_to_write,
                    .sampler       = s.albedo_sampler,
                });
                ++slot_idx;
            }
            for (; slot_idx < 256u; ++slot_idx)
            {
                dw_b.push_back(cd::rhi::DescriptorWrite {
                    .binding       = 0,
                    .array_element = slot_idx,
                    .type          = cd::rhi::DescriptorType::kBindlessSampledImage,
                    .view          = s.albedo_tex.view,
                    .sampler       = s.albedo_sampler,
                });
            }
            (void)device.update_descriptor_set(
                s.prim_bindless_set,
                std::span<const cd::rhi::DescriptorWrite>(dw_b));
            // phase1000-pbr-sphere-rt-texture: seed the per-entity
            // span returned by w8be_metadata_for() for PBR spheres
            // that have ent.use_texture == true. Slot 255 (the last
            // bindless slot) was filled above with the earth_albedo
            // view in the fallback loop, so it's a guaranteed-safe
            // target. mesh_id == 2 routes the shader into the
            // analytical spherical-UV branch added in the same
            // phase 1000 shader edit. index_offset is unused for
            // sphere hits (no IB needed).
            cd_sample::W8BEGeomMeta sphere_meta {};
            sphere_meta.albedo_tex_slot = 255U;
            sphere_meta.index_offset    = 0U;
            sphere_meta.mesh_id         = 2U;
            s.pbr_sphere_textured_meta.assign(1, sphere_meta);
        }
        else
        {
            std::fprintf(stderr,
                "hello_engine: prim_bindless_set allocate failed; "
                "chrome reflection texture path stays disabled\n");
        }
    }
    else
    {
        // Fallback to procedural sphere VB/IB so the layout has
        // non-null buffers for bindings 11/12.
        const cd::rhi::DescriptorWrite dw_be_fb[2] = {
            { .binding = 11, .array_element = 0,
              .type = cd::rhi::DescriptorType::kStorageBuffer,
              .buffer = s.meshes.sphere.vb },
            { .binding = 12, .array_element = 0,
              .type = cd::rhi::DescriptorType::kStorageBuffer,
              .buffer = s.meshes.sphere.ib },
        };
        (void)s.prim_inst.update(std::span<const cd::rhi::DescriptorWrite>(dw_be_fb));
    }

    // phase-descriptor-sync: write global bindings (0,1,3,5,6,7,10) to every
    // per-prim descriptor set so textured Sponza/CesiumMan prims see valid
    // light, shadow, IBL, and reflection data on the GPU.  Binding 2 (TLAS)
    // is synced per-frame in the TLAS rebuild path below.
    {
        const auto& gspec = s.ibl_gpu.gpu_spec_cube;
        const auto& gdiff = s.ibl_gpu.gpu_diff_cube;
        const auto& gbrdf = s.ibl_gpu.gpu_brdf_lut;
        auto sync = [&](std::vector<cd_sample::GltfPrimRange>& ranges) {
            // phase848-W8-BE-perprim-bindings-11-12-fix: also propagate
            // bindings 11 + 12 (Sponza VB + IB) to every per-prim
            // descriptor set so the chrome reflection branch in the
            // shared shader can compile-evaluate the (sentinel-gated)
            // bindless sample path without faulting on uninitialised
            // bindings.
            cd_sample::sync_perprim_global_bindings(
                ranges,
                s.shadow_ubo, s.shadow_target.view, s.shadow_sampler,
                s.lights_ubo, light_ubo_bytes,
                gspec.view, s.ibl_gpu.ibl_sampler,
                gdiff.view, gbrdf.view,
                s.inst_mat_ssbo, inst_mat_bytes,
                s.meshes.gltf.vb.is_valid()
                    ? s.meshes.gltf.vb : s.meshes.sphere.vb,
                s.meshes.gltf.ib.is_valid()
                    ? s.meshes.gltf.ib : s.meshes.sphere.ib,
                // phase849-W8-BE-perprim-bindless-slot-0-fallback
                s.albedo_tex.view, s.albedo_sampler,
                // phase888-non-sponza-bindless-shader: cesium VB/IB
                // bindings 14/15 — fall back to Sponza when not
                // loaded (shader gates on mesh_id so safe).
                [&s]
                {
                    if (s.meshes.gltf_cesium.vb.is_valid()) return s.meshes.gltf_cesium.vb;
                    if (s.meshes.gltf.vb.is_valid())        return s.meshes.gltf.vb;
                    return s.meshes.sphere.vb;
                }(),
                [&s]
                {
                    if (s.meshes.gltf_cesium.ib.is_valid()) return s.meshes.gltf_cesium.ib;
                    if (s.meshes.gltf.ib.is_valid())        return s.meshes.gltf.ib;
                    return s.meshes.sphere.ib;
                }());
        };
        sync(s.meshes.gltf_prim_ranges);
        sync(s.meshes.gltf_cesium_prim_ranges);
    }

    // phase465-perprim: derive a representative per-prim albedo for each
    // Sponza prim range from its glTF base_color_factor.  The raster path
    // uses the per-prim base color TEXTURE (binding 4 on the per-prim
    // descriptor set) so it stays texture-driven.  The reflection SSBO,
    // by contrast, has no texture sampling — we ship a flat factor here.
    // Sponza materials author legitimate factors (vegetation greens,
    // fabric reds, sandstone warm-grey) so the factor alone gives a
    // recognisable mirror colour without needing a per-prim ray-side
    // texture sample.
    s.sponza_geom_albedos.clear();
    s.sponza_geom_albedos.reserve(s.meshes.gltf_prim_ranges.size());
    // phase796-rt-chrome-sponza-real-prim-color: HelloGltf.hpp now folds
    // the texture-average colour into `base_color_factor` per-prim during
    // load. So for a Sponza prim with an authored (1,1,1) factor but a
    // red-fabric texture, `pr.base_color_factor` arrives here as the
    // real visual red — and the chrome reflection SSBO can paint the
    // curtain shape with that prim's actual colour. The near-white check
    // below is now a LAST-RESORT safety net: it triggers only when the
    // prim is textureless AND the author left the factor at white (rare
    // in Sponza — happens mostly for placeholder / debug prims). In that
    // case fall back to phase434's representative warm-sandstone so the
    // chrome reflection does not collapse to pure white.
    constexpr float kSponzaWhiteThreshold = 0.85F;
    const cd::math::Vec3f kSponzaSandstone { 0.72F, 0.60F, 0.48F };
    for (const auto& pr : s.meshes.gltf_prim_ranges)
    {
        const float r = pr.base_color_factor[0];
        const float g = pr.base_color_factor[1];
        const float b = pr.base_color_factor[2];
        const bool near_white =
            r > kSponzaWhiteThreshold &&
            g > kSponzaWhiteThreshold &&
            b > kSponzaWhiteThreshold;
        s.sponza_geom_albedos.push_back(near_white
            ? kSponzaSandstone
            : cd::math::Vec3f { r, g, b });
    }

    // phase843-W8-BE-rt-bindless-texture-sampling: parallel W8-BE
    // metadata for each Sponza prim. albedo_tex_slot is the prim's
    // slot index in the binding-13 bindless sampler2D array (set
    // 1:1 with `gltf_prim_ranges`); index_offset is the prim's first
    // index in the shared 32-bit index buffer. Sentinel slot value
    // is reserved for prims without a per-prim texture; we still
    // need the index_offset to land at the per-geom SSBO entry so
    // the shader could choose to use it for procedural / debug paths.
    s.sponza_w8be_meta.clear();
    s.sponza_w8be_meta.reserve(s.meshes.gltf_prim_ranges.size());
    {
        std::uint32_t i = 0u;
        for (const auto& pr : s.meshes.gltf_prim_ranges)
        {
            cd_sample::W8BEGeomMeta m {};
            m.albedo_tex_slot = pr.has_texture
                ? i
                : cd::hello_engine::kBindlessAlbedoSlotNone;
            m.index_offset    = pr.index_offset;
            s.sponza_w8be_meta.push_back(m);
            ++i;
        }
    }

    s.cesium_geom_albedos.clear();
    s.cesium_geom_albedos.reserve(s.meshes.gltf_cesium_prim_ranges.size());
    for (const auto& pr : s.meshes.gltf_cesium_prim_ranges)
    {
        s.cesium_geom_albedos.emplace_back(
            pr.base_color_factor[0],
            pr.base_color_factor[1],
            pr.base_color_factor[2]);
    }

    // phase890-cesium-bindless-textures: build cesium_w8be_meta in
    // parallel with the cesium albedos. The slot index points into
    // the dedicated bindless set's binding 0, OFFSET by the
    // cesium_bindless_slot_base captured during the slot fill.
    // index_offset stays at the prim's first-index inside
    // gltf_cesium.ib so the shader's barycentric UV reconstruction
    // hits the right triangle.
    s.cesium_w8be_meta.clear();
    s.cesium_w8be_meta.reserve(s.meshes.gltf_cesium_prim_ranges.size());
    {
        std::uint32_t i = 0u;
        for (const auto& pr : s.meshes.gltf_cesium_prim_ranges)
        {
            cd_sample::W8BEGeomMeta m {};
            m.albedo_tex_slot = pr.has_texture
                ? (s.cesium_bindless_slot_base + i)
                : cd::hello_engine::kBindlessAlbedoSlotNone;
            m.index_offset    = pr.index_offset;
            m.mesh_id         = 1u;  // phase890: 1 = CesiumMan VB/IB
            s.cesium_w8be_meta.push_back(m);
            ++i;
        }
    }
}

}  // namespace cd_sample
