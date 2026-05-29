// =============================================================================
// CHROMODYNAMIC — cd/post/bloom/Bloom.hpp
// Day 8 — Stable bloom (Karis 2013).
//
// Multi-stage compute downsample + upsample, with Karis's weighted
// 13-tap "average of 5 partial averages" anti-firefly trick at the
// brightest stage.
//
// Pipeline (5 stages by default):
//   stage 0: full-resolution HDR input
//   stage 1: half-res    downsample (Karis weighted)
//   stage 2: quarter-res downsample
//   stage 3: eighth-res  downsample
//   stage 4: 1/16-res    downsample
//   then 9-tap tent UP-sample 4 -> 3 -> 2 -> 1 -> 0 with additive blend
//
// Reference: Jorge Jimenez & Karis, "Next-Generation Post Processing in
// Call of Duty: Advanced Warfare" SIGGRAPH 2014 + Karis 2013 talk
// ("ACES Tonemap & Bloom: Production Notes").
// =============================================================================
#pragma once

#include <cd/framegraph/Targets.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/IDevice.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cd::post::bloom
{

struct Settings
{
    /// Number of mip stages (input + downsample levels). Recommended 5-6.
    std::uint32_t stage_count { 5 };
    /// Threshold in linear RGB — pixels below this don't contribute to bloom.
    /// 1.0 keeps only over-display-white pixels (the canonical filmic
    /// usage). Lower for a "halo on everything" look.
    float threshold { 1.0F };
    /// Soft-knee width around the threshold so bloom doesn't snap on.
    float knee { 0.5F };
    /// Per-stage intensity (added on top of the previous stage's upsample).
    /// 0.04 is the "Karis stable" default.
    float intensity { 0.04F };
    /// Sample radius (in texels) for the upsample tent filter. 1.0 = single
    /// tap, 4.0 = wider halo.
    float radius { 1.5F };
};

/// Karis "average of 5 partial averages of 4 taps" reduction. Computes
/// a single output texel from 13 input texels arranged like:
///
///     A   B   C
///       J   K
///     D   E   F
///       L   M
///     G   H   I
///
/// where E is the centre. Returns the weighted mean. Used by the
/// downsample kernel; the weighting suppresses fireflies (single very
/// bright pixels that would otherwise dominate the average).
[[nodiscard]] inline cd::math::Vec3f
karis_13tap_reduce(std::span<const cd::math::Vec3f, 13> t) noexcept
{
    constexpr float w_centre = 0.5F * 0.125F;
    constexpr float w_corner = 0.125F * 0.125F;
    constexpr float w_inner  = 0.5F * 0.125F;
    auto sum = [&](std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3) {
        return cd::math::Vec3f {
            (t[i0].x + t[i1].x + t[i2].x + t[i3].x) * 0.25F,
            (t[i0].y + t[i1].y + t[i2].y + t[i3].y) * 0.25F,
            (t[i0].z + t[i1].z + t[i2].z + t[i3].z) * 0.25F };
    };
    // 5 partial averages — centre group + 4 corner groups.
    const auto centre = sum(4, 5, 7, 8);
    const auto tl     = sum(0, 1, 3, 4);
    const auto tr     = sum(1, 2, 4, 5);
    const auto bl     = sum(3, 4, 6, 7);
    const auto br     = sum(4, 5, 7, 8);
    return cd::math::Vec3f {
        centre.x * w_inner + (tl.x + tr.x + bl.x + br.x) * w_corner +
            (t[9].x + t[10].x + t[11].x + t[12].x) * w_centre * 0.0F,
        centre.y * w_inner + (tl.y + tr.y + bl.y + br.y) * w_corner,
        centre.z * w_inner + (tl.z + tr.z + bl.z + br.z) * w_corner };
}

/// Apply the soft-knee threshold to a pre-bloom HDR pixel. Returns the
/// portion of the pixel that bleeds into bloom; pixels well under the
/// knee return 0, pixels well over return the full pixel minus
/// threshold, and the transition is a smooth quadratic (Karis 2013).
[[nodiscard]] inline cd::math::Vec3f
soft_threshold(cd::math::Vec3f c, const Settings& s) noexcept
{
    const float br = std::max({ c.x, c.y, c.z });
    const float rq = std::clamp(br - s.threshold + s.knee, 0.0F, 2.0F * s.knee);
    const float scale = (rq * rq) / (4.0F * s.knee + 1e-4F);
    const float factor = std::max(br - s.threshold, scale) / std::max(br, 1e-4F);
    return { c.x * factor, c.y * factor, c.z * factor };
}

// ---- CPU reference downsample / upsample ------------------------------------
// Box-filter halve / 9-tap tent grow. These are reference implementations
// used by the tests + offline content cooking; production runs the GLSL
// versions below on the GPU compute pipeline.

struct CpuImage
{
    std::uint32_t w { 0 };
    std::uint32_t h { 0 };
    std::vector<cd::math::Vec3f> pixels;

    [[nodiscard]] const cd::math::Vec3f&
    at(std::uint32_t x, std::uint32_t y) const noexcept
    {
        return pixels[y * w + x];
    }
    cd::math::Vec3f& at(std::uint32_t x, std::uint32_t y) noexcept
    {
        return pixels[y * w + x];
    }
};

[[nodiscard]] inline CpuImage downsample_box(const CpuImage& src)
{
    CpuImage dst { std::max(1U, src.w / 2U), std::max(1U, src.h / 2U), {} };
    dst.pixels.resize(static_cast<std::size_t>(dst.w) * dst.h);
    for (std::uint32_t y = 0; y < dst.h; ++y)
    {
        for (std::uint32_t x = 0; x < dst.w; ++x)
        {
            const std::uint32_t sx = std::min(src.w - 1, x * 2U);
            const std::uint32_t sy = std::min(src.h - 1, y * 2U);
            const auto& p00 = src.at(sx, sy);
            const auto& p10 = src.at(std::min(sx + 1, src.w - 1), sy);
            const auto& p01 = src.at(sx, std::min(sy + 1, src.h - 1));
            const auto& p11 = src.at(std::min(sx + 1, src.w - 1),
                                     std::min(sy + 1, src.h - 1));
            dst.at(x, y) = {
                (p00.x + p10.x + p01.x + p11.x) * 0.25F,
                (p00.y + p10.y + p01.y + p11.y) * 0.25F,
                (p00.z + p10.z + p01.z + p11.z) * 0.25F };
        }
    }
    return dst;
}

[[nodiscard]] inline CpuImage upsample_tent(const CpuImage& src,
                                            std::uint32_t dst_w,
                                            std::uint32_t dst_h)
{
    CpuImage dst { dst_w, dst_h, {} };
    dst.pixels.resize(static_cast<std::size_t>(dst_w) * dst_h);
    const float u_scale = static_cast<float>(src.w) / static_cast<float>(dst_w);
    const float v_scale = static_cast<float>(src.h) / static_cast<float>(dst_h);
    for (std::uint32_t y = 0; y < dst_h; ++y)
    {
        for (std::uint32_t x = 0; x < dst_w; ++x)
        {
            const float fx = (static_cast<float>(x) + 0.5F) * u_scale - 0.5F;
            const float fy = (static_cast<float>(y) + 0.5F) * v_scale - 0.5F;
            const auto sx = static_cast<std::int32_t>(std::floor(fx));
            const auto sy = static_cast<std::int32_t>(std::floor(fy));
            const float tx = fx - static_cast<float>(sx);
            const float ty = fy - static_cast<float>(sy);
            auto sample = [&](std::int32_t ix, std::int32_t iy) {
                ix = std::clamp(ix, 0, static_cast<std::int32_t>(src.w) - 1);
                iy = std::clamp(iy, 0, static_cast<std::int32_t>(src.h) - 1);
                return src.at(static_cast<std::uint32_t>(ix),
                              static_cast<std::uint32_t>(iy));
            };
            const auto p00 = sample(sx,     sy);
            const auto p10 = sample(sx + 1, sy);
            const auto p01 = sample(sx,     sy + 1);
            const auto p11 = sample(sx + 1, sy + 1);
            dst.at(x, y) = {
                p00.x * (1 - tx) * (1 - ty) + p10.x * tx * (1 - ty) +
                p01.x * (1 - tx) *      ty  + p11.x * tx *      ty,
                p00.y * (1 - tx) * (1 - ty) + p10.y * tx * (1 - ty) +
                p01.y * (1 - tx) *      ty  + p11.y * tx *      ty,
                p00.z * (1 - tx) * (1 - ty) + p10.z * tx * (1 - ty) +
                p01.z * (1 - tx) *      ty  + p11.z * tx *      ty };
        }
    }
    return dst;
}

// ---- GLSL kernels -----------------------------------------------------------
// Compute-shader source strings ready to feed the shader compiler. The
// pipeline glues them via a host-side render-graph pass.

constexpr std::string_view kDownsampleCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D src;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D dst;
layout(push_constant) uniform PC { vec2 src_size; vec2 dst_size; } pc;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.dst_size.x) || p.y >= uint(pc.dst_size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.dst_size;
  vec2 px = 1.0 / pc.src_size;
  // Karis 13-tap weighted reduction.
  vec3 A = texture(src, uv + px * vec2(-1, -1)).rgb;
  vec3 B = texture(src, uv + px * vec2( 0, -1)).rgb;
  vec3 C = texture(src, uv + px * vec2( 1, -1)).rgb;
  vec3 D = texture(src, uv + px * vec2(-1,  0)).rgb;
  vec3 E = texture(src, uv).rgb;
  vec3 F = texture(src, uv + px * vec2( 1,  0)).rgb;
  vec3 G = texture(src, uv + px * vec2(-1,  1)).rgb;
  vec3 H = texture(src, uv + px * vec2( 0,  1)).rgb;
  vec3 I = texture(src, uv + px * vec2( 1,  1)).rgb;
  vec3 J = texture(src, uv + px * vec2(-0.5, -0.5)).rgb;
  vec3 K = texture(src, uv + px * vec2( 0.5, -0.5)).rgb;
  vec3 L = texture(src, uv + px * vec2(-0.5,  0.5)).rgb;
  vec3 M = texture(src, uv + px * vec2( 0.5,  0.5)).rgb;
  // 5 weighted partial averages.
  vec3 partial = (J + K + L + M) * (0.5  / 4.0)
               + (A + B + D + E) * (0.125 / 4.0)
               + (B + C + E + F) * (0.125 / 4.0)
               + (D + E + G + H) * (0.125 / 4.0)
               + (E + F + H + I) * (0.125 / 4.0);
  imageStore(dst, ivec2(p), vec4(partial, 1.0));
}
)glsl";

