// =============================================================================
// CHROMODYNAMIC — cd/rhi/DepthStencilPresets.hpp
// Phase 42.A / Wave 210 — common DepthStencilState presets.
//
// Companion to BlendPresets. Centralizes the 4 typical depth/stencil
// recipes call sites would otherwise hand-roll:
//
//   * depth_default()  — depth_test=on,  depth_write=on,  Less
//   * depth_readonly() — depth_test=on,  depth_write=off, Less    (transparent pass)
//   * depth_disabled() — depth_test=off, depth_write=off          (UI, fullscreen quads)
//   * depth_equal()    — depth_test=on,  depth_write=off, Equal   (pre-pass + main)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>

namespace cd::rhi
{

[[nodiscard]] inline DepthStencilState depth_default() noexcept
{
    DepthStencilState s {};   // defaults already match this preset
    return s;
}

[[nodiscard]] inline DepthStencilState depth_readonly() noexcept
{
    DepthStencilState s {};
    s.depth_test    = true;
    s.depth_write   = false;
    s.depth_compare = CompareOp::kLess;
    return s;
}

[[nodiscard]] inline DepthStencilState depth_disabled() noexcept
{
    DepthStencilState s {};
    s.depth_test    = false;
    s.depth_write   = false;
    s.depth_compare = CompareOp::kAlways;
    return s;
}

[[nodiscard]] inline DepthStencilState depth_equal() noexcept
{
    DepthStencilState s {};
    s.depth_test    = true;
    s.depth_write   = false;
    s.depth_compare = CompareOp::kEqual;
    return s;
}

}  // namespace cd::rhi
