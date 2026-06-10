// =============================================================================
// CHROMODYNAMIC — cd::asset::image::compress_bc7 tests
// =============================================================================
#include <cd/asset/image/Bc7.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{

constexpr std::uint32_t kBytesPerBlock = 16;

[[nodiscard]] std::vector<std::uint8_t>
make_solid_rgba(std::uint32_t w, std::uint32_t h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4U);
    for (std::size_t i = 0; i < out.size(); i += 4)
    {
        out[i + 0] = r;
        out[i + 1] = g;
        out[i + 2] = b;
        out[i + 3] = a;
    }
    return out;
}

}  // namespace

TEST(Bc7, Compress4x4Block)
{
    auto rgba = make_solid_rgba(4, 4, 0x80, 0x40, 0x20, 0xFF);
    auto r = cd::asset::image::compress_bc7(rgba, 4, 4);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->block_w, 1U);
    EXPECT_EQ(r->block_h, 1U);
    EXPECT_EQ(r->data.size(), static_cast<std::size_t>(kBytesPerBlock));
}

TEST(Bc7, Compress16x16YieldsSixteenBlocks)
{
    auto rgba = make_solid_rgba(16, 16, 200, 150, 100, 255);
    auto r = cd::asset::image::compress_bc7(rgba, 16, 16);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->block_w, 4U);
    EXPECT_EQ(r->block_h, 4U);
    EXPECT_EQ(r->data.size(), 4U * 4U * kBytesPerBlock);
}

TEST(Bc7, NonMultipleOf4DimensionsRoundUp)
{
    // 5×3 → block grid 2×1 (round up to next multiple of 4).
    auto rgba = make_solid_rgba(5, 3, 64, 64, 64, 64);
    auto r = cd::asset::image::compress_bc7(rgba, 5, 3);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->block_w, 2U);
    EXPECT_EQ(r->block_h, 1U);
    EXPECT_EQ(r->data.size(), 2U * 1U * kBytesPerBlock);
}

TEST(Bc7, FastVsHighQualityProducesDifferentOutput)
{
    // Use a gradient so the encoder has interesting work to do.
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(16U) * 16U * 4U);
    for (std::uint32_t y = 0; y < 16; ++y)
    {
        for (std::uint32_t x = 0; x < 16; ++x)
        {
            const auto i = (y * 16 + x) * 4U;
            rgba[i + 0] = static_cast<std::uint8_t>(x * 16);
            rgba[i + 1] = static_cast<std::uint8_t>(y * 16);
            rgba[i + 2] = static_cast<std::uint8_t>((x + y) * 8);
            rgba[i + 3] = 0xFF;
        }
    }
    auto fast = cd::asset::image::compress_bc7(rgba, 16, 16, cd::asset::image::Bc7Quality::kFast);
    auto high = cd::asset::image::compress_bc7(rgba, 16, 16, cd::asset::image::Bc7Quality::kHigh);
    ASSERT_TRUE(fast.has_value());
    ASSERT_TRUE(high.has_value());
    ASSERT_EQ(fast->data.size(), high->data.size());
    // The two encodings might agree on a few solid-color blocks but the
    // gradient guarantees at least one differs.
    bool any_diff = false;
    for (std::size_t i = 0; i < fast->data.size(); ++i)
    {
        if (fast->data[i] != high->data[i])
        {
            any_diff = true;
            break;
        }
    }
    EXPECT_TRUE(any_diff);
}

TEST(Bc7, ZeroDimensionRejected)
{
    auto r = cd::asset::image::compress_bc7({}, 0, 4);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::image::bc7_errors::Code::kUnsupportedDimensions));
}

TEST(Bc7, ShortInputBufferRejected)
{
    std::vector<std::uint8_t> tiny(4);  // claims 4×4 but provides 4 bytes
    auto r = cd::asset::image::compress_bc7(tiny, 4, 4);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::image::bc7_errors::Code::kInvalidArgument));
}
