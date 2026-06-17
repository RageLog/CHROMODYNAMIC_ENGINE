// =============================================================================
// CHROMODYNAMIC — Phase 469 froxel-based volumetric fog tests.
//
// Pure-CPU coverage of:
//   * Coordinate conversion (froxel <-> uvw, froxel <-> view-space)
//   * Slice quadratic distribution + round-trip
//   * Henyey-Greenstein phase function across 5+ cases
//   * Beer-Lambert + integrator regression
//   * GLSL kernel string sanity (inject / integrate / composite)
// =============================================================================
#include <cd/volumetric/VolumetricFog.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace
{

using cd::volumetric::beer_lambert;
using cd::volumetric::cell_count;
using cd::volumetric::extinction_of;
using cd::volumetric::froxel_to_uvw;
using cd::volumetric::froxel_to_view;
using cd::volumetric::FroxelGrid;
using cd::volumetric::FroxelGridDesc;
using cd::volumetric::henyey_greenstein;
using cd::volumetric::inject_cell;
using cd::volumetric::integrate_view_ray;
using cd::volumetric::kVolFogCompositeCS;
using cd::volumetric::kVolFogInjectCS;
using cd::volumetric::kVolFogIntegrateCS;
using cd::volumetric::scattering_of;
using cd::volumetric::slice_thickness;
using cd::volumetric::slice_to_view_z;
using cd::volumetric::view_to_froxel;
using cd::volumetric::view_z_to_slice;
using cd::volumetric::VolumetricFogSettings;
using cd::math::Vec3f;
using cd::math::Vec4f;

constexpr float kEps = 1.0e-3F;

// ---- Defaults & settings ----------------------------------------------------

TEST(VolFogFroxel, DefaultGridIs160x90x64)
{
    const FroxelGridDesc d {};
    EXPECT_EQ(d.width,  160u);
    EXPECT_EQ(d.height,  90u);
    EXPECT_EQ(d.depth,   64u);
    EXPECT_EQ(cell_count(d),
              static_cast<std::size_t>(160) * 90 * 64);
}

TEST(VolFogFroxel, ExtinctionEqualsDensityPlusAbsorption)
{
    VolumetricFogSettings s {};
    s.density = 0.10F;
    s.absorption = 0.05F;
    EXPECT_NEAR(extinction_of(s), 0.15F, 1e-6F);
    EXPECT_NEAR(scattering_of(s), 0.10F, 1e-6F);
}

TEST(VolFogFroxel, NegativeDensityClampedToZero)
{
    VolumetricFogSettings s {};
    s.density = -1.0F;
    s.absorption = 0.2F;
    // extinction_of clamps the sum to >=0, but a negative density
    // + positive absorption can still be net positive — verify both.
    EXPECT_GE(extinction_of(s), 0.0F);
    EXPECT_GE(scattering_of(s), 0.0F);
}

// ---- Coordinate conversion --------------------------------------------------

TEST(VolFogFroxel, FroxelToUvwCentred)
{
    FroxelGridDesc d {};
    d.width = 4; d.height = 4; d.depth = 4;
    const auto uvw = froxel_to_uvw(0, 0, 0, d);
    EXPECT_NEAR(uvw.x, 0.125F, 1e-6F);  // (0 + 0.5) / 4
    EXPECT_NEAR(uvw.y, 0.125F, 1e-6F);
    EXPECT_NEAR(uvw.z, 0.125F, 1e-6F);

    const auto uvw_last = froxel_to_uvw(3, 3, 3, d);
    EXPECT_NEAR(uvw_last.x, 0.875F, 1e-6F);  // (3 + 0.5) / 4
    EXPECT_NEAR(uvw_last.y, 0.875F, 1e-6F);
    EXPECT_NEAR(uvw_last.z, 0.875F, 1e-6F);
}

TEST(VolFogFroxel, SliceQuadraticDensePerNearCamera)
{
    FroxelGridDesc d {};
    d.near_z = 0.1F; d.far_z = 64.0F;
    const float near_half = slice_to_view_z(0.5F, d) - d.near_z;
    const float far_half  = d.far_z - slice_to_view_z(0.5F, d);
    EXPECT_LT(near_half, far_half);
}

TEST(VolFogFroxel, SliceRoundTrip)
{
    FroxelGridDesc d {};
    d.near_z = 0.1F; d.far_z = 100.0F;
    // cert-flp30-c: int induction; `s` accumulates `+= 0.1F` in the body so
    // the sample points are bit-identical to the former float-counter loop
    // (which ran 10 iterations: the accumulated 10th step exceeds 1.0F).
    float s = 0.0F;
    for (int i = 0; i < 10; ++i)
    {
        const float vz = slice_to_view_z(s, d);
        const float back = view_z_to_slice(vz, d);
        EXPECT_NEAR(back, s, kEps);
        s += 0.1F;
    }
}

TEST(VolFogFroxel, FroxelToViewRoundTrip)
{
    FroxelGridDesc d {};
    d.width = 16; d.height = 9; d.depth = 16;
    d.near_z = 0.5F; d.far_z = 32.0F;
    const float tan_half_fov_x = std::tan(60.0F * 0.5F * cd::math::pi / 180.0F);
    const float aspect = 16.0F / 9.0F;
    for (std::uint32_t z = 0; z < d.depth; ++z)
    {
        for (std::uint32_t y = 0; y < d.height; ++y)
        {
            for (std::uint32_t x = 0; x < d.width; ++x)
            {
                const auto view = froxel_to_view(x, y, z, d, tan_half_fov_x, aspect);
                const auto back = view_to_froxel(view, d, tan_half_fov_x, aspect);
                EXPECT_NEAR(back.x, static_cast<float>(x), kEps)
                    << "x=" << x << " y=" << y << " z=" << z;
                EXPECT_NEAR(back.y, static_cast<float>(y), kEps);
                EXPECT_NEAR(back.z, static_cast<float>(z), kEps);
            }
        }
    }
}

TEST(VolFogFroxel, ViewToFroxelRejectsBehindCamera)
{
    FroxelGridDesc d {};
    const float tan_half = std::tan(0.5F);
    const Vec3f behind { 0.0F, 0.0F, +1.0F };  // +Z is behind camera.
    const auto idx = view_to_froxel(behind, d, tan_half, 1.0F);
    EXPECT_LT(idx.x, 0.0F);
    EXPECT_LT(idx.y, 0.0F);
    EXPECT_LT(idx.z, 0.0F);
}

// ---- Phase function: 5+ cases ----------------------------------------------

TEST(VolFogFroxel, HenyeyGreensteinIsotropic)
{
    // g = 0 ⇒ uniform 1/(4π) regardless of cos θ.
    const float expected = 1.0F / (4.0F * cd::math::pi);
    EXPECT_NEAR(henyey_greenstein( 1.0F, 0.0F), expected, 1e-5F);
    EXPECT_NEAR(henyey_greenstein( 0.0F, 0.0F), expected, 1e-5F);
    EXPECT_NEAR(henyey_greenstein(-1.0F, 0.0F), expected, 1e-5F);
}

TEST(VolFogFroxel, HenyeyGreensteinForwardPeak)
{
    // g = +0.8: forward scatter dominates (cos θ = 1 is the max).
    const float fwd = henyey_greenstein( 1.0F, 0.8F);
    const float side = henyey_greenstein(0.0F, 0.8F);
    const float back = henyey_greenstein(-1.0F, 0.8F);
    EXPECT_GT(fwd, side);
    EXPECT_GT(side, back);
}

TEST(VolFogFroxel, HenyeyGreensteinBackwardPeak)
{
    // g = -0.5: peak is at cos θ = -1 (back-scatter).
    const float fwd = henyey_greenstein( 1.0F, -0.5F);
    const float side = henyey_greenstein(0.0F, -0.5F);
    const float back = henyey_greenstein(-1.0F, -0.5F);
    EXPECT_GT(back, side);
    EXPECT_GT(side, fwd);
}

TEST(VolFogFroxel, HenyeyGreensteinNormalisesOverSphere)
{
    // ∫ p(cos θ) dΩ = 1.  Sample on a regular grid in cos θ and φ
    // and verify Riemann sum ≈ 1 within tolerance.
    constexpr int kCosSteps = 256;
    constexpr int kPhiSteps = 1;  // azimuthally symmetric → factor of 2π
    const float dCos = 2.0F / static_cast<float>(kCosSteps);
    for (const float g : { -0.7F, -0.3F, 0.0F, 0.3F, 0.7F })
    {
        float sum = 0.0F;
        for (int i = 0; i < kCosSteps; ++i)
        {
            const float cosT = -1.0F + (static_cast<float>(i) + 0.5F) * dCos;
            sum += henyey_greenstein(cosT, g) * dCos;
        }
        sum *= 2.0F * cd::math::pi * static_cast<float>(kPhiSteps);
        EXPECT_NEAR(sum, 1.0F, 5e-3F) << "g=" << g;
    }
}

TEST(VolFogFroxel, HenyeyGreensteinNeverNegative)
{
    for (const float g : { -0.95F, -0.5F, -0.1F, 0.1F, 0.5F, 0.95F })
    {
        for (int i = 0; i <= 32; ++i)
        {
            const float cosT = -1.0F + 2.0F * static_cast<float>(i) / 32.0F;
            EXPECT_GE(henyey_greenstein(cosT, g), 0.0F)
                << "g=" << g << " cosT=" << cosT;
        }
    }
}

TEST(VolFogFroxel, HenyeyGreensteinSymmetricInGSign)
{
    // p(cos θ, g) == p(-cos θ, -g).
    for (const float g : { 0.1F, 0.4F, 0.85F })
    {
        for (const float cosT : { -0.9F, -0.4F, 0.0F, 0.4F, 0.9F })
        {
            const float a = henyey_greenstein( cosT,  g);
            const float b = henyey_greenstein(-cosT, -g);
            EXPECT_NEAR(a, b, 1e-5F) << "g=" << g << " cosT=" << cosT;
        }
    }
}

// ---- Beer-Lambert -----------------------------------------------------------

TEST(VolFogFroxel, BeerLambertHalfLife)
{
    // σ_t · d = ln 2 → T = 0.5.
    EXPECT_NEAR(beer_lambert(1.0F, std::log(2.0F)), 0.5F, 1e-5F);
}

TEST(VolFogFroxel, BeerLambertClampNegativeSigma)
{
    // Negative σ would amplify; helper clamps to 0 → T = 1.
    EXPECT_NEAR(beer_lambert(-2.0F, 10.0F), 1.0F, 1e-6F);
}

// ---- Inject + Integrate ------------------------------------------------------

TEST(VolFogFroxel, InjectCellAlphaIsExtinction)
{
    VolumetricFogSettings s {};
    s.density = 0.2F;
    s.absorption = 0.1F;
    const auto cell = inject_cell(s, 1.0F, Vec3f { 1, 1, 1 },
                                  Vec3f { 0, 0, -1 },
                                  Vec3f { 0, 0, -1 },
                                  1.0F);
    EXPECT_NEAR(cell.w, 0.3F, 1e-6F);
}

TEST(VolFogFroxel, IntegrateTransmittanceMonotonicallyDrops)
{
    FroxelGrid g;
    g.desc = { 4, 4, 8, 0.1F, 8.0F };
    g.resize();
    VolumetricFogSettings s {};
    s.density = 0.3F;
    for (std::uint32_t z = 0; z < g.desc.depth; ++z)
        for (std::uint32_t y = 0; y < g.desc.height; ++y)
            for (std::uint32_t x = 0; x < g.desc.width; ++x)
                g.at(x, y, z) = inject_cell(s, 1.0F, Vec3f { 1, 1, 1 },
                                            Vec3f { 0, 0, -1 },
                                            Vec3f { 0, 0, -1 },
                                            slice_thickness(z, g.desc));
    std::vector<Vec4f> out;
    integrate_view_ray(g, 1, 1, out);
    ASSERT_EQ(out.size(), g.desc.depth);
    float prev = 1.0F + kEps;
    for (const auto& a : out)
    {
        EXPECT_LE(a.w, prev + kEps);
        EXPECT_GE(a.w, 0.0F);
        prev = a.w;
    }
    // In-scattered RGB strictly accumulates (sun_color positive, phase
    // forward) for all slices.
    EXPECT_GT(out.back().x, 0.0F);
}

TEST(VolFogFroxel, IntegrateZeroExtinctionLeavesTransmittanceUnity)
{
    FroxelGrid g;
    g.desc = { 2, 2, 4, 0.0F, 4.0F };
    g.resize();
    std::vector<Vec4f> out;
    integrate_view_ray(g, 0, 0, out);
    for (const auto& a : out)
        EXPECT_NEAR(a.w, 1.0F, 1e-6F);
}

// ---- Froxel-bounds + accumulation edge branches (B4 topup) -----------------

TEST(VolFogFroxel, ViewToFroxelOutsideFrustumGoesOutOfRange)
{
    // A view-space point off to the far side of the +X frustum wall maps
    // to a froxel X index past the grid width (caller-clamp contract): the
    // helper does NOT clamp, it reports the out-of-range coordinate so the
    // consumer can skip the cell. Prior tests only checked behind-camera.
    FroxelGridDesc d {};
    d.width = 8; d.height = 8; d.depth = 8;
    d.near_z = 0.1F; d.far_z = 32.0F;
    const float tan_half = std::tan(0.5F);
    const float aspect = 1.0F;
    // Point well to the right of the right frustum edge at view-Z = 10.
    const Vec3f far_right { 100.0F, 0.0F, -10.0F };
    const auto idx = view_to_froxel(far_right, d, tan_half, aspect);
    EXPECT_GT(idx.x, static_cast<float>(d.width));  // beyond the grid
    EXPECT_GE(idx.z, 0.0F);                          // Z still in front
}

TEST(VolFogFroxel, SliceThicknessGrowsWithDepth)
{
    // Wronski quadratic warp: slices get thicker toward the far plane.
    // The last slice (z = depth-1) is the thickest; the first the thinnest.
    FroxelGridDesc d {};
    d.depth = 16; d.near_z = 0.1F; d.far_z = 64.0F;
    const float first = slice_thickness(0, d);
    const float last  = slice_thickness(d.depth - 1, d);
    EXPECT_GT(first, 0.0F);
    EXPECT_GT(last, first);
    // Thicknesses sum to the full near->far span (no gaps/overlaps).
    float total = 0.0F;
    for (std::uint32_t z = 0; z < d.depth; ++z) total += slice_thickness(z, d);
    EXPECT_NEAR(total, d.far_z - d.near_z, 1e-2F);
}

TEST(VolFogFroxel, IntegrateInScatterStopsAccumulatingPastFullExtinction)
{
    // With heavy extinction the transmittance collapses to ~0 within the
    // first slices; later slices contribute essentially nothing to RGB
    // because they are multiplied by the (now ~0) accumulated transmittance.
    // This pins the front-to-back occlusion order the integrator must obey.
    FroxelGrid g;
    g.desc = { 1, 1, 8, 0.1F, 8.0F };
    g.resize();
    VolumetricFogSettings s {};
    s.density = 50.0F;  // extreme extinction -> opaque fog
    for (std::uint32_t z = 0; z < g.desc.depth; ++z)
        g.at(0, 0, z) = inject_cell(s, 1.0F, Vec3f { 1, 1, 1 },
                                    Vec3f { 0, 0, -1 }, Vec3f { 0, 0, -1 },
                                    slice_thickness(z, g.desc));
    std::vector<Vec4f> out;
    integrate_view_ray(g, 0, 0, out);
    ASSERT_EQ(out.size(), g.desc.depth);
    // Transmittance is monotonically non-increasing and ends near 0.
    EXPECT_LT(out.back().w, 1e-3F);
    // The RGB delta between the last two slices is tiny vs the first step:
    // most in-scatter was captured up front (front-to-back is correct).
    const float first_step = out[0].x;
    const float tail_step  = out[g.desc.depth - 1].x - out[g.desc.depth - 2].x;
    EXPECT_GT(first_step, 0.0F);
    EXPECT_LT(tail_step, first_step);
}

// ---- GLSL kernel sanity -----------------------------------------------------

TEST(VolFogFroxel, GlslKernelsContainExpectedDirectives)
{
    EXPECT_NE(kVolFogInjectCS.find("#version 460"),     std::string_view::npos);
    EXPECT_NE(kVolFogInjectCS.find("image3D"),          std::string_view::npos);
    EXPECT_NE(kVolFogInjectCS.find("hg_phase"),         std::string_view::npos);

    EXPECT_NE(kVolFogIntegrateCS.find("#version 460"),  std::string_view::npos);
    EXPECT_NE(kVolFogIntegrateCS.find("sampler3D"),     std::string_view::npos);
    EXPECT_NE(kVolFogIntegrateCS.find("exp(-cell.a"),   std::string_view::npos);

    EXPECT_NE(kVolFogCompositeCS.find("#version 460"),  std::string_view::npos);
    EXPECT_NE(kVolFogCompositeCS.find("uIntegrated"),   std::string_view::npos);
    EXPECT_NE(kVolFogCompositeCS.find("scene.rgb * fog.a"),
                                                        std::string_view::npos);
}

}  // namespace
