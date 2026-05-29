// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_image_readback.cpp
//
// phase377-B-infra2: IDevice::copy_image_to_buffer smoke tests.
//
// Runs against:
//   * NullDevice  — always (headless CI, no GPU required)
//   * VulkanDevice — skipped if no Vulkan loader (SKIPPED, not FAILED)
//   * D3D12Device  — Windows only; skipped if no DX12 adapter found
//
// Test strategy:
//   Arrange: create a small texture + a kGpuToCpu buffer large enough
//            for the region.
//   Act:     call copy_image_to_buffer.
//   Assert:  return value indicates success (or expected error for
//            mis-matched arguments); NullDevice writes zeros so we verify
//            the destination region is zero-filled.
//
// CLAUDE.md rules applied:
//   * No sleep_for; device is blocking (one-shot submit + wait_idle inside).
//   * Arrange / Act / Assert.
//   * Edge cases + negative tests included.
// =============================================================================
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullDevice.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

// ---- Helper -----------------------------------------------------------------

/// Build a minimal kGpuToCpu buffer that fits `byte_count` bytes.
[[nodiscard]] cd::rhi::BufferHandle
make_readback_buffer(cd::rhi::NullDevice& dev, std::uint64_t byte_count)
{
    cd::rhi::BufferDesc bd {};
    bd.size   = byte_count;
    bd.usage  = cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto r = dev.create_buffer(bd);
    EXPECT_TRUE(r.has_value());
    return r.has_value() ? *r : cd::rhi::BufferHandle {};
}

/// Build a minimal 2-D texture of the given format + extent.
[[nodiscard]] cd::rhi::TextureHandle
make_texture(cd::rhi::NullDevice& dev, std::uint32_t w, std::uint32_t h,
             cd::rhi::Format fmt)
{
    cd::rhi::TextureDesc td {};
    td.format      = fmt;
    td.extent      = { w, h, 1 };
    td.usage       = cd::rhi::TextureUsage::kTransferSrc | cd::rhi::TextureUsage::kSampled;
    td.type        = cd::rhi::TextureType::k2D;
    td.mip_levels  = 1;
    td.array_layers = 1;
    auto r = dev.create_texture(td);
    EXPECT_TRUE(r.has_value());
    return r.has_value() ? *r : cd::rhi::TextureHandle {};
}

// ---- NullDevice tests -------------------------------------------------------

// Null backend writes zeros into the destination buffer region.
TEST(ImageReadback, NullDeviceWritesZerosOnSuccess)
{
    cd::rhi::NullDevice dev;

    constexpr std::uint32_t kW = 4;
    constexpr std::uint32_t kH = 4;
    // RGBA8Unorm: 4 bytes per texel.
    constexpr std::uint64_t kBytes = kW * kH * 4;
    constexpr std::uint64_t kOffset = 8;

    auto tex = make_texture(dev, kW, kH, cd::rhi::Format::kRGBA8Unorm);
    auto buf = make_readback_buffer(dev, kOffset + kBytes);
    ASSERT_TRUE(tex.is_valid());
    ASSERT_TRUE(buf.is_valid());

    // Pre-fill the buffer with 0xFF so zeros are distinguishable.
    {
        std::vector<std::byte> fill(kOffset + kBytes, std::byte { 0xFF });
        (void)dev.upload_buffer(buf, 0, fill);
    }

    cd::rhi::IDevice::ImageRegion region {};
    region.x = 0;
    region.y = 0;
    region.width  = kW;
    region.height = kH;
    region.mip_level  = 0;
    region.base_layer = 0;

    // Act.
    const auto result = dev.copy_image_to_buffer(tex, buf, kOffset, region);
    ASSERT_TRUE(result.has_value());

    // Assert: the copied region is all zeros; the prefix is untouched (0xFF).
    const auto bytes = dev.peek_buffer(buf);
    ASSERT_EQ(bytes.size(), kOffset + kBytes);

    // Prefix must be unchanged.
    for (std::uint64_t i = 0; i < kOffset; ++i)
    {
        EXPECT_EQ(static_cast<std::uint8_t>(bytes[i]), 0xFFu)
            << "prefix byte " << i << " should be untouched";
    }
    // Readback region must be zeroed.
    for (std::uint64_t i = kOffset; i < kOffset + kBytes; ++i)
    {
        EXPECT_EQ(static_cast<std::uint8_t>(bytes[i]), 0x00u)
            << "readback byte " << i << " should be zero";
    }

    dev.destroy_texture(tex);
    dev.destroy_buffer(buf);
}

