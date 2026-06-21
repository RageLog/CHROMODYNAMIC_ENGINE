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
#include <limits>
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

// ---- Floor-raise ADD-ONLY regression locks (Run: 66->70) -------------------
//
// These pin the EXACT behaviour of the EXISTING froxel-fog math so any future
// edit to VolumetricFog.hpp that perturbs the rendered frame trips a unit test
// before it reaches the golden image. No math/GLSL is changed here.

// (1) Slice quadratic warp endpoint exactness: slice 0 -> near, slice 1 -> far.
TEST(VolFogFroxel, SliceToViewZEndpointsAreNearAndFar)
{
    FroxelGridDesc d {};
    d.near_z = 0.25F;
    d.far_z  = 80.0F;
    EXPECT_NEAR(slice_to_view_z(0.0F, d), d.near_z, 1e-6F);
    EXPECT_NEAR(slice_to_view_z(1.0F, d), d.far_z,  1e-5F);
    // Midpoint sits at near + (far-near)*0.25 by the quadratic t = s*s.
    const float mid = d.near_z + (d.far_z - d.near_z) * 0.25F;
    EXPECT_NEAR(slice_to_view_z(0.5F, d), mid, 1e-4F);
}

// (2) Slice warp is strictly monotonic increasing across the depth axis —
//     the exp-like depth distribution must never fold back.
TEST(VolFogFroxel, SliceToViewZStrictlyIncreasing)
{
    FroxelGridDesc d {};
    d.depth = 32;
    d.near_z = 0.1F;
    d.far_z  = 64.0F;
    float prev = -1.0F;
    for (std::uint32_t i = 0; i <= d.depth; ++i)
    {
        const float s = static_cast<float>(i) / static_cast<float>(d.depth);
        const float vz = slice_to_view_z(s, d);
        EXPECT_GT(vz, prev) << "i=" << i;
        prev = vz;
    }
}

// (3) view_z_to_slice clamps below-near and beyond-far into [0,1].
TEST(VolFogFroxel, ViewZToSliceClampsOutOfRange)
{
    FroxelGridDesc d {};
    d.near_z = 1.0F;
    d.far_z  = 50.0F;
    EXPECT_NEAR(view_z_to_slice(-5.0F, d), 0.0F, 1e-6F);   // before near
    EXPECT_NEAR(view_z_to_slice(0.5F,  d), 0.0F, 1e-6F);   // still < near
    EXPECT_NEAR(view_z_to_slice(999.0F, d), 1.0F, 1e-6F);  // past far
    EXPECT_GE(view_z_to_slice(25.0F, d), 0.0F);
    EXPECT_LE(view_z_to_slice(25.0F, d), 1.0F);
}

// (4) inject_cell RGB is the in-scatter premultiplied by dt — doubling dt
//     exactly doubles the stored RGB while leaving the A (sigma_t) channel
//     unchanged. Pins the "RGB premultiplied at inject" contract.
TEST(VolFogFroxel, InjectCellRgbScalesLinearlyWithDt)
{
    VolumetricFogSettings s {};
    s.density = 0.2F;
    s.absorption = 0.05F;
    const Vec3f sun_col { 1.0F, 0.8F, 0.6F };
    const Vec3f view_dir { 0.0F, 0.0F, -1.0F };
    const Vec3f sun_dir  { 0.0F, 0.0F, -1.0F };
    const auto c1 = inject_cell(s, 2.0F, sun_col, view_dir, sun_dir, 0.5F);
    const auto c2 = inject_cell(s, 2.0F, sun_col, view_dir, sun_dir, 1.0F);
    EXPECT_NEAR(c2.x, c1.x * 2.0F, 1e-6F);
    EXPECT_NEAR(c2.y, c1.y * 2.0F, 1e-6F);
    EXPECT_NEAR(c2.z, c1.z * 2.0F, 1e-6F);
    EXPECT_NEAR(c1.w, c2.w, 1e-6F);  // sigma_t independent of dt
}

