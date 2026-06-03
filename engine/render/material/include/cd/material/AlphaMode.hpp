// =============================================================================
// CHROMODYNAMIC — cd/material/AlphaMode.hpp
// M9 W3C — T1.10 lesson: glTF alphaMode (MASK/BLEND/OPAQUE) end-to-end.
//
// One canonical enum that downstream render code can share without dragging
// in the `cd::asset::gltf` header (which would create a circular tie of the
// render tier on the asset tier). The glTF loader continues to emit
// `cd::asset::gltf::GltfAlphaMode` because that's the wire-level decode; the
// render-tier `MaterialInstance` carries the same logical state via
// `cd::material::AlphaMode` (one-to-one mapping below).
//
// Why a tiny dedicated header:
//   * Both `MaterialInstance` and any future shader-side material switcher
//     (cd::scene, cd::ui_renderer_rhi, the editor) need to read alpha state
//     without pulling pipeline / descriptor headers.
//   * Keeps the type trivially default-constructible — no RHI handles,
//     no allocation.
//
// Spec mapping (glTF 2.0 §3.9 - "Materials"):
//   OPAQUE  → kOpaque  : alpha is fully ignored, depth-tested + depth-written.
//   MASK    → kMask    : fragments where alpha < alphaCutoff are discarded.
//   BLEND   → kBlend   : alpha is the source factor for over-blending; depth
//                        write disabled by the renderer's sort path.
// =============================================================================
#pragma once

#include <cstdint>

namespace cd::material
{

/// Engine-side alphaMode mirror. Identical numeric values to
/// `cd::asset::gltf::GltfAlphaMode` so a `static_cast` round-trip is valid,
/// but the two types are deliberately kept distinct so render-tier headers
/// don't transitively pull in tinygltf via the asset loader.
enum class AlphaMode : std::uint8_t
{
    kOpaque = 0,  ///< default — no alpha test, no blend
    kMask   = 1,  ///< fragment discarded when alpha < alpha_cutoff
    kBlend  = 2,  ///< alpha-blended; sort + no depth write at the renderer
};

/// Trivially-copyable bundle that travels with a `MaterialInstance` or any
/// render-side material aggregate. Designed to be passed by value.
struct AlphaParams
{
    AlphaMode mode { AlphaMode::kOpaque };
    /// Cutoff threshold used when `mode == kMask`. Ignored otherwise. Default
    /// matches the glTF 2.0 spec default of 0.5.
    float cutoff { 0.5F };
};

/// Convenience predicate: does this material need an alpha test in the shader?
[[nodiscard]] constexpr bool needs_alpha_test(AlphaMode m) noexcept
{
    return m == AlphaMode::kMask;
}

/// Convenience predicate: does this material need a blended pipeline?
[[nodiscard]] constexpr bool needs_blend(AlphaMode m) noexcept
{
    return m == AlphaMode::kBlend;
}

}  // namespace cd::material
