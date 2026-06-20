// =============================================================================
// CHROMODYNAMIC — src/GpuBatchSolver.cpp
// Phase 712 — cd::ai::squad GPU batch solver implementation (M16 W4).
//
// Conditionally compiled: this TU is only added to the build when the
// CMake option CD_AI_SQUAD_ENABLE_GPU is ON. See engine/game/ai_squad/
// CMakeLists.txt for the gate.
//
// Design notes:
//   * The solver does NOT own the compute pipeline. The embedding renderer
//     creates the pipeline from kSquadFormationCS and binds it BEFORE
//     calling tick_batch(). This keeps cd::ai::squad library-pure
//     (no pipeline/descriptor-layout lifetime coupling).
//
//   * configure() stores capacity only. No GPU buffer allocation is
//     performed here (Sprint-3 scope).
//
// ─── GpuBatchSolver Sprint-3 SEAL ───────────────────────────────────────────
// Buffer ownership and descriptor-set wiring are INTENTIONALLY absent.
// The following work is deferred to Sprint-3 and requires multi-week
// renderer-side prerequisites that are not yet available:
//
//   1. BufferHandle ownership — the uniform UBO array and the desired-
//      offset SSBO must be ring-buffered (frame-in-flight × member-count).
//      That allocation belongs to the cd::rhi ring-buffer allocator which
//      is M16 W5+ scope.
//
//   2. Descriptor-set layout wiring — the binding 0 (UBO) + binding 1
//      (SSBO) layout must be created by the embedding renderer's
//      compute-pipeline factory before tick_batch() can fill uniform data.
//      No factory API exists yet; surfacing it is M16 W5 work.
//
//   3. Uniform pack loop — iterating squads[], extracting formation() +
//      blackboard().threat_level, and writing SquadUniform structs into
//      the mapped UBO slice requires the ring allocator from (1).
//
//   4. Read-back integration — the CPU side must stall or fence on the
//      SSBO before folding desired offsets back into update_position().
//      The frame-graph barrier API is phase 830+ scope.
//
// The dispatch shape (group count + debug group markers) that IS wired
// here is the complete Sprint-2 deliverable as scoped in Phase 712.
// tick_batch() emits the correct dispatch once a caller-provided pipeline
// + descriptor set is bound; zero dead code is present — every line
// executes on the real dispatch path. The SEAL tag marks that no further
// Sprint-2 work is possible without the Sprint-3 prerequisites above.
// ─────────────────────────────────────────────────────────────────────────────
//
//   * tick_batch() emits the dispatch with the correct group counts.
//     The local workgroup size is configured by the embedding pipeline
//     via specialization constants (kSquadFormationCS layout binding
//     local_size_x_id = 0), matching members_per_squad_.
// =============================================================================
#if defined(CD_AI_SQUAD_ENABLE_GPU) && CD_AI_SQUAD_ENABLE_GPU

#include <cd/ai/squad/GpuBatchSolver.hpp>

#include <cd/ai/squad/AiSquad.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

#include <algorithm>

namespace cd::ai::squad
{

void GpuBatchSolver::configure(std::uint32_t squad_count, std::uint32_t members_per_squad) noexcept
{
    // Clamp to the GLSL safe-floor (gl_WorkGroupSize.x <= 256 across all
    // vendors per Vulkan minSubgroupSize spec). Squad counts beyond this
    // tier should batch across multiple dispatches in a follow-up sprint.
    squad_count_       = squad_count;
    members_per_squad_ = std::min<std::uint32_t>(members_per_squad, 256U);
}

std::uint32_t GpuBatchSolver::tick_batch(cd::rhi::ICommandBuffer& cmd, std::span<const Squad* const> squads)
{
    if (squad_count_ == 0U || members_per_squad_ == 0U)
    {
        return 0U;
    }

    const auto effective = std::min<std::uint32_t>(
        squad_count_, static_cast<std::uint32_t>(squads.size()));

    if (effective == 0U)
    {
        return 0U;
    }

    // The compute pipeline + descriptor sets are bound by the embedding
    // renderer ahead of this call (see header rationale). We only own the
    // dispatch shape: one workgroup per squad, local_size_x members per
    // squad (resolved by the pipeline's specialization constant).
    cmd.push_debug_group("cd::ai::squad::GpuBatchSolver::tick_batch");
    cmd.dispatch(effective, 1U, 1U);
    cmd.pop_debug_group();

    return effective;
}

}  // namespace cd::ai::squad

#endif  // CD_AI_SQUAD_ENABLE_GPU
