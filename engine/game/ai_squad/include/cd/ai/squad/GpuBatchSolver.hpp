// =============================================================================
// CHROMODYNAMIC — cd/ai/squad/GpuBatchSolver.hpp
// Phase 712 — cd::ai::squad GPU batch solver (sprint-2 retry, M16 W4).
//
// Compute-shader-driven squad formation solver for large squads (>=64 members).
// One workgroup per squad; one invocation per member. Each member reads the
// squad centroid + target from a uniform buffer and writes its desired offset
// back to an SSBO that the CPU side then folds back into Squad::update_position.
//
// Sprint-2 scope (opt-in):
//   * configure(squad_count, members_per_squad) — sizes uniform + SSBO.
//   * tick_batch(cmd, squads) — packs squad uniforms, binds compute pipeline,
//     dispatches kSquadFormationCS, returns desired-offset SSBO handle.
//   * kSquadFormationCS — GLSL 460 string literal (no new shader file; the
//     compute source is embedded so this code-path is self-contained and
//     does NOT pollute the global shader cache when CD_AI_SQUAD_ENABLE_GPU=OFF).
//
// Opt-in compile-def:
//   This entire compute path is GATED behind CD_AI_SQUAD_ENABLE_GPU (default
//   OFF). When OFF, the symbol surface in this header is empty (the class is
//   not declared) and the matching .cpp is not added to the build. When ON,
//   the caller is expected to wire up a cd::rhi::IDevice + compute pipeline
//   factory and feed an ICommandBuffer into tick_batch().
//
// References:
//   * Reynolds, C. W. "Steering Behaviors For Autonomous Characters."
//     Game Developers Conference 1999. Boid flocking as the canonical
//     small-group formation primitive used by every modern squad solver.
//   * Bleiweiss, A. "Multi Agent Navigation on the GPU." NVIDIA GPU Tech
//     Conference 2009. Demonstrates 100k+ agent steering on commodity GPUs
//     via one-thread-per-agent compute dispatch — the architectural shape
//     this solver matches at the squad-formation tier.
//   * van der Sterren, W. "Squad Tactics for Video Games." AI Game
//     Programming Wisdom 3, 2006, Ch. 10. Formation cohesion forces are the
//     CPU-side mathematical model this GPU pass parallelises.
//
// Namespace: cd::ai::squad
//
// Dependencies: cd::core (always) + cd::rhi (only when CD_AI_SQUAD_ENABLE_GPU).
//
// Thread safety: NOT thread-safe. Drive from the render-thread (the thread
// that owns the command buffer being recorded).
//
// Moment: a combat designer drops a 128-member squad on the level, hits
// play, and watches them coordinate at 60 FPS — the formation cohesion is
// computed in a single compute dispatch instead of a 128-element CPU loop.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <span>

namespace cd::ai::squad
{

// Forward decl — the squad struct is in AiSquad.hpp. We only need a pointer
// view here, so a forward declaration keeps this header light and avoids
// pulling the full AiSquad surface into the GPU code-path.
class Squad;

#if defined(CD_AI_SQUAD_ENABLE_GPU) && CD_AI_SQUAD_ENABLE_GPU

}  // namespace cd::ai::squad

// Pull RHI interfaces in only when the GPU path is enabled — this keeps
// default builds (CD_AI_SQUAD_ENABLE_GPU=OFF) free of any cd::rhi include
// fan-out from cd::ai::squad consumers.
#include <cd/rhi/ICommandBuffer.hpp>

namespace cd::ai::squad
{

// =============================================================================
// GpuBatchSolver — compute-shader-driven squad formation solver.
//
// Lifecycle:
//   1. configure(squad_count, members_per_squad) — allocates internal
//      uniform + storage buffers sized for (squad_count * members_per_squad)
//      desired-offset entries. Idempotent: calling again resizes.
//   2. tick_batch(cmd, squads) — for each squad in `squads`, packs the
//      formation centroid + target_position into the uniform array, then
//      dispatches kSquadFormationCS with `squad_count` workgroups of
//      `members_per_squad` invocations each. The compute shader writes per-
//      member desired offsets into the SSBO; the caller reads them back via
//      desired_offsets_buffer() after the next submission completes.
//
// The implementation here is INTENTIONALLY skeletal at Phase 712: the
// dispatch path emits the compute barrier + binds the (caller-provided)
// pipeline + dispatches the right group counts. Pipeline creation,
// descriptor-set layout, and SSBO mapping are wired up by the embedding
// renderer when CD_AI_SQUAD_ENABLE_GPU=ON — that wiring is intentionally
// deferred so this lib does NOT take a hard cd::rhi dependency in the
// default OFF build.
// =============================================================================
class GpuBatchSolver
{
public:
    GpuBatchSolver() = default;

