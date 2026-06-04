// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/src/FullSvgfPipeline.cpp
// Phase 669 / Sprint-5 -- ReSTIR DI SVGF full pipeline chain.
//
// Thin facade over `cd::restir_di::SvgfDenoiser`: configures it with the
// production filter iteration count (`kFullSvgfFilterIterations = 3`) and
// re-orders the argument list to the framegraph G-buffer slot order
// `(reservoir, normal, depth, mesh_id, out)` -- matching the seam the
// integrator already plumbs.
//
// See cd/restir_di/FullSvgfPipeline.hpp for the design rationale.
// =============================================================================
#include <cd/restir_di/FullSvgfPipeline.hpp>

#include <cd/restir_di/SvgfDenoiser.hpp>
#include <cd/rhi/IDevice.hpp>

namespace cd::restir_di
{

namespace
{
// Build the SvgfDenoiserConfig used by FullSvgfPipeline. The temporal_alpha
// + edge-stop sigmas mirror the Schied 2017 paper Section 5 reference
// values; the filter_iterations is fixed at the production default.
SvgfDenoiserConfig make_full_config(std::uint32_t w, std::uint32_t h)
{
    SvgfDenoiserConfig cfg {};
    cfg.viewport_width    = w;
    cfg.viewport_height   = h;
    cfg.filter_iterations = kFullSvgfFilterIterations;
    cfg.depth_phi         = 1.0F;
    cfg.normal_phi        = 128.0F;
    cfg.temporal_alpha    = 0.2F;
    return cfg;
}
}  // namespace

FullSvgfPipeline::FullSvgfPipeline()
    : denoiser_(std::make_unique<SvgfDenoiser>())
{
}

FullSvgfPipeline::~FullSvgfPipeline()
{
    shutdown();
}

cd::core::Result<void>
FullSvgfPipeline::configure(cd::rhi::IDevice& device,
                            std::uint32_t     viewport_width,
                            std::uint32_t     viewport_height)
{
    using cd::rhi::rhi_errors::Code;
    using cd::rhi::rhi_errors::make;

    if (viewport_width == 0U || viewport_height == 0U)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "restir_di::FullSvgfPipeline::configure: zero viewport extent"));
    }

    // Idempotent re-configure: drop the SvgfDenoiser state before re-prepping.
    if (ready_)
    {
        denoiser_->shutdown();
        ready_ = false;
    }

    const auto cfg = make_full_config(viewport_width, viewport_height);
    auto r = denoiser_->configure(device, cfg);
    if (!r.has_value())
        return std::unexpected(r.error());

    viewport_width_  = viewport_width;
    viewport_height_ = viewport_height;
    ready_           = true;
    return {};
}

bool FullSvgfPipeline::execute(cd::rhi::ICommandBuffer&   cb,
                               cd::rhi::BufferHandle      reservoir_buf,
                               cd::rhi::TextureViewHandle normal_tex,
                               cd::rhi::TextureViewHandle depth_tex,
                               cd::rhi::TextureViewHandle mesh_id_tex,
                               cd::rhi::BufferHandle      out_tex) const
{
    if (!ready_ || denoiser_ == nullptr)
        return false;
    if (!reservoir_buf.is_valid() || !out_tex.is_valid())
        return false;

    // SvgfDenoiser::execute takes (depth, normal, mesh_id) in its internal
    // kernel order; remap from the framegraph G-buffer slot order here.
    return denoiser_->execute(cb,
                              reservoir_buf,
                              depth_tex,
                              normal_tex,
                              mesh_id_tex,
                              out_tex);
}

void FullSvgfPipeline::shutdown()
{
    if (denoiser_ != nullptr)
        denoiser_->shutdown();
    viewport_width_  = 0U;
    viewport_height_ = 0U;
    ready_           = false;
}

}  // namespace cd::restir_di
