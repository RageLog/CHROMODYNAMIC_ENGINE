// =============================================================================
// CHROMODYNAMIC — cd/render/TextureUpload.hpp
//
// Phase 439 — RGBA8 texture upload helper extracted from
// samples/engine/hello_engine/main.cpp::create_texture_rgba8.
//
// Sibling of MeshUpload.hpp. Allocates a `cd::rhi::TextureHandle` + view
// pair for a 2D RGBA8 image and uploads pixel data via a one-shot staging
// buffer + command-buffer submission. The staging buffer is freed before
// returning; the texture and view are owned by the caller.
//
// Why a separate header from cd::asset:
//   cd::asset has no `cd::rhi` dependency (it decodes pixels into a CPU
//   array). cd::render owns the GPU side; this helper is the bridge so
//   anyone holding an RGBA8 byte vector + width/height can land it on
//   the GPU in one call without rewriting the staging dance.
//
// Header-only on purpose: zero TU cost; the impl is a couple-dozen lines.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cd::render
{

/// Owning pair: a 2D texture handle + its primary view. Returned by
/// `upload_texture_2d_rgba8`. `image.is_valid() == false` marks failure.
struct GpuTexture2D
{
    cd::rhi::TextureHandle     image {};
    cd::rhi::TextureViewHandle view  {};
};

/// Upload a 2D RGBA8 image to the GPU. Allocates the texture + view,
/// creates a transient staging buffer, copies pixels through it, then
/// destroys the staging buffer. Returns an empty `GpuTexture2D` on any
/// failure so the caller can detect via `out.image.is_valid()`.
///
/// Pixel layout: `rgba` must point at `w * h * 4` bytes in row-major
/// (top-to-bottom) order. `w` and `h` must both be > 0 — the function
/// returns immediately with an empty result otherwise.
///
/// Synchronization: the function submits its own command buffer and
/// `wait_idle()`s before returning. Suitable for boot-time uploads;
/// callers doing per-frame uploads should batch through their own
/// staging pool instead.
[[nodiscard]] inline GpuTexture2D
upload_texture_2d_rgba8(cd::rhi::IDevice&     dev,
                        const std::uint8_t*   rgba,
                        std::uint32_t         w,
                        std::uint32_t         h)
{
    GpuTexture2D out {};
    if (rgba == nullptr || w == 0U || h == 0U)
    {
        return out;
    }

    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { w, h, 1U };
    td.mip_levels   = 1U;
    td.array_layers = 1U;
    td.usage        = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value())
    {
        return out;
    }
    out.image = *img;

    const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U;
    cd::rhi::BufferDesc sd {};
    sd.size   = bytes;
    sd.usage  = cd::rhi::BufferUsage::kTransferSrc;
    sd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto staging_r = dev.create_buffer(sd);
    if (!staging_r.has_value())
    {
        dev.destroy_texture(out.image);
        out.image = {};
        return out;
    }
    const auto staging = *staging_r;

    (void)dev.upload_buffer(
        staging, 0U,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(rgba), bytes));

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr)
    {
        dev.destroy_buffer(staging);
        dev.destroy_texture(out.image);
        out.image = {};
        return out;
    }
    cmd->begin();

    std::array<cd::rhi::TextureBarrier, 1> tb_dst {
        cd::rhi::TextureBarrier { .texture = out.image,
            .from = cd::rhi::ResourceState::kUndefined,
            .to   = cd::rhi::ResourceState::kTransferDst,
            .range = { 0U, 1U, 0U, 1U } }
    };
    cmd->barrier({}, tb_dst);

    std::array<cd::rhi::BufferImageCopyRegion, 1> regs {
        cd::rhi::BufferImageCopyRegion { .buffer_offset = 0U, .mip_level = 0U,
            .base_layer = 0U, .layer_count = 1U,
            .image_offset = { 0, 0, 0 }, .image_extent = { w, h, 1U } }
    };
    cmd->copy_buffer_to_image(staging, out.image, regs);

    std::array<cd::rhi::TextureBarrier, 1> tb_read {
        cd::rhi::TextureBarrier { .texture = out.image,
            .from = cd::rhi::ResourceState::kTransferDst,
            .to   = cd::rhi::ResourceState::kShaderResource,
            .range = { 0U, 1U, 0U, 1U } }
    };
    cmd->barrier({}, tb_read);
    cmd->end();

    cd::rhi::SubmitDesc sub {};
    std::array<cd::rhi::ICommandBuffer*, 1> cbs { cmd.get() };
    sub.command_buffers = cbs;
    (void)dev.submit(sub);
    dev.wait_idle();
    dev.destroy_buffer(staging);

    cd::rhi::TextureViewDesc vd {};
    vd.texture     = out.image;
    vd.type        = cd::rhi::TextureType::k2D;
    vd.format      = cd::rhi::Format::kRGBA8Unorm;
    vd.base_mip    = 0U; vd.mip_count   = 1U;
    vd.base_layer  = 0U; vd.layer_count = 1U;
    auto v = dev.create_texture_view(vd);
    if (!v.has_value())
    {
        dev.destroy_texture(out.image);
        out.image = {};
        return out;
    }
    out.view = *v;
    return out;
}

/// Release a GpuTexture2D's view + texture. Idempotent.
inline void destroy_texture_2d(cd::rhi::IDevice& dev, GpuTexture2D& t) noexcept
{
    if (t.view.is_valid())  dev.destroy_texture_view(t.view);
    if (t.image.is_valid()) dev.destroy_texture(t.image);
    t = {};
}

}  // namespace cd::render