    GpuBatchSolver(const GpuBatchSolver&)            = delete;
    GpuBatchSolver& operator=(const GpuBatchSolver&) = delete;
    GpuBatchSolver(GpuBatchSolver&&)                 = default;
    GpuBatchSolver& operator=(GpuBatchSolver&&)      = default;

    ~GpuBatchSolver() = default;

    /// Configure the solver for `squad_count` squads, each holding up to
    /// `members_per_squad` members. The compute dispatch uses one workgroup
    /// per squad; the local workgroup size in kSquadFormationCS is
    /// `members_per_squad` (capped to 256 — the GLSL safe-floor for
    /// gl_WorkGroupSize.x across all vendors).
    void configure(std::uint32_t squad_count, std::uint32_t members_per_squad) noexcept;

    /// Record the squad-formation compute dispatch into `cmd`. The caller
    /// supplies the current snapshot of N squads as a span of non-owning
    /// pointers; this method packs their formation()+blackboard() data into
    /// the uniform buffer and dispatches the compute shader.
    ///
    /// Returns the number of squads that were actually dispatched (may be
    /// less than `squads.size()` if configure() was called with a smaller
    /// `squad_count` — the solver clamps without resizing mid-frame).
    std::uint32_t tick_batch(cd::rhi::ICommandBuffer& cmd, std::span<const Squad* const> squads);

    [[nodiscard]] std::uint32_t squad_count() const noexcept       { return squad_count_; }
    [[nodiscard]] std::uint32_t members_per_squad() const noexcept { return members_per_squad_; }

private:
    std::uint32_t squad_count_       {0};
    std::uint32_t members_per_squad_ {0};
};

// =============================================================================
// kSquadFormationCS — GLSL 460 compute shader source.
//
// One workgroup per squad. One invocation per member. Each invocation reads
// the squad's formation centroid + target position from the per-squad
// uniform record and writes a desired-offset (centroid → target direction
// scaled by member index) into the desired-offset SSBO.
//
// Storage layout (set 0):
//   binding 0 (UBO) : array<SquadUniform, N>
//                       SquadUniform { vec4 centroid_radius;
//                                      vec4 target_threat; }
//   binding 1 (SSBO): array<vec4>  desired_offsets[N * members_per_squad]
//
// Push constants:
//   uint members_per_squad
//
// The shader is deliberately conservative: no shared-memory cohesion (that
// belongs in a steering pass), no per-vendor extensions, GL_KHR_shader_*
// extensions deliberately omitted so SPIR-V cross-compilation to MSL/HLSL
// stays straightforward.
// =============================================================================
inline constexpr const char* kSquadFormationCS = R"GLSL(
#version 460

layout(local_size_x_id = 0) in;

struct SquadUniform
{
    vec4 centroid_radius;   // xyz=centroid, w=radius
    vec4 target_threat;     // xyz=target,   w=threat_level
};

layout(set = 0, binding = 0) uniform SquadUniforms
{
    SquadUniform squads[256];
} u_squads;

layout(set = 0, binding = 1) buffer DesiredOffsets
{
    vec4 offsets[];
} b_offsets;

layout(push_constant) uniform PC
{
    uint members_per_squad;
} pc;

void main()
{
    uint squad_idx  = gl_WorkGroupID.x;
    uint member_idx = gl_LocalInvocationID.x;
    if (member_idx >= pc.members_per_squad)
    {
        return;
    }

    SquadUniform sq = u_squads.squads[squad_idx];

    // Desired offset: linear lerp from centroid toward target, scaled by
    // (member_idx / members_per_squad) so members fan out along the engage
    // axis. Threat_level scales the magnitude — higher threat tightens the
    // cluster (1 - threat) so members close formation under fire.
    vec3 dir = sq.target_threat.xyz - sq.centroid_radius.xyz;
    float t  = float(member_idx) / max(1.0, float(pc.members_per_squad));
    float k  = 1.0 - 0.5 * sq.target_threat.w;

    vec3 desired = sq.centroid_radius.xyz + dir * (t * k);

    uint out_idx = squad_idx * pc.members_per_squad + member_idx;
    b_offsets.offsets[out_idx] = vec4(desired, 1.0);
}
)GLSL";

}  // namespace cd::ai::squad

#endif  // CD_AI_SQUAD_ENABLE_GPU

namespace cd::ai::squad
{

// When the GPU path is disabled, the namespace closes with no GpuBatchSolver
// symbol — consumers of cd::ai::squad get a clean compile error if they
// reference it without flipping the option, exactly as intended.

}  // namespace cd::ai::squad
