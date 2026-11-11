// =============================================================================
// CHROMODYNAMIC — apps/common/src/FrameCapture.cpp
// =============================================================================
#include <cd/app_common/FrameCapture.hpp>

#include <cd/asset/image/Image.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/rhi/Barriers.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <span>

namespace cd::app_common
{

namespace
{

inline constexpr std::uint32_t kCaptureErrorDomain = 0x0A11U;

[[nodiscard]] cd::core::ErrorCode make_capture_error(std::uint32_t code, std::string_view msg) noexcept
{
    return cd::core::ErrorCode { kCaptureErrorDomain, code, msg };
}

[[nodiscard]] std::uint64_t rgba_byte_size(std::uint32_t w, std::uint32_t h) noexcept
{
    return static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4ULL;
}

}  // namespace

bool schedule_swapchain_readback(
    cd::rhi::IDevice&        device,
    cd::rhi::ICommandBuffer& cmd,
    cd::rhi::TextureHandle   swapchain_image,
    cd::rhi::Extent2D        extent,
    SwapchainReadbackState&  state)
{
    if (state.armed)
    {
        return false;
    }
    if (!swapchain_image.is_valid() || extent.width == 0U || extent.height == 0U)
    {
        return false;
    }

    cd::rhi::BufferDesc bd {};
    bd.size   = rgba_byte_size(extent.width, extent.height);
    bd.usage  = cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto buf_r = device.create_buffer(bd);
    if (!buf_r.has_value())
    {
        return false;
    }

    const std::array<cd::rhi::TextureBarrier, 1> to_src {
        cd::rhi::TextureBarrier {
            .texture = swapchain_image,
            .from    = cd::rhi::ResourceState::kColorAttachment,
            .to      = cd::rhi::ResourceState::kTransferSrc,
            .range   = { .base_mip = 0U, .mip_count = 1U, .base_layer = 0U, .layer_count = 1U },
        }
    };
    cmd.barrier({}, to_src);

    const std::array<cd::rhi::BufferImageCopyRegion, 1> regions {
        cd::rhi::BufferImageCopyRegion {
            .buffer_offset = 0U,
            .mip_level     = 0U,
            .base_layer    = 0U,
            .layer_count   = 1U,
            .image_offset  = { 0, 0, 0 },
            .image_extent  = { extent.width, extent.height, 1U },
        }
    };
    cmd.copy_image_to_buffer(swapchain_image, *buf_r, regions);

    const std::array<cd::rhi::TextureBarrier, 1> back {
        cd::rhi::TextureBarrier {
            .texture = swapchain_image,
            .from    = cd::rhi::ResourceState::kTransferSrc,
            .to      = cd::rhi::ResourceState::kColorAttachment,
            .range   = { .base_mip = 0U, .mip_count = 1U, .base_layer = 0U, .layer_count = 1U },
        }
    };
    cmd.barrier({}, back);

    state.staging = *buf_r;
    state.width   = extent.width;
    state.height  = extent.height;
    state.armed   = true;
    return true;
}

cd::core::Result<DecodedSwapchainRgba> finish_swapchain_readback_rgba(
    cd::rhi::IDevice&       device,
    SwapchainReadbackState& state)
{
    if (!state.armed || !state.staging.is_valid())
    {
        return std::unexpected(make_capture_error(1U, "readback not scheduled"));
    }

    const std::uint64_t bytes = rgba_byte_size(state.width, state.height);
    std::vector<std::byte> raw(static_cast<std::size_t>(bytes));
    auto dl = device.download_buffer(state.staging, 0U, std::span<std::byte> { raw });
    device.destroy_buffer(state.staging);
    state.staging = {};
    state.armed   = false;
    if (!dl.has_value())
    {
        return std::unexpected(dl.error());
    }

    DecodedSwapchainRgba out {};
    out.width  = state.width;
    out.height = state.height;
    out.rgba.resize(static_cast<std::size_t>(bytes));
    for (std::size_t i = 0U; i < out.rgba.size(); i += 4U)
    {
        out.rgba[i + 0U] = static_cast<std::uint8_t>(raw[i + 2U]);
        out.rgba[i + 1U] = static_cast<std::uint8_t>(raw[i + 1U]);
        out.rgba[i + 2U] = static_cast<std::uint8_t>(raw[i + 0U]);
        out.rgba[i + 3U] = static_cast<std::uint8_t>(raw[i + 3U]);
    }
    return out;
}

cd::core::Result<void> finish_swapchain_readback_to_png(
    cd::rhi::IDevice&       device,
    SwapchainReadbackState& state,
    std::string_view        out_png_path)
{
    auto decoded = finish_swapchain_readback_rgba(device, state);
    if (!decoded.has_value())
    {
        return std::unexpected(decoded.error());
    }

    const std::filesystem::path fs_path(out_png_path);
    if (fs_path.has_parent_path())
    {
        std::error_code ec {};
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }

    return cd::asset::image::write_png_rgba(
        out_png_path,
        decoded->rgba.data(),
        decoded->width,
        decoded->height);
}

cd::core::Result<void> rasterize_draw_batcher_to_png(
    const cd::ui::renderer::DrawBatcher& batcher,
    std::uint32_t                        width,
    std::uint32_t                        height,
    std::array<std::uint8_t, 4>          clear_rgba,
    std::string_view                     out_png_path)
{
    if (width == 0U || height == 0U || out_png_path.empty())
    {
        return std::unexpected(make_capture_error(2U, "invalid dimensions or output path"));
    }

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t i = 0U; i < rgba.size(); i += 4U)
    {
        rgba[i + 0U] = clear_rgba[0];
        rgba[i + 1U] = clear_rgba[1];
        rgba[i + 2U] = clear_rgba[2];
        rgba[i + 3U] = clear_rgba[3];
    }

