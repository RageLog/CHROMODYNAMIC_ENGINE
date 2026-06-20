// =============================================================================
// CHROMODYNAMIC -- engine/render/ddgi/tests/test_ddgi_probe_math.cpp
// 80->100 marathon -- host-side probe-grid / atlas / octahedral / fallback
// edge + negative coverage.
//
// ADD-ONLY: this file extends the existing CPU-math coverage in test_ddgi.cpp
// with the boundary / degenerate paths the original six required cases did not
// pin:
//   * ProbeGrid::probe_world_pos (member) + flat_index <-> shader-decode
//     round-trip (px = idx % x, py = (idx/x) % y, pz = idx/(x*y)) for the
//     full 8x4x8 grid.
//   * probe_count for 0 / 1 / asymmetric grids.
//   * probe_index_from_world for partial-out-of-bounds cells (some corners in,
//     some out -> in corners keep trilinear weight, out corners weight 0 idx 0)
//     and for points below the origin.
//   * trilinear_probe_weights edge-clamp behaviour for out-of-bounds corners.
//   * ProbeAtlas::init_from_grid dimensions + probe_uv zero-atlas guard +
//     octahedral-fold (-Z) UV branch.
//   * octahedral_encode/decode round-trip over a dense direction set.
//   * IrradianceField::ambient_fallback for the empty / sky / hit branches +
//     init() atlas sizing.
//
// None of these change rendered output -- they exercise the library's pure
// host math standalone. CPU-only: no Vulkan ICD required, runs everywhere.
//
// hello_engine is NOT touched. No existing probe / blend / sample math is
// modified -- the GLSL strings + host helpers stay byte-identical.
// =============================================================================

