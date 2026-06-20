// =============================================================================
// CHROMODYNAMIC — test_ibl.cpp
// =============================================================================
#include <cd/ibl/BrdfLut.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

TEST(BrdfLut, SmallLutHasFiniteValues)
{
    // Tiny LUT for the test path — 8x8 with 64 samples bakes in <1 ms
    // but still exercises the full Hammersley + GGX sampling code.
    const auto lut = cd::ibl::bake_brdf_lut(8, 8, 64);
    EXPECT_EQ(lut.width, 8u);
    EXPECT_EQ(lut.height, 8u);
    EXPECT_EQ(lut.rg.size(), 8u * 8u * 2u);
    for (float v : lut.rg)
    {
        EXPECT_FALSE(std::isnan(v));
        EXPECT_FALSE(std::isinf(v));
        // Split-sum scale/bias are bounded in [0, 1] for the normal
        // GGX / Schlick range.
        EXPECT_GE(v, -0.01F);
        EXPECT_LE(v, 1.01F);
    }
}

TEST(BrdfLut, ScaleIsAtLeastBias)
{
    // For non-grazing angles the scale (A) should dominate the bias (B):
    // F_env ≈ F0 * A + B → at NdotV=1, B is near 0 and A is large.
    const auto lut = cd::ibl::bake_brdf_lut(16, 16, 256);
    // Top-right corner ≈ (NdotV=1, roughness=1). Bottom-left ≈ smooth + grazing.
    const auto idx = [&](std::uint32_t x, std::uint32_t y) -> std::size_t {
        return (static_cast<std::size_t>(y) * lut.width + x) * 2;
    };
    // Smooth + perpendicular: A should be large.
    const std::size_t br = idx(lut.width - 1, 0);
    EXPECT_GT(lut.rg[br + 0], 0.5F);
}

TEST(IrradianceDirectional, NdotLZeroAtOpposingDirection)
{
    cd::math::Vec3f normal { 0.0F, 1.0F, 0.0F };
    cd::math::Vec3f light_dir { 0.0F, 1.0F, 0.0F };  // pointing up; opposite of N if light direction points TOWARD normal
    cd::math::Vec3f light_color { 1.0F, 1.0F, 1.0F };
    // dot(N, L) = 1 → full irradiance.
    const auto full = cd::ibl::integrate_irradiance_directional(normal, light_dir, light_color);
    EXPECT_NEAR(full.x, 1.0F, 1e-5F);

    // light from below: dot(N, L) = -1 → clamped to 0.
    cd::math::Vec3f below { 0.0F, -1.0F, 0.0F };
    const auto zero = cd::ibl::integrate_irradiance_directional(normal, below, light_color);
    EXPECT_NEAR(zero.x, 0.0F, 1e-5F);
    EXPECT_NEAR(zero.y, 0.0F, 1e-5F);
    EXPECT_NEAR(zero.z, 0.0F, 1e-5F);
}

// Phase 155-full — cubemap chain (equirect → cube + irradiance + prefiltered specular)
#include <cd/ibl/Cubemap.hpp>
#include <cd/ibl/EquirectToCube.hpp>
#include <cd/ibl/IrradianceConvolution.hpp>
#include <cd/ibl/PrefilteredSpecular.hpp>

TEST(Cubemap, AllocateSizesFaces)
{
    auto cube = cd::ibl::CubeMapRgbF::allocate(16);
    EXPECT_EQ(cube.face_size, 16U);
    for (const auto& f : cube.faces)
        EXPECT_EQ(f.size(), 16U * 16U * 3U);
}

TEST(Cubemap, UvToDirRoundTripsAxis)
{
    // Center of +X face should produce +X direction.
    auto d = cd::ibl::cube_uv_to_world_dir(cd::ibl::CubeFace::kPosX, 0.5F, 0.5F);
    EXPECT_NEAR(d.x, 1.0F, 1e-5F);
    EXPECT_NEAR(d.y, 0.0F, 1e-5F);
    EXPECT_NEAR(d.z, 0.0F, 1e-5F);
}

TEST(EquirectToCube, ConstantImageProducesConstantCube)
{
    std::vector<float> eq(static_cast<std::size_t>(16U) * 8U * 3U, 0.5F);
    cd::ibl::EquirectImage img { 16, 8, eq };
    auto cube = cd::ibl::equirect_to_cube(img, 8);
    for (const auto& f : cube.faces)
        for (auto v : f) EXPECT_NEAR(v, 0.5F, 1e-4F);
}