    const auto verts = batcher.vertices();
    const auto idxs  = batcher.indices();

    auto edge_fn = [](float ax, float ay, float bx, float by, float px, float py) noexcept -> float
    {
        return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    };

    for (const auto& dc : batcher.commands())
    {
        std::int32_t clip_x0 = 0;
        std::int32_t clip_y0 = 0;
        std::int32_t clip_x1 = static_cast<std::int32_t>(width);
        std::int32_t clip_y1 = static_cast<std::int32_t>(height);
        if (dc.scissor.width != 0xFFFFFFFFU && dc.scissor.height != 0xFFFFFFFFU)
        {
            clip_x0 = std::max<std::int32_t>(0, dc.scissor.x);
            clip_y0 = std::max<std::int32_t>(0, dc.scissor.y);
            clip_x1 = std::min<std::int32_t>(
                static_cast<std::int32_t>(width),
                dc.scissor.x + static_cast<std::int32_t>(dc.scissor.width));
            clip_y1 = std::min<std::int32_t>(
                static_cast<std::int32_t>(height),
                dc.scissor.y + static_cast<std::int32_t>(dc.scissor.height));
        }

        for (std::uint32_t i = 0U; i + 2U < dc.index_count; i += 3U)
        {
            const std::size_t base = static_cast<std::size_t>(dc.index_offset + i);
            if (base + 2U >= idxs.size())
            {
                break;
            }
            const auto& v0 = verts[idxs[base + 0U]];
            const auto& v1 = verts[idxs[base + 1U]];
            const auto& v2 = verts[idxs[base + 2U]];

            const float area = edge_fn(v0.pos_x, v0.pos_y, v1.pos_x, v1.pos_y, v2.pos_x, v2.pos_y);
            if (std::fabs(area) < 1e-4F)
            {
                continue;
            }
            const float inv_area = 1.0F / area;

            const std::int32_t min_x = std::max(
                clip_x0,
                static_cast<std::int32_t>(std::floor(std::min({ v0.pos_x, v1.pos_x, v2.pos_x }))));
            const std::int32_t max_x = std::min(
                clip_x1 - 1,
                static_cast<std::int32_t>(std::ceil(std::max({ v0.pos_x, v1.pos_x, v2.pos_x }))));
            const std::int32_t min_y = std::max(
                clip_y0,
                static_cast<std::int32_t>(std::floor(std::min({ v0.pos_y, v1.pos_y, v2.pos_y }))));
            const std::int32_t max_y = std::min(
                clip_y1 - 1,
                static_cast<std::int32_t>(std::ceil(std::max({ v0.pos_y, v1.pos_y, v2.pos_y }))));

            for (std::int32_t py = min_y; py <= max_y; ++py)
            {
                const float fy = static_cast<float>(py) + 0.5F;
                for (std::int32_t px = min_x; px <= max_x; ++px)
                {
                    const float fx = static_cast<float>(px) + 0.5F;
                    const float w0 = edge_fn(v1.pos_x, v1.pos_y, v2.pos_x, v2.pos_y, fx, fy) * inv_area;
                    const float w1 = edge_fn(v2.pos_x, v2.pos_y, v0.pos_x, v0.pos_y, fx, fy) * inv_area;
                    const float w2 = 1.0F - w0 - w1;
                    if (w0 < -1e-4F || w1 < -1e-4F || w2 < -1e-4F)
                    {
                        continue;
                    }

                    const float sr  = w0 * static_cast<float>(v0.r) + w1 * static_cast<float>(v1.r) + w2 * static_cast<float>(v2.r);
                    const float sg  = w0 * static_cast<float>(v0.g) + w1 * static_cast<float>(v1.g) + w2 * static_cast<float>(v2.g);
                    const float sb  = w0 * static_cast<float>(v0.b) + w1 * static_cast<float>(v1.b) + w2 * static_cast<float>(v2.b);
                    const float sa  = (w0 * static_cast<float>(v0.a) + w1 * static_cast<float>(v1.a) + w2 * static_cast<float>(v2.a)) * (1.0F / 255.0F);
                    const float isa = 1.0F - sa;

                    const std::size_t off =
                        (static_cast<std::size_t>(py) * width + static_cast<std::size_t>(px)) * 4U;
                    rgba[off + 0U] = static_cast<std::uint8_t>(std::clamp(sr * sa + static_cast<float>(rgba[off + 0U]) * isa, 0.0F, 255.0F));
                    rgba[off + 1U] = static_cast<std::uint8_t>(std::clamp(sg * sa + static_cast<float>(rgba[off + 1U]) * isa, 0.0F, 255.0F));
                    rgba[off + 2U] = static_cast<std::uint8_t>(std::clamp(sb * sa + static_cast<float>(rgba[off + 2U]) * isa, 0.0F, 255.0F));
                    rgba[off + 3U] = 255U;
                }
            }
        }
    }

    const std::filesystem::path fs_path(out_png_path);
    if (fs_path.has_parent_path())
    {
        std::error_code ec {};
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }

    return cd::asset::image::write_png_rgba(out_png_path, rgba.data(), width, height);
}

}  // namespace cd::app_common
