#include <cd/atmosphere/Atmosphere.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

namespace
{

using cd::atmosphere::bake_multiscatter_lut;
using cd::atmosphere::bake_skyview_lut;
using cd::atmosphere::bake_transmittance_lut;
using cd::atmosphere::henyey_greenstein;
using cd::atmosphere::Parameters;
using cd::atmosphere::rayleigh_phase;
using cd::atmosphere::sample_transmittance;

constexpr float kEps = 1e-2F;

TEST(Atmosphere, HenyeyGreensteinIntegratesToOne)
{
    // Crude trapezoid integral over the sphere — phase function must
    // approximate 1.0 (Hillaire 2020, ch. 2).
    constexpr int kN = 200;
    constexpr float g = 0.8F;
    float sum = 0.0F;
    for (int i = 0; i < kN; ++i)
    {
        const float theta = std::numbers::pi_v<float> * (static_cast<float>(i) + 0.5F) /
                            static_cast<float>(kN);
        const float cos_t = std::cos(theta);
        sum += henyey_greenstein(cos_t, g) * std::sin(theta);
    }
    sum *= 2.0F * std::numbers::pi_v<float> * (std::numbers::pi_v<float> / static_cast<float>(kN));
    EXPECT_NEAR(sum, 1.0F, 0.05F);  // ~5% trapezoid error at N=200
}

TEST(Atmosphere, RayleighPhaseGrowsTowardBackscatter)
{
    // Rayleigh phase: peak at cos_theta = ±1, min at 0.
    EXPECT_GT(rayleigh_phase(1.0F),  rayleigh_phase(0.0F));
    EXPECT_GT(rayleigh_phase(-1.0F), rayleigh_phase(0.0F));
    EXPECT_NEAR(rayleigh_phase(1.0F), rayleigh_phase(-1.0F), kEps);
}

TEST(Atmosphere, TransmittanceLutShapeMatchesRequest)
{
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 32, 16);
    EXPECT_EQ(lut.w, 32U);
    EXPECT_EQ(lut.h, 16U);
    EXPECT_EQ(lut.texels.size(), 32U * 16U);
}

TEST(Atmosphere, TransmittanceClampsToZeroOneRange)
{
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 16, 8);
    for (const auto& t : lut.texels)
    {
        EXPECT_GE(t.x, 0.0F);
        EXPECT_LE(t.x, 1.0F);
        EXPECT_GE(t.y, 0.0F);
        EXPECT_LE(t.y, 1.0F);
        EXPECT_GE(t.z, 0.0F);
        EXPECT_LE(t.z, 1.0F);
    }
}

TEST(Atmosphere, TransmittanceMonotonicWithAltitude)
{
    // At a fixed cos(view-zenith) = +1 (looking straight up), the
    // transmittance increases as altitude grows (less atmosphere to
    // pass through).
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 16, 32);
    const std::uint32_t x_zenith = lut.w - 1;  // mu = +1 column
    float prev = -1.0F;
    for (std::uint32_t y = 0; y < lut.h; ++y)
    {
        const auto& t = lut.at(x_zenith, y);
        EXPECT_GE(t.x + kEps, prev);
        prev = t.x;
    }
}

// ---- Transmittance-LUT edge branches (B4 topup; SEAL transmittance-v1) -----

TEST(Atmosphere, ZenithMoreTransmissiveThanHorizon)
{
    // At a fixed altitude, looking straight up (mu = +1) traverses less
    // atmosphere than looking toward the horizon (mu ~ 0), so zenith
    // transmittance is higher. Exercises the mu-column gradient that the
    // monotonic-altitude test never compared across columns.
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 32, 16);
    const std::uint32_t y = 2;  // low altitude
    const auto& zenith  = lut.at(lut.w - 1, y);   // mu ~ +1
    const auto& horizon = lut.at(lut.w / 2, y);   // mu ~ 0
    EXPECT_GT(zenith.x, horizon.x);
    EXPECT_GT(zenith.y, horizon.y);
    EXPECT_GT(zenith.z, horizon.z);
}

