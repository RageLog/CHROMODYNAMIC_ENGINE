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
template <typename MaterialInstanceT, typename EntityT, typename BlasForKind,
          typename TintFor, typename KindFor, typename ModelFor>
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
    std::vector<cd::rhi::AccelInstance> instances;
    instances.reserve(entities.size() + 25 + 1);
    // Parallel material array; kept in lockstep with instances so the
    // GPU rayQueryGetIntersectionInstanceIdEXT indexes the right slot.
    std::vector<cd::hello_engine::InstanceMatGpu> inst_mats;
    inst_mats.reserve(entities.size() + 1);

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

    // X1B parallel TLAS instance build via cd::concurrency::parallel_for.
    // Per-entity slot written by index into pre-sized scratch arrays
    // (no push_back from worker threads); a serial compaction step
    // collects valid slots into the final instances/inst_mats arrays so
    // the floor and skinned gltf tail stays in deterministic order and
    // the GPU instance-index correspondence is preserved.
    const std::size_t kEntCount = entities.size();
    std::vector<cd::rhi::AccelInstance>           ent_inst_scratch(kEntCount);
    std::vector<cd::hello_engine::InstanceMatGpu> ent_mat_scratch(kEntCount);
    std::vector<std::uint8_t>                     ent_valid(kEntCount, 0u);
    cd::concurrency::parallel_for(
        std::size_t { 0 },
        kEntCount,
        [&](std::size_t i)
        {
            const auto& ent = entities[i];
            auto model_opt = model_for(ent);
            if (!model_opt.has_value())
                return;
            const auto blas = blas_for_kind(kind_for(ent));
            if (!blas.is_valid())
                return;
            ent_inst_scratch[i] = cd::hello_engine::make_accel_instance(
                blas, *model_opt);
            cd::hello_engine::InstanceMatGpu im {};
            cd::hello_engine::fill_inst_mat(im, tint_for(ent));
            ent_mat_scratch[i] = im;
            ent_valid[i] = 1u;
        });
    // Serial compaction preserves entity ordering so the GPU
    // instanceCustomIndex lookup into inst_mat_ssbo stays aligned with
    // the TLAS hit instance id.
    for (std::size_t i = 0; i < kEntCount; ++i)
    {
        if (ent_valid[i] == 0u)
            continue;
        instances.push_back(ent_inst_scratch[i]);
        inst_mats.push_back(ent_mat_scratch[i]);
    }

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

    // W8-BC: upload the per-frame instance materials. Clamp to the SSBO
    // capacity (defensive; kMaxInstMats = 256 dwarfs current entity
    // count, but futureproof). Re-issue the binding-10 descriptor write
    // each frame so the GPU sees the freshly uploaded contents even if
    // the underlying buffer handle stays put.
    if (!inst_mats.empty())
    {
        const std::uint32_t n =
            std::min<std::uint32_t>(
                static_cast<std::uint32_t>(inst_mats.size()),
                cd::hello_engine::kMaxInstMats);
        const std::size_t bytes =
            static_cast<std::size_t>(n)
            * sizeof(cd::hello_engine::InstanceMatGpu);
        (void)device.upload_buffer(
            inst_mat_ssbo,
            0,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(inst_mats.data()),
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
