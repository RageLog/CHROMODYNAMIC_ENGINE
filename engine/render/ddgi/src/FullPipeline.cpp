// =============================================================================
// CHROMODYNAMIC — engine/render/ddgi/src/FullPipeline.cpp
// phase680 — Sprint-5: full DDGI pipeline compose
//            (trace -> blend_irradiance + blend_visibility -> sample).
//
// Library-only orchestrator built on top of cd::ddgi::DispatchPass. See the
// header for the rationale + per-frame contract; this TU records the four
// dispatches + inter-pass memory barriers into the caller's command buffer.
// =============================================================================
#include <cd/ddgi/FullPipeline.hpp>

#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <array>

namespace cd::ddgi
{

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
cd::core::Result<void>
FullPipeline::init(cd::rhi::IDevice& device, const DispatchPassDesc& desc)
{
    return pass_.init(device, desc);
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------
void FullPipeline::shutdown(cd::rhi::IDevice& device) noexcept
{
    pass_.shutdown(device);
    execute_call_count_ = 0;
}

// ---------------------------------------------------------------------------
// bind_sample_resources
// ---------------------------------------------------------------------------
cd::core::Result<void>
FullPipeline::bind_sample_resources(cd::rhi::IDevice&          device,
                                    cd::rhi::TextureViewHandle output_view,
                                    cd::rhi::TextureViewHandle world_pos_view,
                                    cd::rhi::TextureViewHandle world_normal_view,
                                    std::uint32_t              output_width,
                                    std::uint32_t              output_height)
{
    return pass_.bind_sample_resources(device,
                                       output_view,
                                       world_pos_view,
                                       world_normal_view,
                                       output_width,
                                       output_height);
}

// ---------------------------------------------------------------------------
// execute — record the four-pass DDGI pipeline.
// ---------------------------------------------------------------------------
cd::core::Result<void>
FullPipeline::execute(cd::rhi::ICommandBuffer&      cmd,
                      const cd::math::Mat4f&        /*view_proj*/,
                      cd::rhi::AccelStructureHandle scene_tlas,
                      std::uint32_t                 frame_index)
{
    // 0. Validate state — pipeline must be initialised AND the sample pass
    //    must have its G-buffer + output bindings wired. Validation runs
    //    BEFORE recording any command so the caller's command buffer is
    //    untouched on failure.
    if (!pass_.initialised())
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kInvalidArgument,
            "FullPipeline::execute: init() must run before execute()"));
    }
    if (pass_.sample_output_width() == 0U || pass_.sample_output_height() == 0U)
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kInvalidArgument,
            "FullPipeline::execute: bind_sample_resources(output, "
            "world_pos, world_normal, w, h) must run before execute()"));
    }

    // 1. Bind the scene TLAS into the trace pass. When the underlying
    //    DispatchPass was initialised with `needs_tlas = false` (smoke /
    //    no-RT variant) this becomes a no-op success.
    if (pass_.needs_tlas())
    {
        // We need an IDevice handle to update the descriptor set. The
        // trace-pass TLAS update is descriptor-write only; it doesn't
        // record commands into `cmd`. The caller already passed `device`
        // into init() so the descriptor set has been allocated against
        // it — `bind_tlas` re-uses that allocation. The IDevice pointer
        // is therefore re-derived from the command buffer's owning queue,
        // which every backend exposes through the command buffer's parent
        // device. Since cd::rhi::ICommandBuffer doesn't currently expose
        // that back-reference, we instead require the caller to have
        // pre-bound the TLAS through DispatchPass::bind_tlas() prior to
        // execute(). Skipping silently here keeps the orchestrator
        // single-call from the caller's perspective for the common case
        // where the TLAS is rebuilt every frame and re-bound through
        // pass().bind_tlas(device, tlas) once per frame.
        (void)scene_tlas;
    }

    // 2. Trace pass: writes per-ray radiance + direction/distance.
    pass_.dispatch(cmd, frame_index);

    // 3. Memory barrier — ray_radiance + ray_dir_dist (kUAV -> kUAV).
    //    Trace pass wrote; blend passes are about to read.
    {
        std::array<cd::rhi::TextureBarrier, 2> sync_barriers {
            cd::rhi::TextureBarrier {
                .texture = pass_.ray_radiance(),
                .from    = cd::rhi::ResourceState::kUnorderedAccess,
                .to      = cd::rhi::ResourceState::kUnorderedAccess,
                .range   = { 0U, 1U, 0U, 1U },
            },
            cd::rhi::TextureBarrier {
                .texture = pass_.ray_dir_dist(),
                .from    = cd::rhi::ResourceState::kUnorderedAccess,
                .to      = cd::rhi::ResourceState::kUnorderedAccess,
                .range   = { 0U, 1U, 0U, 1U },
            },
        };
        cmd.barrier({}, sync_barriers);
    }

    // 4. Blend passes — both read the ray images (same TLAS-derived data)
    //    and write to disjoint atlas images, so they are parallel-OK from
    //    a memory-correctness standpoint. We record them back-to-back; the
    //    GPU may overlap them whenever the driver decides to.
    pass_.execute_blend_irradiance(cmd, frame_index);
    pass_.execute_blend_visibility(cmd, frame_index);

    // 5. Memory barrier — irradiance_atlas + visibility_atlas (kUAV -> kUAV).
    //    Both atlases were just written; the sample pass is about to read.
    {
        std::array<cd::rhi::TextureBarrier, 2> sync_barriers {
            cd::rhi::TextureBarrier {
                .texture = pass_.irradiance_atlas(),
                .from    = cd::rhi::ResourceState::kUnorderedAccess,
                .to      = cd::rhi::ResourceState::kUnorderedAccess,
                .range   = { 0U, 1U, 0U, 1U },
            },
            cd::rhi::TextureBarrier {
                .texture = pass_.visibility_atlas(),
                .from    = cd::rhi::ResourceState::kUnorderedAccess,
                .to      = cd::rhi::ResourceState::kUnorderedAccess,
                .range   = { 0U, 1U, 0U, 1U },
            },
        };
        cmd.barrier({}, sync_barriers);
    }

    // 6. Sample pass — read both atlases + the caller's G-buffer; write
    //    per-pixel indirect irradiance into the caller's output image.
    pass_.execute_sample(cmd);

    ++execute_call_count_;
    return {};
}

}  // namespace cd::ddgi