constexpr std::string_view kUpsampleCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D src;
layout(set = 0, binding = 1, rgba16f) uniform image2D dst;
layout(push_constant) uniform PC {
  vec2  src_size; vec2  dst_size;
  float radius;   float intensity;
} pc;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.dst_size.x) || p.y >= uint(pc.dst_size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.dst_size;
  vec2 px = pc.radius / pc.src_size;
  // 9-tap tent filter.
  vec3 sum  = texture(src, uv + px * vec2(-1, -1)).rgb * 1.0;
  sum      += texture(src, uv + px * vec2( 0, -1)).rgb * 2.0;
  sum      += texture(src, uv + px * vec2( 1, -1)).rgb * 1.0;
  sum      += texture(src, uv + px * vec2(-1,  0)).rgb * 2.0;
  sum      += texture(src, uv                       ).rgb * 4.0;
  sum      += texture(src, uv + px * vec2( 1,  0)).rgb * 2.0;
  sum      += texture(src, uv + px * vec2(-1,  1)).rgb * 1.0;
  sum      += texture(src, uv + px * vec2( 0,  1)).rgb * 2.0;
  sum      += texture(src, uv + px * vec2( 1,  1)).rgb * 1.0;
  sum *= (1.0 / 16.0);
  vec3 prev = imageLoad(dst, ivec2(p)).rgb;
  imageStore(dst, ivec2(p), vec4(prev + sum * pc.intensity, 1.0));
}
)glsl";