#include <cd/ddgi/Ddgi.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace
{

using cd::ddgi::IrradianceField;
using cd::ddgi::ProbeAtlas;
using cd::ddgi::ProbeGrid;
using cd::ddgi::TraceSettings;

using cd::ddgi::octahedral_decode;
using cd::ddgi::octahedral_encode;
using cd::ddgi::trilinear_probe_weights;
using cd::math::Vec2f;
using cd::math::Vec3f;

constexpr float kEps = 1e-3F;

[[nodiscard]] float angle_deg(Vec3f a, Vec3f b)
{
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z;
    const float c   = std::max(-1.0F, std::min(1.0F, dot));
    constexpr float kDegToRad = static_cast<float>(std::numbers::pi) / 180.0F;
    return std::acos(c) / kDegToRad;
}

// ===========================================================================
// ProbeGrid::probe_count -- 0 / 1 / asymmetric
// ===========================================================================
TEST(DdgiProbeMath, ProbeCountDefaultGrid)
{
    const ProbeGrid g {};
    EXPECT_EQ(g.probe_count(), 8U * 4U * 8U);
}

TEST(DdgiProbeMath, ProbeCountSingleProbe)
{
    ProbeGrid g {};
    g.probes_x = 1;
    g.probes_y = 1;
    g.probes_z = 1;
    EXPECT_EQ(g.probe_count(), 1U);
}

TEST(DdgiProbeMath, ProbeCountZeroWhenAnyAxisZero)
{
    ProbeGrid g {};
    g.probes_y = 0;
    EXPECT_EQ(g.probe_count(), 0U);
}

TEST(DdgiProbeMath, ProbeCountAsymmetricGrid)
{
    ProbeGrid g {};
    g.probes_x = 3;
    g.probes_y = 5;
    g.probes_z = 7;
    EXPECT_EQ(g.probe_count(), 3U * 5U * 7U);
}

// ===========================================================================
// ProbeGrid::probe_world_pos (member) -- offset by spacing from origin.
// ===========================================================================
TEST(DdgiProbeMath, ProbeWorldPosMemberOffsetsBySpacing)
{
    ProbeGrid g {};
    g.origin  = { 1.0F, 2.0F, 3.0F };
    g.spacing = { 2.0F, 4.0F, 8.0F };
    const Vec3f p = g.probe_world_pos(2U, 1U, 3U);
    EXPECT_NEAR(p.x, 1.0F + 2.0F * 2.0F, kEps);
    EXPECT_NEAR(p.y, 2.0F + 1.0F * 4.0F, kEps);
    EXPECT_NEAR(p.z, 3.0F + 3.0F * 8.0F, kEps);
}

TEST(DdgiProbeMath, ProbeWorldPosOriginProbeIsOrigin)
{
    ProbeGrid g {};
    g.origin = { -5.0F, 7.0F, 0.5F };
    const Vec3f p = g.probe_world_pos(0U, 0U, 0U);
    EXPECT_NEAR(p.x, g.origin.x, kEps);
    EXPECT_NEAR(p.y, g.origin.y, kEps);
    EXPECT_NEAR(p.z, g.origin.z, kEps);
}

// ===========================================================================
// flat_index <-> shader-decode round-trip.
//
// The trace / blend / sample GLSL all decode a flat index as:
//   pz  = idx / (x*y);  rem = idx % (x*y);  py = rem / x;  px = rem % x;
// flat_index packs as px + x*(py + y*pz). Verify the two are exact inverses
// across the entire default grid so the host index math the shaders mirror
// stays consistent (a silent divergence here would corrupt every atlas read).
// ===========================================================================
TEST(DdgiProbeMath, FlatIndexRoundTripsShaderDecode)
{
    const ProbeGrid g {};
    const std::uint32_t xy = g.probes_x * g.probes_y;
    for (std::uint32_t pz = 0; pz < g.probes_z; ++pz)
        for (std::uint32_t py = 0; py < g.probes_y; ++py)
            for (std::uint32_t px = 0; px < g.probes_x; ++px)
            {
                const std::uint32_t idx = g.flat_index(px, py, pz);
                EXPECT_LT(idx, g.probe_count());

                // Inverse decode (matches GLSL).
                const std::uint32_t d_pz  = idx / xy;
                const std::uint32_t d_rem = idx % xy;
                const std::uint32_t d_py  = d_rem / g.probes_x;
                const std::uint32_t d_px  = d_rem % g.probes_x;
                EXPECT_EQ(d_px, px) << "idx=" << idx;
                EXPECT_EQ(d_py, py) << "idx=" << idx;
                EXPECT_EQ(d_pz, pz) << "idx=" << idx;
            }
}

TEST(DdgiProbeMath, FlatIndexIsContiguousZeroToCount)
{
    const ProbeGrid g {};
    // Every flat index in [0, count) must be produced exactly once.
    std::array<bool, static_cast<std::size_t>(8U) * 4U * 8U> seen {};
    for (std::uint32_t pz = 0; pz < g.probes_z; ++pz)
        for (std::uint32_t py = 0; py < g.probes_y; ++py)
            for (std::uint32_t px = 0; px < g.probes_x; ++px)
            {
                const std::uint32_t idx = g.flat_index(px, py, pz);
                ASSERT_LT(idx, seen.size());
                EXPECT_FALSE(seen.at(idx)) << "duplicate flat index " << idx;
                seen.at(idx) = true;
            }
    for (bool s : seen)
        EXPECT_TRUE(s) << "a flat index in [0,count) was never produced";
}

// ===========================================================================
// probe_index_from_world -- partial out-of-bounds + below-origin.
// ===========================================================================
TEST(DdgiProbeMath, ProbeIndexFromWorldPartialOutOfBounds)
{
    // 8x4x8 grid, unit spacing. A point just inside the +x face at x=7.5 sits
    // between integer probes x0=7 (in-bounds) and x1=8 (out-of-bounds). The
    // four corners with dx==1 fall outside -> weight 0, index 0; the four
    // dx==0 corners stay in-bounds with non-trivial weight.
    const ProbeGrid g {};
    const Vec3f p { 7.5F, 1.5F, 3.5F };
    std::array<float, 8> w {};
    const auto indices = g.probe_index_from_world(p, w);

    // Corner ordering: dz outer, dy, dx inner -> dx==1 corners are odd idx.
    float in_sum = 0.0F;
    for (int i = 0; i < 8; ++i)
    {
        const bool dx_hi = (i % 2) == 1;
        if (dx_hi)
        {
            EXPECT_NEAR(w[static_cast<std::size_t>(i)], 0.0F, kEps)
                << "out-of-bounds corner " << i << " must have weight 0";
            EXPECT_EQ(indices[static_cast<std::size_t>(i)], 0U)
                << "out-of-bounds corner " << i << " must have index 0";
        }
        else
        {
            in_sum += w[static_cast<std::size_t>(i)];
        }
    }
    // The four in-bounds (dx==0) corners carry weight (1-fx)=0.5 of the total
    // -> they sum to 0.5 (fy/fz still partition their plane).
    EXPECT_NEAR(in_sum, 0.5F, kEps);
}

TEST(DdgiProbeMath, ProbeIndexFromWorldBelowOriginIsAllZeroWeight)
{
    const ProbeGrid g {};
    // Point well below the origin probe -> every corner out of bounds.
    const Vec3f p { -5.0F, -5.0F, -5.0F };
    std::array<float, 8> w {};
    const auto indices = g.probe_index_from_world(p, w);
    float sum = 0.0F;
    for (float v : w)
        sum += v;
    EXPECT_NEAR(sum, 0.0F, kEps);
    for (auto idx : indices)
        EXPECT_EQ(idx, 0U);
}

TEST(DdgiProbeMath, ProbeIndexFromWorldExactProbeHasUnitWeight)
{
    const ProbeGrid g {};
    // Land exactly on interior probe (3,2,5).
    const Vec3f p { 3.0F, 2.0F, 5.0F };
    std::array<float, 8> w {};
    const auto indices = g.probe_index_from_world(p, w);
    // Corner 0 is (x0,y0,z0) = the probe itself -> weight 1.
    EXPECT_NEAR(w[0], 1.0F, kEps);
    EXPECT_EQ(indices[0], g.flat_index(3, 2, 5));
    float sum = 0.0F;
    for (float v : w)
        sum += v;
    EXPECT_NEAR(sum, 1.0F, kEps);
}

// ===========================================================================
// trilinear_probe_weights -- edge clamp for out-of-bounds corners.
// ===========================================================================
TEST(DdgiProbeMath, TrilinearWeightsClampCornersToGridEdge)
{
    const ProbeGrid g {};
    // Point past the +x edge: out-of-bounds corners get weight 0 but their
    // corner coord is clamped to a valid grid index (>= 0, <= max).
    const Vec3f p { 7.5F, 1.5F, 3.5F };
    std::array<float, 8> w {};
    std::array<std::array<std::uint32_t, 3>, 8> c {};
    trilinear_probe_weights(g, p, w, c);
    for (const auto& corner : c)
    {
        EXPECT_LT(corner[0], g.probes_x + 1U);  // clamp keeps coords sane
        EXPECT_LT(corner[1], g.probes_y + 1U);
        EXPECT_LT(corner[2], g.probes_z + 1U);
    }
    // dx==1 corners (odd index) carry zero weight here.
    for (int i = 1; i < 8; i += 2)
        EXPECT_NEAR(w[static_cast<std::size_t>(i)], 0.0F, kEps);
}

TEST(DdgiProbeMath, TrilinearWeightsBelowOriginClampToZeroCorner)
{
    const ProbeGrid g {};
    const Vec3f p { -3.0F, -3.0F, -3.0F };
    std::array<float, 8> w {};
    std::array<std::array<std::uint32_t, 3>, 8> c {};
    trilinear_probe_weights(g, p, w, c);
    float sum = 0.0F;
    for (float v : w)
        sum += v;
    EXPECT_NEAR(sum, 0.0F, kEps);
    // std::max(0, cx) clamps negative corners to 0.
    for (const auto& corner : c)
    {
        EXPECT_EQ(corner[0], 0U);
        EXPECT_EQ(corner[1], 0U);
        EXPECT_EQ(corner[2], 0U);
    }
}

// ===========================================================================
// ProbeAtlas::init_from_grid -- dimensions + probe_uv guards.
// ===========================================================================
TEST(DdgiProbeMath, AtlasInitFromGridDimensions)
{
    ProbeGrid g {};
    g.probes_x = 4;
    g.probes_y = 2;
    g.probes_z = 4;
    ProbeAtlas atlas {};
    atlas.probe_face_size = 8;
    atlas.init_from_grid(g);
    // width = probes_x * probes_z * face ; height = probes_y * face.
    EXPECT_EQ(atlas.irradiance_width, 4U * 4U * 8U);
    EXPECT_EQ(atlas.irradiance_height, 2U * 8U);
    EXPECT_EQ(atlas.visibility_width, atlas.irradiance_width);
    EXPECT_EQ(atlas.visibility_height, atlas.irradiance_height);
}

TEST(DdgiProbeMath, AtlasInitFromGridFaceSize16)
{
    const ProbeGrid g {};  // 8x4x8
    ProbeAtlas atlas {};
    atlas.probe_face_size = 16;
    atlas.init_from_grid(g);
    EXPECT_EQ(atlas.irradiance_width, 8U * 8U * 16U);
    EXPECT_EQ(atlas.irradiance_height, 4U * 16U);
}

TEST(DdgiProbeMath, AtlasProbeUvFallsBackOnZeroSizeAtlas)
{
    const ProbeGrid g {};
    ProbeAtlas atlas {};  // NOT init_from_grid -> width/height stay 0.
    const Vec3f d { 0.0F, 1.0F, 0.0F };
    const Vec2f uv = atlas.probe_uv(g, 0U, d);
    // Guard path returns the bare face UV (octahedral encode of +Y).
    const Vec2f face = octahedral_encode(d);
    EXPECT_NEAR(uv.x, face.x, kEps);
    EXPECT_NEAR(uv.y, face.y, kEps);
}

TEST(DdgiProbeMath, AtlasProbeUvNegativeZFoldStaysInRange)
{
    const ProbeGrid g {};
    ProbeAtlas atlas {};
    atlas.init_from_grid(g);
    // A -Z direction exercises the octahedral fold branch inside probe_uv.
    const float k = std::numbers::inv_sqrt3_v<float>;
    const Vec3f d { k, k, -k };
    const Vec2f uv = atlas.probe_uv(g, g.flat_index(3, 1, 5), d);
    EXPECT_GE(uv.x, 0.0F);
    EXPECT_LE(uv.x, 1.0F);
    EXPECT_GE(uv.y, 0.0F);
    EXPECT_LE(uv.y, 1.0F);
}

// ===========================================================================
// octahedral_encode/decode -- dense round-trip + UV range.
// ===========================================================================
TEST(DdgiProbeMath, OctahedralDenseRoundTripWithinHalfDegree)
{
    // Sample a Fibonacci sphere of directions and verify each survives the
    // encode->decode round trip within 0.5 degrees (both hemispheres).
    constexpr int kN = 256;
    const float ga = static_cast<float>(std::numbers::pi) * (3.0F - std::sqrt(5.0F));
    for (int i = 0; i < kN; ++i)
    {
        const float z   = 1.0F - 2.0F * (static_cast<float>(i) + 0.5F)
                                       / static_cast<float>(kN);
        const float r   = std::sqrt(std::max(0.0F, 1.0F - z * z));
        const float phi = ga * static_cast<float>(i);
        const Vec3f n { r * std::cos(phi), r * std::sin(phi), z };

        const Vec2f uv = octahedral_encode(n);
        EXPECT_GE(uv.x, 0.0F);
        EXPECT_LE(uv.x, 1.0F);
        EXPECT_GE(uv.y, 0.0F);
        EXPECT_LE(uv.y, 1.0F);

        const Vec3f rd = octahedral_decode(uv);
        EXPECT_LE(angle_deg(n, rd), 0.5F)
            << "i=" << i << " angle=" << angle_deg(n, rd);
    }
}

TEST(DdgiProbeMath, OctahedralDecodeReturnsUnitVector)
{
    const Vec2f uv { 0.37F, 0.81F };
    const Vec3f n = octahedral_decode(uv);
    const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    EXPECT_NEAR(len, 1.0F, kEps);
}

// ===========================================================================
// IrradianceField -- init() atlas sizing + ambient_fallback branches.
// ===========================================================================
TEST(DdgiProbeMath, IrradianceFieldInitSizesAtlasFromGrid)
{
    IrradianceField field {};
    field.grid.probes_x = 4;
    field.grid.probes_y = 2;
    field.grid.probes_z = 4;
    field.atlas.probe_face_size = 8;
    field.init();
    EXPECT_EQ(field.atlas.irradiance_width, 4U * 4U * 8U);
    EXPECT_EQ(field.atlas.irradiance_height, 2U * 8U);
}

TEST(DdgiProbeMath, AmbientFallbackHitReturnsZero)
{
    IrradianceField field {};
    field.init();
    const Vec3f r = field.ambient_fallback(/*any_probes_hit=*/true);
    EXPECT_NEAR(r.x, 0.0F, kEps);
    EXPECT_NEAR(r.y, 0.0F, kEps);
    EXPECT_NEAR(r.z, 0.0F, kEps);
}

TEST(DdgiProbeMath, AmbientFallbackNoHitReturnsSkyColor)
{
    IrradianceField field {};
    field.sky_color = { 0.11F, 0.22F, 0.33F };
    field.init();
    const Vec3f r = field.ambient_fallback(/*any_probes_hit=*/false);
    EXPECT_NEAR(r.x, field.sky_color.x, kEps);
    EXPECT_NEAR(r.y, field.sky_color.y, kEps);
    EXPECT_NEAR(r.z, field.sky_color.z, kEps);
}

TEST(DdgiProbeMath, AmbientFallbackEmptyGridReturnsDefaultAmbientEvenOnHit)
{
    IrradianceField field {};
    field.grid.probes_x = 0;
    field.grid.probes_y = 0;
    field.grid.probes_z = 0;
    field.default_ambient = { 0.07F, 0.08F, 0.09F };
    field.init();
    // probe_count == 0 dominates -> default_ambient irrespective of hit flag.
    for (bool hit : { false, true })
    {
        const Vec3f r = field.ambient_fallback(hit);
        EXPECT_NEAR(r.x, field.default_ambient.x, kEps);
        EXPECT_NEAR(r.y, field.default_ambient.y, kEps);
        EXPECT_NEAR(r.z, field.default_ambient.z, kEps);
    }
}

// ===========================================================================
// TraceSettings -- documented default parameter values (Majercik 2019 §4).
// ===========================================================================
TEST(DdgiProbeMath, TraceSettingsDefaults)
{
    const TraceSettings s {};
    EXPECT_EQ(s.rays_per_probe, 64U);
    EXPECT_NEAR(s.hysteresis, 0.97F, kEps);
    EXPECT_NEAR(s.max_distance, 20.0F, kEps);
    EXPECT_NEAR(s.backface_threshold, 0.25F, kEps);
}

}  // namespace
