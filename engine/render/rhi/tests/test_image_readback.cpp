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
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
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
    constexpr std::uint64_t kBytes = static_cast<std::uint64_t>(kW) * kH * 4;
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
    // phase1127 (X4-B): legacy default — discard-permitting transition.
    EXPECT_EQ(r.src_state, cd::rhi::ResourceState::kUndefined);
}

// ---- GPU round-trip (phase1127, X4-B parity gate) ---------------------------
//
// Clear a 4x4 RGBA8 offscreen target to a known colour through a real
// render pass, declare the image's true state via ImageRegion::src_state,
// read the texels back through copy_image_to_buffer + download_buffer and
// assert every channel. This is the test the ROADMAP_PHASE_2 §2.4 gate
// names: the same body runs against the Vulkan AND D3D12 backends so the
// two readback paths cannot drift.

void run_gpu_clear_roundtrip(cd::rhi::IDevice& dev)
{
    constexpr std::uint32_t kW = 4;
    constexpr std::uint32_t kH = 4;
    constexpr std::uint64_t kBytes = std::uint64_t { kW } * kH * 4u;

    // Arrange: colour target + readback buffer.
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { kW, kH, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kColorAttachment |
                      cd::rhi::TextureUsage::kTransferSrc |
                      cd::rhi::TextureUsage::kSampled;
    auto tex_r = dev.create_texture(td);
    ASSERT_TRUE(tex_r.has_value());
    const auto tex = *tex_r;

    cd::rhi::TextureViewDesc vd {};
    vd.texture = tex;
    vd.type    = cd::rhi::TextureType::k2D;
    auto view_r = dev.create_texture_view(vd);
    ASSERT_TRUE(view_r.has_value());
    const auto view = *view_r;

    cd::rhi::BufferDesc bd {};
    bd.size   = kBytes;
    bd.usage  = cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto buf_r = dev.create_buffer(bd);
    ASSERT_TRUE(buf_r.has_value());
    const auto buf = *buf_r;

    // Act 1: clear the target via a render pass, leave it in
    // kShaderResource (the state we declare to the readback).
    auto cmd = dev.create_command_buffer();
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    cd::rhi::TextureBarrier to_color {};
    to_color.texture = tex;
    to_color.from    = cd::rhi::ResourceState::kUndefined;
    to_color.to      = cd::rhi::ResourceState::kColorAttachment;
    to_color.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, { &to_color, 1 });

    cd::rhi::ColorAttachmentInfo att {};
    att.view        = view;
    att.load_op     = cd::rhi::LoadOp::kClear;
    att.store_op    = cd::rhi::StoreOp::kStore;
    att.clear_color = { .f32 = { 1.0F, 0.5F, 0.25F, 1.0F } };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments = { &att, 1 };
    rp.render_area.extent = { kW, kH };
    cmd->begin_render_pass(rp);
    cmd->end_render_pass();

    cd::rhi::TextureBarrier to_read {};
    to_read.texture = tex;
    to_read.from    = cd::rhi::ResourceState::kColorAttachment;
    to_read.to      = cd::rhi::ResourceState::kShaderResource;
    to_read.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, { &to_read, 1 });
    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();

    // Act 2: readback with the TRUE current state declared. (With the
    // legacy kUndefined default the spec would allow the driver to
    // discard the cleared texels in the to-transfer transition.)
    cd::rhi::IDevice::ImageRegion region {};
    region.width     = kW;
    region.height    = kH;
    region.src_state = cd::rhi::ResourceState::kShaderResource;
    const auto copy_r = dev.copy_image_to_buffer(tex, buf, 0, region);
    ASSERT_TRUE(copy_r.has_value())
        << std::string(copy_r.error().message.begin(), copy_r.error().message.end());

    std::vector<std::byte> raw(static_cast<std::size_t>(kBytes));
    auto dl = dev.download_buffer(buf, 0, std::span<std::byte> { raw });
    ASSERT_TRUE(dl.has_value());

    // Assert: every texel is the clear colour (unorm rounding ±1).
    const auto near_u8 = [](std::byte got, int want)
    {
        const int g = static_cast<int>(std::to_integer<std::uint8_t>(got));
        return g >= want - 1 && g <= want + 1;
    };
    for (std::uint32_t i = 0; i < kW * kH; ++i)
    {
        const std::size_t o = static_cast<std::size_t>(i) * 4u;
        EXPECT_TRUE(near_u8(raw[o + 0], 255)) << "texel " << i << " R";
        EXPECT_TRUE(near_u8(raw[o + 1], 128)) << "texel " << i << " G";
        EXPECT_TRUE(near_u8(raw[o + 2], 64))  << "texel " << i << " B";
        EXPECT_TRUE(near_u8(raw[o + 3], 255)) << "texel " << i << " A";
    }

    dev.destroy_buffer(buf);
    dev.destroy_texture_view(view);
    dev.destroy_texture(tex);
}

TEST(ImageReadback, VulkanGpuClearColorRoundTrip)
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto dev_r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!dev_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD available on this host";
    run_gpu_clear_roundtrip(**dev_r);
}

