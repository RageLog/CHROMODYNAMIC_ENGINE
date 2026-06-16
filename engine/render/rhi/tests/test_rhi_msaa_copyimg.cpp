// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_rhi_msaa_copyimg.cpp
//
// V-MSAA-RESOLVE + V-COPY-IMG (Backend-to-100 Wave 3c).
//
// V-MSAA-RESOLVE: begin_render_pass previously hardcoded resolveMode=NONE +
//   null resolve attachment (Vulkan) and had no resolve on D3D12's render-pass
//   path. Wave 3c wires ColorAttachmentInfo::resolve_view → the backend resolve
//   (Vulkan VkRenderingAttachmentInfo.resolveMode=AVERAGE + resolveImageView;
//   D3D12 ResolveSubresource after the pass; Metal resolveTexture +
//   MultisampleResolve store action). PROOF: clear a 4x-MSAA target to a known
//   colour with a 1x resolve target attached, read the RESOLVED 1x target back —
//   it shows the (averaged) clear colour, NOT the all-zero of an un-run resolve.
//
// V-COPY-IMG: copy_texture_to_texture (image→image) is new — only buffer↔buffer
//   and buffer↔image existed. PROOF: write a per-texel pattern into a src
//   texture (cmd-level buffer→image upload), copy_texture_to_texture a region
//   into a dst texture, read dst back — it matches the source pattern.
//
// Both bodies run against the real Vulkan (lavapipe/RTX 3080) and D3D12 (WARP)
// backends so the two paths cannot drift. SKIP cleanly when no device is present.
// Pattern: Arrange / Act / Assert.
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
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace
{

// ---- V-MSAA-RESOLVE ---------------------------------------------------------

void run_msaa_resolve(cd::rhi::IDevice& dev)
{
    constexpr std::uint32_t kW = 4;
    constexpr std::uint32_t kH = 4;
    constexpr std::uint64_t kBytes = std::uint64_t { kW } * kH * 4u;

    // Arrange: a 4x-MSAA colour target + a 1x resolve target (same format/extent).
    cd::rhi::TextureDesc ms_td {};
    ms_td.type         = cd::rhi::TextureType::k2D;
    ms_td.format       = cd::rhi::Format::kRGBA8Unorm;
    ms_td.extent       = { kW, kH, 1 };
    ms_td.mip_levels   = 1;
    ms_td.array_layers = 1;
    ms_td.samples      = cd::rhi::SampleCount::k4;
    ms_td.usage        = cd::rhi::TextureUsage::kColorAttachment;
    auto ms_r = dev.create_texture(ms_td);
    if (!ms_r.has_value())
        GTEST_SKIP() << "4x MSAA colour target unsupported on this adapter: "
                     << std::string(ms_r.error().message.begin(),
                                    ms_r.error().message.end());
    const auto ms_tex = *ms_r;
    cd::rhi::TextureViewDesc ms_vd {};
    ms_vd.texture = ms_tex;
    auto ms_view_r = dev.create_texture_view(ms_vd);
    ASSERT_TRUE(ms_view_r.has_value());
    const auto ms_view = *ms_view_r;

    cd::rhi::TextureDesc rs_td {};
    rs_td.type         = cd::rhi::TextureType::k2D;
    rs_td.format       = cd::rhi::Format::kRGBA8Unorm;
    rs_td.extent       = { kW, kH, 1 };
    rs_td.mip_levels   = 1;
    rs_td.array_layers = 1;
    rs_td.samples      = cd::rhi::SampleCount::k1;
    rs_td.usage        = cd::rhi::TextureUsage::kColorAttachment |
                         cd::rhi::TextureUsage::kTransferSrc |
                         cd::rhi::TextureUsage::kSampled;
    auto rs_r = dev.create_texture(rs_td);
    ASSERT_TRUE(rs_r.has_value());
    const auto rs_tex = *rs_r;
    cd::rhi::TextureViewDesc rs_vd {};
    rs_vd.texture = rs_tex;
    auto rs_view_r = dev.create_texture_view(rs_vd);
    ASSERT_TRUE(rs_view_r.has_value());
    const auto rs_view = *rs_view_r;

    cd::rhi::BufferDesc bd {};
    bd.size   = kBytes;
    bd.usage  = cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto buf_r = dev.create_buffer(bd);
    ASSERT_TRUE(buf_r.has_value());
    const auto buf = *buf_r;

    // Act: clear the MSAA target to a known colour WITH the resolve target
    // attached. resolveMode=AVERAGE of a uniform clear == the clear colour.
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    std::array<cd::rhi::TextureBarrier, 2> to_color {};
    to_color[0] = { .texture = ms_tex, .from = cd::rhi::ResourceState::kUndefined,
                    .to = cd::rhi::ResourceState::kColorAttachment, .range = { 0, 1, 0, 1 } };
    to_color[1] = { .texture = rs_tex, .from = cd::rhi::ResourceState::kUndefined,
                    .to = cd::rhi::ResourceState::kColorAttachment, .range = { 0, 1, 0, 1 } };
    cmd->barrier({}, to_color);

    cd::rhi::ColorAttachmentInfo ca {};
    ca.view         = ms_view;
    ca.resolve_view = rs_view;  // <-- the Wave 3c resolve wiring under test.
    ca.load_op      = cd::rhi::LoadOp::kClear;
    ca.store_op     = cd::rhi::StoreOp::kStore;
    ca.clear_color  = { .f32 = { 0.25F, 0.5F, 0.75F, 1.0F } };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&ca, 1);
    rp.render_area.extent = { kW, kH };
    cmd->begin_render_pass(rp);
    cmd->end_render_pass();

    // Read the RESOLVED 1x target.
    cd::rhi::TextureBarrier to_src {};
    to_src.texture = rs_tex;
    to_src.from    = cd::rhi::ResourceState::kColorAttachment;
    to_src.to      = cd::rhi::ResourceState::kTransferSrc;
    to_src.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_src, 1));

    cd::rhi::BufferImageCopyRegion reg {};
    reg.image_extent = { kW, kH, 1 };
    cmd->copy_image_to_buffer(
        rs_tex, buf, std::span<const cd::rhi::BufferImageCopyRegion>(&reg, 1));
    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();

    // Assert: the resolved target is the clear colour, not all-zero.
    std::vector<std::byte> raw(static_cast<std::size_t>(kBytes));
    auto dl = dev.download_buffer(buf, 0, std::span<std::byte> { raw });
    ASSERT_TRUE(dl.has_value());

    const auto u8 = [&](std::size_t i) {
        return static_cast<int>(std::to_integer<std::uint8_t>(raw[i]));
    };
    const auto near = [](int g, int want) { return g >= want - 2 && g <= want + 2; };
    int nonzero = 0;
    for (std::uint32_t i = 0; i < kW * kH; ++i)
    {
        const std::size_t o = static_cast<std::size_t>(i) * 4u;
        if (u8(o + 0) != 0 || u8(o + 1) != 0 || u8(o + 2) != 0) ++nonzero;
        EXPECT_TRUE(near(u8(o + 0), 64))  << "texel " << i << " R (resolve not run?)";
        EXPECT_TRUE(near(u8(o + 1), 128)) << "texel " << i << " G";
        EXPECT_TRUE(near(u8(o + 2), 191)) << "texel " << i << " B";
    }
    EXPECT_GT(nonzero, 0)
        << "BUG: the resolve target is ALL-ZERO — the MSAA resolve never ran "
           "(resolveMode=NONE / no ResolveSubresource). resolve_view was ignored.";

    dev.destroy_buffer(buf);
    dev.destroy_texture_view(rs_view);
    dev.destroy_texture(rs_tex);
    dev.destroy_texture_view(ms_view);
    dev.destroy_texture(ms_tex);
}

