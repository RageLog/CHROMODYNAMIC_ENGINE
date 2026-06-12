// =============================================================================
// HelloRenderTargets.hpp
// -----------------------------------------------------------------------------
// hello_engine-local render-target aggregate. Bundles the boot-time creation
// of every offscreen color / depth target the sample uses for its HDR scene
// + 4-target G-buffer + ping-pong TAA history layout. Lifted out of main() in
// Marathon Run 13 phase N15a.
//
// Why one aggregate?  The 8 targets are layout-coupled (same width / height,
// shared MRT attachment layout for every scene pipeline) and ALL of them have
// to be torn down + recreated together when the window resizes.  Threading
// them through main() as 8 separate locals turned into ~70 lines of boot
// boilerplate + ~30 lines of identical recreate boilerplate in the resize
// branch; both blocks now collapse to a single call into create_render_targets.
//
// Scope rule: this aggregate carries ONLY swapchain-sized offscreen targets.
// It deliberately does NOT carry:
//   - shadow_target (logically a separate render pass at 2048x2048; lives in
//     HelloMaterials' shadow material block, not here)
//   - IBL cubemaps / Earth procedural textures (HelloIbl)
//   - per-mesh buffers / BLAS / TLAS (per-asset state, not framebuffer state)
//
// The format constants live as inline constexpr at namespace scope so the
// resize branch in main() can recreate each target with the same format
// without re-declaring them.  Every scene pipeline's MaterialDesc reads them
// from here too, so any future format change is one-line.
//
// Rendering behaviour: unchanged.  Same formats, same usage flags, same
// per-recreate failure exit codes (return 6 / 31 / 47 / 49 / 50 / 51 / 48
// preserved).
// =============================================================================
#pragma once

#include <cd/framegraph/Targets.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <cstdio>

namespace cd_sample {

// ---- Shared format / size constants -----------------------------------------
// kSampled on the depth target is required because the composite-pass GTAO
// inline AO samples scene depth after the HDR pass ends.

inline constexpr cd::rhi::Format kDepthFormat    = cd::rhi::Format::kD32Float;
inline constexpr cd::rhi::Format kHdrFormat      = cd::rhi::Format::kRGBA16Float;
inline constexpr cd::rhi::Format kNormalFormat   = cd::rhi::Format::kRGBA16Float;
inline constexpr cd::rhi::Format kAlbedoFormat   = cd::rhi::Format::kRGBA8Unorm;
inline constexpr cd::rhi::Format kMrFormat       = cd::rhi::Format::kRG8Unorm;
inline constexpr cd::rhi::Format kVelocityFormat = cd::rhi::Format::kRG16Float;
inline constexpr cd::rhi::Format kHistoryFormat  = cd::rhi::Format::kBGRA8Unorm;

/// Bundle of swapchain-sized offscreen render targets.
///
/// All targets share the same width / height (= window extent at boot, or
/// the most recent post-resize extent).  Layout discipline:
///   - location 0: hdr (RGBA16F) — main scene colour
///   - location 1: gbuf_normal (RGBA16F) — world-space normal + surface flag
///   - location 2: gbuf_albedo (RGBA8) — albedo + material flag
///   - location 3: gbuf_mr (RG8) — metallic + roughness
/// Velocity is a SEPARATE pass into gbuf_velocity (RG16F); history[2] are
/// the composite pass's TAA ping-pong buffers.
struct RenderTargets
{
    cd::framegraph::DepthTarget                depth        {};
    cd::framegraph::ColorTarget                hdr          {};
    cd::framegraph::ColorTarget                gbuf_normal  {};
    cd::framegraph::ColorTarget                gbuf_albedo  {};
    cd::framegraph::ColorTarget                gbuf_mr      {};
    cd::framegraph::ColorTarget                gbuf_velocity{};
    std::array<cd::framegraph::ColorTarget, 2> history      {};
    bool                                       ok           { false };

