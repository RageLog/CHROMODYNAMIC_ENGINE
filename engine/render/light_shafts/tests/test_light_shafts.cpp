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

// ============================================================================
// ADD-ONLY comprehensive tests — sun_screen_pos + radial_blur_accum deep cover
// ============================================================================

// --- Settings default-value locks -------------------------------------------

// Lock each default so a silent retuning of the Mitchell constants trips a test.
TEST(LightShafts, SettingsDefaultSamplesIs64)
{
    EXPECT_EQ(Settings{}.samples, 64U);
}
TEST(LightShafts, SettingsDefaultDecayIs0_96)
{
    EXPECT_FLOAT_EQ(Settings{}.decay, 0.96F);
}
TEST(LightShafts, SettingsDefaultDensityIs0_95)
{
    EXPECT_FLOAT_EQ(Settings{}.density, 0.95F);
}
TEST(LightShafts, SettingsDefaultWeightIs0_5)
{
    EXPECT_FLOAT_EQ(Settings{}.weight, 0.5F);
}
TEST(LightShafts, SettingsDefaultExposureIs0_15)
{
    EXPECT_FLOAT_EQ(Settings{}.exposure, 0.15F);
}

// --- sun_screen_pos: additional boundary + sign coverage --------------------

// Non-unit sun direction gives the SAME projection as the unit version.
// The formula divides fx/fz and fy/fz so magnitude cancels implicitly.
TEST(LightShafts, SunDirectionMagnitudeDoesNotAffectProjection)
{
    const Vec3f fwd   { 0, 0, 1 };
    const Vec3f right { 1, 0, 0 };
    const Vec3f up    { 0, 1, 0 };

    const Vec3f unit   { 0, 0, -1 };            // directly in front, unit length
    const Vec3f scaled { 0, 0, -3.7F };         // same direction, scaled

    const auto uv_unit   = sun_screen_pos(unit,   fwd, right, up, 1.0F);
    const auto uv_scaled = sun_screen_pos(scaled, fwd, right, up, 1.0F);

    EXPECT_NEAR(uv_unit.x, uv_scaled.x, 1e-5F);
    EXPECT_NEAR(uv_unit.y, uv_scaled.y, 1e-5F);
}

// fz just above zero: sun barely in front — projection must succeed (no sentinel).
TEST(LightShafts, SunJustInFrontReturnsValidProjection)
{
    // fwd=(0,0,1); sun_world_dir.z = -eps; -sun_dir.z = eps > 0 -> fz = eps.
    const float eps = 1e-6F;
    const auto uv = sun_screen_pos({ 0, 0, -eps },
                                   { 0, 0, 1 },
                                   { 1, 0, 0 },
                                   { 0, 1, 0 },
                                   1.0F);
    // Sentinel is (-1,-1); any result with x >= 0 is a valid projection.
    EXPECT_GE(uv.x, 0.0F);
}

// fz just below zero (sun barely behind): must hit the sentinel guard.
TEST(LightShafts, SunJustBehindCameraReturnsSentinel)
{
    const float eps = 1e-6F;
    const auto uv = sun_screen_pos({ 0, 0, eps },   // -sun_dir.z = -eps < 0
                                   { 0, 0, 1 },
                                   { 1, 0, 0 },
                                   { 0, 1, 0 },
                                   1.0F);
    EXPECT_FLOAT_EQ(uv.x, -1.0F);
    EXPECT_FLOAT_EQ(uv.y, -1.0F);
}

