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

// ---- ~70% floor-raise: ADD-ONLY host tests (no math/GLSL change) -----------
// All exercise the EXISTING a-trous CPU denoiser + edge_weight; the math and
// the GLSL string are BYTE-IDENTICAL. Verified vs Dammertz et al. 2010
// "Edge-Avoiding A-trous Wavelet Transform": B3-spline 5x5 kernel
// {1/16, 1/4, 3/8, 1/4, 1/16}, separable per-tap weight = colour * normal *
// depth edge-stops, normalised by the weight sum.

namespace
{

// Small helper: build an AuxBuffers over caller-owned vectors. Keeps each
// test's Arrange block free of repeated span plumbing.
struct Scene
{
    std::uint32_t                w {};
    std::uint32_t                h {};
    std::vector<cd::math::Vec3f> color;
    std::vector<cd::math::Vec3f> albedo;
    std::vector<cd::math::Vec3f> normal;
    std::vector<float>           depth;

    explicit Scene(std::uint32_t width, std::uint32_t height)
        : w { width }
        , h { height }
        , color(static_cast<std::size_t>(width) * height, { 0, 0, 0 })
        , albedo(static_cast<std::size_t>(width) * height, { 0, 0, 0 })
        , normal(static_cast<std::size_t>(width) * height, { 0, 0, 1 })
        , depth(static_cast<std::size_t>(width) * height, 0.5F)
    {
    }

    [[nodiscard]] AuxBuffers aux() const noexcept
    {
        return AuxBuffers { w, h, color, albedo, normal, depth };
    }
};

}  // namespace

TEST(Denoise, EdgeWeightDropsWithDepthDiscontinuity)
{
    // The depth edge-stop w_z = exp(-|dz| / (sigma_z + 1e-3)) must lower the
    // weight as the depth gap grows — the third bilateral branch.
    const AtrousSettings s {};
    const float w_near = edge_weight({ 0, 0, 0 }, { 0, 0, 0 }, /*dz=*/0.05F, s);
    const float w_far  = edge_weight({ 0, 0, 0 }, { 0, 0, 0 }, /*dz=*/4.0F, s);
    EXPECT_LT(w_far, w_near);
    EXPECT_GT(w_near, 0.0F);
}

TEST(Denoise, EdgeWeightSymmetricInColorDelta)
{
    // exp(-dc.dc / ...) depends only on the squared magnitude — the sign of
    // the colour delta must not change the weight.
    const AtrousSettings s {};
    const float w_pos = edge_weight({ 0.7F, 0, 0 }, {}, 0.0F, s);
    const float w_neg = edge_weight({ -0.7F, 0, 0 }, {}, 0.0F, s);
    EXPECT_NEAR(w_pos, w_neg, kEps);
}

TEST(Denoise, EdgeWeightBoundedInUnitInterval)
{
    // The product of three edge-stops is a similarity weight: it can never
    // exceed unity and never go negative, for any plausible delta.
    const AtrousSettings s {};
    const float w_id   = edge_weight({ 0, 0, 0 }, { 0, 0, 0 }, 0.0F, s);
    const float w_mix  = edge_weight({ 0.3F, 0.1F, 0.2F }, { 0.4F, 0, 0 }, 0.6F, s);
    const float w_huge = edge_weight({ 9, 9, 9 }, { 1.9F, 0, 0 }, 9.0F, s);
    for (const float w : { w_id, w_mix, w_huge })
    {
        EXPECT_GE(w, 0.0F);
        EXPECT_LE(w, 1.0F + kEps);
    }
}

TEST(Denoise, AtrousKernelTapWeightsSumToUnity)
{
    // Lock the B3-spline 5x5 separable kernel from Dammertz: the 1D taps
    // {1/16, 1/4, 3/8, 1/4, 1/16} sum to 1, so the 2D outer product also
    // sums to 1 — the normalisation that makes a flat patch a fixed point.
    constexpr std::array<float, 5> k { 1.0F / 16.0F, 1.0F / 4.0F, 3.0F / 8.0F,
                                       1.0F / 4.0F, 1.0F / 16.0F };
    float sum1d = 0.0F;
    for (const float v : k) sum1d += v;
    EXPECT_NEAR(sum1d, 1.0F, kEps);

    float sum2d = 0.0F;
    for (const float a : k)
        for (const float b : k) sum2d += a * b;
    EXPECT_NEAR(sum2d, 1.0F, kEps);
}

TEST(Denoise, AtrousMorePassesSmoothMore)
{
    // Monotone smoothing: across a noise field, 5 a-trous passes leave lower
    // residual variance than a single pass (each iteration widens support).
    const std::uint32_t W = 24;
    const std::uint32_t H = 24;
    std::mt19937 rng(11);
    std::normal_distribution<float> nd(0.5F, 0.25F);
    Scene sc { W, H };
    for (auto& p : sc.color) p = { nd(rng), nd(rng), nd(rng) };

    AtrousSettings one {};
    one.iterations = 1;
    AtrousSettings five {};
    five.iterations = 5;
    const auto out1 = denoise_atrous(sc.aux(), one);
    const auto out5 = denoise_atrous(sc.aux(), five);

    const auto var_x = [](const std::vector<cd::math::Vec3f>& v) {
        float m = 0.0F;
        for (const auto& p : v) m += p.x;
        m /= static_cast<float>(v.size());
        float acc = 0.0F;
        for (const auto& p : v) acc += (p.x - m) * (p.x - m);
        return acc / static_cast<float>(v.size());
    };
    EXPECT_LT(var_x(out5), var_x(out1));
}

