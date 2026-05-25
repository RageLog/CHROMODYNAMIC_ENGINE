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
    const std::uint32_t W = 16, H = 16;
    std::vector<cd::math::Vec3f> color(W * H, { 0.5F, 0.5F, 0.5F });
    std::vector<cd::math::Vec3f> albedo(W * H);
    std::vector<cd::math::Vec3f> normal(W * H, { 0, 0, 1 });
    std::vector<float> depth(W * H, 0.5F);
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
    const std::uint32_t W = 32, H = 32;
    std::mt19937 rng(7);
    std::normal_distribution<float> n(0.5F, 0.2F);
    std::vector<cd::math::Vec3f> color(W * H);
    for (auto& p : color) p = { n(rng), n(rng), n(rng) };
    std::vector<cd::math::Vec3f> albedo(W * H);
    std::vector<cd::math::Vec3f> normal(W * H, { 0, 0, 1 });
    std::vector<float> depth(W * H, 0.5F);
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

TEST(Denoise, GlslKernelNonEmpty)
{
    EXPECT_FALSE(cd::denoise::kAtrousCS.empty());
    EXPECT_NE(cd::denoise::kAtrousCS.find("imageStore"),
              std::string_view::npos);
}

}  // namespace