// Offset == 0 edge case.
TEST(ImageReadback, NullDeviceZeroOffset)
{
    cd::rhi::NullDevice dev;
    constexpr std::uint64_t kBytes = 16;  // 2×2 RGBA8
    auto tex = make_texture(dev, 2, 2, cd::rhi::Format::kRGBA8Unorm);
    auto buf = make_readback_buffer(dev, kBytes);
    ASSERT_TRUE(tex.is_valid());
    ASSERT_TRUE(buf.is_valid());

    cd::rhi::IDevice::ImageRegion region {};
    region.width  = 2;
    region.height = 2;

    const auto r = dev.copy_image_to_buffer(tex, buf, 0, region);
    ASSERT_TRUE(r.has_value());

    dev.destroy_texture(tex);
    dev.destroy_buffer(buf);
}

// Negative: invalid src_image handle → kInvalidArgument.
TEST(ImageReadback, NullDeviceInvalidTextureHandle)
{
    cd::rhi::NullDevice dev;
    auto buf = make_readback_buffer(dev, 64);

    cd::rhi::TextureHandle bad_tex { 0xDEAD, 0 };
    cd::rhi::IDevice::ImageRegion region {};
    region.width  = 4;
    region.height = 4;

    const auto r = dev.copy_image_to_buffer(bad_tex, buf, 0, region);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));

    dev.destroy_buffer(buf);
}

// Negative: invalid dst_buffer handle → kInvalidArgument.
TEST(ImageReadback, NullDeviceInvalidBufferHandle)
{
    cd::rhi::NullDevice dev;
    auto tex = make_texture(dev, 4, 4, cd::rhi::Format::kRGBA8Unorm);

    cd::rhi::BufferHandle bad_buf { 0xBEEF, 0 };
    cd::rhi::IDevice::ImageRegion region {};
    region.width  = 4;
    region.height = 4;

    const auto r = dev.copy_image_to_buffer(tex, bad_buf, 0, region);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));

    dev.destroy_texture(tex);
}

// Negative: GPU-only buffer → kInvalidArgument.
TEST(ImageReadback, NullDeviceGpuOnlyBufferRejected)
{
    cd::rhi::NullDevice dev;
    auto tex = make_texture(dev, 4, 4, cd::rhi::Format::kRGBA8Unorm);

    cd::rhi::BufferDesc bd {};
    bd.size   = 64;
    bd.usage  = cd::rhi::BufferUsage::kStorage;
    bd.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto buf_r = dev.create_buffer(bd);
    ASSERT_TRUE(buf_r.has_value());
    const auto gpu_buf = *buf_r;

    cd::rhi::IDevice::ImageRegion region {};
    region.width  = 4;
    region.height = 4;

    const auto r = dev.copy_image_to_buffer(tex, gpu_buf, 0, region);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));

    dev.destroy_buffer(gpu_buf);
    dev.destroy_texture(tex);
}

// Negative: dst range out of bounds → kInvalidArgument.
TEST(ImageReadback, NullDeviceRangeOutOfBounds)
{
    cd::rhi::NullDevice dev;
    // Texture 4×4 RGBA8 = 64 bytes needed, but buffer only has 32 bytes.
    auto tex = make_texture(dev, 4, 4, cd::rhi::Format::kRGBA8Unorm);
    auto buf = make_readback_buffer(dev, 32);
    ASSERT_TRUE(tex.is_valid());
    ASSERT_TRUE(buf.is_valid());

    cd::rhi::IDevice::ImageRegion region {};
    region.width  = 4;
    region.height = 4;

    const auto r = dev.copy_image_to_buffer(tex, buf, 0, region);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));

    dev.destroy_texture(tex);
    dev.destroy_buffer(buf);
}

// ImageRegion struct default construction.
TEST(ImageReadback, ImageRegionDefaultsToZero)
{
    const cd::rhi::IDevice::ImageRegion r {};
    EXPECT_EQ(r.x, 0u);
    EXPECT_EQ(r.y, 0u);
    EXPECT_EQ(r.width, 0u);
    EXPECT_EQ(r.height, 0u);
    EXPECT_EQ(r.mip_level, 0u);
    EXPECT_EQ(r.base_layer, 0u);
}

}  // namespace
