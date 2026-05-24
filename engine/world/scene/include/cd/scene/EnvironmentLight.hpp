// =============================================================================
// CHROMODYNAMIC — cd/scene/EnvironmentLight.hpp
// Phase 47.A / Wave 215 — IBL environment + ambient parameters.
//
// Light data the cluster/PBR path needs once per scene (not per frame
// uniform): the sky / cubemap reference, an exposure scalar, and a
// flat ambient fallback for offline shading.
//
// `cubemap` is an AssetId to a pre-filtered radiance cubemap (cdtex
// or similar). `intensity` is a multiplier applied at sample time.
// `ambient_rgb[3]` is the fallback flat color when the cubemap is
// missing or for visualization passes.
//
// Tone-mapping (exposure / film curve) belongs to the camera; this
// struct only carries the **light** parameters.
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/core/Defines.hpp>

namespace cd::scene
{

struct EnvironmentLight
{
    cd::asset::AssetId cubemap;       // pre-filtered IBL radiance
    cd::asset::AssetId irradiance;    // diffuse SH or low-res cubemap
    float              intensity { 1.0F };
    float              ambient_rgb[3] { 0.05F, 0.05F, 0.06F };
};

[[nodiscard]] constexpr bool has_ibl(const EnvironmentLight& e) noexcept
{
    return e.cubemap.is_valid();
}

}  // namespace cd::scene
