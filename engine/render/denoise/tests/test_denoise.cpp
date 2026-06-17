#include <cd/denoise/Denoise.hpp>

#include <gtest/gtest.h>

#include <random>

namespace
{

using cd::denoise::AtrousSettings;
using cd::denoise::AuxBuffers;
using cd::denoise::denoise_atrous;
using cd::denoise::denoise_oidn;
using cd::denoise::edge_weight;
using cd::denoise::OidnFilterKind;

constexpr float kEps = 1e-3F;

TEST(Denoise, EdgeWeightUnityForZeroDeltas)
{
    AtrousSettings s {};
    const float w = edge_weight({ 0, 0, 0 }, { 0, 0, 0 }, 0.0F, s);
    EXPECT_NEAR(w, 1.0F, kEps);
}

TEST(Denoise, EdgeWeightDropsWithLargeColorDelta)
{
    AtrousSettings s {};
    const float w_small = edge_weight({ 0.1F, 0, 0 }, {}, 0, s);
    const float w_big   = edge_weight({ 2.0F, 0, 0 }, {}, 0, s);
    EXPECT_LT(w_big, w_small);
}

TEST(Denoise, AtrousPreservesConstantImage)
{
    const std::uint32_t W = 16;
    const std::uint32_t H = 16;
    std::vector<cd::math::Vec3f> color(static_cast<std::size_t>(W) * H, { 0.5F, 0.5F, 0.5F });
    std::vector<cd::math::Vec3f> albedo(static_cast<std::size_t>(W) * H);
    std::vector<cd::math::Vec3f> normal(static_cast<std::size_t>(W) * H, { 0, 0, 1 });
    std::vector<float> depth(static_cast<std::size_t>(W) * H, 0.5F);
    AuxBuffers aux { W, H, color, albedo, normal, depth };
    const auto out = denoise_atrous(aux, {});
    for (const auto& p : out)
    {
        EXPECT_NEAR(p.x, 0.5F, 0.02F);
        EXPECT_NEAR(p.y, 0.5F, 0.02F);
        EXPECT_NEAR(p.z, 0.5F, 0.02F);
    }
}

TEST(Denoise, AtrousReducesGaussianNoiseVariance)
{
    const std::uint32_t W = 32;
    const std::uint32_t H = 32;
    std::mt19937 rng(7);
    std::normal_distribution<float> n(0.5F, 0.2F);
    std::vector<cd::math::Vec3f> color(static_cast<std::size_t>(W) * H);
    for (auto& p : color) p = { n(rng), n(rng), n(rng) };
    std::vector<cd::math::Vec3f> albedo(static_cast<std::size_t>(W) * H);
    std::vector<cd::math::Vec3f> normal(static_cast<std::size_t>(W) * H, { 0, 0, 1 });
    std::vector<float> depth(static_cast<std::size_t>(W) * H, 0.5F);
    AuxBuffers aux { W, H, color, albedo, normal, depth };
    const auto out = denoise_atrous(aux, {});
    // Variance after denoise should be lower than before.
    auto var_of = [&](const auto& v) {
        float m = 0;
        for (const auto& p : v) m += p.x;
        m /= static_cast<float>(v.size());
        float s = 0;
        for (const auto& p : v) s += (p.x - m) * (p.x - m);
        return s / static_cast<float>(v.size());
    };
    EXPECT_LT(var_of(out), var_of(color));
}

TEST(Denoise, OidnStubReturnsInputUnchanged)
{
    // Without the OIDN dep wired, the stub is identity — so consumers
    // can compile + run against the final API today.
    std::vector<cd::math::Vec3f> color { { 0.1F, 0.2F, 0.3F } };
    std::vector<cd::math::Vec3f> aux_v(1);
    std::vector<float> dep_v(1);
    AuxBuffers aux { 1, 1, color, aux_v, aux_v, dep_v };
    const auto out = denoise_oidn(aux, OidnFilterKind::kRT);
    ASSERT_EQ(out.size(), 1U);
    EXPECT_NEAR(out[0].x, 0.1F, kEps);
}

// ---- A-trous edge branches (B4 topup) --------------------------------------

TEST(Denoise, EdgeWeightDropsAcrossNormalDiscontinuity)
{
    // A large normal delta must lower the tap weight (geometry-aware
    // edge stop) — the (1 - clamp(w_n)) normal branch that the prior
    // tests (zero/colour-only deltas) never exercised.
    AtrousSettings s {};
    const float w_flat = edge_weight({ 0, 0, 0 }, { 0, 0, 0 }, 0.0F, s);
    const float w_edge = edge_weight({ 0, 0, 0 }, { 2.0F, 0, 0 }, 0.0F, s);
    EXPECT_LT(w_edge, w_flat);
}

TEST(Denoise, AtrousPreservesSharpDepthEdge)
{
    // Two flat halves at different depths: a-trous must NOT bleed across
    // the depth discontinuity (the edge-stop keeps the step crisp). This
    // exercises the per-tap reject path (large d0 - d1 -> ~0 weight).
    const std::uint32_t W = 16;
    const std::uint32_t H = 16;
    const std::size_t n = static_cast<std::size_t>(W) * H;
    std::vector<cd::math::Vec3f> color(n);
    std::vector<cd::math::Vec3f> albedo(n);
    std::vector<cd::math::Vec3f> normal(n, { 0, 0, 1 });
    std::vector<float> depth(n);
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y) * W + x;
            const bool left = x < W / 2;
            color[i] = left ? cd::math::Vec3f { 0.2F, 0.2F, 0.2F }
                            : cd::math::Vec3f { 0.8F, 0.8F, 0.8F };
            depth[i] = left ? 0.1F : 5.0F;  // big depth gap at the seam
        }
    AuxBuffers aux { W, H, color, albedo, normal, depth };
    AtrousSettings s {};
    s.sigma_depth = 0.05F;  // tight depth tolerance -> hard edge
    const auto out = denoise_atrous(aux, s);
    // Column just left of the seam stays near 0.2; just right near 0.8.
    const std::size_t li = static_cast<std::size_t>(H / 2) * W + (W / 2 - 1);
    const std::size_t ri = static_cast<std::size_t>(H / 2) * W + (W / 2);
    EXPECT_NEAR(out[li].x, 0.2F, 0.06F);
    EXPECT_NEAR(out[ri].x, 0.8F, 0.06F);
}

