#include <cd/brdf/sheen_clearcoat/SheenClearcoat.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <string_view>

namespace
{

using cd::brdf::sheen_clearcoat::charlie_d;
using cd::brdf::sheen_clearcoat::clearcoat_d_v;
using cd::brdf::sheen_clearcoat::v_neubelt;

TEST(SheenClearcoat, CharlieDPositiveAtGrazing)
{
    // Charlie peaks at sin^2 ~ 1; n_dot_h ~ 0 means full grazing.
    EXPECT_GT(charlie_d(0.3F, 0.0F), 0.0F);
    EXPECT_GT(charlie_d(0.5F, 0.3F), 0.0F);
}

TEST(SheenClearcoat, CharlieDPeaksAtGrazing)
{
    // Sheen distribution peaks at sin^2 = 1 (grazing-angle scatter).
    EXPECT_GT(charlie_d(0.3F, 0.0F), charlie_d(0.3F, 1.0F));
}

TEST(SheenClearcoat, NeubeltVisibilityFiniteAndPositive)
{
    EXPECT_GT(v_neubelt(0.5F, 0.5F), 0.0F);
    EXPECT_GT(v_neubelt(0.1F, 0.1F), 0.0F);
}

TEST(SheenClearcoat, ClearcoatDvFiniteAtNormalIncidence)
{
    const float dv = clearcoat_d_v(0.2F, 1.0F, 1.0F, 1.0F);
    EXPECT_GT(dv, 0.0F);
    EXPECT_TRUE(std::isfinite(dv));
}

TEST(SheenClearcoat, GlslNonEmpty)
{
    EXPECT_FALSE(cd::brdf::sheen_clearcoat::kSheenClearcoatGlsl.empty());
}

// === ADD-ONLY edge/negative coverage ========================================
// Charlie sheen: Estevez & Kulla 2017 "Production Friendly Microfacet Sheen
// BRDF". Clearcoat D*V: Filament GGX model. Reference values pinned against the
// exact code in SheenClearcoat.hpp; no lobe math is altered.

constexpr float kPi = std::numbers::pi_v<float>;

TEST(SheenClearcoat, CharlieDReferenceValueAtGrazing)
{
    // n_dot_h = 0 -> sin2 = 1, sin2^p = 1 so D = (2 + 1/a) / 2pi with a = 0.3.
    // (2 + 3.3333) / (2*pi) = 0.84883.
    EXPECT_NEAR(charlie_d(0.3F, 0.0F), 0.84883F, 1e-4F);
}

TEST(SheenClearcoat, CharlieDRoughnessFlooredAtMin)
{
    // alpha = max(roughness, 0.05): roughness below 0.05 is pinned to the floor
    // so charlie_d(0.0,*) == charlie_d(0.05,*). At grazing this is (2+20)/2pi.
    EXPECT_FLOAT_EQ(charlie_d(0.0F, 0.0F), charlie_d(0.05F, 0.0F));
    EXPECT_NEAR(charlie_d(0.0F, 0.0F), 22.0F / (2.0F * kPi), 1e-3F);
}

TEST(SheenClearcoat, CharlieDZeroAtNormalIncidence)
{
    // n_dot_h = 1 -> sin2 = 0; pow(0, positive) = 0 so the sheen lobe vanishes
    // exactly along the half-vector (Estevez & Kulla grazing-only scatter).
    EXPECT_FLOAT_EQ(charlie_d(0.3F, 1.0F), 0.0F);
    EXPECT_FLOAT_EQ(charlie_d(0.8F, 1.0F), 0.0F);
}

TEST(SheenClearcoat, CharlieDFiniteAcrossSweep)
{
    // NaN-guard: finite, non-negative over the full (roughness, n_dot_h) grid.
    for (int ri = 0; ri <= 10; ++ri)
    {
        for (int hi = 0; hi <= 10; ++hi)
        {
            const float r  = static_cast<float>(ri) / 10.0F;
            const float nh = static_cast<float>(hi) / 10.0F;
            const float d  = charlie_d(r, nh);
            EXPECT_TRUE(std::isfinite(d));
            EXPECT_GE(d, 0.0F);
        }
    }
}

TEST(SheenClearcoat, NeubeltVisibilityIsReciprocalInViewLight)
{
    // V(nv, nl) == V(nl, nv): the Neubelt fit is symmetric in the swap, which
    // is the geometric reciprocity guarantee of the sheen visibility term.
    EXPECT_FLOAT_EQ(v_neubelt(0.3F, 0.7F), v_neubelt(0.7F, 0.3F));
    EXPECT_FLOAT_EQ(v_neubelt(0.1F, 0.9F), v_neubelt(0.9F, 0.1F));
}

TEST(SheenClearcoat, NeubeltVisibilityReferenceValueAtNormalIncidence)
{
    // nv = nl = 1: denom = 4*(1+1-1)+1e-4 = 4.0001 -> V ~ 0.25.
    EXPECT_NEAR(v_neubelt(1.0F, 1.0F), 0.25F, 1e-3F);
}

TEST(SheenClearcoat, NeubeltVisibilityFiniteAtDegenerateZero)
{
    // nv = nl = 0: the +1e-4 epsilon keeps the reciprocal finite (no div-by-0).
    const float v = v_neubelt(0.0F, 0.0F);
    EXPECT_TRUE(std::isfinite(v));
    EXPECT_GT(v, 0.0F);
}

TEST(SheenClearcoat, ClearcoatDvReferenceValueAtNormalIncidence)
{
    // r = 0.2 -> a = max(0.04, 0.045^2) = 0.04 (alpha^2 below floor is pinned).
    // At nh=nv=nl=1: D = a2/(pi*(a2)^2) with den = a2; V = 1/4.0001.
    // Hand value: 49.7347.
    EXPECT_NEAR(clearcoat_d_v(0.2F, 1.0F, 1.0F, 1.0F), 49.7347F, 1e-2F);
}

TEST(SheenClearcoat, ClearcoatRoughnessFlooredBelowMin)
{
    // alpha^2 floored at 0.045^2: roughness 0 (mirror) collapses to the same
    // value as the floor instead of an infinite spike.
    const float at_zero  = clearcoat_d_v(0.0F, 1.0F, 1.0F, 1.0F);
    const float at_floor = clearcoat_d_v(0.045F, 1.0F, 1.0F, 1.0F);
    EXPECT_TRUE(std::isfinite(at_zero));
    EXPECT_FLOAT_EQ(at_zero, at_floor);
}

TEST(SheenClearcoat, ClearcoatDvReciprocalInViewLight)
{
    // V = 1/(4*nv*nl + eps) is symmetric, and D depends only on nh, so the full
    // D*V product is reciprocal under the nv<->nl swap.
    EXPECT_FLOAT_EQ(clearcoat_d_v(0.3F, 0.6F, 0.4F, 0.8F),
                    clearcoat_d_v(0.3F, 0.6F, 0.8F, 0.4F));
}

TEST(SheenClearcoat, ClearcoatDvFiniteAtGrazingDegenerate)
{
    // nv = nl = 0 grazing: +1e-4 in V keeps the product finite (NaN-guard).
    const float dv = clearcoat_d_v(0.5F, 0.0F, 0.0F, 0.0F);
    EXPECT_TRUE(std::isfinite(dv));
    EXPECT_GT(dv, 0.0F);
}

TEST(SheenClearcoat, InlineRimApproxGlslPresent)
{
    // The stand-in forward-shading rim helpers ship in their own GLSL block.
    using cd::brdf::sheen_clearcoat::kInlineRimApproxGlsl;
    EXPECT_FALSE(kInlineRimApproxGlsl.empty());
    EXPECT_NE(kInlineRimApproxGlsl.find("clearcoat_inline_lobe"),
              std::string_view::npos);
    EXPECT_NE(kInlineRimApproxGlsl.find("sheen_inline_lobe"),
              std::string_view::npos);
}

TEST(SheenClearcoat, GlslMirrorsCpuConstants)
{
    // GLSL helper keeps the 0.05 sheen floor, 0.045 clearcoat floor and the
    // 2*pi divisor so CPU/GPU sheen+clearcoat stay byte-consistent.
    using cd::brdf::sheen_clearcoat::kSheenClearcoatGlsl;
    EXPECT_NE(kSheenClearcoatGlsl.find("6.28318530"), std::string_view::npos);
    EXPECT_NE(kSheenClearcoatGlsl.find("0.045 * 0.045"), std::string_view::npos);
    EXPECT_NE(kSheenClearcoatGlsl.find("max(r, 0.05)"), std::string_view::npos);
}

}  // namespace
