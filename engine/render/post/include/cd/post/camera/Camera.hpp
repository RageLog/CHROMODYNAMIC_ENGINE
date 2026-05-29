// =============================================================================
// CHROMODYNAMIC — cd/post/camera/Camera.hpp
//
// Camera/composition post-fx — vignette, chromatic aberration, film
// grain. Header-only GLSL helper strings (consumed inline by forward
// shaders) + Settings structs for editor UI binding.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <string_view>

namespace cd::post::camera
{

struct Settings
{
    float vignette { 0.0F };          ///< [0, 1] — edge darkening intensity
    float chromatic_aberration { 0.0F };///< [0, 1] — per-channel UV offset
    float film_grain { 0.0F };         ///< [0, 1] — overlay strength
};

/// Inline fragment-shader helpers. Suitable for in-forward composition
/// before the v1.7 off-screen post-fx chain lands.
constexpr std::string_view kInlineCameraGlsl = R"glsl(
// Cone-radius vignette from a camera-relative direction. radius
// expected in roughly [0, 1.5]; smoothstep darkens edges only.
vec3 camera_vignette(vec3 c, float radial, float strength) {
  float mask = smoothstep(0.0, 1.4, radial);
  return c * mix(1.0, 1.0 - mask, strength);
}
// Cheap chromatic aberration: per-channel scale asymmetry. Real
// per-pixel UV offset needs an off-screen target; inline version
// just shifts hue toward warmth/coolness.
vec3 camera_chromatic(vec3 c, float strength) {
  c.r *= 1.0 + strength * 0.08;
  c.b *= 1.0 - strength * 0.08;
  return c;
}
// Hash-noise grain overlay. Tone over -strength/2 .. +strength/2.
vec3 camera_grain(vec3 c, vec2 world_xy, float strength) {
  float g = fract(sin(dot(world_xy, vec2(12.9898, 78.233))) * 43758.5453);
  return c + (g - 0.5) * strength * 0.05;
}
)glsl";

}  // namespace cd::post::camera
