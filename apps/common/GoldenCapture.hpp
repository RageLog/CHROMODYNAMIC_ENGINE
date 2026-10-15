// =============================================================================
// CHROMODYNAMIC — apps/common/GoldenCapture.hpp
// =============================================================================
#pragma once

#include "SampleRuntime.hpp"

#include <cd/asset/image/Image.hpp>
#include <cd/imgdiff/ImageDiff.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace cd::sample
{

struct GoldenState
{
    bool armed { false };
    cd::rhi::BufferHandle staging {};
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
};

[[nodiscard]] inline std::uint64_t golden_rgba_size(std::uint32_t w, std::uint32_t h) noexcept
{
    return static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4U;
}

[[nodiscard]] inline bool schedule_golden_capture(
    cd::rhi::IDevice& device,
    cd::rhi::ICommandBuffer& cmd,
    cd::rhi::TextureHandle swapchain_image,
    cd::rhi::Extent2D extent,
    GoldenState& state
)
{
    if (state.armed)
    {
        std::fprintf(stderr, "[golden] schedule_golden_capture called twice\n");
        return false;
    }
    if (!swapchain_image.is_valid() || extent.width == 0 || extent.height == 0)
    {
        std::fprintf(stderr, "[golden] invalid swapchain image / extent\n");
        return false;
    }

    cd::rhi::BufferDesc bd {};
    bd.size = golden_rgba_size(extent.width, extent.height);
    bd.usage = cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto buf_r = device.create_buffer(bd);
    if (!buf_r.has_value())
    {
        std::fprintf(stderr, "[golden] staging buffer create failed: %.*s\n",
                     static_cast<int>(buf_r.error().message.size()),
                     buf_r.error().message.data());
        return false;
    }

    const std::array<cd::rhi::TextureBarrier, 1> to_src {
        cd::rhi::TextureBarrier {
            .texture = swapchain_image,
            .from = cd::rhi::ResourceState::kColorAttachment,
            .to = cd::rhi::ResourceState::kTransferSrc,
            .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
        }
    };
    cmd.barrier({}, to_src);

    const std::array<cd::rhi::BufferImageCopyRegion, 1> regions {
        cd::rhi::BufferImageCopyRegion {
            .buffer_offset = 0,
            .mip_level = 0,
            .base_layer = 0,
            .layer_count = 1,
            .image_offset = { 0, 0, 0 },
            .image_extent = { extent.width, extent.height, 1 },
        }
    };
    cmd.copy_image_to_buffer(swapchain_image, *buf_r, regions);

    const std::array<cd::rhi::TextureBarrier, 1> back {
        cd::rhi::TextureBarrier {
            .texture = swapchain_image,
            .from = cd::rhi::ResourceState::kTransferSrc,
            .to = cd::rhi::ResourceState::kColorAttachment,
            .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
        }
    };
    cmd.barrier({}, back);

    state.staging = *buf_r;
    state.width = extent.width;
    state.height = extent.height;
    state.armed = true;
    return true;
}

[[nodiscard]] inline int finish_golden_capture(
    cd::rhi::IDevice& device,
    const Runtime& runtime,
    GoldenState& state
)
{
    if (!state.armed)
    {
        std::fprintf(stderr, "[golden] finish called without schedule\n");
        return 1;
    }

    const std::uint64_t bytes = golden_rgba_size(state.width, state.height);
    std::vector<std::byte> raw(static_cast<std::size_t>(bytes));
    auto dl = device.download_buffer(state.staging, 0, std::span<std::byte> { raw });
    device.destroy_buffer(state.staging);
    state.armed = false;
    if (!dl.has_value())
    {
        std::fprintf(stderr, "[golden] download_buffer failed: %.*s\n",
                     static_cast<int>(dl.error().message.size()),
                     dl.error().message.data());
        return 1;
    }

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(bytes));
    for (std::size_t i = 0; i < rgba.size(); i += 4)
    {
        rgba[i + 0] = static_cast<std::uint8_t>(raw[i + 2]);
        rgba[i + 1] = static_cast<std::uint8_t>(raw[i + 1]);
        rgba[i + 2] = static_cast<std::uint8_t>(raw[i + 0]);
        rgba[i + 3] = static_cast<std::uint8_t>(raw[i + 3]);
    }

    if (runtime.golden_mode == GoldenMode::kCapture)
    {
        auto w = cd::asset::image::write_png_rgba(runtime.golden_path, rgba.data(), state.width, state.height);
        if (!w.has_value())
        {
            std::fprintf(stderr, "[golden] PNG write failed: %.*s\n",
                         static_cast<int>(w.error().message.size()),
                         w.error().message.data());
            return 1;
        }
        std::fprintf(stdout, "[golden] captured %ux%u to %s\n",
                     state.width, state.height, runtime.golden_path.c_str());
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
    if (ref->width != state.width || ref->height != state.height)
    {
        std::fprintf(stderr, "[golden] reference dim %ux%u differs from capture %ux%u\n",
                     ref->width, ref->height, state.width, state.height);
        return 1;
    }
    const cd::imgdiff::ImageView a { rgba.data(), state.width, state.height };
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
                 state.width, state.height,
                 static_cast<unsigned>(runtime.golden_tolerance),
                 report.different_pixels, report.pixel_count,
                 static_cast<unsigned>(report.max_abs_delta_r),
                 static_cast<unsigned>(report.max_abs_delta_g),
                 static_cast<unsigned>(report.max_abs_delta_b),
                 static_cast<unsigned>(report.max_abs_delta_a),
                 report.rmse, report.psnr_db);

    if (report.different_pixels > 0)
    {
        if (!runtime.golden_diff_out.empty())
        {
            auto hl = cd::imgdiff::highlight(a, b, runtime.golden_tolerance);
            if (hl.has_value())
                (void)cd::asset::image::write_png_rgba(
                    runtime.golden_diff_out,
                    hl->data(),
                    state.width, state.height);
        }
        return 2;
    }
    return 0;
}

}  // namespace cd::sample