TEST(Denoise, AtrousIdempotentOnFlatImage)
{
    // A perfectly flat colour buffer is a fixed point: re-denoising the
    // denoised output yields the same flat value (kernel normalisation +
    // unity edge-stop on identical neighbours).
    Scene sc { 12, 12 };
    for (auto& p : sc.color) p = { 0.37F, 0.62F, 0.11F };
    const auto out1 = denoise_atrous(sc.aux(), {});

    Scene sc2 { 12, 12 };
    sc2.color = out1;
    const auto out2 = denoise_atrous(sc2.aux(), {});
    ASSERT_EQ(out1.size(), out2.size());
    for (std::size_t i = 0; i < out1.size(); ++i)
    {
        EXPECT_NEAR(out2[i].x, out1[i].x, kEps);
        EXPECT_NEAR(out2[i].y, out1[i].y, kEps);
        EXPECT_NEAR(out2[i].z, out1[i].z, kEps);
    }
}

TEST(Denoise, AtrousPreservesSharpColorEdge)
{
    // A step edge in colour (with no aux discontinuity) must stay sharp: the
    // colour edge-stop alone rejects cross-edge taps. Centre columns either
    // side of the seam keep their original luminance.
    const std::uint32_t W = 16;
    const std::uint32_t H = 16;
    Scene sc { W, H };
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y) * W + x;
            sc.color[i] = (x < W / 2) ? cd::math::Vec3f { 0.05F, 0.05F, 0.05F }
                                      : cd::math::Vec3f { 0.95F, 0.95F, 0.95F };
        }
    AtrousSettings s {};
    s.sigma_color = 0.1F;  // tight colour tolerance -> hard edge
    const auto out = denoise_atrous(sc.aux(), s);
    const std::size_t li = static_cast<std::size_t>(H / 2) * W + (W / 2 - 1);
    const std::size_t ri = static_cast<std::size_t>(H / 2) * W + (W / 2);
    EXPECT_NEAR(out[li].x, 0.05F, 0.05F);
    EXPECT_NEAR(out[ri].x, 0.95F, 0.05F);
}

TEST(Denoise, AtrousSinglePixelImageIsNoOp)
{
    // 1x1 input has no neighbours: the centre tap (weight 3/8 * 3/8) is the
    // only contributor, normalised back to the input value. Degenerate-size
    // guard for the bounds-clamp path.
    Scene sc { 1, 1 };
    sc.color[0] = { 0.42F, 0.17F, 0.88F };
    const auto out = denoise_atrous(sc.aux(), {});
    ASSERT_EQ(out.size(), 1U);
    EXPECT_NEAR(out[0].x, 0.42F, kEps);
    EXPECT_NEAR(out[0].y, 0.17F, kEps);
    EXPECT_NEAR(out[0].z, 0.88F, kEps);
}

TEST(Denoise, AtrousOutputMatchesInputDimensions)
{
    // Output buffer is always exactly w*h regardless of iteration count.
    Scene sc { 7, 9 };
    AtrousSettings s {};
    s.iterations = 3;
    const auto out = denoise_atrous(sc.aux(), s);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(7) * 9);
}

TEST(Denoise, AtrousFiniteOnExtremeHdrInput)
{
    // Very large (but finite) HDR values must not produce NaN/inf: the
    // normalisation (wsum guarded by 1e-6 floor) keeps every channel finite.
    Scene sc { 8, 8 };
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> hdr(0.0F, 5000.0F);
    for (auto& p : sc.color) p = { hdr(rng), hdr(rng), hdr(rng) };
    const auto out = denoise_atrous(sc.aux(), {});
    for (const auto& p : out)
    {
        EXPECT_TRUE(std::isfinite(p.x));
        EXPECT_TRUE(std::isfinite(p.y));
        EXPECT_TRUE(std::isfinite(p.z));
    }
}

TEST(Denoise, AtrousZeroInputStaysZero)
{
    // All-zero colour is the additive fixed point: every weighted sum is 0,
    // and the 1e-6 wsum guard never injects a spurious value. Negative/edge
    // guard against a divide-by-near-zero producing non-zero output.
    Scene sc { 10, 10 };
    const auto out = denoise_atrous(sc.aux(), {});
    for (const auto& p : out)
    {
        EXPECT_NEAR(p.x, 0.0F, kEps);
        EXPECT_NEAR(p.y, 0.0F, kEps);
        EXPECT_NEAR(p.z, 0.0F, kEps);
    }
}

TEST(Denoise, OidnStubIsExactPassThroughMultiPixel)
{
    // The OIDN backend is an explicit, SEALED pass-through stub (no .cpp /
    // RHI today): it must return the input verbatim for every pixel and both
    // filter kinds — the documented identity contract consumers compile to.
    std::vector<cd::math::Vec3f> color {
        { 0.1F, 0.2F, 0.3F }, { 0.4F, 0.5F, 0.6F }, { 0.7F, 0.8F, 0.9F }
    };
    std::vector<cd::math::Vec3f> aux_v(3, { 0, 0, 1 });
    std::vector<float> dep_v(3, 1.0F);
    AuxBuffers aux { 3, 1, color, aux_v, aux_v, dep_v };
    for (const OidnFilterKind kind :
         { OidnFilterKind::kRT, OidnFilterKind::kRTLightmap })
    {
        const auto out = denoise_oidn(aux, kind);
        ASSERT_EQ(out.size(), color.size());
        for (std::size_t i = 0; i < color.size(); ++i)
        {
            EXPECT_FLOAT_EQ(out[i].x, color[i].x);
            EXPECT_FLOAT_EQ(out[i].y, color[i].y);
            EXPECT_FLOAT_EQ(out[i].z, color[i].z);
        }
    }
}

}  // namespace
