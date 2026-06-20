// =============================================================================
// cd::post_bloom unit tests — Day 8.
//
// Edge/negative host-side coverage added (≥80→100 pass): Karis 13-tap reduce
// invariants, soft-threshold extremes (zero/negative knee, 1px-min clamps),
// downsample/upsample boundary behaviour, the FS-variant push-constant
// static-asserts + GLSL strings, and the BloomMipChain RAII helper against
// NullDevice. ADD-ONLY — no existing test, kernel math, GLSL, or default
// parameter is modified.
// =============================================================================
#include <cd/post/bloom/Bloom.hpp>

#include <cd/rhi/NullDevice.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <span>
#include <string_view>

namespace
{

using cd::post::bloom::CpuImage;
using cd::post::bloom::downsample_box;
using cd::post::bloom::karis_13tap_reduce;
using cd::post::bloom::Settings;
using cd::post::bloom::soft_threshold;
using cd::post::bloom::upsample_tent;

constexpr float kEps = 1e-3F;

TEST(PostBloom, SoftThresholdBelowKneeEmitsZero)
{
    Settings s {};
    s.threshold = 1.0F;
    s.knee = 0.5F;
    // 0.4 is well under the knee (1.0 - 0.5 = 0.5).
    const cd::math::Vec3f c { 0.4F, 0.4F, 0.4F };
    const auto out = soft_threshold(c, s);
    EXPECT_NEAR(out.x, 0.0F, kEps);
    EXPECT_NEAR(out.y, 0.0F, kEps);
    EXPECT_NEAR(out.z, 0.0F, kEps);
}

TEST(PostBloom, SoftThresholdWellAboveLetsThroughExcess)
{
    Settings s {};
    s.threshold = 1.0F;
    s.knee = 0.5F;
    // 3.0 input, threshold 1.0: pixel is 3x as bright as threshold;
    // soft-knee math hands back something proportional to (br - thr).
    const cd::math::Vec3f c { 3.0F, 3.0F, 3.0F };
    const auto out = soft_threshold(c, s);
    EXPECT_GT(out.x, 1.5F);    // most of the pixel passes
    EXPECT_LT(out.x, 3.0F);
}

TEST(PostBloom, DownsampleBoxHalvesDimensions)
{
    CpuImage img { 8, 8, std::vector<cd::math::Vec3f>(64, { 1, 1, 1 }) };
    const auto d = downsample_box(img);
    EXPECT_EQ(d.w, 4U);
    EXPECT_EQ(d.h, 4U);
    // Uniform input -> uniform output, value preserved.
    EXPECT_NEAR(d.at(0, 0).x, 1.0F, kEps);
    EXPECT_NEAR(d.at(3, 3).x, 1.0F, kEps);
}

TEST(PostBloom, DownsampleBoxAveragesPixels)
{
    // 2x2 checkerboard at full res; box-halve should produce uniform 0.5.
    CpuImage img { 2, 2, {} };
    img.pixels = { {1, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 0, 0} };
    const auto d = downsample_box(img);
    EXPECT_EQ(d.w, 1U);
    EXPECT_EQ(d.h, 1U);
    EXPECT_NEAR(d.at(0, 0).x, 0.5F, kEps);
}

TEST(PostBloom, UpsampleTentDoublesDimensions)
{
    CpuImage img { 2, 2, {} };
    img.pixels = { {1,1,1}, {1,1,1}, {1,1,1}, {1,1,1} };
    const auto u = upsample_tent(img, 4, 4);
    EXPECT_EQ(u.w, 4U);
    EXPECT_EQ(u.h, 4U);
    EXPECT_NEAR(u.at(2, 2).x, 1.0F, kEps);  // uniform interior
}

TEST(PostBloom, GlslKernelsNonEmpty)
{
    EXPECT_FALSE(cd::post::bloom::kDownsampleCS.empty());
    EXPECT_FALSE(cd::post::bloom::kUpsampleCS.empty());
    EXPECT_FALSE(cd::post::bloom::kPrefilterCS.empty());
    // Each kernel must declare the local workgroup size (sanity).
    EXPECT_NE(cd::post::bloom::kDownsampleCS.find("local_size_x"),
              std::string_view::npos);
    EXPECT_NE(cd::post::bloom::kUpsampleCS.find("local_size_x"),
              std::string_view::npos);
}

// =============================================================================
// Karis 13-tap reduce invariants (host reference).
// =============================================================================

TEST(PostBloom, Karis13TapZeroInputIsZero)
{
    std::array<cd::math::Vec3f, 13> taps {};  // all zero
    const auto out = karis_13tap_reduce(std::span<const cd::math::Vec3f, 13>(taps));
    EXPECT_NEAR(out.x, 0.0F, kEps);
    EXPECT_NEAR(out.y, 0.0F, kEps);
    EXPECT_NEAR(out.z, 0.0F, kEps);
}

TEST(PostBloom, Karis13TapUniformInputScalesByEighth)
{
    // For a uniform field the CPU reference reduces to centre*w_inner +
    // 4*corner*w_corner = 0.0625 + 4*0.015625 = 0.125 of the input value.
    std::array<cd::math::Vec3f, 13> taps {};
    for (auto& t : taps) t = { 2.0F, 4.0F, 8.0F };
    const auto out = karis_13tap_reduce(std::span<const cd::math::Vec3f, 13>(taps));
    EXPECT_NEAR(out.x, 0.125F * 2.0F, kEps);
    EXPECT_NEAR(out.y, 0.125F * 4.0F, kEps);
    EXPECT_NEAR(out.z, 0.125F * 8.0F, kEps);
}

TEST(PostBloom, Karis13TapIsLinearInScale)
{
    // Reduce(2x) == 2 * Reduce(x): the weighted average is a linear operator.
    std::array<cd::math::Vec3f, 13> base {};
    std::array<cd::math::Vec3f, 13> scaled {};
    for (std::size_t i = 0; i < base.size(); ++i)
    {
        const float f = static_cast<float>(i) * 0.1F;
        base[i]   = { f, f * 0.5F, f * 0.25F };
        scaled[i] = { f * 2.0F, f, f * 0.5F };
    }
    const auto rb = karis_13tap_reduce(std::span<const cd::math::Vec3f, 13>(base));
    const auto rs = karis_13tap_reduce(std::span<const cd::math::Vec3f, 13>(scaled));
    EXPECT_NEAR(rs.x, rb.x * 2.0F, kEps);
    EXPECT_NEAR(rs.y, rb.y * 2.0F, kEps);
    EXPECT_NEAR(rs.z, rb.z * 2.0F, kEps);
}

// =============================================================================
// soft_threshold edge / negative cases.
// =============================================================================

TEST(PostBloom, SoftThresholdAtExactThresholdIsNonNegative)
{
    Settings s {};
    s.threshold = 2.0F;
    s.knee = 0.5F;
    // br == threshold: br - thr = 0, knee ramp gives a small positive scale.
    const auto out = soft_threshold({ 2.0F, 2.0F, 2.0F }, s);
    EXPECT_GE(out.x, 0.0F);
    EXPECT_LT(out.x, 2.0F);
}

TEST(PostBloom, SoftThresholdZeroInputIsZero)
{
    Settings s {};
    const auto out = soft_threshold({ 0.0F, 0.0F, 0.0F }, s);
    EXPECT_NEAR(out.x, 0.0F, kEps);
    EXPECT_NEAR(out.y, 0.0F, kEps);
    EXPECT_NEAR(out.z, 0.0F, kEps);
}

TEST(PostBloom, SoftThresholdZeroKneeDoesNotDivideByZero)
{
    // knee = 0 stresses the 4*knee+1e-4 guard — must stay finite, never NaN.
    Settings s {};
    s.threshold = 1.0F;
    s.knee = 0.0F;
    const auto out = soft_threshold({ 5.0F, 5.0F, 5.0F }, s);
    EXPECT_TRUE(std::isfinite(out.x));
    EXPECT_GT(out.x, 0.0F);
    EXPECT_LE(out.x, 5.0F);
}

TEST(PostBloom, SoftThresholdPreservesChannelRatioForGreyInput)
{
    // factor is computed from max(channel), then applied uniformly, so a
    // grey input keeps R==G==B on output.
    Settings s {};
    s.threshold = 1.0F;
    s.knee = 0.5F;
    const auto out = soft_threshold({ 4.0F, 4.0F, 4.0F }, s);
    EXPECT_NEAR(out.x, out.y, kEps);
    EXPECT_NEAR(out.y, out.z, kEps);
}

// =============================================================================
// downsample / upsample boundary behaviour.
// =============================================================================

TEST(PostBloom, DownsampleClampsToOnePixelMinimum)
{
    // 1x1 source: w/2 and h/2 floor to 0 but the helper clamps to 1.
    CpuImage img { 1, 1, std::vector<cd::math::Vec3f>(1, { 0.7F, 0.7F, 0.7F }) };
    const auto d = downsample_box(img);
    EXPECT_EQ(d.w, 1U);
    EXPECT_EQ(d.h, 1U);
    EXPECT_NEAR(d.at(0, 0).x, 0.7F, kEps);
}

TEST(PostBloom, DownsampleOddDimensionFloorsExtent)
{
    // 3x3 -> 1x1 (3/2 == 1). No out-of-bounds read on the odd edge.
    CpuImage img { 3, 3, std::vector<cd::math::Vec3f>(9, { 1.0F, 0.0F, 0.0F }) };
    const auto d = downsample_box(img);
    EXPECT_EQ(d.w, 1U);
    EXPECT_EQ(d.h, 1U);
    EXPECT_NEAR(d.at(0, 0).x, 1.0F, kEps);
}

TEST(PostBloom, UpsampleToSameSizeIsIdentityOnUniform)
{
    CpuImage img { 4, 4, std::vector<cd::math::Vec3f>(16, { 0.3F, 0.6F, 0.9F }) };
    const auto u = upsample_tent(img, 4, 4);
    EXPECT_EQ(u.w, 4U);
    EXPECT_EQ(u.h, 4U);
    EXPECT_NEAR(u.at(1, 1).x, 0.3F, kEps);
    EXPECT_NEAR(u.at(2, 2).y, 0.6F, kEps);
    EXPECT_NEAR(u.at(3, 3).z, 0.9F, kEps);
}

TEST(PostBloom, UpsampleClampsSamplesAtBorder)
{
    // 1x1 -> 4x4: every bilinear tap clamps to the single source texel, so
    // the whole output equals it (no border-read crash, no NaN).
    CpuImage img { 1, 1, std::vector<cd::math::Vec3f>(1, { 0.5F, 0.25F, 0.125F }) };
    const auto u = upsample_tent(img, 4, 4);
    EXPECT_EQ(u.w, 4U);
    EXPECT_EQ(u.h, 4U);
    EXPECT_NEAR(u.at(0, 0).x, 0.5F, kEps);
    EXPECT_NEAR(u.at(3, 3).x, 0.5F, kEps);
    EXPECT_NEAR(u.at(3, 3).z, 0.125F, kEps);
}

// =============================================================================
// FS-variant push-constant layout + GLSL string contract.
// =============================================================================

TEST(PostBloom, PrefilterPushIs16Bytes)
{
    EXPECT_EQ(sizeof(cd::post::bloom::PrefilterPush), 16U);
}

TEST(PostBloom, UpsamplePushIs16Bytes)
{
    EXPECT_EQ(sizeof(cd::post::bloom::UpsamplePush), 16U);
}

TEST(PostBloom, FsVariantGlslStringsNonEmpty)
{
    EXPECT_FALSE(cd::post::bloom::kPrefilterFS.empty());
    EXPECT_FALSE(cd::post::bloom::kDownsampleFS.empty());
    EXPECT_FALSE(cd::post::bloom::kUpsampleFS.empty());
}

TEST(PostBloom, PrefilterFsCarriesEvAwareScale)
{
    // phase511 EV-aware prefilter: with params.z (ev) == 0 the exp2(-0) == 1
    // identity preserves pre-phase511 behaviour. Lock the scale term so a
    // refactor can't silently drop the ev gate (which feeds the default frame).
    EXPECT_NE(cd::post::bloom::kPrefilterFS.find("exp2(-pc.params.z)"),
              std::string_view::npos);
}

TEST(PostBloom, DefaultMipCountIsFour)
{
    EXPECT_EQ(cd::post::bloom::kDefaultMipCount, 4U);
    EXPECT_EQ(cd::post::bloom::BloomMipChain::kCount, 4U);
}

// =============================================================================
// BloomMipChain RAII helper (NullDevice — no live GPU).
// =============================================================================

TEST(PostBloomChain, CreateAllocatesHalvingMipChain)
{
    cd::rhi::NullDevice dev;
    cd::post::bloom::BloomMipChain chain {};
    const bool ok = cd::post::bloom::create_bloom_chain(
        dev, cd::rhi::Extent2D { 256U, 128U }, chain);
    ASSERT_TRUE(ok);
    // mip0 = base/2 = 128x64, then halving each level.
    EXPECT_EQ(chain.mips[0].extent.width, 128U);
    EXPECT_EQ(chain.mips[0].extent.height, 64U);
    EXPECT_EQ(chain.mips[1].extent.width, 64U);
    EXPECT_EQ(chain.mips[3].extent.width, 16U);
    for (const auto& m : chain.mips)
    {
        EXPECT_TRUE(m.image.is_valid());
        EXPECT_TRUE(m.view.is_valid());
        EXPECT_EQ(m.format, cd::rhi::Format::kRGBA16Float);
    }
    chain.destroy(dev);
    for (const auto& m : chain.mips)
    {
        EXPECT_FALSE(m.image.is_valid());
        EXPECT_FALSE(m.view.is_valid());
    }
}

TEST(PostBloomChain, CreateClampsTinyBaseToOnePixel)
{
    // 1x1 base: every mip floors to 1x1 (min clamp) instead of zero, so all
    // four texture creations succeed (NullDevice rejects zero-extent).
    cd::rhi::NullDevice dev;
    cd::post::bloom::BloomMipChain chain {};
    const bool ok = cd::post::bloom::create_bloom_chain(
        dev, cd::rhi::Extent2D { 1U, 1U }, chain);
    ASSERT_TRUE(ok);
    for (const auto& m : chain.mips)
    {
        EXPECT_EQ(m.extent.width, 1U);
        EXPECT_EQ(m.extent.height, 1U);
        EXPECT_TRUE(m.image.is_valid());
    }
    chain.destroy(dev);
}

TEST(PostBloomChain, DestroyOnDefaultConstructedIsNoOp)
{
    cd::rhi::NullDevice dev;
    cd::post::bloom::BloomMipChain chain {};
    chain.destroy(dev);  // no allocated mips — must be a clean no-op.
    for (const auto& m : chain.mips)
    {
        EXPECT_FALSE(m.image.is_valid());
    }
    SUCCEED();
}

TEST(PostBloomChain, CreateOverwritesPreviousChainWithoutLeak)
{
    // Calling create twice on the same chain must destroy the prior mips
    // first (the helper calls out.destroy(dev) up front).
    cd::rhi::NullDevice dev;
    cd::post::bloom::BloomMipChain chain {};
    ASSERT_TRUE(cd::post::bloom::create_bloom_chain(
        dev, cd::rhi::Extent2D { 64U, 64U }, chain));
    const auto first_image = chain.mips[0].image;
    ASSERT_TRUE(cd::post::bloom::create_bloom_chain(
        dev, cd::rhi::Extent2D { 128U, 128U }, chain));
    // New allocation -> fresh handles (different from the freed first set).
    EXPECT_TRUE(chain.mips[0].image.is_valid());
    EXPECT_NE(chain.mips[0].image.index(), first_image.index());
    EXPECT_EQ(chain.mips[0].extent.width, 64U);  // 128/2
    chain.destroy(dev);
}

}  // namespace