    /// Destroy every contained target.  Must be called before a fresh
    /// create_render_targets() into the same bundle (e.g. on window
    /// resize) so the underlying RHI textures + views are not leaked.
    void destroy(cd::rhi::IDevice& dev) noexcept
    {
        depth.destroy(dev);
        hdr.destroy(dev);
        gbuf_normal.destroy(dev);
        gbuf_albedo.destroy(dev);
        gbuf_mr.destroy(dev);
        gbuf_velocity.destroy(dev);
        for (auto& h : history) h.destroy(dev);
        ok = false;
    }
};

// ---- create_render_targets --------------------------------------------------
// Boot-time + post-resize factory.  On failure prints the offending target's
// name to stderr and returns a RenderTargets with ok=false; caller maps each
// failure to its historical exit code via the `which` field.
//
// Returns the failure code (matches pre-extract exit codes) or 0 on success.
[[nodiscard]] inline int
create_render_targets(cd::rhi::IDevice&      device,
                      cd::rhi::Extent2D      extent,
                      RenderTargets&         out)
{
    if (!cd::framegraph::create_depth_target(
            device,
            extent,
            kDepthFormat,
            out.depth,
            cd::rhi::TextureUsage::kSampled
        ))
    {
        return 6;
    }
    if (!cd::framegraph::create_color_target(device, extent, kHdrFormat, out.hdr))
    {
        return 31;
    }
    if (!cd::framegraph::create_color_target(device, extent, kNormalFormat, out.gbuf_normal))
    {
        return 47;
    }
    if (!cd::framegraph::create_color_target(device, extent, kAlbedoFormat, out.gbuf_albedo))
    {
        return 49;
    }
    if (!cd::framegraph::create_color_target(device, extent, kMrFormat, out.gbuf_mr))
    {
        return 50;
    }
    if (!cd::framegraph::create_color_target(device, extent, kVelocityFormat, out.gbuf_velocity))
    {
        return 51;
    }
    for (auto& h : out.history)
    {
        if (!cd::framegraph::create_color_target(device, extent, kHistoryFormat, h))
        {
            return 48;
        }
    }
    out.ok = true;
    return 0;
}


// =============================================================================
// phase1140: GpuTexture2D + create_texture_rgba8 — moved VERBATIM from
// hello_engine main.cpp file scope (modulo `static`) so header-level
// boot helpers (HelloBootGpu.hpp) can create procedural textures too.
// One-shot upload path: staging buffer -> barrier -> copy -> barrier ->
// submit + wait_idle (boot-time only; not for per-frame use).
// =============================================================================
// ---- GPU texture 2D --------------------------------------------------------
struct GpuTexture2D
{
    cd::rhi::TextureHandle     image {};
    cd::rhi::TextureViewHandle view  {};
};

[[nodiscard]] inline GpuTexture2D
create_texture_rgba8(cd::rhi::IDevice& dev, const std::uint8_t* rgba,
                     std::uint32_t w, std::uint32_t h)
{
    GpuTexture2D out {};
    if (rgba == nullptr || w == 0 || h == 0) return out;
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { w, h, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value()) return out;
    out.image = *img;
    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
    cd::rhi::BufferDesc sd {};
    sd.size   = bytes;
    sd.usage  = cd::rhi::BufferUsage::kTransferSrc;
    sd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto staging_r = dev.create_buffer(sd);
    if (!staging_r.has_value()) return out;
    const auto staging = *staging_r;
    (void)dev.upload_buffer(staging, 0,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(rgba), bytes));
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr) { dev.destroy_buffer(staging); return out; }
    cmd->begin();
    std::array<cd::rhi::TextureBarrier, 1> tb_dst {
        cd::rhi::TextureBarrier { .texture = out.image,
            .from = cd::rhi::ResourceState::kUndefined,
            .to   = cd::rhi::ResourceState::kTransferDst, .range = { 0,1,0,1 } }
    };
    cmd->barrier({}, tb_dst);
    std::array<cd::rhi::BufferImageCopyRegion, 1> regs {
        cd::rhi::BufferImageCopyRegion { .buffer_offset = 0, .mip_level = 0,
            .base_layer = 0, .layer_count = 1,
            .image_offset = { 0,0,0 }, .image_extent = { w,h,1 } }
    };
    cmd->copy_buffer_to_image(staging, out.image, regs);
    std::array<cd::rhi::TextureBarrier, 1> tb_read {
        cd::rhi::TextureBarrier { .texture = out.image,
            .from = cd::rhi::ResourceState::kTransferDst,
            .to   = cd::rhi::ResourceState::kShaderResource, .range = { 0,1,0,1 } }
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
    vd.base_mip    = 0; vd.mip_count   = 1;
    vd.base_layer  = 0; vd.layer_count = 1;
    auto v = dev.create_texture_view(vd);
    if (!v.has_value()) { dev.destroy_texture(out.image); out.image = {}; return out; }
    out.view = *v;
    return out;
}

} // namespace cd_sample