// ---- V-COPY-IMG -------------------------------------------------------------

void run_copy_texture_to_texture(cd::rhi::IDevice& dev)
{
    constexpr std::uint32_t kW = 8;
    constexpr std::uint32_t kH = 8;
    constexpr std::uint64_t kBytes = std::uint64_t { kW } * kH * 4u;

    // A per-texel-unique RGBA8 pattern.
    std::vector<std::uint8_t> pattern(static_cast<std::size_t>(kBytes));
    for (std::uint32_t i = 0; i < kW * kH; ++i)
    {
        const std::size_t o = static_cast<std::size_t>(i) * 4u;
        pattern[o + 0] = static_cast<std::uint8_t>(i & 0xFFu);
        pattern[o + 1] = static_cast<std::uint8_t>((i * 3u) & 0xFFu);
        pattern[o + 2] = static_cast<std::uint8_t>((i * 7u) & 0xFFu);
        pattern[o + 3] = 0xFFu;
    }

    auto make_tex = [&](cd::rhi::TextureUsage extra) {
        cd::rhi::TextureDesc td {};
        td.type         = cd::rhi::TextureType::k2D;
        td.format       = cd::rhi::Format::kRGBA8Unorm;
        td.extent       = { kW, kH, 1 };
        td.mip_levels   = 1;
        td.array_layers = 1;
        td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
        td.usage        = cd::rhi::TextureUsage::kTransferDst |
                          cd::rhi::TextureUsage::kTransferSrc | extra;
        return dev.create_texture(td);
    };
    auto src_r = make_tex(cd::rhi::TextureUsage::kSampled);
    ASSERT_TRUE(src_r.has_value());
    const auto src_tex = *src_r;
    auto dst_r = make_tex(cd::rhi::TextureUsage::kSampled);
    ASSERT_TRUE(dst_r.has_value());
    const auto dst_tex = *dst_r;

    cd::rhi::BufferDesc up_bd {};
    up_bd.size   = kBytes;
    up_bd.usage  = cd::rhi::BufferUsage::kTransferSrc;
    up_bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto up_r = dev.create_buffer(up_bd);
    ASSERT_TRUE(up_r.has_value());
    const auto up_buf = *up_r;
    {
        auto put = dev.upload_buffer(
            up_buf, 0,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(pattern.data()), kBytes));
        ASSERT_TRUE(put.has_value());
    }

    cd::rhi::BufferDesc rb_bd {};
    rb_bd.size   = kBytes;
    rb_bd.usage  = cd::rhi::BufferUsage::kTransferDst;
    rb_bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto rb_r = dev.create_buffer(rb_bd);
    ASSERT_TRUE(rb_r.has_value());
    const auto rb_buf = *rb_r;

    // Act: upload pattern into src, copy_texture_to_texture src→dst, read dst.
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    std::array<cd::rhi::TextureBarrier, 2> to_dst {};
    to_dst[0] = { .texture = src_tex, .from = cd::rhi::ResourceState::kUndefined,
                  .to = cd::rhi::ResourceState::kTransferDst, .range = { 0, 1, 0, 1 } };
    to_dst[1] = { .texture = dst_tex, .from = cd::rhi::ResourceState::kUndefined,
                  .to = cd::rhi::ResourceState::kTransferDst, .range = { 0, 1, 0, 1 } };
    cmd->barrier({}, to_dst);

    cd::rhi::BufferImageCopyRegion up_reg {};
    up_reg.image_extent = { kW, kH, 1 };
    cmd->copy_buffer_to_image(
        up_buf, src_tex, std::span<const cd::rhi::BufferImageCopyRegion>(&up_reg, 1));

    // src → kTransferSrc, dst stays kTransferDst for the image→image copy.
    cd::rhi::TextureBarrier src_to_src {};
    src_to_src.texture = src_tex;
    src_to_src.from    = cd::rhi::ResourceState::kTransferDst;
    src_to_src.to      = cd::rhi::ResourceState::kTransferSrc;
    src_to_src.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&src_to_src, 1));

    cd::rhi::TextureCopyRegion tcr {};
    tcr.layer_count = 1;
    tcr.extent      = { kW, kH, 1 };
    cmd->copy_texture_to_texture(
        src_tex, dst_tex, std::span<const cd::rhi::TextureCopyRegion>(&tcr, 1));

    // dst → kTransferSrc for readback.
    cd::rhi::TextureBarrier dst_to_src {};
    dst_to_src.texture = dst_tex;
    dst_to_src.from    = cd::rhi::ResourceState::kTransferDst;
    dst_to_src.to      = cd::rhi::ResourceState::kTransferSrc;
    dst_to_src.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&dst_to_src, 1));

    cd::rhi::BufferImageCopyRegion dn_reg {};
    dn_reg.image_extent = { kW, kH, 1 };
    cmd->copy_image_to_buffer(
        dst_tex, rb_buf, std::span<const cd::rhi::BufferImageCopyRegion>(&dn_reg, 1));
    cmd->end();

    EXPECT_FALSE(cmd->recording_error())
        << "no recording call hit an invalid handle in the copy sequence";

    dev.submit(*cmd);
    dev.wait_idle();

    // Assert: dst matches the source pattern byte-for-byte.
    std::vector<std::byte> got(static_cast<std::size_t>(kBytes));
    auto dl = dev.download_buffer(rb_buf, 0, std::span<std::byte> { got });
    ASSERT_TRUE(dl.has_value());

    auto first_bad = static_cast<std::size_t>(kBytes);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kBytes); ++i)
    {
        if (std::to_integer<std::uint8_t>(got[i]) != pattern[i]) { first_bad = i; break; }
    }
    EXPECT_EQ(first_bad, static_cast<std::size_t>(kBytes))
        << "BUG: copy_texture_to_texture did NOT reproduce the source pattern; "
           "first mismatch at byte " << first_bad;

    dev.destroy_buffer(rb_buf);
    dev.destroy_buffer(up_buf);
    dev.destroy_texture(dst_tex);
    dev.destroy_texture(src_tex);
}

}  // namespace

// ---- V-MSAA-RESOLVE tests ---------------------------------------------------

TEST(RhiMsaaResolve, VulkanResolveTargetShowsClear)
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto dev_r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!dev_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD available on this host";
    run_msaa_resolve(**dev_r);
}

#if defined(_WIN32)
TEST(RhiMsaaResolve, D3D12ResolveTargetShowsClear)
{
    auto dev_r = cd::rhi::d3d12::create_d3d12_device({});
    if (!dev_r.has_value())
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    run_msaa_resolve(**dev_r);
}
#endif

// ---- V-COPY-IMG tests -------------------------------------------------------

TEST(RhiCopyImage, VulkanCopyTextureToTextureRoundTrip)
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto dev_r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!dev_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD available on this host";
    run_copy_texture_to_texture(**dev_r);
}

#if defined(_WIN32)
TEST(RhiCopyImage, D3D12CopyTextureToTextureRoundTrip)
{
    auto dev_r = cd::rhi::d3d12::create_d3d12_device({});
    if (!dev_r.has_value())
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    run_copy_texture_to_texture(**dev_r);
}
#endif