TEST(Denoise, AtrousZeroIterationsReturnsInputCopy)
{
    // iterations = 0 -> the loop never runs, output equals the input
    // buffer verbatim (the degenerate-settings early-return-of-copy path).
    const std::uint32_t W = 4;
    const std::uint32_t H = 4;
    const std::size_t n = static_cast<std::size_t>(W) * H;
    std::vector<cd::math::Vec3f> color(n);
    for (std::size_t i = 0; i < n; ++i)
        color[i] = { static_cast<float>(i) * 0.01F, 0.0F, 0.0F };
    std::vector<cd::math::Vec3f> albedo(n);
    std::vector<cd::math::Vec3f> normal(n, { 0, 0, 1 });
    std::vector<float> depth(n, 0.5F);
    AuxBuffers aux { W, H, color, albedo, normal, depth };
    AtrousSettings s {};
    s.iterations = 0;
    const auto out = denoise_atrous(aux, s);
    ASSERT_EQ(out.size(), n);
    for (std::size_t i = 0; i < n; ++i)
        EXPECT_FLOAT_EQ(out[i].x, color[i].x);
}

TEST(Denoise, GlslKernelNonEmpty)
{
    EXPECT_FALSE(cd::denoise::kAtrousCS.empty());
    EXPECT_NE(cd::denoise::kAtrousCS.find("imageStore"),
              std::string_view::npos);
}

}  // namespace
