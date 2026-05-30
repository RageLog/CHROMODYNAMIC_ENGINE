// =============================================================================
// HelloTlasRebuild.hpp
// -----------------------------------------------------------------------------
// hello_engine-local helper that lifts the per-frame TLAS rebuild
// (Faz 1.7) + the depth-target ring barrier out of main(). The two
// blocks fire back-to-back at the start of every frame and share the
// same command buffer + boot-frame gate; teasing them apart would
// mean re-introducing the depth_image + depth_initialised_on_gpu bool
// reference re-binding twice.
//
// Marathon Run 11 phase N9 (~180 line extract).
//
// Behaviour-preserving: same descriptor writes, same destroy-queue
// 3-frame margin, same parallel_for entity scatter + serial compaction,
// same floor + skinned-gltf in-place BLAS refresh + AS->AS barrier,
// same kUndefined->kDepthWrite boot transition vs kShaderResource->
// kDepthWrite resume transition. Only the surface (function call vs
// inline block) changes.
//
// Templated on EntityT + four small callables (BlasForKind, TintFor,
// KindFor, ModelFor) so it can be lifted into a header without dragging
// SceneEntity / PrimitiveKind out of main.cpp's anonymous namespace.
// =============================================================================
#pragma once

#include "HelloRayQuery.hpp"
#include "HelloTlasCompaction.hpp"
#include "HelloTlasRing.hpp"

#include <cd/concurrency/ParallelFor.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Barriers.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace cd_sample {

// ---- rebuild_tlas_and_transition_depth ------------------------------------
// Runs the Faz 1.7 per-frame TLAS rebuild followed by the depth-target
// ring barrier. Both predicate on cmd already being in a begin()ed state.
//
// phase465-perprim: an OPTIONAL `geom_albedos_for(const EntityT&)`
// callback returns a span of per-geometry albedos when the entity owns a
// multi-geometry BLAS (today: Sponza).  Empty span -> use the instance
// tint for every geom slot (today: every non-Sponza entity).  The full
// per-(instance, geom) SSBO is expanded host-side from those inputs and
// uploaded to binding 10; the shader reads slot[inst*32 + geom].
template <typename MaterialInstanceT, typename EntityT, typename BlasForKind,
          typename TintFor, typename KindFor, typename ModelFor,
          typename GeomAlbedosFor>
