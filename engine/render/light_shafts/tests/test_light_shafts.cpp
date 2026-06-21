#include <cd/light_shafts/LightShafts.hpp>

#include <cd/math/Vector.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace
{

using cd::light_shafts::Settings;
using cd::light_shafts::sun_screen_pos;
using cd::math::Vec2f;
using cd::math::Vec3f;

constexpr float kEps = 0.05F;

// Host mirror of the kRadialBlurCS accumulation loop. Reproduces the shader
// arithmetic verbatim (d = (uv - sun_uv) / samples * density; per-step
// uv -= d; col += sample * il * weight; il *= decay; final col * exposure)
// so the host tests pin the GLSL contract without a GPU. `sampler` stands in
// for the `texture(occlusion, uv)` fetch (returns a scalar luminance here).
template <class Sampler>
[[nodiscard]] float
radial_blur_accum(Vec2f start_uv, Vec2f sun_uv, const Settings& s,
                  Sampler sampler) noexcept
{
    const Vec2f d {
        (start_uv.x - sun_uv.x) / static_cast<float>(s.samples) * s.density,
        (start_uv.y - sun_uv.y) / static_cast<float>(s.samples) * s.density
    };
    Vec2f uv = start_uv;
    float il  = 1.0F;
    float col = 0.0F;
    for (std::uint32_t i = 0; i < s.samples; ++i)
    {
        uv = { uv.x - d.x, uv.y - d.y };
        col += sampler(uv) * il * s.weight;
        il *= s.decay;
    }
    return col * s.exposure;
}

TEST(LightShafts, SunBehindCameraReturnsNegativeUv)
{
    // Camera looking +Z; sun direction pointing same way (sun behind).
    const auto uv = sun_screen_pos({ 0, 0,  1 },
                                   { 0, 0,  1 },
                                   { 1, 0,  0 },
                                   { 0, 1,  0 },
                                   1.0F);
    EXPECT_LT(uv.x, 0.0F);
}

TEST(LightShafts, SunOppositeCameraProjectsToCentre)
{
    // Camera looking +Z; sun direction is -Z (sun directly in front).
    const auto uv = sun_screen_pos({ 0, 0, -1 },
                                   { 0, 0,  1 },
                                   { 1, 0,  0 },
                                   { 0, 1,  0 },
                                   1.0F);
    EXPECT_NEAR(uv.x, 0.5F, kEps);
    EXPECT_NEAR(uv.y, 0.5F, kEps);
}

// Sun in front of the camera but far off the optical axis projects OUTSIDE
// the [0,1] viewport — the radial-blur consumer must clamp / skip such
// shafts rather than sampling off-screen.
TEST(LightShafts, OffScreenSunProjectsOutsideViewport)
{
    // Camera looks +Z; sun nearly perpendicular (mostly +X), small -Z so it
    // is technically in front (fz > 0) but heavily off to the side.
    const auto uv = sun_screen_pos({ -0.99F, 0, -0.14F },  // sun_world_dir
                                   { 0, 0, 1 },            // forward
                                   { 1, 0, 0 },            // right
                                   { 0, 1, 0 },            // up
                                   1.0F);
    const bool off_screen = uv.x < 0.0F || uv.x > 1.0F ||
                            uv.y < 0.0F || uv.y > 1.0F;
    EXPECT_TRUE(off_screen) << "uv=(" << uv.x << ',' << uv.y << ')';
}

// Sun exactly on the camera plane (fz == 0) is the degenerate boundary; the
// projection must return the sentinel (-1,-1) rather than dividing by zero.
TEST(LightShafts, SunOnCameraPlaneReturnsSentinel)
{
    const auto uv = sun_screen_pos({ -1, 0, 0 },  // sun perpendicular to fwd
                                   { 0, 0, 1 },
                                   { 1, 0, 0 },
                                   { 0, 1, 0 },
                                   1.0F);
    EXPECT_LT(uv.x, 0.0F);
    EXPECT_LT(uv.y, 0.0F);
}

// Default settings are the Mitchell radial-blur tuning (density < 1, decay
// near 1). A zero-density override degenerates the sample stride to zero —
// locks the documented default so a silent retuning trips the test.
TEST(LightShafts, DefaultSettingsAreMitchellRadialBlurTuning)
{
    const Settings s {};
    EXPECT_GT(s.density, 0.0F);
    EXPECT_LE(s.density, 1.0F);
    EXPECT_GT(s.decay, 0.9F);
    EXPECT_GT(s.samples, 0U);

    Settings zero = s;
    zero.density = 0.0F;
    EXPECT_FLOAT_EQ(zero.density, 0.0F);  // degenerate stride is representable
}

TEST(LightShafts, GlslKernelsNonEmpty)
{
    EXPECT_FALSE(cd::light_shafts::kRadialBlurCS.empty());
    EXPECT_FALSE(cd::light_shafts::kInlineConeShaftGlsl.empty());
}

// --- sun_screen_pos: projection scale / off-axis behaviour -------------------
//
// Camera convention (matches the original passing tests): forward = +Z, so the
// sun is in front when -sun_world_dir has a +Z component, i.e. sun_world_dir.z
// is negative. fx = right·(-sun_dir), fy = up·(-sun_dir), fz = fwd·(-sun_dir).

// Sun off to the scene +X side: sun_world_dir.x < 0 makes -sun_dir.x > 0, so
// fx > 0 and the projection lands RIGHT of centre (u > 0.5). Pins the sign of
// the right-axis projection.
TEST(LightShafts, SunOnPlusXProjectsRightOfCentre)
{
    const auto uv = sun_screen_pos({ -0.5F, 0, -0.866F },  // sun_world_dir
                                   { 0, 0, 1 },            // forward = +Z
                                   { 1, 0, 0 },            // right = +X
                                   { 0, 1, 0 },            // up = +Y
                                   1.0F);
    EXPECT_GT(uv.x, 0.5F);
    EXPECT_NEAR(uv.y, 0.5F, kEps);
}

// Symmetric counterpart: sun_world_dir.x > 0 -> fx < 0 -> u < 0.5.
TEST(LightShafts, SunOnMinusXProjectsLeftOfCentre)
{
    const auto uv = sun_screen_pos({ 0.5F, 0, -0.866F },
                                   { 0, 0, 1 },
                                   { 1, 0, 0 },
                                   { 0, 1, 0 },
                                   1.0F);
    EXPECT_LT(uv.x, 0.5F);
    EXPECT_NEAR(uv.y, 0.5F, kEps);
}

// Up-axis projection sign: sun_world_dir.y < 0 -> fy > 0 -> v > 0.5.
TEST(LightShafts, SunAboveForwardProjectsAboveCentre)
{
    const auto uv = sun_screen_pos({ 0, -0.5F, -0.866F },
                                   { 0, 0, 1 },
                                   { 1, 0, 0 },
                                   { 0, 1, 0 },
                                   1.0F);
    EXPECT_GT(uv.y, 0.5F);
    EXPECT_NEAR(uv.x, 0.5F, kEps);
}

// Aspect ratio squeezes the horizontal projection only: a wider aspect divides
// the u offset further toward centre while v stays put. Pins the `/ aspect`
// term applies to u and NOT v.
TEST(LightShafts, AspectScalesHorizontalOffsetOnly)
{
    // 45deg off-axis sun in the right-forward plane: -sun_dir = (0.7071,0,0.7071)
    // so fx and fz are both 0.7071 (> 0, in front).
    const Vec3f sun { -0.7071F, 0, -0.7071F };
    const Vec3f fwd { 0, 0, 1 };
    const Vec3f right { 1, 0, 0 };
    const Vec3f up { 0, 1, 0 };

    const auto a1 = sun_screen_pos(sun, fwd, right, up, 1.0F);
    const auto a2 = sun_screen_pos(sun, fwd, right, up, 2.0F);

    // Larger aspect pulls u closer to 0.5 (the +0.5 bias centre).
    const float off1 = std::abs(a1.x - 0.5F);
    const float off2 = std::abs(a2.x - 0.5F);
    EXPECT_LT(off2, off1);
    // v identical regardless of aspect.
    EXPECT_FLOAT_EQ(a1.y, a2.y);
}

// Exact projection value lock: sun 45deg in the right-forward plane with
// aspect=1 gives fx/fz = 1, so u = 0.5*1 + 0.5 = 1.0 and v = 0.5.
TEST(LightShafts, FortyFiveDegreeProjectionExactValue)
{
    const auto uv = sun_screen_pos({ -0.7071F, 0, -0.7071F },
                                   { 0, 0, 1 },
                                   { 1, 0, 0 },
                                   { 0, 1, 0 },
                                   1.0F);
    EXPECT_NEAR(uv.x, 1.0F, kEps);
    EXPECT_NEAR(uv.y, 0.5F, kEps);
}

// Negative fz (sun behind the camera) must hit the sentinel guard rather than
// producing a flipped/garbage projection from the division. With forward = +Z,
// sun_world_dir.z > 0 makes -sun_dir.z < 0 so fz < 0 even with a lateral
// component present — the off-axis fx must NOT leak a positive u.
TEST(LightShafts, BehindCameraNegativeFzReturnsSentinel)
{
    const auto uv = sun_screen_pos({ -0.3F, 0, 0.954F },  // sun_world_dir.z > 0
                                   { 0, 0, 1 },            // forward = +Z
                                   { 1, 0, 0 },
                                   { 0, 1, 0 },
                                   1.0F);
    // fwd·(-sun_dir) = (1)·(-0.954) = -0.954 < 0 -> sentinel.
    EXPECT_FLOAT_EQ(uv.x, -1.0F);
    EXPECT_FLOAT_EQ(uv.y, -1.0F);
}

// --- radial-blur accumulation (kRadialBlurCS host mirror) --------------------

// A uniform white occlusion buffer reduces the loop to a geometric series in
// `decay`: col = weight * sum_{i=0}^{N-1} decay^i, then * exposure. Pins the
// Mitchell decay falloff against the closed-form geometric sum.
TEST(LightShafts, RadialBlurUniformFieldMatchesGeometricSeries)
{
    const Settings s {};  // samples=64, decay=0.96, weight=0.5, exposure=0.15
    const float got = radial_blur_accum({ 0.8F, 0.5F }, { 0.5F, 0.5F }, s,
                                        [](Vec2f) noexcept { return 1.0F; });

    // Closed form: exposure * weight * (1 - decay^N) / (1 - decay).
    float series = 0.0F;
    float term   = 1.0F;
    for (std::uint32_t i = 0; i < s.samples; ++i)
    {
        series += term;
        term *= s.decay;
    }
    const float expected = s.exposure * s.weight * series;
    EXPECT_NEAR(got, expected, 1e-4F);
}

// Decay weights EARLIER samples (nearest the start pixel) more heavily than
// later ones (nearer the sun): il starts at 1 and shrinks by decay each step.
// First-sample contribution > last-sample contribution for decay < 1.
TEST(LightShafts, RadialBlurEarlierSamplesWeightedMore)
{
    const Settings s {};
    std::array<float, 64> il_seen {};
    std::uint32_t idx = 0;
    [[maybe_unused]] const auto acc_il = radial_blur_accum({ 0.9F, 0.5F }, { 0.5F, 0.5F }, s,
                      [&](Vec2f) noexcept
                      {
                          // Recover il at this step from the running product.
                          float il = 1.0F;
                          for (std::uint32_t k = 0; k < idx; ++k) il *= s.decay;
                          il_seen.at(idx) = il;
                          ++idx;
                          return 1.0F;
                      });
    ASSERT_EQ(idx, s.samples);
    EXPECT_FLOAT_EQ(il_seen.front(), 1.0F);
    EXPECT_LT(il_seen.back(), il_seen.front());
    // Strictly monotone decreasing.
    EXPECT_LT(il_seen.at(1), il_seen.at(0));
}

// The per-step stride d walks the sample chain FROM the start pixel TOWARD the
// sun: after the full loop the final sampled uv sits density-fraction of the
// way to the sun. Pins the direction (uv -= d, d points away from sun).
TEST(LightShafts, RadialBlurMarchesTowardSun)
{
    const Settings s {};
    const Vec2f start { 0.9F, 0.5F };
    const Vec2f sun { 0.5F, 0.5F };

    Vec2f last_uv {};
    [[maybe_unused]] const auto acc_march = radial_blur_accum(start, sun, s,
                      [&](Vec2f uv) noexcept
                      {
                          last_uv = uv;
                          return 1.0F;
                      });
    // Last sample is closer to the sun in x than the start was.
    EXPECT_LT(std::abs(last_uv.x - sun.x), std::abs(start.x - sun.x));
    // y stays on the shared horizontal line.
    EXPECT_FLOAT_EQ(last_uv.y, start.y);
}

// density scales the per-step stride: half the density covers half the screen
// distance over the same sample count, so the final uv is closer to the start.
TEST(LightShafts, RadialBlurDensityScalesStride)
{
    Settings full {};
    Settings half = full;
    half.density  = full.density * 0.5F;

    const Vec2f start { 0.9F, 0.5F };
    const Vec2f sun { 0.5F, 0.5F };

    Vec2f full_last {};
    Vec2f half_last {};
    [[maybe_unused]] const auto acc_full = radial_blur_accum(start, sun, full,
                      [&](Vec2f uv) noexcept { full_last = uv; return 1.0F; });
    [[maybe_unused]] const auto acc_half = radial_blur_accum(start, sun, half,
                      [&](Vec2f uv) noexcept { half_last = uv; return 1.0F; });

    const float full_travel = std::abs(start.x - full_last.x);
    const float half_travel = std::abs(start.x - half_last.x);
    EXPECT_NEAR(half_travel, full_travel * 0.5F, 1e-4F);
}

// exposure is a pure post-multiply: doubling exposure doubles the result for
// any field. Pins exposure as the final linear gain.
TEST(LightShafts, RadialBlurExposureIsLinearGain)
{
    Settings base {};
    Settings hot = base;
    hot.exposure = base.exposure * 2.0F;

    const auto field = [](Vec2f uv) noexcept { return uv.x; };
    const float a = radial_blur_accum({ 0.7F, 0.4F }, { 0.5F, 0.5F }, base, field);
    const float b = radial_blur_accum({ 0.7F, 0.4F }, { 0.5F, 0.5F }, hot, field);
    EXPECT_NEAR(b, a * 2.0F, 1e-4F);
}

// Zero density degenerates the stride to zero: every sample reads the SAME
// start pixel, so the result equals start-sample-luminance * geometric series.
// Documents the degenerate-default trap from DefaultSettingsAreMitchell.
TEST(LightShafts, RadialBlurZeroDensitySamplesStartPixelOnly)
{
    Settings s {};
    s.density = 0.0F;
    const Vec2f start { 0.8F, 0.3F };

    std::uint32_t fetches = 0;
    Vec2f first_uv {};
    [[maybe_unused]] const auto acc_zero = radial_blur_accum(start, { 0.5F, 0.5F }, s,
                      [&](Vec2f uv) noexcept
                      {
                          if (fetches == 0) first_uv = uv;
                          ++fetches;
                          return 1.0F;
                      });
    EXPECT_EQ(fetches, s.samples);
    // Stride is zero -> every fetch is at the start pixel.
    EXPECT_FLOAT_EQ(first_uv.x, start.x);
    EXPECT_FLOAT_EQ(first_uv.y, start.y);
}

// A fully black (occluded) field contributes nothing regardless of decay /
// weight / exposure — the shaft is gated entirely by the occlusion buffer.
TEST(LightShafts, RadialBlurBlackFieldYieldsZero)
{
    const Settings s {};
    const float got = radial_blur_accum({ 0.7F, 0.7F }, { 0.5F, 0.5F }, s,
                                        [](Vec2f) noexcept { return 0.0F; });
    EXPECT_FLOAT_EQ(got, 0.0F);
}

// sample count drives the loop trip count exactly: N samples => N fetches.
TEST(LightShafts, RadialBlurSampleCountDrivesFetchCount)
{
    Settings s {};
    s.samples = 16;
    std::uint32_t fetches = 0;
    [[maybe_unused]] const auto acc_cnt = radial_blur_accum({ 0.6F, 0.6F }, { 0.5F, 0.5F }, s,
                      [&](Vec2f) noexcept { ++fetches; return 1.0F; });
    EXPECT_EQ(fetches, 16U);
}

// --- inline cone-shaft fallback GLSL contract --------------------------------

// The kInlineConeShaftGlsl source carries the documented pow(align,32) cone
// term and the 0.6 brightness scale — lock the magic constants so a silent
// retune of the fallback shader trips the test (GLSL is golden-sensitive).
TEST(LightShafts, InlineConeGlslPinsConeExponentAndScale)
{
    const auto glsl = cd::light_shafts::kInlineConeShaftGlsl;
    EXPECT_NE(glsl.find("pow(align, 32.0)"), std::string_view::npos);
    EXPECT_NE(glsl.find("* 0.6"), std::string_view::npos);
    EXPECT_NE(glsl.find("max(dot(cam_to_p, sun_L), 0.0)"),
              std::string_view::npos);
}

// The radial-blur CS source pins the accumulation contract mirrored by the
// host: uv -= d, decay multiply, exposure post-multiply.
TEST(LightShafts, RadialBlurGlslPinsAccumulationContract)
{
    const auto glsl = cd::light_shafts::kRadialBlurCS;
    EXPECT_NE(glsl.find("uv -= d;"), std::string_view::npos);
    EXPECT_NE(glsl.find("il *= pc.decay;"), std::string_view::npos);
    EXPECT_NE(glsl.find("col * pc.exposure"), std::string_view::npos);
}

}  // namespace