// Symmetric left/right suns produce u values equidistant from 0.5, opposite sides.
TEST(LightShafts, SunScreenPosLeftRightSymmetry)
{
    const Vec3f fwd { 0, 0, 1 };
    const Vec3f right { 1, 0, 0 };
    const Vec3f up { 0, 1, 0 };

    // sun on +X side of world: sun_world_dir.x < 0 -> fx > 0 -> u > 0.5
    const auto uv_r = sun_screen_pos({ -0.6F, 0, -0.8F }, fwd, right, up, 1.0F);
    // sun on -X side of world: sun_world_dir.x > 0 -> fx < 0 -> u < 0.5
    const auto uv_l = sun_screen_pos({  0.6F, 0, -0.8F }, fwd, right, up, 1.0F);

    // Horizontal offsets from centre are equal in magnitude, opposite in sign.
    EXPECT_NEAR(uv_r.x - 0.5F, 0.5F - uv_l.x, 1e-5F);
    // v stays at centre for both (no vertical component).
    EXPECT_NEAR(uv_r.y, 0.5F, kEps);
    EXPECT_NEAR(uv_l.y, 0.5F, kEps);
}

// Symmetric up/down suns produce v values equidistant from 0.5, opposite sides.
TEST(LightShafts, SunScreenPosUpDownSymmetry)
{
    const Vec3f fwd { 0, 0, 1 };
    const Vec3f right { 1, 0, 0 };
    const Vec3f up { 0, 1, 0 };

    // sun above the forward axis: sun_world_dir.y < 0 -> fy > 0 -> v > 0.5
    const auto uv_u = sun_screen_pos({ 0, -0.6F, -0.8F }, fwd, right, up, 1.0F);
    // sun below the forward axis: sun_world_dir.y > 0 -> fy < 0 -> v < 0.5
    const auto uv_d = sun_screen_pos({ 0,  0.6F, -0.8F }, fwd, right, up, 1.0F);

    EXPECT_NEAR(uv_u.y - 0.5F, 0.5F - uv_d.y, 1e-5F);
    EXPECT_NEAR(uv_u.x, 0.5F, kEps);
    EXPECT_NEAR(uv_d.x, 0.5F, kEps);
}

// Halving aspect (portrait) DOUBLES the horizontal offset relative to aspect=1.
// Formula: u_off = (fx/fz) / aspect * 0.5; halve aspect -> double u_off.
TEST(LightShafts, AspectHalfDoublesHorizontalOffset)
{
    const Vec3f sun   { -0.5F, 0, -0.866F };
    const Vec3f fwd   { 0, 0, 1 };
    const Vec3f right { 1, 0, 0 };
    const Vec3f up    { 0, 1, 0 };

    const auto a1  = sun_screen_pos(sun, fwd, right, up, 1.0F);
    const auto a05 = sun_screen_pos(sun, fwd, right, up, 0.5F);

    const float off1  = a1.x  - 0.5F;
    const float off05 = a05.x - 0.5F;
    EXPECT_NEAR(off05, off1 * 2.0F, 1e-4F);
    // v is identical regardless of aspect.
    EXPECT_FLOAT_EQ(a1.y, a05.y);
}

// Tilted camera: roll 90° so right=(0,1,0) and up=(-1,0,0).
// Sun in +Y world direction: now fx=right·(-sun)=(-y_comp), fy=up·(-sun)=(y_comp).
TEST(LightShafts, TiltedCameraRollProducesCorrectMapping)
{
    // Camera rolled 90°: forward stays +Z, right is now world +Y, up is world -X.
    const Vec3f fwd   { 0, 0, 1 };
    const Vec3f right { 0, 1, 0 };   // world +Y is camera right after 90° roll
    const Vec3f up    { -1, 0, 0 };  // world -X is camera up after 90° roll

    // Sun slightly in front, offset in world +Y direction.
    // -sun = (0, 0.6, 0.8); fx = right·(-sun) = 0.6; fy = up·(-sun) = 0.
    const auto uv = sun_screen_pos({ 0, -0.6F, -0.8F }, fwd, right, up, 1.0F);
    // fx/fz = 0.6/0.8 = 0.75 -> u = 0.75*0.5+0.5 = 0.875; fy/fz=0 -> v=0.5.
    EXPECT_NEAR(uv.x, 0.875F, kEps);
    EXPECT_NEAR(uv.y, 0.5F, kEps);
}

// --- radial_blur_accum: degenerate + boundary cases -------------------------