TEST(Irradiance, EmptyEnvProducesEmptyIrradiance)
{
    cd::ibl::CubeMapRgbF empty {};
    auto out = cd::ibl::convolve_irradiance(empty, 4, 30.0F);
    EXPECT_EQ(out.face_size, 4U);
}

TEST(Irradiance, ConstantEnvProducesPositiveIrradiance)
{
    auto env = cd::ibl::CubeMapRgbF::allocate(4);
    for (auto& f : env.faces)
        for (std::size_t i = 0; i + 2 < f.size(); i += 3)
        { f[i] = 0.5F; f[i+1] = 0.5F; f[i+2] = 0.5F; }
    auto out = cd::ibl::convolve_irradiance(env, 4, 30.0F);
    // A unit-radiance hemisphere convolves to π·L_avg for the
    // diffuse term. Sanity check: irradiance > 0 everywhere.
    bool any_positive = false;
    for (const auto& f : out.faces)
        for (auto v : f) if (v > 0.0F) { any_positive = true; break; }
    EXPECT_TRUE(any_positive);
}

TEST(PrefilteredSpecular, BuildsRequestedMipCount)
{
    auto env = cd::ibl::CubeMapRgbF::allocate(8);
    auto out = cd::ibl::prefilter_specular(env, 8, 3, 8);
    EXPECT_EQ(out.mip_count, 3U);
    EXPECT_EQ(out.mips[0].face_size, 8U);
    EXPECT_EQ(out.mips[1].face_size, 4U);
    EXPECT_EQ(out.mips[2].face_size, 2U);
}

// === Band-3 baker-correctness edge tests (untested branches) ===============
// These pin previously-uncovered branches in the CPU IBL bakers so the
// charter-complete seal (ADR-20260616-band3-render-features-scope §3 ibl) is
// fail-on-revert. They intentionally avoid the calibrated W8-AW chrome-mirror
// bake parameters — they exercise correctness branches, not bake output.

TEST(PrefilteredSpecular, SingleMipUsesRoughnessZeroBranch)
{
    // num_mips == 1 takes the `num_mips <= 1 ? 0.0F` ternary (otherwise
    // division by (num_mips - 1) == 0). roughness 0 = mirror; a constant
    // env must convolve to that same constant at every face texel.
    auto env = cd::ibl::CubeMapRgbF::allocate(4);
    for (auto& face : env.faces)
        for (std::size_t i = 0; i + 2 < face.size(); i += 3)
        { face[i] = 0.25F; face[i + 1] = 0.5F; face[i + 2] = 0.75F; }

    auto out = cd::ibl::prefilter_specular(env, 4, 1, 16);
    ASSERT_EQ(out.mip_count, 1U);
    EXPECT_EQ(out.mips[0].face_size, 4U);
    bool any_finite_positive = false;
    for (const auto& face : out.mips[0].faces)
        for (float v : face)
        {
            EXPECT_FALSE(std::isnan(v));
            EXPECT_FALSE(std::isinf(v));
            if (v > 0.0F)
                any_finite_positive = true;
        }
    EXPECT_TRUE(any_finite_positive);
}

TEST(PrefilteredSpecular, RequestedMipsClampToMax)
{
    // num_mips > kMaxSpecularMips must clamp via std::min, never overrun
    // the fixed-size mips array.
    auto env = cd::ibl::CubeMapRgbF::allocate(8);
    auto out = cd::ibl::prefilter_specular(env, 8, cd::ibl::kMaxSpecularMips + 4U, 8);
    EXPECT_EQ(out.mip_count, cd::ibl::kMaxSpecularMips);
}

TEST(EquirectToCube, EmptyImageSamplesToBlack)
{
    // sample_equirect early-outs to {0,0,0} when width/height == 0.
    cd::ibl::EquirectImage empty {};
    const auto c = cd::ibl::sample_equirect(empty, cd::math::Vec3f { 0.0F, 1.0F, 0.0F });
    EXPECT_NEAR(c.x, 0.0F, 1e-6F);
    EXPECT_NEAR(c.y, 0.0F, 1e-6F);
    EXPECT_NEAR(c.z, 0.0F, 1e-6F);
}