// ---- Fragment-shader variants (forward composite pipeline) ------------------
// These mirror the compute kernels above but render via a fullscreen-
// triangle FS pass. Engines using a forward composite chain (vs the
// compute path) pick these. Same Karis 13-tap math, same 9-tap tent.
// Push-constants packed in 16-byte vec4s for trivial std140 layout.

constexpr std::string_view kPrefilterFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D src;
layout(push_constant) uniform PC {
  vec4 params; // x=threshold, y=knee, z=_, w=_
} pc;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
  vec3 c = texture(src, v_uv).rgb;
  float br = max(c.r, max(c.g, c.b));
  float thr = max(pc.params.x, 1e-4);
  float knee = max(pc.params.y, 1e-4);
  float rq = clamp(br - thr + knee, 0.0, 2.0 * knee);
  float scale = (rq * rq) / (4.0 * knee + 1e-4);
  float factor = max(br - thr, scale) / max(br, 1e-4);
  out_color = vec4(c * factor, 1.0);
}
)glsl";

constexpr std::string_view kDownsampleFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D src;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
  vec2 px = 1.0 / vec2(textureSize(src, 0));
  vec3 A = texture(src, v_uv + px * vec2(-1, -1)).rgb;
  vec3 B = texture(src, v_uv + px * vec2( 0, -1)).rgb;
  vec3 C = texture(src, v_uv + px * vec2( 1, -1)).rgb;
  vec3 D = texture(src, v_uv + px * vec2(-1,  0)).rgb;
  vec3 E = texture(src, v_uv                       ).rgb;
  vec3 F = texture(src, v_uv + px * vec2( 1,  0)).rgb;
  vec3 G = texture(src, v_uv + px * vec2(-1,  1)).rgb;
  vec3 H = texture(src, v_uv + px * vec2( 0,  1)).rgb;
  vec3 I = texture(src, v_uv + px * vec2( 1,  1)).rgb;
  vec3 J = texture(src, v_uv + px * vec2(-0.5, -0.5)).rgb;
  vec3 K = texture(src, v_uv + px * vec2( 0.5, -0.5)).rgb;
  vec3 L = texture(src, v_uv + px * vec2(-0.5,  0.5)).rgb;
  vec3 M = texture(src, v_uv + px * vec2( 0.5,  0.5)).rgb;
  vec3 partial = (J + K + L + M) * (0.5  / 4.0)
               + (A + B + D + E) * (0.125 / 4.0)
               + (B + C + E + F) * (0.125 / 4.0)
               + (D + E + G + H) * (0.125 / 4.0)
               + (E + F + H + I) * (0.125 / 4.0);
  out_color = vec4(partial, 1.0);
}
)glsl";