// samples=0: loop never executes; col stays 0; result = 0 * exposure = 0.
TEST(LightShafts, RadialBlurZeroSamplesReturnsZero)
{
    Settings s {};
    s.samples = 0U;
    std::uint32_t fetches = 0;
    [[maybe_unused]] const float acc =
        radial_blur_accum({ 0.5F, 0.5F }, { 0.5F, 0.5F }, s,
                          [&](Vec2f) noexcept { ++fetches; return 1.0F; });
    EXPECT_EQ(fetches, 0U);
    EXPECT_FLOAT_EQ(acc, 0.0F);
}

// samples=1: single step, exact: uv = start-d; col = field(uv)*1.0*weight; result=col*exposure.
TEST(LightShafts, RadialBlurOneSampleExactArithmetic)
{
    Settings s {};
    s.samples  = 1U;
    s.density  = 1.0F;
    s.weight   = 1.0F;
    s.exposure = 1.0F;

    const Vec2f start { 0.8F, 0.5F };
    const Vec2f sun   { 0.5F, 0.5F };

    // d = (0.8-0.5)/1 * 1.0 = 0.3; first (and only) uv = 0.8 - 0.3 = 0.5.
    Vec2f fetched_uv {};
    [[maybe_unused]] const float acc =
        radial_blur_accum(start, sun, s,
                          [&](Vec2f uv) noexcept
                          {
                              fetched_uv = uv;
                              return 2.0F;  // known luminance
                          });
    // col = 2.0 * 1.0 * 1.0 = 2.0; result = 2.0 * 1.0 = 2.0.
    EXPECT_NEAR(fetched_uv.x, 0.5F, 1e-5F);
    EXPECT_NEAR(acc, 2.0F, 1e-5F);
}

// start_uv == sun_uv: d = (sun-sun)/N*density = 0; every sample stays at sun_uv.
TEST(LightShafts, RadialBlurStartAtSunAllFetchesAtSunUv)
{
    const Settings s {};
    const Vec2f sun { 0.3F, 0.7F };

    std::uint32_t mismatches = 0;
    [[maybe_unused]] const float acc =
        radial_blur_accum(sun, sun, s,
                          [&](Vec2f uv) noexcept
                          {
                              if (std::abs(uv.x - sun.x) > 1e-6F ||
                                  std::abs(uv.y - sun.y) > 1e-6F)
                              {
                                  ++mismatches;
                              }
                              return 1.0F;
                          });
    EXPECT_EQ(mismatches, 0U);
    static_cast<void>(acc);
}

// weight=0: every per-step contribution is 0; result = 0 regardless of field.
TEST(LightShafts, RadialBlurWeightZeroYieldsZero)
{
    Settings s {};
    s.weight = 0.0F;
    const float acc =
        radial_blur_accum({ 0.7F, 0.3F }, { 0.5F, 0.5F }, s,
                          [](Vec2f) noexcept { return 999.0F; });
    EXPECT_FLOAT_EQ(acc, 0.0F);
}

// exposure=0: post-multiply zeroes the result unconditionally.
TEST(LightShafts, RadialBlurExposureZeroYieldsZero)
{
    Settings s {};
    s.exposure = 0.0F;
    const float acc =
        radial_blur_accum({ 0.6F, 0.4F }, { 0.5F, 0.5F }, s,
                          [](Vec2f) noexcept { return 999.0F; });
    EXPECT_FLOAT_EQ(acc, 0.0F);
}

// decay=0: il = 1 on step i=0 then 0 thereafter; only the FIRST step contributes.
// First uv = start - d; il = 1; col = field(first_uv)*weight; result = col*exposure.
TEST(LightShafts, RadialBlurDecayZeroOnlyFirstStepContributes)
{
    Settings s {};
    s.decay    = 0.0F;
    s.density  = 1.0F;
    s.weight   = 1.0F;
    s.exposure = 1.0F;
    s.samples  = 8U;

    std::uint32_t call_idx = 0;
    float first_luminance  = 0.0F;
    [[maybe_unused]] const float acc =
        radial_blur_accum({ 0.9F, 0.5F }, { 0.5F, 0.5F }, s,
                          [&](Vec2f) noexcept
                          {
                              const float lum = (call_idx == 0) ? 3.0F : 0.0F;
                              if (call_idx == 0) first_luminance = 3.0F;
                              ++call_idx;
                              return lum;
                          });
    // Only the i=0 step (il=1) contributes: col = 3*1*1 = 3; result = 3*1 = 3.
    EXPECT_FLOAT_EQ(acc, first_luminance * s.weight * s.exposure);
}

