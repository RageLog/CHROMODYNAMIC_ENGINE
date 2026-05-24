// =============================================================================
// CHROMODYNAMIC — cd/rhi/RasterStatePresets.hpp
// Phase 97.A / Wave 265 — common RasterState presets.
//
// Companion to BlendPresets (Phase 34) and DepthStencilPresets
// (Phase 43). PSO construction now has one-line accessors for the
// four rasterization recipes:
//
//   * raster_solid_back()      — fill + cull back faces (default mesh).
//   * raster_solid_none()      — fill + no cull (transparent, both sides).
//   * raster_wireframe()       — line draw + no cull (debug overlay).
//   * raster_shadow_front()    — fill + cull front (shadow pass).
//
// Front-face = clockwise (Vulkan/D3D12 default after the v0.25 fix).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>

namespace cd::rhi
{

[[nodiscard]] inline RasterState raster_solid_back() noexcept
{
    return RasterState {};   // defaults already match
}

[[nodiscard]] inline RasterState raster_solid_none() noexcept
{
    RasterState s {};
    s.cull = CullMode::kNone;
    return s;
}

[[nodiscard]] inline RasterState raster_wireframe() noexcept
{
    RasterState s {};
    s.polygon_mode = PolygonMode::kLine;
    s.cull         = CullMode::kNone;
    return s;
}

[[nodiscard]] inline RasterState raster_shadow_front() noexcept
{
    RasterState s {};
    s.cull = CullMode::kFront;
    return s;
}

}  // namespace cd::rhi