inline void
rebuild_tlas_and_transition_depth(
    cd::rhi::IDevice&                       device,
    cd::rhi::ICommandBuffer&                cmd,
    std::uint32_t                           frame_idx,
    std::deque<DeferredTlas>&               destroy_queue,
    cd::rhi::AccelStructureHandle&          current_tlas,
    MaterialInstanceT&                       prim_inst,
    std::span<const EntityT>                entities,
    BlasForKind                             blas_for_kind,
    TintFor                                 tint_for,
    KindFor                                 kind_for,
    ModelFor                                model_for,
    GeomAlbedosFor                          geom_albedos_for,
    cd::rhi::AccelStructureHandle           blas_floor,
    cd::rhi::AccelStructureHandle           blas_gltf,
    bool                                    skinned_valid,
    cd::rhi::BufferHandle                   inst_mat_ssbo,
    cd::rhi::TextureHandle                  depth_image,
    bool&                                   depth_initialised_on_gpu)
{
    // 1) tick deferred destroy queue (3-frame margin beyond fif=2).
    while (!destroy_queue.empty()
           && destroy_queue.front().destroy_at_frame <= frame_idx)
    {
        device.destroy_acceleration_structure(destroy_queue.front().h);
        destroy_queue.pop_front();
    }

    // 2) collect instances (ECS entities + floor + skinned gltf).
    //
    // Phase N2A (Marathon Run 23): the parallel scatter + serial
    // compaction step is delegated to compact_tlas_entity_instances --
    // a pure-CPU helper that is regression-tested in
    // tests/test_tlas_compaction.cpp for the ghost-shadow exclusion
    // invariant (model_for() == nullopt => zero TLAS instances) and
    // the GPU instanceCustomIndex / inst_mat SSBO slot alignment.
    auto compact = compact_tlas_entity_instances<EntityT>(
        entities, blas_for_kind, tint_for, kind_for, model_for);
    std::vector<cd::rhi::AccelInstance>&           instances = compact.instances;
    std::vector<cd::hello_engine::InstanceMatGpu>& inst_mats = compact.inst_mats;

    auto push_inst = [&](cd::rhi::AccelStructureHandle blas,
                         const cd::math::Mat4f& m,
                         const cd::math::Vec3f& albedo)
    {
        if (!blas.is_valid())
            return;
        instances.push_back(cd::hello_engine::make_accel_instance(blas, m));
        cd::hello_engine::InstanceMatGpu im {};
        cd::hello_engine::fill_inst_mat(im, albedo);
        inst_mats.push_back(im);
    };

    // Floor: identity scale, y = kFloorY (matches the floor draw).
    // W8-BC: distinct neutral grey so chrome reflections show a proper
    // grey floor, not garbage or a wrong entity tint.
    {
        cd::math::Mat4f fm = cd::math::Mat4f::identity();
        fm[3][1] = -0.55F;
        push_inst(blas_floor, fm, cd::math::Vec3f { 0.5F, 0.5F, 0.5F });
    }

    // Phase 251: refresh the skinned BLAS so RT shadow rays trace
    // against the current animation pose instead of the bind pose.
    // CPU-LBS already re-uploaded gltf_mesh.vb earlier in this frame;
    // the BLAS storage + scratch were sized for the original triangle
    // count (unchanged), so an in-place rebuild via
    // vkCmdBuildAccelerationStructuresKHR (MODE_BUILD_KHR with the same
    // dst handle) overwrites the BLAS contents from the freshly-skinned
    // vertex data. We then issue an AS-build -> AS-build memory barrier
    // so the TLAS build (which dereferences blas device addresses)
    // observes the updated BLAS rather than racing the write. Static-
    // geometry BLAS (cube/sphere/etc.) stay at boot-time build; only
    // the animated gltf BLAS needs the refresh.
    if (skinned_valid && blas_gltf.is_valid())
    {
        cmd.build_acceleration_structure(blas_gltf);
        cmd.acceleration_structure_barrier();
    }

    // 3) create + build the TLAS on this frame cmd buffer.
    cd::rhi::AccelStructureDesc tld {};
    tld.kind = cd::rhi::AccelStructureKind::kTopLevel;
    tld.instances = std::span<const cd::rhi::AccelInstance>(instances);
    tld.debug_name = "tlas_frame";
    auto new_r = device.create_acceleration_structure(tld);
    if (new_r.has_value())
    {
        cmd.build_acceleration_structure(*new_r);
        // 4) defer destroy of the previous frame TLAS (3-frame margin).
        if (current_tlas.is_valid())
            destroy_queue.push_back({ current_tlas, frame_idx + 3u });
        current_tlas = *new_r;
        // 5) update the prim_inst descriptor binding 2 to the new TLAS.
        std::array<cd::rhi::DescriptorWrite, 1> tlas_writes {
            cd::rhi::DescriptorWrite {
                .binding = 2,
                .array_element = 0,
                .type = cd::rhi::DescriptorType::kAccelerationStructure,
                .accel = current_tlas }
        };
        (void)prim_inst.update(tlas_writes);
    }

    // W8-BC + phase465-perprim: upload the per-frame instance materials
    // in 2D layout — slot[inst*kMaxGeomsPerInst + geom] carries the
    // material for that (instance, geometry) pair.  The default
    // expansion replicates the instance tint across all 32 geom slots
    // so non-Sponza hits read back the unchanged W8-BC behaviour.  For
    // entities whose geom_albedos_for() returns a non-empty span (today:
    // Sponza), the per-geom albedos overwrite the corresponding slots.
    //
    // The TLAS body comes from `compact_tlas_entity_instances` in the
    // SAME order as the input entity span (skipping ghost-shadow
    // entries), so we re-walk the entity span with the same gating
    // logic to map entity index -> TLAS instance index.  The floor
    // (pushed AFTER the entity body) sits at the trailing slot and
    // never carries per-geom data (single-geom BLAS).
    if (!inst_mats.empty())
    {
        const std::uint32_t inst_n =
            std::min<std::uint32_t>(
                static_cast<std::uint32_t>(inst_mats.size()),
                cd::hello_engine::kMaxInstances);
        std::vector<cd::hello_engine::InstanceMatGpu> expanded(
            static_cast<std::size_t>(inst_n)
            * cd::hello_engine::kMaxGeomsPerInst);
        // Default expansion: replicate each instance tint across every
        // geom slot so single-geom hits read back the same albedo.
        for (std::uint32_t i = 0; i < inst_n; ++i)
        {
            for (std::uint32_t g = 0; g < cd::hello_engine::kMaxGeomsPerInst; ++g)
            {
                expanded[static_cast<std::size_t>(i)
                         * cd::hello_engine::kMaxGeomsPerInst + g] = inst_mats[i];
            }
        }
        // Per-geom override for multi-geometry BLAS instances.  We
        // re-walk the entity span and replay the SAME inclusion logic
        // as compact_tlas_entity_instances so the TLAS slot index is
        // identical.  Cheap (linear, no parallel scatter) and avoids
        // changing the compaction API.
        std::uint32_t tlas_idx = 0;
        for (const auto& ent : entities)
        {
            auto model_opt = model_for(ent);
            if (!model_opt.has_value())
                continue;  // ghost-shadow exclusion
            const auto blas = blas_for_kind(kind_for(ent));
            if (!blas.is_valid())
                continue;
            // This entity contributed instances[tlas_idx]; check for per-geom data.
            if (tlas_idx < inst_n)
            {
                auto geom_albs = geom_albedos_for(ent);
                if (!geom_albs.empty())
                {
                    const std::uint32_t gn = std::min<std::uint32_t>(
                        static_cast<std::uint32_t>(geom_albs.size()),
                        cd::hello_engine::kMaxGeomsPerInst);
                    for (std::uint32_t g = 0; g < gn; ++g)
                    {
                        cd::hello_engine::InstanceMatGpu im {};
                        cd::hello_engine::fill_inst_mat(im, geom_albs[g]);
                        expanded[static_cast<std::size_t>(tlas_idx)
                                 * cd::hello_engine::kMaxGeomsPerInst + g] = im;
                    }
                    // Geom slots beyond the supplied list (gn..31) keep
                    // the default instance-tint replication from above,
                    // safe for stray hits to a higher geometry_index.
                }
            }
            ++tlas_idx;
        }
        const std::size_t bytes = expanded.size()
            * sizeof(cd::hello_engine::InstanceMatGpu);
        (void)device.upload_buffer(
            inst_mat_ssbo,
            0,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(expanded.data()),
                bytes));
        std::array<cd::rhi::DescriptorWrite, 1> ssbo_writes {
            cd::rhi::DescriptorWrite {
                .binding = 10,
                .array_element = 0,
                .type = cd::rhi::DescriptorType::kStorageBuffer,
                .buffer = inst_mat_ssbo,
                .buffer_offset = 0,
                .buffer_range = cd::hello_engine::kInstMatBytes }
        };
        (void)prim_inst.update(ssbo_writes);
    }

    // ---- Depth target ring barrier --------------------------------------
    // First frame: kUndefined -> kDepthWrite. Subsequent frames: composite-
    // pass GTAO sampled the depth target as kShaderResource at end of prior
    // frame; bring it back to kDepthWrite before the HDR scene pass.
    const cd::rhi::ResourceState from =
        depth_initialised_on_gpu
            ? cd::rhi::ResourceState::kShaderResource
            : cd::rhi::ResourceState::kUndefined;
    std::array<cd::rhi::TextureBarrier, 1> db {
        cd::rhi::TextureBarrier {
            .texture = depth_image,
            .from = from,
            .to = cd::rhi::ResourceState::kDepthWrite,
            .range = { .base_mip = 0, .mip_count = 1,
                       .base_layer = 0, .layer_count = 1 } }
    };
    cmd.barrier({}, db);
    depth_initialised_on_gpu = true;
}

}  // namespace cd_sample
