// =============================================================================
// CHROMODYNAMIC — cd/material/PbrParams.hpp
// Phase 43.A / Wave 211 — physically-based material parameters.
//
// One struct that captures the artist-facing knobs for the metallic /
// roughness PBR workflow (Khronos glTF 2.0 + Disney 2012 base). The
// uniform-buffer layout matches the std140 packing every modern engine
// follows so the same struct works on both Vulkan (UBO) and D3D12 (CB).
//
// Field defaults map to "white diffuse, fully dielectric, half-rough":
//   * albedo = (1, 1, 1, 1)
//   * metallic = 0
//   * roughness = 0.5
//   * emissive = (0, 0, 0)
//   * occlusion = 1 (no AO)
//
// All texture slots are referenced by AssetId; the binding side is the
// renderer's job. This header is data-only — no rhi types leak in,
// so the runtime, tooling, and importer all share the same definition.
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::material
{

struct PbrFactors
{
    float albedo[4]   { 1.0F, 1.0F, 1.0F, 1.0F };  // RGBA, alpha for cutout
    float emissive[3] { 0.0F, 0.0F, 0.0F };
    float metallic    { 0.0F };
    float roughness   { 0.5F };
    float occlusion   { 1.0F };
    float normal_scale { 1.0F };
    float pad        { 0.0F };
};
static_assert(sizeof(PbrFactors) == 48, "PbrFactors must stay std140-packed (48 bytes)");

struct PbrTextures
{
    cd::asset::AssetId albedo;
    cd::asset::AssetId metallic_roughness;
    cd::asset::AssetId normal;
    cd::asset::AssetId occlusion;
    cd::asset::AssetId emissive;
};

struct PbrParams
{
    PbrFactors  factors {};
    PbrTextures textures {};
};

[[nodiscard]] constexpr bool is_opaque(const PbrFactors& f) noexcept
{
    return f.albedo[3] >= 0.999F;
}

[[nodiscard]] constexpr bool is_emissive(const PbrFactors& f) noexcept
{
    return f.emissive[0] > 0.0F || f.emissive[1] > 0.0F || f.emissive[2] > 0.0F;
}

}  // namespace cd::material