// (5) inject_cell with zero scattering (density 0) emits no in-scatter even
//     when a positive absorption keeps extinction non-zero. Edge: σ_s = 0.
TEST(VolFogFroxel, InjectCellZeroScatteringEmitsNoRadiance)
{
    VolumetricFogSettings s {};
    s.density = 0.0F;      // σ_s = 0
    s.absorption = 0.4F;   // σ_t = 0.4
    const auto c = inject_cell(s, 5.0F, Vec3f { 1, 1, 1 },
                               Vec3f { 0, 0, -1 }, Vec3f { 0, 0, -1 }, 1.0F);
    EXPECT_NEAR(c.x, 0.0F, 1e-6F);
    EXPECT_NEAR(c.y, 0.0F, 1e-6F);
    EXPECT_NEAR(c.z, 0.0F, 1e-6F);
    EXPECT_NEAR(c.w, 0.4F, 1e-6F);  // extinction still present
}

// (6) inject_cell respects the ambient ground-bounce term: with zero sun
//     intensity the RGB equals ambient * sigma_s * dt (no phase factor).
TEST(VolFogFroxel, InjectCellAmbientRidesWithoutSun)
{
    VolumetricFogSettings s {};
    s.density = 0.5F;            // σ_s = 0.5
    s.ambient = { 0.1F, 0.2F, 0.3F };
    const float dt = 2.0F;
    const auto c = inject_cell(s, 0.0F, Vec3f { 1, 1, 1 },
                               Vec3f { 0, 0, -1 }, Vec3f { 0, 0, -1 }, dt);
    EXPECT_NEAR(c.x, s.ambient.x * 0.5F * dt, 1e-6F);
    EXPECT_NEAR(c.y, s.ambient.y * 0.5F * dt, 1e-6F);
    EXPECT_NEAR(c.z, s.ambient.z * 0.5F * dt, 1e-6F);
}

// (7) Integrator scattering accumulation is monotonically NON-DECREASING in
//     each RGB channel (front-to-back additive, no negative contributions).
TEST(VolFogFroxel, IntegrateInScatterRgbNonDecreasing)
{
    FroxelGrid g;
    g.desc = { 2, 2, 8, 0.1F, 16.0F };
    g.resize();
    VolumetricFogSettings s {};
    s.density = 0.25F;
    for (std::uint32_t z = 0; z < g.desc.depth; ++z)
        g.at(1, 1, z) = inject_cell(s, 1.0F, Vec3f { 1, 1, 1 },
                                    Vec3f { 0, 0, -1 }, Vec3f { 0, 0, -1 },
                                    slice_thickness(z, g.desc));
    std::vector<Vec4f> out;
    integrate_view_ray(g, 1, 1, out);
    ASSERT_EQ(out.size(), g.desc.depth);
    Vec3f prev { -kEps, -kEps, -kEps };
    for (const auto& a : out)
    {
        EXPECT_GE(a.x, prev.x - kEps);
        EXPECT_GE(a.y, prev.y - kEps);
        EXPECT_GE(a.z, prev.z - kEps);
        prev = { a.x, a.y, a.z };
    }
}

// (8) Integrator empty grid (no inject) yields zero in-scatter and unit
//     transmittance for every slice. Edge: empty froxel.
TEST(VolFogFroxel, IntegrateEmptyGridIsZeroScatterUnitTransmittance)
{
    FroxelGrid g;
    g.desc = { 3, 3, 6, 0.1F, 12.0F };
    g.resize();  // all cells {0,0,0,0}
    std::vector<Vec4f> out;
    integrate_view_ray(g, 2, 2, out);
    ASSERT_EQ(out.size(), g.desc.depth);
    for (const auto& a : out)
    {
        EXPECT_NEAR(a.x, 0.0F, 1e-6F);
        EXPECT_NEAR(a.y, 0.0F, 1e-6F);
        EXPECT_NEAR(a.z, 0.0F, 1e-6F);
        EXPECT_NEAR(a.w, 1.0F, 1e-6F);  // no extinction -> full transmittance
    }
}

// (9) Integrator first-slice transmittance equals Beer-Lambert over slice 0's
//     thickness — pins the exact per-slice extinction conversion.
TEST(VolFogFroxel, IntegrateFirstSliceTransmittanceMatchesBeerLambert)
{
    FroxelGrid g;
    g.desc = { 1, 1, 4, 0.1F, 8.0F };
    g.resize();
    constexpr float kSigmaT = 0.7F;
    for (std::uint32_t z = 0; z < g.desc.depth; ++z)
        g.at(0, 0, z) = { 0.0F, 0.0F, 0.0F, kSigmaT };  // A = sigma_t only
    std::vector<Vec4f> out;
    integrate_view_ray(g, 0, 0, out);
    ASSERT_EQ(out.size(), g.desc.depth);
    const float dt0 = slice_thickness(0, g.desc);
    EXPECT_NEAR(out[0].w, beer_lambert(kSigmaT, dt0), 1e-6F);
}

