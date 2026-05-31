// =============================================================================
// CHROMODYNAMIC — tests/test_ddgi.cpp
// phase526 — CPU-math unit tests for cd::ddgi (Majercik 2019).
//
// Six required cases:
//   1. Octahedral encode/decode round-trip preserves direction within 0.5°.
//   2. ProbeGrid::probe_index_from_world maps a world point to the nearest
//      8 probes (flat indices within grid, dominant weight correct).
//   3. Blend weight math sums to 1.0 across 8 corners (trilinear partition).
//   4. Irradiance atlas U,V wraps / clamps correctly for boundary probes.
//   5. Sky probe (no hits) returns sun-environment colour (sky_color fallback).
//   6. Empty probe grid returns default ambient.
//
// Additional pre-existing cases kept to avoid regression.
//
// phase596 — CPU-stub blend-method tests (no Vulkan dispatch required):
//   7. BlendIrradianceIncrementsCounter — call once, counter == 1.
//   8. BlendVisibilityIncrementsCounter — call once, counter == 1.
//   9. BlendsRejectZeroProbeGrid       — probe_count==0 returns error.
// =============================================================================

#include <cd/ddgi/Ddgi.hpp>
#include <cd/ddgi/DispatchPass.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>

namespace
{

using cd::ddgi::IrradianceField;
using cd::ddgi::ProbeAtlas;
using cd::ddgi::ProbeGrid;
using cd::ddgi::TraceSettings;
using cd::ddgi::octahedral_decode;
using cd::ddgi::octahedral_encode;
using cd::ddgi::probe_world_pos;
using cd::ddgi::trilinear_probe_weights;
using cd::math::Vec2f;
using cd::math::Vec3f;

constexpr float kEps      = 1e-3F;
constexpr float kDegToRad = static_cast<float>(std::numbers::pi) / 180.0F;

// ---------------------------------------------------------------------------
// Helper: angle between two (unit) vectors in degrees.
// ---------------------------------------------------------------------------
[[nodiscard]] float angle_deg(Vec3f a, Vec3f b)
{
    const float dot = a.x*b.x + a.y*b.y + a.z*b.z;
    // clamp to [-1,1] to guard against float rounding outside that range
    const float c = std::max(-1.0F, std::min(1.0F, dot));
    return std::acos(c) / kDegToRad;
}

// ===========================================================================
// Test 1 — Octahedral encode/decode round-trip preserves direction within 0.5°
// ===========================================================================
TEST(Ddgi, OctahedralRoundTripPositiveHemisphere)
{
    // +Z axis
    const Vec3f n { 0.0F, 0.0F, 1.0F };
    const Vec2f uv = octahedral_encode(n);
    const Vec3f r  = octahedral_decode(uv);
    EXPECT_LE(angle_deg(n, r), 0.5F)
        << "angle=" << angle_deg(n, r) << " deg";
}

TEST(Ddgi, OctahedralRoundTripNegativeHemisphere)
{
    // -Z hemisphere (the folded region in octahedral mapping).
    // Direction (1,-1,-1)/sqrt(3) via std::numbers::inv_sqrt3_v.
    const Vec3f n = []() {
        const float k = std::numbers::inv_sqrt3_v<float>;
        return Vec3f{ k, -k, -k };
    }();
    const Vec2f uv = octahedral_encode(n);
    const Vec3f r  = octahedral_decode(uv);
    EXPECT_LE(angle_deg(n, r), 0.5F)
        << "angle=" << angle_deg(n, r) << " deg";
}

TEST(Ddgi, OctahedralRoundTripMultipleDirections)
{
    // Several canonical directions
    const std::array<Vec3f, 6> dirs {{
        { 1.0F, 0.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F},
        { 0.0F, 1.0F, 0.0F},
        { 0.0F,-1.0F, 0.0F},
        { 0.0F, 0.0F, 1.0F},
        { 0.0F, 0.0F,-1.0F}
    }};
    for (const auto& n : dirs)
    {
        const Vec2f uv = octahedral_encode(n);
        const Vec3f r  = octahedral_decode(uv);
        EXPECT_LE(angle_deg(n, r), 0.5F)
            << "n=(" << n.x << ',' << n.y << ',' << n.z << ") "
            << "angle=" << angle_deg(n, r) << " deg";
    }
}

// ===========================================================================
// Test 2 — probe_index_from_world maps a world point to nearest 8 probes
// ===========================================================================
TEST(Ddgi, ProbeIndexFromWorldCentreOfCell)
{
    // 8×4×8 grid, unit spacing, origin at (0,0,0).
    ProbeGrid g {};
    // Sample point exactly at the centre of cell (2,1,3) — midpoint between
    // probes (2,1,3) and (3,2,4).
    const Vec3f p { 2.5F, 1.5F, 3.5F };
    std::array<float, 8> w {};
    const auto indices = g.probe_index_from_world(p, w);

    // Weights must sum to 1 (point is fully inside the grid).
    float sum = 0.0F;
    for (float v : w) sum += v;
    EXPECT_NEAR(sum, 1.0F, kEps);

    // All 8 indices must be within [0, probe_count).
    for (const auto idx : indices)
        EXPECT_LT(idx, g.probe_count());

    // Each of the 8 weights should be equal (0.5^3 = 0.125) for a cell centre.
    for (const float v : w)
        EXPECT_NEAR(v, 0.125F, kEps);
}

TEST(Ddgi, ProbeIndexFromWorldCornerProbe)
{
    ProbeGrid g {};
    // Exactly at probe (0,0,0) — weight[0] must be 1, rest 0.
    const Vec3f p { 0.0F, 0.0F, 0.0F };
    std::array<float, 8> w {};
    const auto indices = g.probe_index_from_world(p, w);
    EXPECT_NEAR(w[0], 1.0F, kEps);
    EXPECT_EQ(indices[0], 0U);
    float sum = 0.0F;
    for (float v : w) sum += v;
    EXPECT_NEAR(sum, 1.0F, kEps);
}

// ===========================================================================
// Test 3 — Blend weight math sums to 1.0 across 8 corners (trilinear)
// ===========================================================================
TEST(Ddgi, BlendWeightsSumToOneInsideGrid)
{
    ProbeGrid g {};
    std::array<float, 8> w {};
    std::array<std::array<std::uint32_t, 3>, 8> c {};
    trilinear_probe_weights(g, { 2.5F, 1.5F, 3.5F }, w, c);
    float sum = 0.0F;
    for (float v : w) sum += v;
    EXPECT_NEAR(sum, 1.0F, kEps);
}

TEST(Ddgi, BlendWeightsSumToZeroOutsideGrid)
{
    ProbeGrid g {};
    std::array<float, 8> w {};
    std::array<std::array<std::uint32_t, 3>, 8> c {};
    // Point completely outside grid extent (8×4×8 with unit spacing).
    trilinear_probe_weights(g, { 20.0F, 20.0F, 20.0F }, w, c);
    float sum = 0.0F;
    for (float v : w) sum += v;
    EXPECT_NEAR(sum, 0.0F, kEps);
}

TEST(Ddgi, BlendWeightsSumToOneAtMaxGridEdge)
{
    ProbeGrid g {};
    // Probe grid runs x=[0..7], y=[0..3], z=[0..7] in integer coords.
    // Point just inside the far corner — should still sum to 1.
    const Vec3f p { 6.9F, 2.9F, 6.9F };
    std::array<float, 8> w {};
    std::array<std::array<std::uint32_t, 3>, 8> c {};
    trilinear_probe_weights(g, p, w, c);
    float sum = 0.0F;
    for (float v : w) sum += v;
    EXPECT_NEAR(sum, 1.0F, kEps);
}

// ===========================================================================
// Test 4 — Irradiance atlas U,V wrap/clamp correctly for boundary probes
// ===========================================================================
TEST(Ddgi, AtlasUVInUnitRangeForAllCornerProbes)
{
    ProbeGrid g {};
    ProbeAtlas atlas {};
    atlas.init_from_grid(g);

    // Check UV stays in [0,1] for corner probes and several sample directions.
    const std::array<std::uint32_t, 4> corner_flat {
        g.flat_index(0, 0, 0),
        g.flat_index(7, 0, 0),
        g.flat_index(0, 3, 7),
        g.flat_index(7, 3, 7)
    };
    const std::array<Vec3f, 4> dirs {{
        { 1.0F, 0.0F, 0.0F},
        { 0.0F, 1.0F, 0.0F},
        { 0.0F, 0.0F, 1.0F},
        { 0.0F, 0.0F,-1.0F}
    }};
    for (const auto fi : corner_flat)
    {
        for (const auto& d : dirs)
        {
            const Vec2f uv = atlas.probe_uv(g, fi, d);
            EXPECT_GE(uv.x, 0.0F) << "flat_idx=" << fi;
            EXPECT_LE(uv.x, 1.0F) << "flat_idx=" << fi;
            EXPECT_GE(uv.y, 0.0F) << "flat_idx=" << fi;
            EXPECT_LE(uv.y, 1.0F) << "flat_idx=" << fi;
        }
    }
}

TEST(Ddgi, AtlasUVDistinctForDifferentProbes)
{
    // Two distinct probes at the same direction must produce different UVs.
    ProbeGrid g {};
    ProbeAtlas atlas {};
    atlas.init_from_grid(g);

    const Vec3f dir { 0.0F, 1.0F, 0.0F };
    const Vec2f uv0 = atlas.probe_uv(g, g.flat_index(0, 0, 0), dir);
    const Vec2f uv1 = atlas.probe_uv(g, g.flat_index(4, 0, 4), dir);
    // UV.x should differ (probes are in different columns of the atlas).
    EXPECT_NE(uv0.x, uv1.x);
}

// ===========================================================================
// Test 5 — Sky probe (no hits) returns sun-environment colour
// ===========================================================================
TEST(Ddgi, SkyProbeReturnsSkyColorWhenNoHits)
{
    IrradianceField field {};
    field.grid = ProbeGrid{};      // default 8×4×8 grid, has probes
    field.sky_color = { 0.3F, 0.5F, 1.0F };
    field.init();

    // any_probes_hit = false  =>  sky_color returned
    const Vec3f result = field.ambient_fallback(/*any_probes_hit=*/false);
    EXPECT_NEAR(result.x, field.sky_color.x, kEps);
    EXPECT_NEAR(result.y, field.sky_color.y, kEps);
    EXPECT_NEAR(result.z, field.sky_color.z, kEps);
}

TEST(Ddgi, SkyProbeReturnsZeroWhenProbesHaveHits)
{
    IrradianceField field {};
    field.sky_color = { 0.3F, 0.5F, 1.0F };
    field.init();

    // When probes have hit geometry, fallback contribution should be zero
    // (real irradiance is fetched from atlas by the GPU sampler).
    const Vec3f result = field.ambient_fallback(/*any_probes_hit=*/true);
    EXPECT_NEAR(result.x, 0.0F, kEps);
    EXPECT_NEAR(result.y, 0.0F, kEps);
    EXPECT_NEAR(result.z, 0.0F, kEps);
}

// ===========================================================================
// Test 6 — Empty probe grid returns default ambient
// ===========================================================================
TEST(Ddgi, EmptyProbeGridReturnsDefaultAmbient)
{
    IrradianceField field {};
    field.grid.probes_x = 0;
    field.grid.probes_y = 0;
    field.grid.probes_z = 0;
    field.default_ambient = { 0.05F, 0.05F, 0.05F };
    field.init();

    // probe_count() == 0  =>  default_ambient regardless of hit flag.
    const Vec3f r0 = field.ambient_fallback(false);
    const Vec3f r1 = field.ambient_fallback(true);
    EXPECT_NEAR(r0.x, field.default_ambient.x, kEps);
    EXPECT_NEAR(r0.y, field.default_ambient.y, kEps);
    EXPECT_NEAR(r0.z, field.default_ambient.z, kEps);
    EXPECT_NEAR(r1.x, field.default_ambient.x, kEps);
}

// ===========================================================================
// Regression — previously existing tests (kept to avoid breakage)
// ===========================================================================
TEST(Ddgi, ProbeWorldPosOffsetsBySpacing)
{
    ProbeGrid g {};
    g.origin  = { 1.0F, 2.0F, 3.0F };
    g.spacing = { 2.0F, 2.0F, 2.0F };
    const auto p = probe_world_pos(g, 1, 0, 0);
    EXPECT_NEAR(p.x, 3.0F, kEps);
    EXPECT_NEAR(p.y, 2.0F, kEps);
}

TEST(Ddgi, TrilinearWeightsSumToOneInside)
{
    ProbeGrid g {};
    std::array<float, 8> w {};
    std::array<std::array<std::uint32_t, 3>, 8> c {};
    trilinear_probe_weights(g, { 2.5F, 1.5F, 3.5F }, w, c);
    float sum = 0.0F;
    for (float v : w) sum += v;
    EXPECT_NEAR(sum, 1.0F, kEps);
}

TEST(Ddgi, TrilinearWeightsZeroOutsideGrid)
{
    ProbeGrid g {};
    std::array<float, 8> w {};
    std::array<std::array<std::uint32_t, 3>, 8> c {};
    trilinear_probe_weights(g, { 20.0F, 20.0F, 20.0F }, w, c);
    float sum = 0.0F;
    for (float v : w) sum += v;
    EXPECT_NEAR(sum, 0.0F, kEps);
}

TEST(Ddgi, GlslShadersNonEmpty)
{
    EXPECT_FALSE(cd::ddgi::kDdgiTraceCS.empty());
    EXPECT_FALSE(cd::ddgi::kDdgiBlendIrradianceCS.empty());
    EXPECT_FALSE(cd::ddgi::kDdgiBlendVisibilityCS.empty());
    EXPECT_FALSE(cd::ddgi::kDdgiSampleFS.empty());
    EXPECT_NE(cd::ddgi::kDdgiTraceCS.find("ray_query"),
              std::string_view::npos);
    // kDdgiBlendVisibilityCS accumulates mean depth + depth² (the data that the
    // sample FS later feeds into chebyshev_weight); it does not call the function
    // itself — verify it writes to the visibility atlas instead.
    EXPECT_NE(cd::ddgi::kDdgiBlendVisibilityCS.find("visibility_atlas"),
              std::string_view::npos);
    EXPECT_NE(cd::ddgi::kDdgiSampleFS.find("chebyshev_weight"),
              std::string_view::npos);
}


// ===========================================================================
// phase596 — CPU-stub blend method tests (no Vulkan dispatch required)
//
// DispatchPass::execute_blend_irradiance(frame_index) and
// execute_blend_visibility(frame_index) are overloads that take no
// ICommandBuffer. They validate probe-grid + atlas state, increment an
// internal call counter, and return Result<void>. No GPU device needed.
// prime_for_cpu_test() seeds grid_ + atlas dims without init().
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 7 — BlendIrradianceIncrementsCounter
// Call execute_blend_irradiance once on a primed pass; counter must be 1.
// ---------------------------------------------------------------------------
TEST(Ddgi, BlendIrradianceIncrementsCounter)
{
    cd::ddgi::DispatchPass pass;
    pass.prime_for_cpu_test(cd::ddgi::ProbeGrid{}, 64U, 32U);

    EXPECT_EQ(pass.blend_irr_call_count(), 0U);
    const auto r = pass.execute_blend_irradiance(/*frame_index=*/0U);
    EXPECT_TRUE(r.has_value()) << "CPU-stub must succeed on a primed pass";
    EXPECT_EQ(pass.blend_irr_call_count(), 1U);
}

// ---------------------------------------------------------------------------
// Test 8 — BlendVisibilityIncrementsCounter
// Call execute_blend_visibility once on a primed pass; counter must be 1.
// ---------------------------------------------------------------------------
TEST(Ddgi, BlendVisibilityIncrementsCounter)
{
    cd::ddgi::DispatchPass pass;
    pass.prime_for_cpu_test(cd::ddgi::ProbeGrid{}, 64U, 32U);

    EXPECT_EQ(pass.blend_vis_call_count(), 0U);
    const auto r = pass.execute_blend_visibility(/*frame_index=*/0U);
    EXPECT_TRUE(r.has_value()) << "CPU-stub must succeed on a primed pass";
    EXPECT_EQ(pass.blend_vis_call_count(), 1U);
}

// ---------------------------------------------------------------------------
// Test 9 — BlendsRejectZeroProbeGrid
// prime_for_cpu_test with probes_x/y/z == 0 forces probe_count() == 0.
// Both CPU-stub overloads must return an error and leave counters at 0.
// ---------------------------------------------------------------------------
TEST(Ddgi, BlendsRejectZeroProbeGrid)
{
    cd::ddgi::ProbeGrid zero_grid {};
    zero_grid.probes_x = 0;
    zero_grid.probes_y = 0;
    zero_grid.probes_z = 0;

    cd::ddgi::DispatchPass pass;
    pass.prime_for_cpu_test(zero_grid, 64U, 32U);

    {
        const auto r = pass.execute_blend_irradiance(/*frame_index=*/0U);
        EXPECT_FALSE(r.has_value()) << "expected error when probe_count == 0";
        EXPECT_EQ(pass.blend_irr_call_count(), 0U)
            << "counter must not increment on error";
    }
    {
        const auto r = pass.execute_blend_visibility(/*frame_index=*/0U);
        EXPECT_FALSE(r.has_value()) << "expected error when probe_count == 0";
        EXPECT_EQ(pass.blend_vis_call_count(), 0U)
            << "counter must not increment on error";
    }
}

}  // namespace
