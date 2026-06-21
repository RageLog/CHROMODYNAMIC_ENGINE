#include <cd/volumetric/clouds/Clouds.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string_view>

namespace
{

using cd::volumetric::clouds::density;
using cd::volumetric::clouds::height_fraction;
using cd::volumetric::clouds::kCloudsMarchCS;
using cd::volumetric::clouds::remap;
using cd::volumetric::clouds::Settings;

constexpr float kEps = 1e-3F;

TEST(Clouds, HeightFractionZeroOutsideLayer)
{
    Settings s {};
    EXPECT_NEAR(height_fraction(0.5F, s), 0.0F, kEps);
    EXPECT_NEAR(height_fraction(6.0F, s), 0.0F, kEps);
}

TEST(Clouds, HeightFractionPeaksInMiddle)
{
    Settings s {};
    const float mid = (s.layer_bottom_km + s.layer_top_km) * 0.5F;
    EXPECT_NEAR(height_fraction(mid, s), 1.0F, kEps);
}

TEST(Clouds, DensityZeroBelowCoverage)
{
    Settings s {};
    s.coverage = 0.3F;
    EXPECT_NEAR(density(3.0F, 0.5F, s), 0.0F, kEps);  // 0.5 < (1 - 0.3) = 0.7
}

TEST(Clouds, DensityPositiveAboveCoverage)
{
    Settings s {};
    s.coverage = 0.7F;
    EXPECT_GT(density(3.0F, 0.6F, s), 0.0F);
}

TEST(Clouds, RemapMapsRanges)
{
    EXPECT_NEAR(remap(5.0F, 0.0F, 10.0F, 0.0F, 1.0F), 0.5F, kEps);
    EXPECT_NEAR(remap(15.0F, 0.0F, 10.0F, 0.0F, 1.0F), 1.0F, kEps);
}

TEST(Clouds, GlslKernelNonEmpty)
{
    EXPECT_FALSE(kCloudsMarchCS.empty());
}

// ---- ADD-ONLY host regression locks (Run: 70->100) -------------------------
//
// Pin the EXACT behaviour of the EXISTING Schneider cloud math + GLSL string so
// any future edit that perturbs the rendered cloudscape trips a unit test before
// it reaches the golden image. No math/GLSL is changed here.

// (1) Settings defaults match Schneider's authored cloud layer (1.5-5.0 km,
//     half coverage, mild density, 64 march steps). Pins the author contract.
TEST(Clouds, SettingsDefaultsAreAuthoredLayer)
{
    const Settings s {};
    EXPECT_NEAR(s.layer_bottom_km, 1.5F, 1e-6F);
    EXPECT_NEAR(s.layer_top_km, 5.0F, 1e-6F);
    EXPECT_NEAR(s.coverage, 0.5F, 1e-6F);
    EXPECT_NEAR(s.density_scale, 0.05F, 1e-6F);
    EXPECT_EQ(s.march_steps, 64u);
}

// (2) height_fraction is exactly zero AT both layer boundaries (sin(0)=sin(pi)=0)
//     and just inside them. Edge: the inclusive [bottom, top] band endpoints.
TEST(Clouds, HeightFractionZeroAtLayerBoundaries)
{
    const Settings s {};
    EXPECT_NEAR(height_fraction(s.layer_bottom_km, s), 0.0F, 1e-5F);
    EXPECT_NEAR(height_fraction(s.layer_top_km, s), 0.0F, 1e-5F);
}

// (3) height_fraction is symmetric about the layer midpoint: equal offsets
//     above and below centre give equal bell-shape values (sin is symmetric
//     about pi/2). Pins the bell symmetry the GLSL mirrors.
TEST(Clouds, HeightFractionSymmetricAboutMidpoint)
{
    const Settings s {};
    const float mid = (s.layer_bottom_km + s.layer_top_km) * 0.5F;
    const float half_span = (s.layer_top_km - s.layer_bottom_km) * 0.5F;
    for (int i = 1; i < 5; ++i)
    {
        const float off = half_span * (static_cast<float>(i) / 5.0F);
        const float lo = height_fraction(mid - off, s);
        const float hi = height_fraction(mid + off, s);
        EXPECT_NEAR(lo, hi, 1e-4F) << "i=" << i;
        EXPECT_GT(lo, 0.0F);
        EXPECT_LE(lo, 1.0F);
    }
}

// (4) height_fraction never exceeds 1 and never goes negative across the whole
//     inhabited band (sin in [0,1] for [0,pi]). Pins the bounded contract.
TEST(Clouds, HeightFractionBoundedZeroToOne)
{
    const Settings s {};
    for (int i = 0; i <= 32; ++i)
    {
        const float alt = s.layer_bottom_km +
                          (s.layer_top_km - s.layer_bottom_km) *
                          (static_cast<float>(i) / 32.0F);
        const float hf = height_fraction(alt, s);
        // -1e-5 floor: the boundary texel can land a sub-ULP below 0 from the
        // (alt - bottom) / thickness float division, not a logic error.
        EXPECT_GE(hf, -1e-5F) << "i=" << i;
        EXPECT_LE(hf, 1.0F + 1e-5F) << "i=" << i;
    }
}

// (5) remap clamps below the input range to the low output bound (c) and above
//     to the high output bound (d). Edge: out-of-range clamp both directions.
TEST(Clouds, RemapClampsOutOfRange)
{
    EXPECT_NEAR(remap(-5.0F, 0.0F, 10.0F, 2.0F, 8.0F), 2.0F, kEps);  // below a
    EXPECT_NEAR(remap(50.0F, 0.0F, 10.0F, 2.0F, 8.0F), 8.0F, kEps);  // above b
    EXPECT_NEAR(remap(0.0F, 0.0F, 10.0F, 2.0F, 8.0F), 2.0F, kEps);   // at a
    EXPECT_NEAR(remap(10.0F, 0.0F, 10.0F, 2.0F, 8.0F), 8.0F, kEps);  // at b
}

// (6) remap survives a degenerate (zero-width) input range without dividing by
//     zero — the max(b-a, 1e-5) guard pins x at the low end -> output c.
//     Edge: a == b degenerate band.
TEST(Clouds, RemapDegenerateRangeIsFinite)
{
    const float r = remap(5.0F, 3.0F, 3.0F, 0.0F, 1.0F);
    EXPECT_TRUE(std::isfinite(r));
    EXPECT_GE(r, 0.0F);
    EXPECT_LE(r, 1.0F);
}

// (7) remap can invert (c > d) — a value at the low end maps to the high output
//     and vice versa. Pins the linear interpolation direction-agnostic.
TEST(Clouds, RemapInvertedOutputRange)
{
    EXPECT_NEAR(remap(0.0F, 0.0F, 10.0F, 1.0F, 0.0F), 1.0F, kEps);
    EXPECT_NEAR(remap(10.0F, 0.0F, 10.0F, 1.0F, 0.0F), 0.0F, kEps);
    EXPECT_NEAR(remap(5.0F, 0.0F, 10.0F, 1.0F, 0.0F), 0.5F, kEps);
}

// (8) density is exactly zero for any noise OUTSIDE the cloud layer (height
//     fraction collapses to 0). Edge: below bottom and above top.
TEST(Clouds, DensityZeroOutsideLayer)
{
    const Settings s {};
    EXPECT_NEAR(density(0.5F, 1.0F, s), 0.0F, 1e-6F);  // below bottom
    EXPECT_NEAR(density(9.0F, 1.0F, s), 0.0F, 1e-6F);  // above top
}

// (9) density clamps the (noise - (1-coverage)) term at zero: a noise value
//     exactly at the coverage threshold yields zero, just above is positive.
//     Edge: the coverage cut-off boundary.
TEST(Clouds, DensityCoverageThresholdBoundary)
{
    Settings s {};
    s.coverage = 0.4F;  // threshold = 1 - 0.4 = 0.6
    const float mid = (s.layer_bottom_km + s.layer_top_km) * 0.5F;
    EXPECT_NEAR(density(mid, 0.6F, s), 0.0F, 1e-6F);  // exactly at threshold
    EXPECT_GT(density(mid, 0.61F, s), 0.0F);          // just above
}

// (10) density scales LINEARLY with density_scale: doubling the scale doubles
//      the field for the same noise/altitude. Pins the multiplicative contract.
TEST(Clouds, DensityScalesLinearlyWithDensityScale)
{
    Settings s {};
    s.coverage = 0.8F;
    s.density_scale = 0.05F;
    const float mid = (s.layer_bottom_km + s.layer_top_km) * 0.5F;
    const float d1 = density(mid, 0.9F, s);
    s.density_scale = 0.10F;
    const float d2 = density(mid, 0.9F, s);
    EXPECT_GT(d1, 0.0F);
    EXPECT_NEAR(d2, d1 * 2.0F, 1e-5F);
}

// (11) density rises monotonically with coverage at a fixed noise value: more
//      coverage thins the (1-coverage) subtrahend, raising the field.
TEST(Clouds, DensityMonotoneRisesWithCoverage)
{
    const float mid = 3.25F;  // inside the default 1.5..5.0 band
    float prev = -1.0F;
    for (int i = 0; i <= 10; ++i)
    {
        Settings s {};
        s.coverage = static_cast<float>(i) / 10.0F;
        const float d = density(mid, 0.85F, s);
        EXPECT_GE(d, prev - 1e-6F) << "i=" << i;
        prev = d;
    }
}

// (12) density is finite (never NaN/inf) for the full noise sweep including the
//      extreme 0 and 1 endpoints. Pins the NaN-guard contract.
TEST(Clouds, DensityFiniteAcrossNoiseSweep)
{
    const Settings s {};
    const float mid = 3.25F;
    for (int i = 0; i <= 20; ++i)
    {
        const float noise = static_cast<float>(i) / 20.0F;
        const float d = density(mid, noise, s);
        EXPECT_TRUE(std::isfinite(d)) << "noise=" << noise;
        EXPECT_GE(d, 0.0F);
    }
}

// (13) GLSL march kernel mirrors the host contract: Schneider Perlin-Worley
//      two-octave blend, height_fraction(sin), coverage subtract, Beer-Lambert
//      exp(-d*step), early-out at trans<0.01. Pins CPU/GPU parity tokens.
TEST(Clouds, GlslKernelMirrorsHostContract)
{
    EXPECT_NE(kCloudsMarchCS.find("#version 460"), std::string_view::npos);
    EXPECT_NE(kCloudsMarchCS.find("height_fraction"), std::string_view::npos);
    EXPECT_NE(kCloudsMarchCS.find("n_lo * 0.5 + n_hi * 0.5"),
              std::string_view::npos);  // two-octave Perlin-Worley blend
    EXPECT_NE(kCloudsMarchCS.find("1.0 - pc.layer_params.z"),
              std::string_view::npos);  // (1 - coverage) subtrahend
    EXPECT_NE(kCloudsMarchCS.find("exp(-d * step)"),
              std::string_view::npos);  // Beer-Lambert per step
    EXPECT_NE(kCloudsMarchCS.find("trans < 0.01"),
              std::string_view::npos);  // early-out transmittance threshold
}

}  // namespace