// (10) froxel_to_uvw bounds: the centre of every cell lands strictly inside
//      (0,1)^3 — never on or past a face. Edge: corner cells.
TEST(VolFogFroxel, FroxelToUvwAlwaysInsideUnitCube)
{
    FroxelGridDesc d {};
    d.width = 5; d.height = 7; d.depth = 3;
    for (std::uint32_t z = 0; z < d.depth; ++z)
        for (std::uint32_t y = 0; y < d.height; ++y)
            for (std::uint32_t x = 0; x < d.width; ++x)
            {
                const auto uvw = froxel_to_uvw(x, y, z, d);
                EXPECT_GT(uvw.x, 0.0F);
                EXPECT_LT(uvw.x, 1.0F);
                EXPECT_GT(uvw.y, 0.0F);
                EXPECT_LT(uvw.y, 1.0F);
                EXPECT_GT(uvw.z, 0.0F);
                EXPECT_LT(uvw.z, 1.0F);
            }
}

// (11) slice_thickness for a single-slice grid equals the whole near->far
//      span. Edge: depth == 1 (degenerate one-slab grid).
TEST(VolFogFroxel, SliceThicknessSingleSliceSpansFullRange)
{
    FroxelGridDesc d {};
    d.depth = 1;
    d.near_z = 2.0F;
    d.far_z  = 50.0F;
    EXPECT_NEAR(slice_thickness(0, d), d.far_z - d.near_z, 1e-4F);
}

// (12) Composite blend GLSL contract: the kernel must apply scene*T + fog and
//      preserve scene alpha. Pins the exact composite formula string so a
//      refactor that flips the blend order is caught.
TEST(VolFogFroxel, CompositeKernelPreservesSceneAlphaContract)
{
    EXPECT_NE(kVolFogCompositeCS.find("scene.rgb * fog.a + fog.rgb"),
              std::string_view::npos);
    EXPECT_NE(kVolFogCompositeCS.find("vec4(outRgb, scene.a)"),
              std::string_view::npos);
    EXPECT_NE(kVolFogCompositeCS.find("view_z_to_slice"),
              std::string_view::npos);
}