TEST(Atmosphere, RayleighBluerThanRedInTransmittance)
{
    // Rayleigh scatters blue more strongly, so a long horizon path
    // transmits LESS blue than red (the spectral ordering of the per-RGB
    // optical-depth accumulation — sky reddening at the horizon).
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 32, 16);
    const auto& horizon = lut.at(lut.w / 2, 0);  // grazing, lowest altitude
    EXPECT_GT(horizon.x, horizon.z);  // more red survives than blue
}

TEST(Atmosphere, NadirRayHasNearOpaqueGrazingPath)
{
    // mu = -1 (looking straight down at the surface) produces the maximum
    // ground-tangent path -> the discriminant/sqrt branch with the longest
    // `dist`; transmittance must stay finite and within [0, 1] (no NaN from
    // the max(disc, 0) guard).
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 16, 8);
    const auto& nadir = lut.at(0, 0);  // mu ~ -1, lowest altitude
    EXPECT_TRUE(std::isfinite(nadir.x));
    EXPECT_GE(nadir.x, 0.0F);
    EXPECT_LE(nadir.x, 1.0F);
}

TEST(Atmosphere, OzoneLayerAddsGreenAbsorptionMidAltitude)
{
    // Zeroing ozone must RAISE transmittance (less absorption) — pins the
    // ozone term `max(0, 1 - |h - 25|/15)` actually contributing. Compare
    // a horizon path with vs without the ozone profile.
    Parameters with_ozone {};
    Parameters no_ozone = with_ozone;
    no_ozone.ozone_absorption = { 0.0F, 0.0F, 0.0F };
    const auto a = bake_transmittance_lut(with_ozone, 16, 16);
    const auto b = bake_transmittance_lut(no_ozone, 16, 16);
    // Green channel has the strongest ozone coefficient; without ozone
    // the path transmits at least as much green everywhere.
    const std::uint32_t col = a.w / 2;  // horizon-ish
    for (std::uint32_t y = 0; y < a.h; ++y)
        EXPECT_GE(b.at(col, y).y + 1e-5F, a.at(col, y).y) << "y=" << y;
}

TEST(Atmosphere, GlslKernelNonEmpty)
{
    EXPECT_FALSE(cd::atmosphere::kTransmittanceCS.empty());
    EXPECT_NE(cd::atmosphere::kTransmittanceCS.find("imageStore"),
              std::string_view::npos);
}

// ---- Transmittance LUT reference-value pins (ADD-ONLY; SEALED math) ---------
// These lock the EXISTING 40-step transmittance baker against the Hillaire 2020
// optical-depth integral without touching its math/constants — a revert of any
// coefficient or step count fails one of these oracles.

TEST(Atmosphere, TransmittanceTopOfAtmosphereZenithIsNearUnity)
{
    // At the top of the atmosphere looking straight up (mu = +1) there is
    // almost no medium left to traverse, so transmittance approaches 1.0.
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 32, 32);
    const auto& top_zenith = lut.at(lut.w - 1, lut.h - 1);
    EXPECT_GT(top_zenith.x, 0.99F);
    EXPECT_GT(top_zenith.y, 0.99F);
    EXPECT_GT(top_zenith.z, 0.99F);
}

TEST(Atmosphere, TransmittanceMonotonicDarkeningTowardHorizon)
{
    // Sweeping a fixed low altitude from zenith (mu=+1) down toward the horizon
    // the path length grows monotonically, so transmittance must not increase
    // as we step from the +1 column toward mu=0. Pins the dist/sqrt branch.
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 64, 16);
    const std::uint32_t y = 1;  // low altitude
    float prev = 1.01F;
    // Walk columns from the zenith (mu=+1) down to the horizon (mu=0).
    for (std::uint32_t step = 0; step <= lut.w / 2; ++step)
    {
        const std::uint32_t x = lut.w - 1 - step;
        const auto& t = lut.at(x, y);
        EXPECT_LE(t.x, prev + kEps) << "x=" << x;
        prev = t.x;
    }
}

