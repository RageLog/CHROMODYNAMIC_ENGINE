// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_texture.cpp
//
// Backend-to-100 Wave 4b / C-METAL-TIER2 (docs/METAL_MAC_TESTING.md §3 Tier-2):
//   cd_test_metal_texture — create_texture -> lookup; copy_buffer_to_image
//   (M1 registry + the §5.5 bytesPerRow path). A staging buffer of RGBA8 texels
//   is copied into a 2D MTLTexture via a one-shot command buffer; the copy must
//   record + submit without a device fault.
//
// PLATFORM GATE (see test_metal_device.cpp for the full rationale): real
// MTLDevice on Apple; skip-stub everywhere else. Compiles + registers on
// Windows, runs on a Mac.
//
// Pattern: Arrange / Act / Assert. Deterministic; no sleep_for.
// =============================================================================
#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)

    #include <cd/rhi/Barriers.hpp>
    #include <cd/rhi/Descriptors.hpp>
    #include <cd/rhi/Enums.hpp>
    #include <cd/rhi/Format.hpp>
    #include <cd/rhi/Handles.hpp>
    #include <cd/rhi/ICommandBuffer.hpp>
    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/metal/MetalDevice.hpp>

    #include <gtest/gtest.h>

    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <memory>
    #include <span>
    #include <vector>

namespace
{

constexpr std::uint32_t kW = 4;
constexpr std::uint32_t kH = 4;

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_metal_device_or_null()
{
    cd::rhi::metal::MetalCreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::metal::create_metal_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
}

// ---- M1: create a 2D texture + upload via copy_buffer_to_image --------------
TEST(MetalTexture, CreateAndCopyBufferToImage)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    // Arrange: a 4x4 RGBA8 destination texture (transfer-dst + sampled).
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { kW, kH, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kTransferDst |
                      cd::rhi::TextureUsage::kSampled;
    auto tex_r = d.create_texture(td);
    ASSERT_TRUE(tex_r.has_value()) << "create_texture must succeed on a real MTLDevice";
    const auto tex = *tex_r;
    ASSERT_TRUE(tex.is_valid());

    // A tightly-packed staging buffer of 4x4 RGBA8 texels.
    constexpr std::uint64_t kBytes = std::uint64_t { kW } * kH * 4u;
    std::vector<std::byte> texels(static_cast<std::size_t>(kBytes));
    for (std::size_t i = 0; i < texels.size(); ++i)
        texels[i] = static_cast<std::byte>(i & 0xFFu);

    cd::rhi::BufferDesc bd {};
    bd.size   = kBytes;
    bd.usage  = cd::rhi::BufferUsage::kTransferSrc;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto buf_r = d.create_buffer(bd);
    ASSERT_TRUE(buf_r.has_value());
    const auto buf = *buf_r;
    ASSERT_TRUE(d.upload_buffer(buf, 0, std::span<const std::byte>(texels)).has_value());

    // Act: barrier into transfer-dst, copy buffer -> image, barrier to sampled.
    auto cmd = d.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    cd::rhi::TextureBarrier to_dst {};
    to_dst.texture = tex;
    to_dst.from    = cd::rhi::ResourceState::kUndefined;
    to_dst.to      = cd::rhi::ResourceState::kTransferDst;
    to_dst.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_dst, 1));

    cd::rhi::BufferImageCopyRegion region {};
    region.buffer_offset = 0;
    region.mip_level     = 0;
    region.base_layer    = 0;
    region.layer_count   = 1;
    region.image_extent  = { kW, kH, 1 };
    cmd->copy_buffer_to_image(buf, tex,
                              std::span<const cd::rhi::BufferImageCopyRegion>(&region, 1));

    cd::rhi::TextureBarrier to_read {};
    to_read.texture = tex;
    to_read.from    = cd::rhi::ResourceState::kTransferDst;
    to_read.to      = cd::rhi::ResourceState::kShaderResource;
    to_read.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_read, 1));
    cmd->end();

    // Submit + wait. A bytesPerRow / blit-encoder fault would remove the device
    // and surface as a SubmitDesc error (never an infinite wait).
    std::array<cd::rhi::ICommandBuffer*, 1> cmds { cmd.get() };
    cd::rhi::SubmitDesc sd {};
    sd.command_buffers = std::span<cd::rhi::ICommandBuffer* const>(cmds.data(), 1);
    const auto submit_r = d.submit(sd);
    EXPECT_TRUE(submit_r.has_value())
        << "copy_buffer_to_image submit must complete without a Metal device fault";
    d.wait_idle();

    d.destroy_buffer(buf);
    d.destroy_texture(tex);
}

}  // namespace

#else  // not (Apple && CD_RHI_METAL_ENABLED)

    #include <gtest/gtest.h>

TEST(MetalTexture, SkippedOffApple)
{
    GTEST_SKIP() << "Metal backend disabled on this platform (Apple-only)";
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