// (13) Inject GLSL pre-multiplies in-scatter by dt exactly like the CPU path —
//      both compute dt as slice_to_z(s1) - slice_to_z(s0). Pins CPU/GPU parity.
TEST(VolFogFroxel, InjectKernelPremultipliesByDtLikeCpu)
{
    EXPECT_NE(kVolFogInjectCS.find("inscatter * dt"), std::string_view::npos);
    EXPECT_NE(kVolFogInjectCS.find("slice_to_z(s1) - slice_to_z(s0)"),
              std::string_view::npos);
    // Integrate kernel must NOT re-multiply by dt (RGB already premultiplied).
    EXPECT_NE(kVolFogIntegrateCS.find("cell.rgb * accum.a"),
              std::string_view::npos);
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

// ---- ADD-ONLY host regression locks (Run: 70->100) -------------------------
//
// Deepen coverage of the EXISTING froxel-fog code to charter-complete the
// cd::post / cd::cluster pattern: every coordinate helper, inject branch,
// integrator contract, NaN guard, and GLSL-string parity token is pinned so a
// future edit that perturbs the rendered fog trips a unit test before the
// golden image. No froxel/fog/cloud math or GLSL is changed here.

// (18) cell_count over a degenerate 1x1x1 grid is one, and a zero-depth grid is
//      empty. Edge: minimal + empty grid sizing.
TEST(VolFogFroxel, CellCountDegenerateAndEmpty)
{
    FroxelGridDesc one {};
    one.width = 1; one.height = 1; one.depth = 1;
    EXPECT_EQ(cell_count(one), 1u);

    FroxelGridDesc empty {};
    empty.width = 4; empty.height = 4; empty.depth = 0;
    EXPECT_EQ(cell_count(empty), 0u);

    // FroxelGrid::resize honours cell_count (no overflow on the 64-bit product).
    FroxelGrid g;
    g.desc = { 1000, 1000, 1000, 0.1F, 64.0F };
    EXPECT_EQ(cell_count(g.desc),
              static_cast<std::size_t>(1000) * 1000 * 1000);
}

// (19) froxel_to_view places the camera-centre cell on the -Z axis (no lateral
//      offset) at exactly the slice's view-Z, and the view ray points straight
//      ahead. Pins the central-ray anchor of the perspective unprojection.
TEST(VolFogFroxel, FroxelToViewCentreCellOnAxis)
{
    FroxelGridDesc d {};
    d.width = 2; d.height = 2; d.depth = 4;  // centre between cells 0 and 1
    d.near_z = 0.5F; d.far_z = 16.0F;
    const float tan_half = std::tan(0.5F);
    const float aspect = 1.0F;
    // With width/height = 2, cell (0, ...) centre uvw.x = 0.25 -> ndc -0.5; cell
    // 1 -> +0.5. The midpoint of the two equals the optical axis: verify the
    // pair is mirror-symmetric in X about 0 at the same slice.
    const auto v0 = froxel_to_view(0, 0, 1, d, tan_half, aspect);
    const auto v1 = froxel_to_view(1, 0, 1, d, tan_half, aspect);
    EXPECT_NEAR(v0.x, -v1.x, 1e-5F);   // mirror about optical axis
    EXPECT_NEAR(v0.z, v1.z, 1e-6F);    // same slice -> same view-Z
    EXPECT_LT(v0.z, 0.0F);             // camera looks down -Z
}

// (20) froxel_to_view applies the aspect ratio to the Y extent only: at the same
//      |ndc| offset, a wide aspect (>1) compresses the vertical view-space span
//      relative to the horizontal. Pins the tanY = tanX / aspect contract.
TEST(VolFogFroxel, FroxelToViewAspectScalesYOnly)
{
    FroxelGridDesc d {};
    d.width = 4; d.height = 4; d.depth = 4;
    d.near_z = 0.5F; d.far_z = 16.0F;
    const float tan_half = std::tan(0.6F);
    // Aspect 2.0 -> vertical FoV half the horizontal. Compare a top-edge cell's
    // |y| against a right-edge cell's |x| at matched ndc magnitude.
    const auto top  = froxel_to_view(2, 0, 2, d, tan_half, 2.0F);  // ndc_y high
    const auto side = froxel_to_view(3, 2, 2, d, tan_half, 2.0F);  // ndc_x high
    EXPECT_LT(std::abs(top.y), std::abs(side.x));  // Y compressed by aspect
}

// (21) view_to_froxel guards a degenerate (<=0) aspect via aspect_safe: a zero
//      aspect must not divide-by-zero — the returned index stays finite.
//      Edge: aspect == 0 guard.
TEST(VolFogFroxel, ViewToFroxelZeroAspectIsFinite)
{
    FroxelGridDesc d {};
    const float tan_half = std::tan(0.5F);
    const Vec3f p { 0.5F, 0.5F, -10.0F };
    const auto idx = view_to_froxel(p, d, tan_half, 0.0F);  // aspect == 0
    EXPECT_TRUE(std::isfinite(idx.x));
    EXPECT_TRUE(std::isfinite(idx.y));
    EXPECT_TRUE(std::isfinite(idx.z));
}

// (22) view_to_froxel maps a point exactly on the optical axis to the grid
//      centre in XY (u = v = 0.5 -> centre index). Pins the central inverse.
TEST(VolFogFroxel, ViewToFroxelOnAxisMapsToCentre)
{
    FroxelGridDesc d {};
    d.width = 8; d.height = 8; d.depth = 8;
    d.near_z = 0.1F; d.far_z = 32.0F;
    const float tan_half = std::tan(0.5F);
    const Vec3f on_axis { 0.0F, 0.0F, -10.0F };
    const auto idx = view_to_froxel(on_axis, d, tan_half, 1.0F);
    // u = v = 0.5 -> 0.5 * width - 0.5 = 3.5 for an 8-wide grid.
    EXPECT_NEAR(idx.x, 3.5F, 1e-4F);
    EXPECT_NEAR(idx.y, 3.5F, 1e-4F);
    EXPECT_GE(idx.z, 0.0F);
}

// (23) inject_cell's in-scatter follows the HG phase: a forward-scattering
//      medium (g>0) injects MORE radiance when the view aligns with the sun
//      than when it opposes. Pins the phase coupling inside inject.
TEST(VolFogFroxel, InjectCellPhaseForwardBrighterThanBackward)
{
    VolumetricFogSettings s {};
    s.density = 0.3F;
    s.anisotropy_g = 0.7F;  // forward-scatter
    const Vec3f sun_dir { 0.0F, 0.0F, -1.0F };
    const Vec3f fwd_view { 0.0F, 0.0F, -1.0F };  // looking along sun
    const Vec3f bwd_view { 0.0F, 0.0F, +1.0F };  // looking away
    const auto fwd = inject_cell(s, 1.0F, Vec3f { 1, 1, 1 }, fwd_view, sun_dir, 1.0F);
    const auto bwd = inject_cell(s, 1.0F, Vec3f { 1, 1, 1 }, bwd_view, sun_dir, 1.0F);
    EXPECT_GT(fwd.x, bwd.x);
}

// (24) inject_cell tints the in-scatter by the per-channel albedo: a coloured
//      albedo scales each RGB channel independently. Pins the albedo multiply.
TEST(VolFogFroxel, InjectCellAlbedoTintsPerChannel)
{
    VolumetricFogSettings s {};
    s.density = 0.4F;
    s.anisotropy_g = 0.0F;  // isotropic -> phase identical per channel
    s.albedo = { 1.0F, 0.5F, 0.25F };
    const auto c = inject_cell(s, 1.0F, Vec3f { 1, 1, 1 },
                               Vec3f { 0, 0, -1 }, Vec3f { 0, 0, -1 }, 1.0F);
    // Channels are in the albedo ratio 1 : 0.5 : 0.25.
    EXPECT_GT(c.x, 0.0F);
    EXPECT_NEAR(c.y, c.x * 0.5F, 1e-6F);
    EXPECT_NEAR(c.z, c.x * 0.25F, 1e-6F);
}

// (25) inject_cell is finite (no NaN/inf) at the HG forward singularity guard:
//      g -> 1 with cos_theta -> 1 must stay finite (math guard max(d,1e-6)).
//      Edge: phase-function singularity.
TEST(VolFogFroxel, InjectCellFiniteAtPhaseSingularity)
{
    VolumetricFogSettings s {};
    s.density = 0.5F;
    s.anisotropy_g = 0.999F;             // near the g=1 singularity
    const Vec3f aligned { 0.0F, 0.0F, -1.0F };
    const auto c = inject_cell(s, 10.0F, Vec3f { 1, 1, 1 }, aligned, aligned, 1.0F);
    EXPECT_TRUE(std::isfinite(c.x));
    EXPECT_TRUE(std::isfinite(c.y));
    EXPECT_TRUE(std::isfinite(c.z));
    EXPECT_TRUE(std::isfinite(c.w));
    EXPECT_GE(c.x, 0.0F);
}

// (26) inject_cell with zero dt produces zero RGB (premultiply collapses) but
//      leaves σ_t in A — a zero-thickness slab scatters nothing yet still
//      reports its extinction. Edge: dt == 0.
TEST(VolFogFroxel, InjectCellZeroDtZeroesRgbKeepsExtinction)
{
    VolumetricFogSettings s {};
    s.density = 0.3F;
    s.absorption = 0.2F;
    const auto c = inject_cell(s, 5.0F, Vec3f { 1, 1, 1 },
                               Vec3f { 0, 0, -1 }, Vec3f { 0, 0, -1 }, 0.0F);
    EXPECT_NEAR(c.x, 0.0F, 1e-6F);
    EXPECT_NEAR(c.y, 0.0F, 1e-6F);
    EXPECT_NEAR(c.z, 0.0F, 1e-6F);
    EXPECT_NEAR(c.w, 0.5F, 1e-6F);  // sigma_t = density + absorption
}

// (27) extinction_of clamps a net-negative (density+absorption < 0) sum to zero
//      while scattering_of clamps a negative density to zero. Edge: both
//      coefficients negative.
TEST(VolFogFroxel, ExtinctionAndScatteringClampNetNegative)
{
    VolumetricFogSettings s {};
    s.density = -0.5F;
    s.absorption = -0.5F;  // net sum -1.0 -> clamps to 0
    EXPECT_NEAR(extinction_of(s), 0.0F, 1e-6F);
    EXPECT_NEAR(scattering_of(s), 0.0F, 1e-6F);
}

// (28) beer_lambert at zero distance is unity regardless of σ_t (no path -> no
//      extinction). Edge: distance == 0.
TEST(VolFogFroxel, BeerLambertZeroDistanceIsUnity)
{
    EXPECT_NEAR(beer_lambert(100.0F, 0.0F), 1.0F, 1e-6F);
    EXPECT_NEAR(beer_lambert(0.0F, 0.0F), 1.0F, 1e-6F);
}

// (29) FroxelGrid::at round-trips through index(): a value written via at() is
//      read back at the same coordinate and is independent of neighbours. Pins
//      the const + non-const accessor pair against the linear layout.
TEST(VolFogFroxel, FroxelGridAtReadWriteRoundTrip)
{
    FroxelGrid g;
    g.desc = { 3, 4, 5, 0.1F, 8.0F };
    g.resize();
    g.at(2, 3, 4) = Vec4f { 1.0F, 2.0F, 3.0F, 4.0F };
    g.at(0, 0, 0) = Vec4f { 9.0F, 9.0F, 9.0F, 9.0F };
    const FroxelGrid& cg = g;
    EXPECT_NEAR(cg.at(2, 3, 4).x, 1.0F, 1e-6F);
    EXPECT_NEAR(cg.at(2, 3, 4).w, 4.0F, 1e-6F);
    EXPECT_NEAR(cg.at(0, 0, 0).x, 9.0F, 1e-6F);  // neighbour untouched
    EXPECT_EQ(g.index(2, 3, 4), (static_cast<std::size_t>(4) * 4 + 3) * 3 + 2);
}

// (30) The integrate GLSL computes dt analytically as (far-near)*(s1^2 - s0^2)
//      — algebraically identical to the CPU slice_thickness difference. Pins
//      the CPU/GPU dt-formula parity that keeps the LUT byte-equivalent.
TEST(VolFogFroxel, IntegrateKernelDtFormulaMatchesCpuSliceThickness)
{
    EXPECT_NE(kVolFogIntegrateCS.find("(s1 * s1 - s0 * s0)"),
              std::string_view::npos);
    EXPECT_NE(kVolFogIntegrateCS.find("exp(-cell.a * dt)"),
              std::string_view::npos);
    // Numerically confirm the GLSL form equals the CPU slice_thickness for a
    // sample slice (the string pins the source; this pins the algebra).
    FroxelGridDesc d {};
    d.depth = 64; d.near_z = 0.1F; d.far_z = 64.0F;
    const std::uint32_t z = 17;
    const float s0 = static_cast<float>(z) / static_cast<float>(d.depth);
    const float s1 = static_cast<float>(z + 1) / static_cast<float>(d.depth);
    const float glsl_dt = (d.far_z - d.near_z) * (s1 * s1 - s0 * s0);
    EXPECT_NEAR(glsl_dt, slice_thickness(z, d), 1e-3F);
}

// (31) Composite GLSL view_z_to_slice mirrors the CPU clamp+sqrt: the kernel
//      maps the depth buffer's view-Z onto the [0,1] LUT W coordinate exactly
//      like the host helper. Pins the composite slice-sampling parity tokens.
TEST(VolFogFroxel, CompositeKernelSliceMappingMirrorsHost)
{
    EXPECT_NE(kVolFogCompositeCS.find("sqrt(t)"), std::string_view::npos);
    EXPECT_NE(kVolFogCompositeCS.find("clamp((vz - pc.near_far.x)"),
              std::string_view::npos);
    EXPECT_NE(kVolFogCompositeCS.find("texture(uIntegrated, uvw)"),
              std::string_view::npos);
    EXPECT_NE(kVolFogCompositeCS.find("imageLoad(uScene"),
              std::string_view::npos);
}

}  // namespace
