// =============================================================================
// CHROMODYNAMIC — cd/framegraph/Targets.hpp
//
// RAII wrappers for the most common render-pass targets (color +
// depth). These are intentionally tiny — a handle pair (image +
// view) + the extent/format the caller asked for — and they own the
// underlying RHI resources via `destroy(device)`.
//
// They sit alongside the full FrameGraph because most samples need
// the helpers without buying into the declarative graph yet. The
// FrameGraph itself uses similar machinery for transient resources;
// these helpers are for *manual* lifetime management.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>

namespace cd::framegraph
{

// ---- Color target -----------------------------------------------------------

struct ColorTarget
{
    cd::rhi::TextureHandle     image  {};
    cd::rhi::TextureViewHandle view   {};
    cd::rhi::Extent2D          extent {};
    cd::rhi::Format            format { cd::rhi::Format::kRGBA16Float };

    void destroy(cd::rhi::IDevice& dev) noexcept
    {
        if (view.is_valid())  dev.destroy_texture_view(view);
        if (image.is_valid()) dev.destroy_texture(image);
        *this = {};
    }
};

/// Allocate a 2D color target with kColorAttachment + kSampled + kStorage
/// usage. The kStorage flag is needed for compute-shader bloom/AO writes;
/// engines that don't need it can shave a few ms on certain drivers by
/// calling create_texture directly with a tighter usage mask.
[[nodiscard]] inline bool
create_color_target(cd::rhi::IDevice& dev,
                    cd::rhi::Extent2D size,
                    cd::rhi::Format format,
                    ColorTarget& out)
{
    out.destroy(dev);
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = format;
    td.extent       = { size.width, size.height, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kColorAttachment |
                      cd::rhi::TextureUsage::kSampled |
                      cd::rhi::TextureUsage::kStorage;
    td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value()) return false;
    cd::rhi::TextureViewDesc vd {};
    vd.texture     = *img;
    vd.type        = cd::rhi::TextureType::k2D;
    vd.format      = format;
    vd.base_mip    = 0;
    vd.mip_count   = 1;
    vd.base_layer  = 0;
    vd.layer_count = 1;
    auto v = dev.create_texture_view(vd);
    if (!v.has_value())
    {
        dev.destroy_texture(*img);
        return false;
    }
    out.image  = *img;
    out.view   = *v;
    out.extent = size;
    out.format = format;
    return true;
}

// ---- Depth target -----------------------------------------------------------

struct DepthTarget
{
    cd::rhi::TextureHandle     image  {};
    cd::rhi::TextureViewHandle view   {};
    cd::rhi::Extent2D          extent {};

    void destroy(cd::rhi::IDevice& dev) noexcept
    {
        if (view.is_valid())  dev.destroy_texture_view(view);
        if (image.is_valid()) dev.destroy_texture(image);
        *this = {};
    }
};

/// Allocate a depth/stencil 2D target. `extra_usage` is OR'd into the
/// kDepthStencilAttachment base usage — typically kSampled when a
/// composite/AO pass needs to read the depth, kNone otherwise.
[[nodiscard]] inline bool
create_depth_target(cd::rhi::IDevice& dev,
                    cd::rhi::Extent2D size,
                    cd::rhi::Format format,
                    DepthTarget& out,
                    cd::rhi::TextureUsage extra_usage = cd::rhi::TextureUsage::kNone)
{
    out.destroy(dev);
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = format;
    td.extent       = { size.width, size.height, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kDepthStencilAttachment | extra_usage;
    td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value()) return false;
    cd::rhi::TextureViewDesc vd {};
    vd.texture     = *img;
    vd.type        = cd::rhi::TextureType::k2D;
    vd.format      = format;
    vd.base_mip    = 0;
    vd.mip_count   = 1;
    vd.base_layer  = 0;
    vd.layer_count = 1;
    auto v = dev.create_texture_view(vd);
    if (!v.has_value())
    {
        dev.destroy_texture(*img);
        return false;
    }
    out.image  = *img;
    out.view   = *v;
    out.extent = size;
    return true;
}

}  // namespace cd::framegraph