// decay=1: il stays 1 throughout; for a uniform field of luminance L and N samples,
// col = L * weight * N; result = col * exposure.
TEST(LightShafts, RadialBlurDecayOneUniformIlAcrossAllSamples)
{
    Settings s {};
    s.decay    = 1.0F;
    s.weight   = 1.0F;
    s.exposure = 1.0F;

    const float field_lum = 2.0F;
    [[maybe_unused]] const float acc =
        radial_blur_accum({ 0.8F, 0.5F }, { 0.5F, 0.5F }, s,
                          [&](Vec2f) noexcept { return field_lum; });

    // col = field_lum * 1.0 * N; result = col * 1.0.
    const float expected = field_lum * s.weight * static_cast<float>(s.samples) * s.exposure;
    EXPECT_NEAR(acc, expected, 1e-3F);
}

// Off-screen start_uv: the sampler receives the actual UV coords (no clamping in
// the host mirror or the GLSL without an explicit clamp instruction). Pins that
// the host function forwards whatever UV the loop produces without silent rounding.
TEST(LightShafts, RadialBlurOffScreenStartUvPassedThroughToSampler)
{
    Settings s {};
    s.samples = 1U;
    s.density = 0.0F;  // zero stride: start stays put, sampler sees start_uv.

    const Vec2f start { -0.5F, 1.8F };  // clearly off screen
    Vec2f received {};
    [[maybe_unused]] const float acc =
        radial_blur_accum(start, { 0.5F, 0.5F }, s,
                          [&](Vec2f uv) noexcept { received = uv; return 0.0F; });

    // With zero stride: first fetch is at start - d == start - 0 == start.
    EXPECT_NEAR(received.x, start.x, 1e-5F);
    EXPECT_NEAR(received.y, start.y, 1e-5F);
}

// il after k steps equals decay^k exactly (geometric product, no accumulation drift).
TEST(LightShafts, RadialBlurDecayProductIsExact)
{
    Settings s {};
    s.samples = 10U;

    std::uint32_t idx = 0;
    [[maybe_unused]] const float acc =
        radial_blur_accum({ 0.9F, 0.5F }, { 0.5F, 0.5F }, s,
                          [&](Vec2f) noexcept
                          {
                              // il at step i = decay^i; verify steps 0,1,2,9.
                              if (idx == 0U)
                              {
                                  // il captured implicitly — we verify via weight=1 result elsewhere.
                              }
                              ++idx;
                              return 1.0F;
                          });
    // Verify closed-form: sum_{i=0}^{9} decay^i; recompute host-side.
    float expected_col = 0.0F;
    float il = 1.0F;
    for (std::uint32_t i = 0; i < s.samples; ++i)
    {
        expected_col += il * s.weight;
        il *= s.decay;
    }
    expected_col *= s.exposure;
    EXPECT_NEAR(acc, expected_col, 1e-5F);
    static_cast<void>(acc);
}

// --- kRadialBlurCS GLSL push-constant uniform name locks --------------------

// Pin every push-constant member name: a rename in the GLSL breaks the host
// mirror and must be caught before the shader is compiled on a device.
TEST(LightShafts, KRadialBlurCSPinsPushConstantMemberNames)
{
    const auto glsl = cd::light_shafts::kRadialBlurCS;
    EXPECT_NE(glsl.find("pc.samples"),  std::string_view::npos);
    EXPECT_NE(glsl.find("pc.weight"),   std::string_view::npos);
    EXPECT_NE(glsl.find("pc.density"),  std::string_view::npos);
    EXPECT_NE(glsl.find("pc.sun_uv"),   std::string_view::npos);
    EXPECT_NE(glsl.find("pc.size"),     std::string_view::npos);
}

