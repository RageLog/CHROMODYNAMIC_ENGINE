// =============================================================================
// CHROMODYNAMIC — cd/rhi/BlendPresets.hpp
// Phase 33.A / Wave 201 — common BlendAttachmentState presets.
//
// Every renderer recreates the same 4 blend-state recipes by hand at
// every PSO construction site. This header centralizes them so that:
//
//   * UI / sprite path: `blend_alpha()`         (over)
//   * Particle additive: `blend_additive()`     (sum)
//   * Pre-multiplied alpha: `blend_premultiplied()`
//   * Opaque (default): `blend_opaque()`        (blend off)
//
// Each returns a fully-populated `BlendAttachmentState` ready to drop
// into `PipelineDesc::blend.attachments[i]`.
//
// `BlendAttachmentState`, `BlendFactor`, `BlendOp` are defined in
// `Descriptors.hpp` / `Enums.hpp`. This header is rhi-side, header-only,
// and free of backend-specific code (Vulkan / D3D12 / OpenGL all consume
// the same struct).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>

namespace cd::rhi
{

[[nodiscard]] inline BlendAttachmentState blend_opaque() noexcept
{
    return BlendAttachmentState {};   // blend_enable=false default
}

[[nodiscard]] inline BlendAttachmentState blend_alpha() noexcept
{
    BlendAttachmentState s {};
    s.blend_enable = true;
    s.src_color = BlendFactor::kSrcAlpha;
    s.dst_color = BlendFactor::kOneMinusSrcAlpha;
    s.color_op  = BlendOp::kAdd;
    s.src_alpha = BlendFactor::kOne;
    s.dst_alpha = BlendFactor::kOneMinusSrcAlpha;
    s.alpha_op  = BlendOp::kAdd;
    return s;
}

[[nodiscard]] inline BlendAttachmentState blend_premultiplied() noexcept
{
    BlendAttachmentState s {};
    s.blend_enable = true;
    s.src_color = BlendFactor::kOne;
    s.dst_color = BlendFactor::kOneMinusSrcAlpha;
    s.color_op  = BlendOp::kAdd;
    s.src_alpha = BlendFactor::kOne;
    s.dst_alpha = BlendFactor::kOneMinusSrcAlpha;
    s.alpha_op  = BlendOp::kAdd;
    return s;
}

[[nodiscard]] inline BlendAttachmentState blend_additive() noexcept
{
    BlendAttachmentState s {};
    s.blend_enable = true;
    s.src_color = BlendFactor::kSrcAlpha;
    s.dst_color = BlendFactor::kOne;
    s.color_op  = BlendOp::kAdd;
    s.src_alpha = BlendFactor::kOne;
    s.dst_alpha = BlendFactor::kOne;
    s.alpha_op  = BlendOp::kAdd;
    return s;
}

}  // namespace cd::rhi
