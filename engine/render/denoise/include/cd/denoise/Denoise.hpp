// =============================================================================
// CHROMODYNAMIC — cd/denoise/Denoise.hpp
// Day 30/J — Path-traced image denoiser.
//
// Two paths, same public API:
//   * AtrousFilter — edge-aware a-trous wavelet (Dammertz 2010 /
//     used by SVGF). CPU + GLSL implementations. No external deps.
//   * OidnFilter — slot for an OpenImageDenoise (Intel) backend.
//     Reference declaration is here; the actual wire-up is opt-in
//     via the CD_ENABLE_OIDN CMake option and a separate vcpkg /
//     FetchContent dep — see the integration ADR.
//
// References:
//   * Dammertz, Sewtz, Hanika, Lensch 2010 — "Edge-Avoiding A-trous
//     Wavelet Transform for Fast Global Illumination Filtering".
//   * Schied et al. 2017 — SVGF (uses a-trous as the spatial pass).
//   * Intel OpenImageDenoise 2.x — production-grade U-Net path.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::denoise
{

struct AuxBuffers
{
    std::uint32_t w { 0 };
    std::uint32_t h { 0 };
    std::span<const cd::math::Vec3f> color;   ///< HDR linear input
    std::span<const cd::math::Vec3f> albedo;  ///< primary-ray albedo
    std::span<const cd::math::Vec3f> normal;  ///< world normal in [-1, 1]
    std::span<const float>           depth;   ///< linear depth
};

struct AtrousSettings
{
    std::uint32_t iterations    { 5 };     ///< 5 = SVGF default
    float         sigma_color   { 0.45F };
    float         sigma_normal  { 0.5F };
    float         sigma_depth   { 0.5F };
};

/// Edge-stopping function from Dammertz 2010. Returns the per-tap
/// weight given two pixels' colour, normal, and depth differences.
[[nodiscard]] inline float
edge_weight(cd::math::Vec3f dc,
            cd::math::Vec3f dn,
            float            dz,
            const AtrousSettings& s) noexcept
{
    const float w_c = std::exp(-(dc.x * dc.x + dc.y * dc.y + dc.z * dc.z) /
                               (s.sigma_color * s.sigma_color + 1e-8F));
    const float w_n = std::pow(std::max(0.0F, dn.x * dn.x + dn.y * dn.y + dn.z * dn.z),
                               1.0F / std::max(s.sigma_normal, 1e-3F));
    const float w_z = std::exp(-std::abs(dz) /
                               (s.sigma_depth + 1e-3F));
    return w_c * (1.0F - std::clamp(w_n, 0.0F, 1.0F)) * w_z;
}

/// One a-trous iteration at step size `step` (powers of two: 1, 2,
/// 4, 8, 16 across the 5 SVGF iterations). 5×5 kernel.
inline void
atrous_iteration(const AuxBuffers& src_aux,
                 std::span<const cd::math::Vec3f> src_color,
                 std::span<cd::math::Vec3f>       dst_color,
                 std::uint32_t                    step,
                 const AtrousSettings&            s)
{
    static constexpr std::array<float, 5> kKernel { 1.0F / 16.0F,
                                                     1.0F /  4.0F,
                                                     3.0F /  8.0F,
                                                     1.0F /  4.0F,
                                                     1.0F / 16.0F };
    for (std::uint32_t y = 0; y < src_aux.h; ++y)
    {
        for (std::uint32_t x = 0; x < src_aux.w; ++x)
        {
            const std::size_t centre = static_cast<std::size_t>(y) * src_aux.w + x;
            const cd::math::Vec3f c0 = src_color[centre];
            const cd::math::Vec3f n0 = src_aux.normal[centre];
            const float           d0 = src_aux.depth[centre];
            cd::math::Vec3f sum { 0, 0, 0 };
            float wsum = 0.0F;
            for (int dy = -2; dy <= 2; ++dy)
            {
                for (int dx = -2; dx <= 2; ++dx)
                {
                    const std::int32_t sx = static_cast<std::int32_t>(x) +
                                            dx * static_cast<std::int32_t>(step);
                    const std::int32_t sy = static_cast<std::int32_t>(y) +
                                            dy * static_cast<std::int32_t>(step);
                    if (sx < 0 || std::cmp_greater_equal(sx, src_aux.w) ||
                        sy < 0 || std::cmp_greater_equal(sy, src_aux.h)) continue;
                    const std::size_t off = static_cast<std::size_t>(sy) * src_aux.w +
                                            static_cast<std::size_t>(sx);
                    const cd::math::Vec3f c1 = src_color[off];
                    const cd::math::Vec3f n1 = src_aux.normal[off];
                    const float           d1 = src_aux.depth[off];
                    const cd::math::Vec3f dc { c0.x - c1.x, c0.y - c1.y, c0.z - c1.z };
                    const cd::math::Vec3f dn { n0.x - n1.x, n0.y - n1.y, n0.z - n1.z };
                    const float w = edge_weight(dc, dn, d0 - d1, s) *
                                    kKernel[static_cast<std::size_t>(dy) + 2] *
                                    kKernel[static_cast<std::size_t>(dx) + 2];
                    sum.x += c1.x * w; sum.y += c1.y * w; sum.z += c1.z * w;
                    wsum += w;
                }
            }
            const float inv = (wsum > 1e-6F) ? 1.0F / wsum : 1.0F;
            dst_color[centre] = { sum.x * inv, sum.y * inv, sum.z * inv };
        }
    }
}

/// Full multi-iteration a-trous denoise. Returns the denoised colour
/// buffer (host-side; output is the same w*h as the input aux).
[[nodiscard]] inline std::vector<cd::math::Vec3f>
denoise_atrous(const AuxBuffers& aux, const AtrousSettings& s)
{
    const std::size_t total = static_cast<std::size_t>(aux.w) * aux.h;
    std::vector<cd::math::Vec3f> a(aux.color.begin(), aux.color.end());
    std::vector<cd::math::Vec3f> b(total);
    for (std::uint32_t it = 0; it < s.iterations; ++it)
    {
        const std::uint32_t step = 1U << it;
        atrous_iteration(aux,
                         std::span<const cd::math::Vec3f>(a),
                         std::span<cd::math::Vec3f>(b),
                         step, s);
        std::swap(a, b);
    }
    return a;
}

// ---- OIDN API slot ----------------------------------------------------------
// Compile-time stub. When CD_ENABLE_OIDN is defined and the OIDN
// dependency is wired in, this enum + function gain a non-empty body
// in cd/denoise/OidnBackend.cpp. Today the stub returns the input
// unchanged so consumers can write call sites against the final API.

enum class OidnFilterKind : std::uint8_t
{
    kRT       = 0,  ///< single-frame ray-traced denoise
    kRTLightmap = 1, ///< higher-quality offline path for lightmap bakes
};

[[nodiscard]] inline std::vector<cd::math::Vec3f>
denoise_oidn(const AuxBuffers& aux, OidnFilterKind /*kind*/)
{
    return { aux.color.begin(), aux.color.end() };
}

// ---- GLSL a-trous kernel ----------------------------------------------------

constexpr std::string_view kAtrousCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D color_in;
layout(set = 0, binding = 1) uniform sampler2D normal_in;
layout(set = 0, binding = 2) uniform sampler2D depth_in;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D color_out;
layout(push_constant) uniform PC {
  vec2  size;
  uint  step;
  float sigma_c;
  float sigma_n;
  float sigma_z;
  uint  padding0_;
  uint  padding1_;
} pc;

const float kK[5] = float[5](1.0/16.0, 1.0/4.0, 3.0/8.0, 1.0/4.0, 1.0/16.0);

void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.size;
  vec3 c0 = texture(color_in,  uv).rgb;
  vec3 n0 = normalize(texture(normal_in, uv).xyz * 2.0 - 1.0);
  float d0 = texture(depth_in,  uv).r;
  vec3 sum = vec3(0); float wsum = 0.0;
  for (int dy = -2; dy <= 2; ++dy) {
    for (int dx = -2; dx <= 2; ++dx) {
      vec2 off = vec2(dx, dy) * float(pc.step) / pc.size;
      vec3 c1 = texture(color_in,  uv + off).rgb;
      vec3 n1 = normalize(texture(normal_in, uv + off).xyz * 2.0 - 1.0);
      float d1 = texture(depth_in, uv + off).r;
      vec3 dc = c0 - c1; vec3 dn = n0 - n1;
      float w_c = exp(-dot(dc, dc) / (pc.sigma_c * pc.sigma_c + 1e-8));
      float w_n = pow(max(0.0, dot(dn, dn)), 1.0 / max(pc.sigma_n, 1e-3));
      float w_z = exp(-abs(d0 - d1) / (pc.sigma_z + 1e-3));
      float w   = w_c * (1.0 - clamp(w_n, 0.0, 1.0)) * w_z *
                  kK[dy + 2] * kK[dx + 2];
      sum += c1 * w; wsum += w;
    }
  }
  imageStore(color_out, ivec2(p), vec4(sum / max(wsum, 1e-6), 1.0));
}
)glsl";

}  // namespace cd::denoise