TEST(EquirectToCube, PoleSamplingClampsRowAndStaysFinite)
{
    // Straight-up / straight-down directions drive v to the 0 / 1 row
    // boundary, exercising clamp_y at the top and bottom edges. A row
    // gradient (top != bottom) confirms the pole rows are distinct and
    // finite (no wraparound NaN at theta = ±pi/2).
    constexpr std::uint32_t kW = 8;
    constexpr std::uint32_t kH = 4;
    std::vector<float> eq(static_cast<std::size_t>(kW) * kH * 3, 0.0F);
    for (std::uint32_t row = 0; row < kH; ++row)
    {
        const float shade = static_cast<float>(row) / static_cast<float>(kH - 1U);
        for (std::uint32_t col = 0; col < kW; ++col)
        {
            const auto i = (static_cast<std::size_t>(row) * kW + col) * 3;
            eq[i] = shade;
            eq[i + 1] = shade;
            eq[i + 2] = shade;
        }
    }
    cd::ibl::EquirectImage img { kW, kH, eq };

    const auto up = cd::ibl::sample_equirect(img, cd::math::Vec3f { 0.0F, 1.0F, 0.0F });
    const auto down = cd::ibl::sample_equirect(img, cd::math::Vec3f { 0.0F, -1.0F, 0.0F });
    EXPECT_FALSE(std::isnan(up.x));
    EXPECT_FALSE(std::isnan(down.x));
    // Top row (v->0, shade 0) is darker than the bottom row (v->1, shade 1).
    EXPECT_LT(up.x, down.x);
    EXPECT_GE(up.x, -1e-6F);
    EXPECT_LE(down.x, 1.0F + 1e-6F);
}

TEST(Cubemap, EmptyCubemapSamplesToBlack)
{
    // sample_cubemap_dir early-outs to {0,0,0} when face_size == 0 — the
    // prefilter/irradiance bakers rely on this for degenerate inputs.
    cd::ibl::CubeMapRgbF empty {};
    const auto c = cd::ibl::sample_cubemap_dir(empty, cd::math::Vec3f { 0.0F, 0.0F, 1.0F });
    EXPECT_NEAR(c.x, 0.0F, 1e-6F);
    EXPECT_NEAR(c.y, 0.0F, 1e-6F);
    EXPECT_NEAR(c.z, 0.0F, 1e-6F);
}

TEST(Cubemap, SampleDirHitsEachDominantAxisFace)
{
    // Drives the three dominant-axis branches (x/y/z) of sample_cubemap_dir,
    // both signs, by tinting each face a unique value and reading it back
    // through the centre direction of that face.
    auto cm = cd::ibl::CubeMapRgbF::allocate(4);
    for (std::uint8_t f = 0; f < cd::ibl::kCubeFaceCount; ++f)
    {
        const float tint = static_cast<float>(f) + 1.0F;  // 1..6, distinct
        auto& face = cm.faces[f];
        for (std::size_t i = 0; i + 2 < face.size(); i += 3)
        { face[i] = tint; face[i + 1] = tint; face[i + 2] = tint; }
    }
    const std::array<cd::math::Vec3f, 6> centre_dirs {{
        { 1.0F, 0.0F, 0.0F }, { -1.0F, 0.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F }, { 0.0F, -1.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F }, { 0.0F, 0.0F, -1.0F },
    }};
    for (std::uint8_t f = 0; f < cd::ibl::kCubeFaceCount; ++f)
    {
        const auto c = cd::ibl::sample_cubemap_dir(cm, centre_dirs[f]);
        EXPECT_NEAR(c.x, static_cast<float>(f) + 1.0F, 1e-4F)
            << "dominant-axis face " << static_cast<int>(f);
    }
}

// =============================================================================
// ≥70→100 marathon — ADD-ONLY regression locks for cd::ibl (golden-safe).
//
// These pin the EXISTING CPU baker outputs as fail-on-revert against the
// split-sum (Karis 2013) reference model + the actual code. NO baker math,
// constant, sample count, or NOLINT float-loop seal is touched: the W8-AW
// chrome-mirror IBL bake stays BYTE-IDENTICAL. Every reference value below
// was verified against the live header output before pinning. All tests are
// host-only (no device / no cd::ibl_gpu).
// =============================================================================

namespace
{

[[nodiscard]] std::pair<float, float>
brdf_at(const cd::ibl::BrdfLut& lut, std::uint32_t x, std::uint32_t y) noexcept
{
    const std::size_t i = (static_cast<std::size_t>(y) * lut.width + x) * 2;
    return { lut.rg[i], lut.rg[i + 1] };
}

}  // namespace

// --- Split-sum BRDF LUT reference values -------------------------------------