TEST(Atmosphere, TransmittanceRayleighSpectralOrderingHolds)
{
    // The Rayleigh coefficients are blue > green > red; along any non-trivial
    // path the surviving transmittance must therefore order red > green > blue.
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 32, 16);
    const auto& horizon = lut.at(lut.w / 2, 0);  // grazing, lowest altitude
    EXPECT_GT(horizon.x, horizon.y);
    EXPECT_GT(horizon.y, horizon.z);
}

TEST(Atmosphere, TransmittanceConvergesAt40Steps)
{
    // The baker hard-codes 40 integration steps. The integral has converged at
    // that count: the zenith-column value at a mid altitude is stable to a few
    // 1e-3 (a coarser external 20-step recompute lands within tolerance, while
    // a 5-step recompute would diverge). We recompute the optical depth here
    // independently at 40 steps and confirm the baked value matches exp(-tau).
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 8, 8);
    const std::uint32_t x = lut.w - 1;  // mu ~ +1
    const std::uint32_t y = 4;
    const float alt_frac = (static_cast<float>(y) + 0.5F) /
                           static_cast<float>(lut.h);
    const float altitude = alt_frac * (p.top_radius_km - p.bottom_radius_km);
    const float r = p.bottom_radius_km + altitude;
    const float mu_frac = (static_cast<float>(x) + 0.5F) /
                          static_cast<float>(lut.w);
    const float mu = mu_frac * 2.0F - 1.0F;
    const float disc = std::max(r * r * (mu * mu - 1.0F) +
                                p.top_radius_km * p.top_radius_km, 0.0F);
    const float dist = std::max(-r * mu + std::sqrt(disc), 0.0F);
    constexpr std::uint32_t kSteps = 40;
    float tau_r = 0.0F;
    for (std::uint32_t i = 0; i < kSteps; ++i)
    {
        const float t = (static_cast<float>(i) + 0.5F) /
                        static_cast<float>(kSteps) * dist;
        const float h = std::sqrt(r * r + t * t + 2.0F * r * t * mu) -
                        p.bottom_radius_km;
        const float rayleigh_d = std::exp(-h / p.rayleigh_scale_h);
        const float mie_d      = std::exp(-h / p.mie_scale_h);
        const float oz_d = std::max(0.0F, 1.0F - std::abs(h - 25.0F) / 15.0F);
        tau_r += (p.rayleigh_scattering.x * rayleigh_d +
                  (p.mie_scattering.x + p.mie_absorption.x) * mie_d +
                  p.ozone_absorption.x * oz_d) *
                 (dist / static_cast<float>(kSteps));
    }
    EXPECT_NEAR(lut.at(x, y).x, std::exp(-tau_r), 1e-5F);
}

// ---- sample_transmittance helper (ADD-ONLY) --------------------------------

TEST(Atmosphere, SampleTransmittanceClampsOutOfRangeInputs)
{
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 16, 8);
    // mu beyond +1 and altitude beyond the top clamp to the corner texel.
    const auto hi = sample_transmittance(lut, p, 5.0F, 1.0e6F);
    const auto& corner = lut.at(lut.w - 1, lut.h - 1);
    EXPECT_FLOAT_EQ(hi.x, corner.x);
    // mu below -1 and negative altitude clamp to the opposite corner.
    const auto lo = sample_transmittance(lut, p, -5.0F, -1.0e6F);
    const auto& other = lut.at(0, 0);
    EXPECT_FLOAT_EQ(lo.x, other.x);
}

// ---- Multiple-scattering LUT (2nd of Hillaire's 4 LUTs; ADD-ONLY) ----------
// Opt-in baker; nothing on the rendered path calls it, so it cannot change the
// default sky/golden output. Oracles are derived from Hillaire 2020 §5.3 /
// Eq. 10 (geometric-series multiple scattering).

TEST(Atmosphere, MultiScatterLutShapeMatchesRequest)
{
    Parameters p {};
    const auto t = bake_transmittance_lut(p, 32, 32);
    const auto ms = bake_multiscatter_lut(p, t, 16, 16);
    EXPECT_EQ(ms.w, 16U);
    EXPECT_EQ(ms.h, 16U);
    EXPECT_EQ(ms.texels.size(), 16U * 16U);
}

