// =============================================================================
// CHROMODYNAMIC — cd/app_common/FrameCapture.hpp
//
// Single canonical GPU swapchain readback (BGRA8 -> RGBA8), golden-image
// comparison, and CPU DrawBatcher raster-to-PNG capture via cd::asset_image.
// Eliminates all duplicate staging/PNG/CRC32 implementations across apps/.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cd::app_common
{

struct SwapchainReadbackState
{
    bool                  armed { false };
    cd::rhi::BufferHandle staging {};
    std::uint32_t         width { 0U };
    std::uint32_t         height { 0U };
};

struct DecodedSwapchainRgba
{
    std::vector<std::uint8_t> rgba {};
    std::uint32_t             width { 0U };
    std::uint32_t             height { 0U };
};

/// Record barriers + `copy_image_to_buffer` from a BGRA8 swapchain texture into a host-readable staging buffer.
[[nodiscard]] bool schedule_swapchain_readback(
    cd::rhi::IDevice&        device,
    cd::rhi::ICommandBuffer& cmd,
    cd::rhi::TextureHandle   swapchain_image,
    cd::rhi::Extent2D        extent,
    SwapchainReadbackState&  state);

/// Download the scheduled staging buffer after GPU execution completes, destroy the buffer, and swizzle BGRA8 -> RGBA8.
[[nodiscard]] cd::core::Result<DecodedSwapchainRgba> finish_swapchain_readback_rgba(
    cd::rhi::IDevice&       device,
    SwapchainReadbackState& state);

/// Download the scheduled staging buffer and write it directly to a PNG file via `cd::asset::image::write_png_rgba`.
[[nodiscard]] cd::core::Result<void> finish_swapchain_readback_to_png(
    cd::rhi::IDevice&       device,
    SwapchainReadbackState& state,
    std::string_view        out_png_path);

/// Rasterize a CPU `DrawBatcher` command stream into an RGBA8 buffer and write a PNG via `cd::asset::image::write_png_rgba`
/// (used for headless CI / NullDevice player captures without duplicating PNG encoders).
[[nodiscard]] cd::core::Result<void> rasterize_draw_batcher_to_png(
    const cd::ui::renderer::DrawBatcher& batcher,
    std::uint32_t                        width,
    std::uint32_t                        height,
    std::array<std::uint8_t, 4>          clear_rgba,
    std::string_view                     out_png_path);

}  // namespace cd::app_common