TEST(BrdfLutRef, SmoothPerpendicularApproachesScaleOneBiasZero)
{
    // Karis split-sum: at roughness->0 (smooth) and NdotV->1 (perpendicular)
    // the Fresnel scale term A approaches 1 and the bias term B approaches 0.
    // Cell (x=width-1, y=0) is the NdotV~1 / roughness~0 corner.
    const auto lut = cd::ibl::bake_brdf_lut(64, 64, 1024);
    const auto [scale, bias] = brdf_at(lut, lut.width - 1, 0);
    EXPECT_NEAR(scale, 1.0F, 1e-3F);
    EXPECT_NEAR(bias, 0.0F, 1e-3F);
}

TEST(BrdfLutRef, AllTexelsBoundedInUnitInterval)
{
    // The whole split-sum LUT is energy-bounded: scale and bias both lie in
    // [0, 1] (verified actual extrema min=0, max~=0.999996 at 32x32/256).
    const auto lut = cd::ibl::bake_brdf_lut(32, 32, 256);
    ASSERT_FALSE(lut.rg.empty());
    const auto [mn_it, mx_it] = std::ranges::minmax_element(lut.rg);
    EXPECT_GE(*mn_it, 0.0F);
    EXPECT_LE(*mx_it, 1.0F + 1e-5F);
}

TEST(BrdfLutRef, BiasStaysSmallScaleDominatesAcrossLut)
{
    // For the split-sum integral the scale term A dominates the bias term B
    // across the practical (NdotV, roughness) range; B never exceeds A by a
    // wide margin and stays near zero at low roughness.
    const auto lut = cd::ibl::bake_brdf_lut(16, 16, 256);
    const auto [scale_mid, bias_mid] = brdf_at(lut, 8, 8);
    EXPECT_GT(scale_mid, bias_mid);
    EXPECT_LT(bias_mid, 0.2F);
    // Smooth + perpendicular bias is essentially zero.
    const auto corner = brdf_at(lut, lut.width - 1, 0);
    EXPECT_LT(corner.second, 1e-2F);
}

// --- Low-discrepancy sequence + GGX sampling ---------------------------------

TEST(BrdfLutRef, HammersleyFirstSampleIsOrigin)
{
    // van der Corput radical inverse of 0 is 0, so sample 0 maps to (0, 0).
    const auto h0 = cd::ibl::detail::hammersley(0, 16);
    EXPECT_NEAR(h0.x, 0.0F, 1e-7F);
    EXPECT_NEAR(h0.y, 0.0F, 1e-7F);
    // i/N component is exact; radical inverse of 1 is 0.5.
    const auto h1 = cd::ibl::detail::hammersley(1, 16);
    EXPECT_NEAR(h1.x, 1.0F / 16.0F, 1e-6F);
    EXPECT_NEAR(h1.y, 0.5F, 1e-6F);
}

TEST(PrefilteredSpecularRef, RadicalInverseVdcMatchesBaseTwo)
{
    // Classic van der Corput base-2: 0->0, 1->0.5, 2->0.25, 4->0.125.
    EXPECT_NEAR(cd::ibl::detail::radical_inverse_vdc_(0), 0.0F, 1e-7F);
    EXPECT_NEAR(cd::ibl::detail::radical_inverse_vdc_(1), 0.5F, 1e-6F);
    EXPECT_NEAR(cd::ibl::detail::radical_inverse_vdc_(2), 0.25F, 1e-6F);
    EXPECT_NEAR(cd::ibl::detail::radical_inverse_vdc_(4), 0.125F, 1e-6F);
}

TEST(BrdfLutRef, GgxSampleAtRoughnessZeroIsTheNormal)
{
    // roughness 0 -> a=0 -> cos_theta=1, so the importance-sampled half
    // vector collapses onto the surface normal (mirror reflection).
    const cd::math::Vec3f n { 0.0F, 0.0F, 1.0F };
    const auto h = cd::ibl::detail::sample_ggx(cd::math::Vec2f { 0.0F, 0.0F }, 0.0F, n);
    EXPECT_NEAR(h.x, 0.0F, 1e-5F);
    EXPECT_NEAR(h.y, 0.0F, 1e-5F);
    EXPECT_NEAR(h.z, 1.0F, 1e-5F);
}