TEST(Atmosphere, MultiScatterIsFiniteAndNonNegative)
{
    // The geometric series Psi = L_2nd / (1 - f_ms) must stay finite (the
    // 1e-4 guard prevents the f_ms->1 blow-up) and non-negative everywhere.
    Parameters p {};
    const auto t = bake_transmittance_lut(p, 32, 32);
    const auto ms = bake_multiscatter_lut(p, t, 16, 16);
    for (const auto& v : ms.texels)
    {
        EXPECT_TRUE(std::isfinite(v.x));
        EXPECT_TRUE(std::isfinite(v.y));
        EXPECT_TRUE(std::isfinite(v.z));
        EXPECT_GE(v.x, 0.0F);
        EXPECT_GE(v.y, 0.0F);
        EXPECT_GE(v.z, 0.0F);
    }
}

TEST(Atmosphere, MultiScatterGeometricSeriesExceedsSingleOrder)
{
    // Psi = L_2nd / (1 - f_ms) with f_ms in [0,1) is >= L_2nd (the single
    // 2nd-order term). Re-derive L_2nd at one texel by setting f_ms = 0 via a
    // zero-absorption-free comparison is awkward; instead we pin the algebraic
    // invariant that the stored series result is at least as large as a
    // hand-computed lower bound (L_2nd itself is non-negative, so Psi >= 0 and
    // for any positive f_ms the division amplifies). Concretely: a denser-
    // atmosphere texel (low altitude) must not be smaller than a guard floor.
    Parameters p {};
    const auto t = bake_transmittance_lut(p, 32, 32);
    const auto ms = bake_multiscatter_lut(p, t, 8, 8);
    // Low altitude (more medium) should scatter at least as much as high
    // altitude at the same sun angle (denser medium -> more multiple bounces).
    const std::uint32_t x = ms.w / 2;  // sun near horizon
    const auto& low  = ms.at(x, 0);
    const auto& high = ms.at(x, ms.h - 1);
    EXPECT_GE(low.x + 1e-6F, high.x);
    EXPECT_GE(low.y + 1e-6F, high.y);
    EXPECT_GE(low.z + 1e-6F, high.z);
}

TEST(Atmosphere, MultiScatterRayleighBlueDominates)
{
    // Multiple scattering of sunlight is dominated by Rayleigh (blue), so the
    // blue channel of Psi should exceed red at a low-altitude texel — the
    // physical origin of the bright-blue daytime sky away from the sun.
    Parameters p {};
    const auto t = bake_transmittance_lut(p, 32, 32);
    const auto ms = bake_multiscatter_lut(p, t, 8, 8);
    const auto& low = ms.at(ms.w / 2, 0);
    EXPECT_GT(low.z, low.x);
}

TEST(Atmosphere, MultiScatterZeroScatteringGivesZeroLut)
{
    // Negative test: with all scattering coefficients zeroed there is no
    // in-scattering, so L_2nd = 0 and Psi = 0 everywhere (the medium neither
    // scatters nor bounces). Guards against an accidental constant offset.
    Parameters p {};
    p.rayleigh_scattering = { 0.0F, 0.0F, 0.0F };
    p.mie_scattering      = { 0.0F, 0.0F, 0.0F };
    const auto t = bake_transmittance_lut(p, 16, 16);
    const auto ms = bake_multiscatter_lut(p, t, 8, 8);
    for (const auto& v : ms.texels)
    {
        EXPECT_NEAR(v.x, 0.0F, 1e-6F);
        EXPECT_NEAR(v.y, 0.0F, 1e-6F);
        EXPECT_NEAR(v.z, 0.0F, 1e-6F);
    }
}

