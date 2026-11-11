// =============================================================================
// CHROMODYNAMIC — apps/common/GoldenCapture.hpp
//
// Thin adapter bridging `cd::app_runtime::Runtime` golden-image CLI options
// onto the canonical `cd::app_common::FrameCapture` swapchain readback owner.
// =============================================================================
#pragma once

#include "AppRuntime.hpp"

#include <cd/app_common/FrameCapture.hpp>
#include <cd/asset/image/Image.hpp>
#include <cd/imgdiff/ImageDiff.hpp>

#include <cstdio>

namespace cd::app_runtime
{

using GoldenState = cd::app_common::SwapchainReadbackState;

[[nodiscard]] inline std::uint64_t golden_rgba_size(std::uint32_t w, std::uint32_t h) noexcept
{
    return static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4ULL;
}

[[nodiscard]] inline bool schedule_golden_capture(
    cd::rhi::IDevice&        device,
    cd::rhi::ICommandBuffer& cmd,
    cd::rhi::TextureHandle   swapchain_image,
    cd::rhi::Extent2D        extent,
    GoldenState&             state)
{
    return cd::app_common::schedule_swapchain_readback(device, cmd, swapchain_image, extent, state);
}

[[nodiscard]] inline int finish_golden_capture(
    cd::rhi::IDevice& device,
    const Runtime&    runtime,
    GoldenState&      state)
{
    auto decoded = cd::app_common::finish_swapchain_readback_rgba(device, state);
    if (!decoded.has_value())
    {
        std::fprintf(stderr, "[golden] readback failed: %.*s\n",
                     static_cast<int>(decoded.error().message.size()),
                     decoded.error().message.data());
        return 1;
    }

    if (runtime.golden_mode == GoldenMode::kCapture)
    {
        auto w = cd::asset::image::write_png_rgba(
            runtime.golden_path,
            decoded->rgba.data(),
            decoded->width,
            decoded->height);
        if (!w.has_value())
        {
            std::fprintf(stderr, "[golden] PNG write failed: %.*s\n",
                         static_cast<int>(w.error().message.size()),
                         w.error().message.data());
            return 1;
        }
        std::fprintf(stdout, "[golden] captured %ux%u to %s\n",
                     decoded->width, decoded->height, runtime.golden_path.c_str());
        return 0;
    }

    auto ref = cd::asset::image::load_image(runtime.golden_path);
    if (!ref.has_value())
    {
        std::fprintf(stderr, "[golden] reference load failed (%s): %.*s\n",
                     runtime.golden_path.c_str(),
                     static_cast<int>(ref.error().message.size()),
                     ref.error().message.data());
        return 1;
    }
    if (ref->width != decoded->width || ref->height != decoded->height)
    {
        std::fprintf(stderr, "[golden] reference dim %ux%u differs from capture %ux%u\n",
                     ref->width, ref->height, decoded->width, decoded->height);
        return 1;
    }

    const cd::imgdiff::ImageView a { decoded->rgba.data(), decoded->width, decoded->height };
    const cd::imgdiff::ImageView b { ref->rgba.data(), ref->width, ref->height };
    auto cmp = cd::imgdiff::compare(a, b, runtime.golden_tolerance);
    if (!cmp.has_value())
    {
        std::fprintf(stderr, "[golden] compare failed: %.*s\n",
                     static_cast<int>(cmp.error().message.size()),
                     cmp.error().message.data());
        return 1;
    }
    const auto& report = *cmp;
    std::fprintf(stdout,
                 "[golden] compare %ux%u tol=%u: diff=%u/%u px  max=R%u G%u B%u A%u  rmse=%.2f  psnr=%.2f dB\n",
                 decoded->width, decoded->height,
                 static_cast<unsigned>(runtime.golden_tolerance),
                 report.different_pixels, report.pixel_count,
                 static_cast<unsigned>(report.max_abs_delta_r),
                 static_cast<unsigned>(report.max_abs_delta_g),
                 static_cast<unsigned>(report.max_abs_delta_b),
                 static_cast<unsigned>(report.max_abs_delta_a),
                 static_cast<double>(report.rmse),
                 static_cast<double>(report.psnr_db));

    if (report.different_pixels > 0U)
    {
        if (!runtime.golden_diff_out.empty())
        {
            auto hl = cd::imgdiff::highlight(a, b, runtime.golden_tolerance);
            if (hl.has_value())
            {
                (void)cd::asset::image::write_png_rgba(
                    runtime.golden_diff_out,
                    hl->data(),
                    decoded->width,
                    decoded->height);
            }
        }
        return 2;
    }
    return 0;
}

}  // namespace cd::app_runtime