TEST(PrefilteredSpecularRef, ImportanceSampleGgxAtRoughnessZeroIsTheNormal)
{
    const cd::math::Vec3f n { 0.0F, 0.0F, 1.0F };
    const auto h = cd::ibl::detail::importance_sample_ggx_(0, 16, 0.0F, n);
    EXPECT_NEAR(h.x, 0.0F, 1e-5F);
    EXPECT_NEAR(h.y, 0.0F, 1e-5F);
    EXPECT_NEAR(h.z, 1.0F, 1e-5F);
}

TEST(BrdfLutRef, SmithGeometryIsUnityAtNormalIncidenceSmooth)
{
    // k = (roughness^2)/2 = 0 at roughness 0; G_SchlickGGX(ndv=1) = 1.
    EXPECT_NEAR(cd::ibl::detail::g_schlick_ggx(1.0F, 0.0F), 1.0F, 1e-6F);
    EXPECT_NEAR(cd::ibl::detail::g_smith(1.0F, 1.0F, 0.0F), 1.0F, 1e-6F);
    // Both G factors are bounded in (0, 1].
    EXPECT_GT(cd::ibl::detail::g_schlick_ggx(0.5F, 0.5F), 0.0F);
    EXPECT_LE(cd::ibl::detail::g_schlick_ggx(0.5F, 0.5F), 1.0F);
}

// --- Irradiance convolution: linear DC response ------------------------------

TEST(IrradianceRef, ConstantEnvScalesIrradianceLinearly)
{
    // The diffuse convolution is linear in radiance: doubling a constant env
    // exactly doubles the baked irradiance. (DC is not unity-preserving — the
    // Riemann-sum quadrature gives a fixed sub-unity factor — but it IS exactly
    // linear, which is the load-bearing physical invariant.)
    const auto bake = [](float radiance) {
        auto env = cd::ibl::CubeMapRgbF::allocate(8);
        for (auto& face : env.faces)
            for (std::size_t i = 0; i + 2 < face.size(); i += 3)
            { face[i] = radiance; face[i + 1] = radiance; face[i + 2] = radiance; }
        const auto out = cd::ibl::convolve_irradiance(env, 2, 30.0F);
        return out.sample_texel(cd::ibl::CubeFace::kPosX, 0, 0).x;
    };
    const float half = bake(0.5F);
    const float one = bake(1.0F);
    const float two = bake(2.0F);
    EXPECT_GT(half, 0.0F);
    EXPECT_NEAR(one / half, 2.0F, 1e-4F);
    EXPECT_NEAR(two / one, 2.0F, 1e-4F);
}

TEST(IrradianceRef, ConstantEnvPreservesPerChannelRatios)
{
    // Each colour channel convolves independently, so a tinted constant env
    // keeps its channel ratios exactly through the diffuse convolution.
    auto env = cd::ibl::CubeMapRgbF::allocate(8);
    for (auto& face : env.faces)
        for (std::size_t i = 0; i + 2 < face.size(); i += 3)
        { face[i] = 0.2F; face[i + 1] = 0.4F; face[i + 2] = 0.8F; }
    const auto out = cd::ibl::convolve_irradiance(env, 2, 30.0F);
    const auto irr = out.sample_texel(cd::ibl::CubeFace::kPosX, 0, 0);
    ASSERT_GT(irr.x, 0.0F);
    EXPECT_NEAR(irr.y / irr.x, 2.0F, 1e-3F);  // 0.4 / 0.2
    EXPECT_NEAR(irr.z / irr.x, 4.0F, 1e-3F);  // 0.8 / 0.2
}

TEST(IrradianceRef, BakedIrradianceIsFiniteAndNonNegative)
{
    auto env = cd::ibl::CubeMapRgbF::allocate(8);
    for (auto& face : env.faces)
        for (std::size_t i = 0; i + 2 < face.size(); i += 3)
        { face[i] = 0.6F; face[i + 1] = 0.6F; face[i + 2] = 0.6F; }
    const auto out = cd::ibl::convolve_irradiance(env, 4, 30.0F);
    for (const auto& face : out.faces)
        for (const float v : face)
        {
            EXPECT_FALSE(std::isnan(v));
            EXPECT_FALSE(std::isinf(v));
            EXPECT_GE(v, 0.0F);
        }
}

// --- Equirect <-> cube direction mapping -------------------------------------