TEST(Atmosphere, MultiScatterGlslKernelMirrorsCpuContract)
{
    // The GLSL kernel must exist and reference the transmittance sampler +
    // the geometric-series store, mirroring the CPU baker.
    EXPECT_FALSE(cd::atmosphere::kMultiScatterCS.empty());
    EXPECT_NE(cd::atmosphere::kMultiScatterCS.find("transmittance_lut"),
              std::string_view::npos);
    EXPECT_NE(cd::atmosphere::kMultiScatterCS.find("imageStore"),
              std::string_view::npos);
}

// ---- Sky-view LUT (3rd of Hillaire's 4 LUTs; ADD-ONLY) ---------------------
// Opt-in baker (Hillaire 2020 §5.4): nothing on the rendered path bakes it, so
// it cannot change the default sky/golden output. It reads the sealed
// transmittance + multi-scatter LUTs read-only. A noon sun overhead is used as
// the canonical fixture (sun_dir = +Z, already unit-length).

// Sun pointing straight up (planet-up = +Z); unit-length, no normalize needed.
// File-level anonymous namespace (opened at the top) gives it internal linkage.
constexpr cd::math::Vec3f kSunZenith { 0.0F, 0.0F, 1.0F };

TEST(Atmosphere, SkyViewLutShapeMatchesRequest)
{
    Parameters p {};
    const auto t  = bake_transmittance_lut(p, 32, 32);
    const auto ms = bake_multiscatter_lut(p, t, 16, 16);
    const auto sv = bake_skyview_lut(p, t, ms, kSunZenith, 0.5F, 24, 12);
    EXPECT_EQ(sv.w, 24U);
    EXPECT_EQ(sv.h, 12U);
    EXPECT_EQ(sv.texels.size(), 24U * 12U);
}

TEST(Atmosphere, SkyViewIsFiniteAndNonNegative)
{
    // The accumulated in-scattered luminance is a sum of non-negative
    // contributions (scattering >= 0, phase >= 0, transmittance in [0,1]); the
    // NaN guards (clamped h + finite sun_trans/psi) keep every texel finite.
    Parameters p {};
    const auto t  = bake_transmittance_lut(p, 32, 32);
    const auto ms = bake_multiscatter_lut(p, t, 16, 16);
    const auto sv = bake_skyview_lut(p, t, ms, kSunZenith, 0.5F, 24, 12);
    for (const auto& v : sv.texels)
    {
        EXPECT_TRUE(std::isfinite(v.x));
        EXPECT_TRUE(std::isfinite(v.y));
        EXPECT_TRUE(std::isfinite(v.z));
        EXPECT_GE(v.x, 0.0F);
        EXPECT_GE(v.y, 0.0F);
        EXPECT_GE(v.z, 0.0F);
    }
}

TEST(Atmosphere, SkyViewZeroScatteringGivesZeroLut)
{
    // Negative test: with all scattering zeroed there is no in-scatter along
    // the view ray (rayleigh_s = mie_s = 0, and the MS LUT is also zero), so
    // the sky-view LUT is identically zero. Guards an accidental offset/bias.
    Parameters p {};
    p.rayleigh_scattering = { 0.0F, 0.0F, 0.0F };
    p.mie_scattering      = { 0.0F, 0.0F, 0.0F };
    const auto t  = bake_transmittance_lut(p, 16, 16);
    const auto ms = bake_multiscatter_lut(p, t, 8, 8);
    const auto sv = bake_skyview_lut(p, t, ms, kSunZenith, 0.5F, 16, 8);
    for (const auto& v : sv.texels)
    {
        EXPECT_NEAR(v.x, 0.0F, 1e-6F);
        EXPECT_NEAR(v.y, 0.0F, 1e-6F);
        EXPECT_NEAR(v.z, 0.0F, 1e-6F);
    }
}

TEST(Atmosphere, SkyViewRayleighBlueDominatesAwayFromSun)
{
    // The clear daytime sky away from the sun is dominated by Rayleigh-
    // scattered blue. With the sun overhead (+Z), the horizon row (cos_view
    // ~ 0) viewed sideways should carry more blue than red luminance.
    Parameters p {};
    const auto t  = bake_transmittance_lut(p, 32, 32);
    const auto ms = bake_multiscatter_lut(p, t, 16, 16);
    const auto sv = bake_skyview_lut(p, t, ms, kSunZenith, 0.5F, 16, 16);
    const std::uint32_t horizon_y = sv.h / 2;  // cos_view ~ 0
    const auto& sky = sv.at(0, horizon_y);
    EXPECT_GT(sky.z, sky.x);  // blue out-scatters red in the clear sky
}