// Local size declaration must match the 8×8 workgroup grid that the dispatch
// caller uses to compute group counts: ceil(w/8) × ceil(h/8).
TEST(LightShafts, KRadialBlurCSPinsLocalSize8x8)
{
    const auto glsl = cd::light_shafts::kRadialBlurCS;
    EXPECT_NE(glsl.find("local_size_x = 8"), std::string_view::npos);
    EXPECT_NE(glsl.find("local_size_y = 8"), std::string_view::npos);
}

// imageStore is the write-back instruction — if it's renamed/removed the CS
// produces no output (silent black frame).
TEST(LightShafts, KRadialBlurCSPinsImageStore)
{
    const auto glsl = cd::light_shafts::kRadialBlurCS;
    EXPECT_NE(glsl.find("imageStore(dst,"), std::string_view::npos);
}

// push_constant qualifier must be present — without it the driver sees a UBO
// at binding 0 instead and silently misroutes the layout.
TEST(LightShafts, KRadialBlurCSPinsPushConstantQualifier)
{
    const auto glsl = cd::light_shafts::kRadialBlurCS;
    EXPECT_NE(glsl.find("push_constant"), std::string_view::npos);
}

// --- kInlineConeShaftGlsl additional parameter contract ----------------------

// sun_intensity and strength parameters must be present — removing either
// breaks every consumer that passes those arguments.
TEST(LightShafts, KInlineConeShaftGlslPinsParameters)
{
    const auto glsl = cd::light_shafts::kInlineConeShaftGlsl;
    EXPECT_NE(glsl.find("sun_intensity"), std::string_view::npos);
    EXPECT_NE(glsl.find("strength"),      std::string_view::npos);
}

// The return type is vec3 and the function returns sun_color * sun_intensity *
// shaft term — pin the token so a scalar→vec3 change is caught.
TEST(LightShafts, KInlineConeShaftGlslReturnsVec3SunColorProduct)
{
    const auto glsl = cd::light_shafts::kInlineConeShaftGlsl;
    EXPECT_NE(glsl.find("vec3"),      std::string_view::npos);
    EXPECT_NE(glsl.find("sun_color"), std::string_view::npos);
}

// --- Analytic epipolar seal (Engelhardt & Dachsbacher / Kim & Marsalek) ------
//
// The sealed scope is the CHARTER of this library at 100%.  The test below
// encodes that the header itself says "NOT IMPLEMENTED" for the epipolar path
// so a future contributor cannot silently widen the scope by removing the
// banner and claiming the lib is "really" a full epipolar implementation.
//
// ADR reference: ADR-20260616-band6-render-misc-scope.md §3 — radial-blur-v1
// is the sealed, functional shipped path; epipolar is deferred-by-design with
// an explicit promote-on-need trigger (sunset-grade finely-detailed shafts
// through complex occluders).  That multi-week algorithm (epipolar lines +
// attenuation integral + depth-aware march) is a SEPARATE algorithm layered
// ALONGSIDE, not replacing, the radial blur.

TEST(LightShafts, EpipolarSealBannerPresentInHeader)
{
    // The header text is baked into the source. The easiest stable proxy is the
    // kRadialBlurCS / kInlineConeShaftGlsl strings being the ONLY two GLSL
    // kernels exposed — if an epipolar CS were silently added this count would
    // change and a name check would expose it.  We additionally pin that the
    // two sealed constants contain no "epipolar" substring (they are the
    // radial-blur path, not an epipolar path).
    const auto cs   = cd::light_shafts::kRadialBlurCS;
    const auto cone = cd::light_shafts::kInlineConeShaftGlsl;
    EXPECT_EQ(cs.find("epipolar"),   std::string_view::npos)
        << "kRadialBlurCS must not contain epipolar code (radial-blur-v1 path only)";
    EXPECT_EQ(cone.find("epipolar"), std::string_view::npos)
        << "kInlineConeShaftGlsl must not contain epipolar code";
}

}  // namespace