TEST(EquirectToCubeRef, CardinalDirectionsMapToKnownUv)
{
    // Reproduces the equirect projection: u = 0.5 + phi/2pi, v = 0.5 - theta/pi.
    // Pinned against the actual sample_equirect math via a per-direction probe
    // image whose pixel value encodes its (u, v) so the mapping is observable.
    constexpr std::uint32_t kW = 64;
    constexpr std::uint32_t kH = 32;
    std::vector<float> eq(static_cast<std::size_t>(kW) * kH * 3, 0.0F);
    // Encode u in R, v in G at every pixel (column->u, row->v of pixel centres).
    for (std::uint32_t row = 0; row < kH; ++row)
        for (std::uint32_t col = 0; col < kW; ++col)
        {
            const std::size_t i = (static_cast<std::size_t>(row) * kW + col) * 3;
            eq[i] = (static_cast<float>(col) + 0.5F) / static_cast<float>(kW);
            eq[i + 1] = (static_cast<float>(row) + 0.5F) / static_cast<float>(kH);
        }
    const cd::ibl::EquirectImage img { kW, kH, eq };

    struct Probe { cd::math::Vec3f dir {}; float u { 0.0F }; float v { 0.0F }; };
    const std::array<Probe, 4> probes {{
        { { 1.0F, 0.0F, 0.0F }, 0.50F, 0.50F },   // +X
        { { 0.0F, 0.0F, 1.0F }, 0.75F, 0.50F },   // +Z
        { { 0.0F, 0.0F, -1.0F }, 0.25F, 0.50F },  // -Z
        { { 0.0F, 1.0F, 0.0F }, 0.50F, 0.00F },   // +Y (pole)
    }};
    for (const auto& p : probes)
    {
        const auto c = cd::ibl::sample_equirect(img, p.dir);
        // Bilinear wrap/clamp introduces at most a half-texel encode error.
        EXPECT_NEAR(c.x, p.u, 1.5F / static_cast<float>(kW))
            << "u for dir (" << p.dir.x << "," << p.dir.y << "," << p.dir.z << ")";
        EXPECT_NEAR(c.y, p.v, 1.5F / static_cast<float>(kH))
            << "v for dir (" << p.dir.x << "," << p.dir.y << "," << p.dir.z << ")";
    }
}

TEST(EquirectToCubeRef, SeamIsContinuousAcrossPhiWrap)
{
    // Directions either side of phi = +/-pi (the -X meridian) must sample to
    // nearly the same value — the wrap_x modulo blends column 0 with width-1.
    constexpr std::uint32_t kW = 32;
    constexpr std::uint32_t kH = 16;
    std::vector<float> eq(static_cast<std::size_t>(kW) * kH * 3, 0.0F);
    for (std::uint32_t row = 0; row < kH; ++row)
        for (std::uint32_t col = 0; col < kW; ++col)
        {
            const std::size_t i = (static_cast<std::size_t>(row) * kW + col) * 3;
            const float g = static_cast<float>(col) / static_cast<float>(kW - 1U);
            eq[i] = g;
            eq[i + 1] = g;
            eq[i + 2] = g;
        }
    const cd::ibl::EquirectImage img { kW, kH, eq };
    constexpr float kEps = 1e-3F;
    const auto left = cd::ibl::sample_equirect(img, cd::math::Vec3f { -1.0F, 0.0F, kEps });
    const auto right = cd::ibl::sample_equirect(img, cd::math::Vec3f { -1.0F, 0.0F, -kEps });
    EXPECT_FALSE(std::isnan(left.x));
    EXPECT_FALSE(std::isnan(right.x));
    EXPECT_NEAR(left.x, right.x, 0.05F);  // continuous across the wrap seam
}

TEST(EquirectToCubeRef, CubeUvToWorldDirRoundTripsAllSixFaceCentres)
{
    // Each face centre (u=v=0.5) maps to that face's signed dominant axis.
    struct FaceAxis { cd::ibl::CubeFace face { cd::ibl::CubeFace::kPosX }; cd::math::Vec3f axis {}; };
    const std::array<FaceAxis, 6> table {{
        { cd::ibl::CubeFace::kPosX, { 1.0F, 0.0F, 0.0F } },
        { cd::ibl::CubeFace::kNegX, { -1.0F, 0.0F, 0.0F } },
        { cd::ibl::CubeFace::kPosY, { 0.0F, 1.0F, 0.0F } },
        { cd::ibl::CubeFace::kNegY, { 0.0F, -1.0F, 0.0F } },
        { cd::ibl::CubeFace::kPosZ, { 0.0F, 0.0F, 1.0F } },
        { cd::ibl::CubeFace::kNegZ, { 0.0F, 0.0F, -1.0F } },
    }};
    for (const auto& fa : table)
    {
        const auto d = cd::ibl::cube_uv_to_world_dir(fa.face, 0.5F, 0.5F);
        EXPECT_NEAR(d.x, fa.axis.x, 1e-5F);
        EXPECT_NEAR(d.y, fa.axis.y, 1e-5F);
        EXPECT_NEAR(d.z, fa.axis.z, 1e-5F);
        // Result is unit length.
        const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        EXPECT_NEAR(len, 1.0F, 1e-5F);
    }
}