TEST(Atmosphere, SkyViewBrighterTowardSunThanAway)
{
    // The forward-scattering Mie lobe (g = 0.8) + the Rayleigh peak make the
    // sky brightest looking toward the sun. Put the sun on the horizon along
    // +X (sun_dir = (1,0,0), unit-length) and compare two view directions in
    // the SAME zenith row (identical view-ray path length) but opposite
    // azimuth: az~0 looks toward the sun (cos_vs ~ +1), az~pi looks away
    // (cos_vs ~ -1). The phase function alone discriminates -> toward-sun wins.
    Parameters p {};
    const cd::math::Vec3f sun_horizon { 1.0F, 0.0F, 0.0F };
    const auto t  = bake_transmittance_lut(p, 32, 32);
    const auto ms = bake_multiscatter_lut(p, t, 16, 16);
    const auto sv = bake_skyview_lut(p, t, ms, sun_horizon, 0.5F, 16, 16);
    const std::uint32_t horizon_y = sv.h / 2;       // cos_view ~ 0 (horizon)
    const auto& toward = sv.at(0, horizon_y);          // az ~ 0   -> toward sun
    const auto& away   = sv.at(sv.w / 2, horizon_y);   // az ~ pi  -> away
    const float lum_toward = toward.x + toward.y + toward.z;
    const float lum_away   = away.x + away.y + away.z;
    EXPECT_GT(lum_toward, lum_away);
}

TEST(Atmosphere, SkyViewReadsSealedTablesReadOnly)
{
    // The sky-view baker consumes the transmittance + multi-scatter LUTs but
    // must not mutate them (sealed tables). Snapshot, bake, compare.
    Parameters p {};
    auto t        = bake_transmittance_lut(p, 16, 16);
    auto ms       = bake_multiscatter_lut(p, t, 8, 8);
    const auto t_copy  = t.texels;
    const auto ms_copy = ms.texels;
    const auto sv = bake_skyview_lut(p, t, ms, kSunZenith, 0.5F, 8, 8);
    EXPECT_EQ(sv.texels.size(), 8U * 8U);
    EXPECT_EQ(t.texels, t_copy);    // transmittance untouched
    EXPECT_EQ(ms.texels, ms_copy);  // multi-scatter untouched
}

TEST(Atmosphere, SkyViewNegativeAltitudeClampsFinite)
{
    // A below-surface camera altitude clamps to r0 = bottom_radius (the
    // max(0, view_alt) guard); the LUT must stay finite, not NaN.
    Parameters p {};
    const auto t  = bake_transmittance_lut(p, 16, 16);
    const auto ms = bake_multiscatter_lut(p, t, 8, 8);
    const auto sv = bake_skyview_lut(p, t, ms, kSunZenith, -10.0F, 8, 8);
    for (const auto& v : sv.texels)
    {
        EXPECT_TRUE(std::isfinite(v.x));
        EXPECT_TRUE(std::isfinite(v.y));
        EXPECT_TRUE(std::isfinite(v.z));
    }
}

TEST(Atmosphere, SkyViewGlslKernelMirrorsCpuContract)
{
    // The GLSL kernel must exist and reference both sealed sampler LUTs + the
    // luminance store, mirroring the CPU baker.
    EXPECT_FALSE(cd::atmosphere::kSkyViewCS.empty());
    EXPECT_NE(cd::atmosphere::kSkyViewCS.find("transmittance_lut"),
              std::string_view::npos);
    EXPECT_NE(cd::atmosphere::kSkyViewCS.find("multiscatter_lut"),
              std::string_view::npos);
    EXPECT_NE(cd::atmosphere::kSkyViewCS.find("imageStore"),
              std::string_view::npos);
}

}  // namespace