#if defined(_WIN32)
TEST(ImageReadback, D3D12GpuClearColorRoundTrip)
{
    auto dev_r = cd::rhi::d3d12::create_d3d12_device({});
    if (!dev_r.has_value())
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    run_gpu_clear_roundtrip(**dev_r);
}
#endif

// ---- 1D / 3D texture create + view (X4-E1 parity gate) ----------------------
//
// Mirrors the GPU-clear round-trip pattern: a single body runs against the
// real Vulkan and (on Windows) D3D12 backends so the 1D/3D create_texture +
// create_texture_view paths cannot drift. NullDevice runs the same body too
// (headless CI). Exercises the X4-E1 D3D12 fixes:
//   * create_texture k1D / k3D (already wired Phase 393/394 — regression net)
//   * create_texture_view k1D / k3D SRV dimensions
// The SampledImage usage routes through the SRV path that picks
// TEXTURE1D / TEXTURE3D dimensions from the view-record flags.

void run_1d_3d_texture_create(cd::rhi::IDevice& dev)
{
    // --- 1D texture (64×1) sampled view. ---
    {
        cd::rhi::TextureDesc td {};
        td.type         = cd::rhi::TextureType::k1D;
        td.format       = cd::rhi::Format::kRGBA8Unorm;
        td.extent       = { 64, 1, 1 };
        td.mip_levels   = 1;
        td.array_layers = 1;
        td.usage        = cd::rhi::TextureUsage::kSampled |
                          cd::rhi::TextureUsage::kTransferSrc;
        auto tex_r = dev.create_texture(td);
        ASSERT_TRUE(tex_r.has_value())
            << std::string(tex_r.error().message.begin(), tex_r.error().message.end());
        const auto tex = *tex_r;

        cd::rhi::TextureViewDesc vd {};
        vd.texture = tex;
        vd.type    = cd::rhi::TextureType::k1D;
        auto view_r = dev.create_texture_view(vd);
        ASSERT_TRUE(view_r.has_value())
            << std::string(view_r.error().message.begin(), view_r.error().message.end());

        dev.destroy_texture_view(*view_r);
        dev.destroy_texture(tex);
    }

    // --- 3D texture (16×16×16) sampled view. ---
    {
        cd::rhi::TextureDesc td {};
        td.type         = cd::rhi::TextureType::k3D;
        td.format       = cd::rhi::Format::kRGBA8Unorm;
        td.extent       = { 16, 16, 16 };
        td.mip_levels   = 1;
        td.array_layers = 1;
        td.usage        = cd::rhi::TextureUsage::kSampled |
                          cd::rhi::TextureUsage::kTransferSrc;
        auto tex_r = dev.create_texture(td);
        ASSERT_TRUE(tex_r.has_value())
            << std::string(tex_r.error().message.begin(), tex_r.error().message.end());
        const auto tex = *tex_r;

        cd::rhi::TextureViewDesc vd {};
        vd.texture = tex;
        vd.type    = cd::rhi::TextureType::k3D;
        auto view_r = dev.create_texture_view(vd);
        ASSERT_TRUE(view_r.has_value())
            << std::string(view_r.error().message.begin(), view_r.error().message.end());

        dev.destroy_texture_view(*view_r);
        dev.destroy_texture(tex);
    }
}

TEST(ImageReadback, NullDevice1Dand3DTextureCreate)
{
    cd::rhi::NullDevice dev;
    run_1d_3d_texture_create(dev);
}

TEST(ImageReadback, Vulkan1Dand3DTextureCreate)
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto dev_r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!dev_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD available on this host";
    run_1d_3d_texture_create(**dev_r);
}

#if defined(_WIN32)
TEST(ImageReadback, D3D121Dand3DTextureCreate)
{
    auto dev_r = cd::rhi::d3d12::create_d3d12_device({});
    if (!dev_r.has_value())
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    run_1d_3d_texture_create(**dev_r);
}

// X4-E1 — a 3D texture used as a render target gets a TEXTURE3D RTV
// (not the previous hardcoded TEXTURE2D dimension). D3D12 *does* allow
// ALLOW_RENDER_TARGET on a TEXTURE3D resource, so this path is reachable
// on real hardware (unlike depth-stencil, which D3D12 forbids on 3D
// resources at CreateCommittedResource time — that guard stays as
// defense-in-depth in create_texture_view but cannot be reached here).
TEST(ImageReadback, D3D123DRenderTargetViewSucceeds)
{
    auto dev_r = cd::rhi::d3d12::create_d3d12_device({});
    if (!dev_r.has_value())
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& dev = **dev_r;

    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k3D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { 8, 8, 8 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kColorAttachment;
    auto tex_r = dev.create_texture(td);
    ASSERT_TRUE(tex_r.has_value())
        << std::string(tex_r.error().message.begin(), tex_r.error().message.end());
    const auto tex = *tex_r;

    cd::rhi::TextureViewDesc vd {};
    vd.texture = tex;
    vd.type    = cd::rhi::TextureType::k3D;
    auto view_r = dev.create_texture_view(vd);
    ASSERT_TRUE(view_r.has_value())
        << std::string(view_r.error().message.begin(), view_r.error().message.end());

    dev.destroy_texture_view(*view_r);
    dev.destroy_texture(tex);
}
#endif

}  // namespace
