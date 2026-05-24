// =============================================================================
// CHROMODYNAMIC — cd/scene/Skybox.hpp
// Phase 89.A / Wave 257 — skybox cubemap + tint params.
//
// `Skybox` is the asset + render-time tint for the background dome.
// The renderer draws a full-screen pass that samples the cubemap by
// `normalize(view_dir)` and multiplies by `tint * intensity`. When
// no cubemap is bound, the tint alone is used as a flat color.
//
// `EnvironmentLight` (Phase 48) is the *lighting contribution* of the
// sky; `Skybox` is the *visual rendering* of it. They reference
// different cubemap assets in general (radiance vs irradiance vs
// rendered sky).
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

namespace cd::scene
{

struct Skybox
{
    cd::asset::AssetId  cubemap;
    cd::math::Vec3f     tint      { 1.0F, 1.0F, 1.0F };
    float               intensity { 1.0F };
    float               rotation_y { 0.0F };   // radians; rotate around world up
};

[[nodiscard]] constexpr bool has_cubemap(const Skybox& s) noexcept
{
    return s.cubemap.is_valid();
}

}  // namespace cd::scene
