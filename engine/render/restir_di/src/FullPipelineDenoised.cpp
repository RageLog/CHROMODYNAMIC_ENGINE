// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/src/FullPipelineDenoised.cpp
// Phase 681 / Sprint-6 -- ReSTIR DI full denoised pipeline (chained).
//
// Owning facade over `cd::restir_di::DispatchPass` (sample +
// temporal_reuse + spatial_reuse) and `cd::restir_di::FullSvgfPipeline`
// (moment + variance + 3x A-trous filter). One configure() + one execute()
// per frame from the integrator's point of view.
//
// See cd/restir_di/FullPipelineDenoised.hpp for the design rationale.
// =============================================================================
#include <cd/restir_di/FullPipelineDenoised.hpp>

#include <cd/restir_di/DispatchPass.hpp>
#include <cd/restir_di/FullSvgfPipeline.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

namespace cd::restir_di
{

FullPipelineDenoised::FullPipelineDenoised()
    : dispatch_(std::make_unique<DispatchPass>())
    , svgf_(std::make_unique<FullSvgfPipeline>())
{
}

FullPipelineDenoised::~FullPipelineDenoised()
{
    shutdown();
}

cd::core::Result<void>
FullPipelineDenoised::configure(cd::rhi::IDevice&                  device,
                                const FullPipelineDenoisedConfig&  cfg)
{
    using cd::rhi::rhi_errors::Code;
    using cd::rhi::rhi_errors::make;

    if (cfg.viewport_width == 0U || cfg.viewport_height == 0U)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "restir_di::FullPipelineDenoised::configure: zero viewport extent"));
    }

    // Idempotent re-configure: drop the sub-pass state before re-prepping so
    // a caller can resize the viewport without leaking device resources.
    if (ready_)
    {
        if (svgf_     != nullptr) svgf_->shutdown();
        if (dispatch_ != nullptr) dispatch_->shutdown();
        ready_ = false;
    }

    // ---- DispatchPass (sample + temporal_reuse + spatial_reuse) -------------
    DispatchConfig dc {};
    dc.viewport_width  = cfg.viewport_width;
    dc.viewport_height = cfg.viewport_height;
    dc.light_count     = cfg.light_count;
    dc.candidates      = (cfg.candidates == 0U)
                       ? kFullPipelineDenoisedDefaultCandidates
                       : cfg.candidates;

    auto dp = dispatch_->prepare(device, dc);
    if (!dp.has_value())
        return std::unexpected(dp.error());

    // ---- FullSvgfPipeline (moment + variance + 3x A-trous filter) -----------
    auto sp = svgf_->configure(device, cfg.viewport_width, cfg.viewport_height);
    if (!sp.has_value())
    {
        // Roll back the DispatchPass so the object stays in a clean,
        // re-configurable state if the caller wants to retry.
        dispatch_->shutdown();
        return std::unexpected(sp.error());
    }

    viewport_width_  = cfg.viewport_width;
    viewport_height_ = cfg.viewport_height;
    light_count_     = cfg.light_count;
    candidates_      = dc.candidates;
    frame_index_     = 0U;
    ready_           = true;
    return {};
}

cd::core::Result<void>
FullPipelineDenoised::configure(cd::rhi::IDevice& device,
                                std::uint32_t     viewport_width,
                                std::uint32_t     viewport_height)
{
    FullPipelineDenoisedConfig cfg {};
    cfg.viewport_width  = viewport_width;
    cfg.viewport_height = viewport_height;
    cfg.light_count     = 0U;
    cfg.candidates      = kFullPipelineDenoisedDefaultCandidates;
    return configure(device, cfg);
}

bool FullPipelineDenoised::execute(cd::rhi::ICommandBuffer&   cb,
                                   SceneLightView             /*scene_lights*/,
                                   cd::rhi::TextureViewHandle depth_tex,
                                   cd::rhi::TextureViewHandle normal_tex,
                                   cd::rhi::TextureViewHandle mesh_id_tex,
                                   cd::rhi::BufferHandle      out_tex)
{
    if (!ready_ || dispatch_ == nullptr || svgf_ == nullptr)
        return false;
    if (!out_tex.is_valid())
        return false;

    cb.push_debug_group("restir_di::full_pipeline_denoised");

    // ---- 1: sample (initial WRS candidates) ---------------------------------
    // Writes DispatchPass::reservoir_buffer().
    dispatch_->record(cb);

    // ---- 2: temporal reuse -------------------------------------------------
    // Reads reservoir_buffer() + previous_reservoir_buffer();
    // writes temporal_reservoir_buffer().
    dispatch_->execute_temporal_reuse(cb, frame_index_);

    // ---- 3: spatial reuse --------------------------------------------------
    // Reads temporal_reservoir_buffer(); writes spatial_reservoir_buffer().
    // The spatial output is the SVGF input (the denoised pipeline's seam).
    dispatch_->execute_spatial_reuse(cb, frame_index_);

    // ---- 4: SVGF chain (moment + variance + 3x A-trous filter) -------------
    // Reads spatial_reservoir_buffer(); writes out_tex (caller-supplied).
    // depth_tex / normal_tex / mesh_id_tex are pass-through seam slots --
    // Sprint-6 leaves them as the SvgfDenoiser fallback (reservoir-luminance
    // edge stop) when null, exactly the path the FullSvgfPipeline test
    // (test_restir_di_full_svgf.cpp) exercises.
    const bool ok = svgf_->execute(cb,
                                   dispatch_->spatial_reservoir_buffer(),
                                   normal_tex,
                                   depth_tex,
                                   mesh_id_tex,
                                   out_tex);

    cb.pop_debug_group();

    if (!ok)
        return false;

    // Advance the per-frame PCG seed for the next call.
    ++frame_index_;
    return true;
}

void FullPipelineDenoised::shutdown()
{
    if (svgf_     != nullptr) svgf_->shutdown();
    if (dispatch_ != nullptr) dispatch_->shutdown();
    viewport_width_  = 0U;
    viewport_height_ = 0U;
    light_count_     = 0U;
    candidates_      = kFullPipelineDenoisedDefaultCandidates;
    frame_index_     = 0U;
    ready_           = false;
}

}  // namespace cd::restir_di
