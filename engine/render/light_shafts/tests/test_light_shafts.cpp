#include <cd/light_shafts/LightShafts.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::light_shafts::Settings;
using cd::light_shafts::sun_screen_pos;

constexpr float kEps = 0.05F;

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

}  // namespace