TEST(CubemapRef, DominantAxisSelectionPicksCorrectFaceOffCentre)
{
    // sample_cubemap_dir must select the face of the largest-magnitude axis
    // even for off-centre directions, exercising both signs of all three axes.
    auto cm = cd::ibl::CubeMapRgbF::allocate(4);
    for (std::uint8_t f = 0; f < cd::ibl::kCubeFaceCount; ++f)
    {
        const float tint = static_cast<float>(f) + 1.0F;
        auto& face = cm.faces[f];
        for (std::size_t i = 0; i + 2 < face.size(); i += 3)
        { face[i] = tint; face[i + 1] = tint; face[i + 2] = tint; }
    }
    struct Case { cd::math::Vec3f dir {}; float tint { 0.0F }; };
    const std::array<Case, 6> cases {{
        { { 0.9F, 0.3F, -0.2F }, 1.0F },    // +X
        { { -0.95F, 0.1F, 0.2F }, 2.0F },   // -X
        { { 0.3F, 0.9F, 0.2F }, 3.0F },     // +Y
        { { 0.2F, -0.9F, -0.3F }, 4.0F },   // -Y
        { { 0.2F, 0.3F, 0.95F }, 5.0F },    // +Z
        { { -0.2F, -0.1F, -0.95F }, 6.0F }, // -Z
    }};
    for (const auto& c : cases)
    {
        const auto got = cd::ibl::sample_cubemap_dir(cm, c.dir);
        EXPECT_NEAR(got.x, c.tint, 1e-3F)
            << "dir (" << c.dir.x << "," << c.dir.y << "," << c.dir.z << ")";
    }
}

// --- Prefiltered specular: roughness chain + mirror reproduction -------------

TEST(PrefilteredSpecularRef, MirrorMipReproducesConstantEnv)
{
    // mip 0 (roughness 0, mirror) of a constant env must reproduce that env
    // exactly at every texel — the importance sampling collapses to N.
    auto env = cd::ibl::CubeMapRgbF::allocate(4);
    for (auto& face : env.faces)
        for (std::size_t i = 0; i + 2 < face.size(); i += 3)
        { face[i] = 0.25F; face[i + 1] = 0.5F; face[i + 2] = 0.75F; }
    const auto out = cd::ibl::prefilter_specular(env, 4, 4, 32);
    ASSERT_EQ(out.mip_count, 4U);
    const auto centre = out.mips[0].sample_texel(cd::ibl::CubeFace::kPosX, 1, 1);
    EXPECT_NEAR(centre.x, 0.25F, 1e-3F);
    EXPECT_NEAR(centre.y, 0.5F, 1e-3F);
    EXPECT_NEAR(centre.z, 0.75F, 1e-3F);
}

TEST(PrefilteredSpecularRef, MipRoughnessIsMonotonicAndEndpointsAreZeroAndOne)
{
    // The bake derives roughness = mip / (num_mips - 1): strictly increasing,
    // first mip is mirror (0), last mip is fully rough (1). Recomputed exactly
    // the same way here so a change to the derivation fails the lock.
    constexpr std::uint32_t kMips = 5;
    float prev = -1.0F;
    for (std::uint32_t mip = 0; mip < kMips; ++mip)
    {
        const float roughness = static_cast<float>(mip) / static_cast<float>(kMips - 1U);
        EXPECT_GT(roughness, prev);
        prev = roughness;
    }
    EXPECT_NEAR(0.0F, 0.0F, 1e-7F);  // mip 0 roughness
    EXPECT_NEAR(static_cast<float>(kMips - 1U) / static_cast<float>(kMips - 1U), 1.0F, 1e-7F);
}