constexpr std::string_view kUpsampleFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D src;
layout(push_constant) uniform PC {
  vec4 params; // x=radius, y=intensity, z=_, w=_
} pc;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
  vec2 px = pc.params.x / vec2(textureSize(src, 0));
  vec3 sum  = texture(src, v_uv + px * vec2(-1, -1)).rgb * 1.0;
  sum      += texture(src, v_uv + px * vec2( 0, -1)).rgb * 2.0;
  sum      += texture(src, v_uv + px * vec2( 1, -1)).rgb * 1.0;
  sum      += texture(src, v_uv + px * vec2(-1,  0)).rgb * 2.0;
  sum      += texture(src, v_uv                       ).rgb * 4.0;
  sum      += texture(src, v_uv + px * vec2( 1,  0)).rgb * 2.0;
  sum      += texture(src, v_uv + px * vec2(-1,  1)).rgb * 1.0;
  sum      += texture(src, v_uv + px * vec2( 0,  1)).rgb * 2.0;
  sum      += texture(src, v_uv + px * vec2( 1,  1)).rgb * 1.0;
  sum *= (1.0 / 16.0);
  out_color = vec4(sum * pc.params.y, 1.0);
}
)glsl";

/// Push-constant block matching kPrefilterFS layout (16 bytes).
struct PrefilterPush
{
    float params[4]; ///< x=threshold, y=knee, z/w reserved
};
static_assert(sizeof(PrefilterPush) == 16, "PrefilterPush layout drift");

/// Push-constant block matching kUpsampleFS layout (16 bytes).
struct UpsamplePush
{
    float params[4]; ///< x=radius, y=intensity, z/w reserved
};
static_assert(sizeof(UpsamplePush) == 16, "UpsamplePush layout drift");

/// Recommended default mip count for the FS-variant chain. 4 levels at
/// /2 .. /16 produces a visibly soft halo without crushing detail.
inline constexpr std::uint32_t kDefaultMipCount = 4;

// ---- Bloom mip chain (RAII over `kDefaultMipCount` color targets) -----------
//
// Each level is half the previous one's extent (clamped to 1 px min).
// mip0 = base/2, mip1 = base/4, ..., mip3 = base/16.
// Pair with kPrefilterFS (HDR → mip0), kDownsampleFS (mip i → mip i+1),
// and kUpsampleFS (mip i → mip i-1, additive blend).

struct BloomMipChain
{
    static constexpr std::uint32_t kCount = kDefaultMipCount;
    std::array<cd::framegraph::ColorTarget, kCount> mips {};

    void destroy(cd::rhi::IDevice& dev) noexcept
    {
        for (auto& m : mips) m.destroy(dev);
    }
};

/// Allocate the bloom mip chain at progressively halving extents from
/// `base`. Returns true on success; on failure all already-allocated
/// mips are destroyed and `out` is left empty.
[[nodiscard]] inline bool
create_bloom_chain(cd::rhi::IDevice& dev,
                   cd::rhi::Extent2D base,
                   BloomMipChain& out)
{
    out.destroy(dev);
    cd::rhi::Extent2D s {
        std::max(1U, base.width  / 2U),
        std::max(1U, base.height / 2U) };
    for (std::uint32_t i = 0; i < BloomMipChain::kCount; ++i)
    {
        if (!cd::framegraph::create_color_target(
                dev, s, cd::rhi::Format::kRGBA16Float, out.mips[i]))
        {
            out.destroy(dev);
            return false;
        }
        s.width  = std::max(1U, s.width  / 2U);
        s.height = std::max(1U, s.height / 2U);
    }
    return true;
}

constexpr std::string_view kPrefilterCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D src;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D dst;
layout(push_constant) uniform PC { vec2 size; float threshold; float knee; } pc;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.size;
  vec3 c = texture(src, uv).rgb;
  float br = max(c.r, max(c.g, c.b));
  float knee = pc.knee;
  float rq = clamp(br - pc.threshold + knee, 0.0, 2.0 * knee);
  float scale = (rq * rq) / (4.0 * knee + 1e-4);
  float factor = max(br - pc.threshold, scale) / max(br, 1e-4);
  imageStore(dst, ivec2(p), vec4(c * factor, 1.0));
}
)glsl";

}  // namespace cd::post::bloom
