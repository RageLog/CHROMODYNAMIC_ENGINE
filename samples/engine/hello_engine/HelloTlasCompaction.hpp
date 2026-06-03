// =============================================================================
// HelloTlasCompaction.hpp
// -----------------------------------------------------------------------------
// PURE-CPU compaction algorithm for the per-frame TLAS instance gather.
//
// Lifted out of `rebuild_tlas_and_transition_depth` in Marathon Run 23
// phase N2A so that the ghost-shadow / TLAS-exclusion invariant can be
// regression-tested without an RHI device, command buffer, or window.
//
// Invariants the compaction must uphold (W8-AS / W8-BC contract):
//
//   1.  An entity whose `model_for(ent)` returns `nullopt` produces ZERO
//       TLAS instances.  This is the ghost-shadow exclusion path: a
//       hidden / removed entity must not cast a shadow ray hit.
//
//   2.  An entity whose `blas_for_kind(kind_for(ent))` returns an invalid
//       BLAS handle is also excluded.  Defensive: a kind we have not
//       registered must not crash the TLAS build.
//
//   3.  The output ordering matches the entity input ordering for all
//       included entities.  This is the GPU
//       `rayQueryGetIntersectionInstanceIdEXT` lookup invariant
//       co-located in HelloRayQuery.hpp -- the instance-id returned by
//       the GPU indexes directly into the inst_mat SSBO.
//
//   4.  The output `instances` vector and `inst_mats` vector have the
//       same length and are aligned slot-for-slot.
//
//   5.  Floor + skinned tail entries are appended by the caller AFTER
//       the entity compaction so the tail order stays deterministic and
//       the entity-side instanceCustomIndex range is dense.
//
// The function is templated on (EntityT, BlasForKind, TintFor, KindFor,
// ModelFor) so it remains namespace-private to hello_engine and does
// not drag SceneEntity / PrimitiveKind / MaterialInstance out of the
// anonymous namespace in main.cpp.
// =============================================================================
#pragma once

#include "HelloRayQuery.hpp"

#include <cd/concurrency/ParallelFor.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/Descriptors.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cd_sample {

// ---- TlasEntityCompactionResult -------------------------------------------
// Two parallel vectors: `instances[i]` is the AccelInstance the TLAS
// build consumes, `inst_mats[i]` is the matching SSBO material entry
// (binding 10 in PrimShader_kPrimFS.inl).  Same length, slot-aligned.
struct TlasEntityCompactionResult
{
    std::vector<cd::rhi::AccelInstance>           instances;
    std::vector<cd::hello_engine::InstanceMatGpu> inst_mats;
};

// ---- compact_tlas_entity_instances ---------------------------------------
// Pure-CPU pass: scatter ECS entities into pre-sized scratch arrays in
// parallel (model_for / kind_for / blas_for_kind queries cheaply on the
// caller side), then serial-compact valid slots in entity order.
//
// Returns an aggregate carrying both parallel vectors so the caller can
// take the floor + skinned tail slots themselves -- that part touches
// RHI buffers and stays in `rebuild_tlas_and_transition_depth`.
template <typename EntityT, typename BlasForKind, typename TintFor,
          typename KindFor, typename ModelFor>
[[nodiscard]] inline TlasEntityCompactionResult
compact_tlas_entity_instances(std::span<const EntityT> entities,
                              BlasForKind              blas_for_kind,
                              TintFor                  tint_for,
                              KindFor                  kind_for,
                              ModelFor                 model_for)
{
    const std::size_t kEntCount = entities.size();

    // Pre-sized scratch arrays so worker threads write by index, never
    // push_back.  ent_valid[i] is the side-channel "this slot took" bit
    // because std::optional<AccelInstance> would need a default ctor we
    // do not want to provide.
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
                return;  // ghost-shadow exclusion: hidden / removed entity
            const auto blas = blas_for_kind(kind_for(ent));
            if (!blas.is_valid())
                return;  // unregistered kind: defensive skip
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
    TlasEntityCompactionResult out;
    out.instances.reserve(kEntCount + 25u + 1u);  // entity body + floor + skinned tail
    out.inst_mats.reserve(kEntCount + 1u);
    for (std::size_t i = 0; i < kEntCount; ++i)
    {
        if (ent_valid[i] == 0u)
            continue;
        auto inst = ent_inst_scratch[i];
        // Stamp the TLAS slot index into instanceCustomIndex so the GPU
        // shader's rayQueryGetIntersectionInstanceIdEXT returns the
        // correct index into the inst_mat SSBO.
        inst.instance_id = static_cast<std::uint32_t>(out.instances.size())
                           & 0x00FFFFFFu;
        out.instances.push_back(inst);
        out.inst_mats.push_back(ent_mat_scratch[i]);
    }
    return out;
}

}  // namespace cd_sample