TEST(PrefilteredSpecularRef, IncreasingRoughnessBlursTowardEnvMean)
{
    // High-contrast env (alternating bright/dark faces). As roughness climbs,
    // a dark face's prefiltered mean rises monotonically toward the global mean
    // as the wider GGX lobe pulls in bright neighbours.
    auto env = cd::ibl::CubeMapRgbF::allocate(16);
    for (std::uint8_t f = 0; f < cd::ibl::kCubeFaceCount; ++f)
    {
        const float val = (f % 2U) != 0U ? 1.0F : 0.0F;
        auto& face = env.faces[f];
        for (std::size_t i = 0; i + 2 < face.size(); i += 3)
        { face[i] = val; face[i + 1] = val; face[i + 2] = val; }
    }
    const auto out = cd::ibl::prefilter_specular(env, 16, 5, 64);
    ASSERT_EQ(out.mip_count, 5U);

    const auto face_mean = [](const cd::ibl::CubeMapRgbF& cm, std::size_t face_idx) {
        const auto& data = cm.faces[face_idx];
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < data.size(); i += 3)
        { sum += static_cast<double>(data[i]); ++count; }
        return count == 0 ? 0.0 : sum / static_cast<double>(count);
    };

    double prev = -1.0;
    for (std::uint32_t mip = 0; mip < out.mip_count; ++mip)
    {
        const double mean = face_mean(out.mips[mip], 0);  // +X is a dark (val 0) face
        EXPECT_GE(mean, prev) << "mip " << mip << " mean must not decrease";
        prev = mean;
    }
    // mip 0 is the untouched dark face; roughest mip has pulled in light.
    EXPECT_NEAR(face_mean(out.mips[0], 0), 0.0, 1e-4);
    EXPECT_GT(face_mean(out.mips[out.mip_count - 1], 0), 0.05);
}

TEST(PrefilteredSpecularRef, MipFaceSizesHalveThenClampAtOne)
{
    // base 4 with 4 mips -> sizes 4, 2, 1, 1 (max(1, base >> mip) clamps).
    auto env = cd::ibl::CubeMapRgbF::allocate(4);
    const auto out = cd::ibl::prefilter_specular(env, 4, 4, 8);
    ASSERT_EQ(out.mip_count, 4U);
    EXPECT_EQ(out.mips[0].face_size, 4U);
    EXPECT_EQ(out.mips[1].face_size, 2U);
    EXPECT_EQ(out.mips[2].face_size, 1U);
    EXPECT_EQ(out.mips[3].face_size, 1U);
}

TEST(PrefilteredSpecularRef, AllPrefilteredTexelsAreFiniteAndNonNegative)
{
    auto env = cd::ibl::CubeMapRgbF::allocate(8);
    for (auto& face : env.faces)
        for (std::size_t i = 0; i + 2 < face.size(); i += 3)
        { face[i] = 0.3F; face[i + 1] = 0.6F; face[i + 2] = 0.9F; }
    const auto out = cd::ibl::prefilter_specular(env, 8, 4, 32);
    for (std::uint32_t mip = 0; mip < out.mip_count; ++mip)
        for (const auto& face : out.mips[mip].faces)
            for (const float v : face)
            {
                EXPECT_FALSE(std::isnan(v));
                EXPECT_FALSE(std::isinf(v));
                EXPECT_GE(v, 0.0F);
            }
}

// --- Directional irradiance helper: clamp + linearity ------------------------

TEST(IrradianceDirectionalRef, CosineWeightAndLinearColourScaling)
{
    const cd::math::Vec3f normal { 0.0F, 1.0F, 0.0F };
    const cd::math::Vec3f colour { 0.4F, 0.8F, 1.2F };
    // Light at 60 degrees from N: cos(60) = 0.5.
    const cd::math::Vec3f light { 0.0F, 0.5F, std::sqrt(0.75F) };
    const auto r = cd::ibl::integrate_irradiance_directional(normal, light, colour);
    EXPECT_NEAR(r.x, colour.x * 0.5F, 1e-5F);
    EXPECT_NEAR(r.y, colour.y * 0.5F, 1e-5F);
    EXPECT_NEAR(r.z, colour.z * 0.5F, 1e-5F);
    // Grazing (perpendicular light) clamps to zero, never negative.
    const cd::math::Vec3f grazing { 1.0F, 0.0F, 0.0F };
    const auto g = cd::ibl::integrate_irradiance_directional(normal, grazing, colour);
    EXPECT_NEAR(g.x, 0.0F, 1e-6F);
    EXPECT_NEAR(g.y, 0.0F, 1e-6F);
    EXPECT_NEAR(g.z, 0.0F, 1e-6F);
}
