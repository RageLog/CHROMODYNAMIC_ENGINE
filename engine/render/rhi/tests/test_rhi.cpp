// =============================================================================
// CHROMODYNAMIC — cd::rhi tests (Sprint S3.0)
// =============================================================================
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/BlendPresets.hpp>
#include <cd/rhi/DepthStencilPresets.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

// --- Format -----------------------------------------------------------------
TEST(RhiFormat, BytesPerPixelStandardFormats)
{
    EXPECT_EQ(cd::rhi::info_of(cd::rhi::Format::kRGBA8Unorm).bytes_per_block, 4u);
    EXPECT_EQ(cd::rhi::info_of(cd::rhi::Format::kRG16Float).bytes_per_block, 4u);
    EXPECT_EQ(cd::rhi::info_of(cd::rhi::Format::kRGBA32Float).bytes_per_block, 16u);
    EXPECT_EQ(cd::rhi::info_of(cd::rhi::Format::kR8Unorm).bytes_per_block, 1u);
}

TEST(RhiFormat, DepthStencilDetection)
{
    EXPECT_TRUE(cd::rhi::is_depth_format(cd::rhi::Format::kD32Float));
    EXPECT_TRUE(cd::rhi::is_depth_format(cd::rhi::Format::kD24UnormS8Uint));
    EXPECT_TRUE(cd::rhi::is_stencil_format(cd::rhi::Format::kD24UnormS8Uint));
    EXPECT_FALSE(cd::rhi::is_depth_format(cd::rhi::Format::kRGBA8Unorm));
    EXPECT_FALSE(cd::rhi::is_stencil_format(cd::rhi::Format::kR8Unorm));
}

TEST(RhiFormat, BlockCompressedSizes)
{
    // BC1: 8 bytes per 4x4 block (4bpp)
    auto bc1 = cd::rhi::info_of(cd::rhi::Format::kBC1RGBAUnorm);
    EXPECT_TRUE(bc1.is_compressed);
    EXPECT_EQ(bc1.bytes_per_block, 8u);
    EXPECT_EQ(bc1.block_width, 4u);
    EXPECT_EQ(bc1.block_height, 4u);

    // BC7: 16 bytes per 4x4 block (8bpp)
    auto bc7 = cd::rhi::info_of(cd::rhi::Format::kBC7Unorm);
    EXPECT_TRUE(bc7.is_compressed);
    EXPECT_EQ(bc7.bytes_per_block, 16u);
}

TEST(RhiFormat, SrgbFlag)
{
    EXPECT_TRUE(cd::rhi::info_of(cd::rhi::Format::kRGBA8Srgb).is_srgb);
    EXPECT_TRUE(cd::rhi::info_of(cd::rhi::Format::kBC7Srgb).is_srgb);
    EXPECT_FALSE(cd::rhi::info_of(cd::rhi::Format::kRGBA8Unorm).is_srgb);
}

// --- Enum flag ops ---------------------------------------------------------
TEST(RhiEnums, ShaderStageFlagComposition)
{
    auto gfx = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment;
    EXPECT_TRUE(cd::rhi::has(gfx, cd::rhi::ShaderStage::kVertex));
    EXPECT_TRUE(cd::rhi::has(gfx, cd::rhi::ShaderStage::kFragment));
    EXPECT_FALSE(cd::rhi::has(gfx, cd::rhi::ShaderStage::kCompute));
    EXPECT_TRUE(cd::rhi::any(gfx));
    EXPECT_FALSE(cd::rhi::any(cd::rhi::ShaderStage::kNone));
}

TEST(RhiEnums, BufferUsageMask)
{
    auto u = cd::rhi::BufferUsage::kVertex | cd::rhi::BufferUsage::kTransferDst;
    EXPECT_TRUE(cd::rhi::has(u, cd::rhi::BufferUsage::kVertex));
    EXPECT_TRUE(cd::rhi::has(u, cd::rhi::BufferUsage::kTransferDst));
    EXPECT_FALSE(cd::rhi::has(u, cd::rhi::BufferUsage::kIndex));
}

// --- Handles ----------------------------------------------------------------
TEST(RhiHandles, TypeSafetyAcrossTags)
{
    cd::rhi::BufferHandle b { 1u, 1u };
    cd::rhi::TextureHandle t { 1u, 1u };
    EXPECT_NE(static_cast<bool>(b), false);
    EXPECT_NE(static_cast<bool>(t), false);
    // The following must NOT compile (cross-tag conversion). Verified manually:
    //     cd::rhi::TextureHandle x = b;  // intentionally absent
}

// --- NullDevice -------------------------------------------------------------
TEST(NullDevice, BackendAndAdapter)
{
    cd::rhi::NullDevice dev;
    EXPECT_EQ(dev.backend(), cd::rhi::Backend::kNull);
    EXPECT_EQ(dev.adapter_name(), "cd::rhi::NullDevice");
    EXPECT_GT(dev.limits().max_texture_dimension_2d, 0u);
}

TEST(NullDevice, BufferCreateUploadPeekDestroy)
{
    cd::rhi::NullDevice dev;
    cd::rhi::BufferDesc desc {};
    desc.size = 64;
    desc.usage = cd::rhi::BufferUsage::kVertex | cd::rhi::BufferUsage::kTransferDst;
    desc.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto r = dev.create_buffer(desc);
    ASSERT_TRUE(r.has_value());
    auto h = *r;
    EXPECT_EQ(dev.live_buffer_count(), 1u);

    std::array<std::byte, 4> payload { std::byte { 0xCA }, std::byte { 0xFE }, std::byte { 0xBA }, std::byte { 0xBE } };
    auto up = dev.upload_buffer(h, 8, std::span<const std::byte> { payload });
    ASSERT_TRUE(up.has_value());

    auto bytes = dev.peek_buffer(h);
    ASSERT_EQ(bytes.size(), 64u);
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[8]), 0xCAu);
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[11]), 0xBEu);

    dev.destroy_buffer(h);
    EXPECT_EQ(dev.live_buffer_count(), 0u);
}

TEST(NullDevice, ZeroSizeBufferRejected)
{
    cd::rhi::NullDevice dev;
    cd::rhi::BufferDesc desc {};
    desc.size = 0;
    desc.usage = cd::rhi::BufferUsage::kUniform;
    auto r = dev.create_buffer(desc);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));
}

TEST(NullDevice, UploadToGpuOnlyBufferRejected)
{
    cd::rhi::NullDevice dev;
    cd::rhi::BufferDesc desc {};
    desc.size = 16;
    desc.usage = cd::rhi::BufferUsage::kStorage;
    desc.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto r = dev.create_buffer(desc);
    ASSERT_TRUE(r.has_value());
    std::array<std::byte, 4> payload {};
    auto up = dev.upload_buffer(*r, 0, std::span<const std::byte> { payload });
    ASSERT_FALSE(up.has_value());
}

TEST(NullDevice, TextureLifecycle)
{
    cd::rhi::NullDevice dev;
    cd::rhi::TextureDesc desc {};
    desc.format = cd::rhi::Format::kRGBA8Srgb;
    desc.extent = { 256, 256, 1 };
    desc.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kColorAttachment;
    auto r = dev.create_texture(desc);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(dev.live_texture_count(), 1u);
    dev.destroy_texture(*r);
    EXPECT_EQ(dev.live_texture_count(), 0u);
}

TEST(NullDevice, TextureZeroExtentRejected)
{
    cd::rhi::NullDevice dev;
    cd::rhi::TextureDesc desc {};
    desc.extent = { 0, 0, 1 };
    auto r = dev.create_texture(desc);
    ASSERT_FALSE(r.has_value());
}

TEST(NullDevice, ShaderModuleRequiresCode)
{
    cd::rhi::NullDevice dev;
    cd::rhi::ShaderModuleDesc desc {};
    desc.stage = cd::rhi::ShaderStage::kVertex;
    auto r = dev.create_shader_module(desc);
    ASSERT_FALSE(r.has_value());

    // With code, it must succeed.
    std::array<std::byte, 8> code {};
    desc.code = code.data();
    desc.code_size = code.size();
    auto ok = dev.create_shader_module(desc);
    ASSERT_TRUE(ok.has_value());
    dev.destroy_shader_module(*ok);
}

TEST(NullDevice, SwapchainAndSamplerCreate)
{
    cd::rhi::NullDevice dev;
    cd::rhi::SamplerDesc sd {};
    auto s = dev.create_sampler(sd);
    ASSERT_TRUE(s.has_value());
    dev.destroy_sampler(*s);

    cd::rhi::SwapchainDesc swd {};
    swd.extent = { 1280, 720 };
    auto sc = dev.create_swapchain(swd);
    ASSERT_TRUE(sc.has_value());
    dev.destroy_swapchain(*sc);
}

// --- S3.1 pipeline + command surface --------------------------------------
TEST(NullDevice, GraphicsPipelineRequiresVertexShader)
{
    cd::rhi::NullDevice dev;
    cd::rhi::GraphicsPipelineDesc desc {};
    auto bad = dev.create_graphics_pipeline(desc);
    ASSERT_FALSE(bad.has_value());

    // Provide a vertex shader → success.
    std::array<std::byte, 4> code {};
    cd::rhi::ShaderModuleDesc vsd {};
    vsd.stage = cd::rhi::ShaderStage::kVertex;
    vsd.code = code.data();
    vsd.code_size = code.size();
    auto vs = dev.create_shader_module(vsd);
    ASSERT_TRUE(vs.has_value());
    desc.vertex_shader = *vs;
    auto pso = dev.create_graphics_pipeline(desc);
    ASSERT_TRUE(pso.has_value());
    dev.destroy_graphics_pipeline(*pso);
    dev.destroy_shader_module(*vs);
}

TEST(NullDevice, ComputePipelineRequiresShader)
{
    cd::rhi::NullDevice dev;
    cd::rhi::ComputePipelineDesc desc {};
    auto bad = dev.create_compute_pipeline(desc);
    ASSERT_FALSE(bad.has_value());
}

TEST(NullCommandBuffer, RecordsLifecycle)
{
    cd::rhi::NullDevice dev;
    auto cb_owned = dev.create_command_buffer();
    ASSERT_NE(cb_owned, nullptr);
    auto* nb = dynamic_cast<cd::rhi::NullCommandBuffer*>(cb_owned.get());
    ASSERT_NE(nb, nullptr);

    EXPECT_FALSE(nb->is_recording());
    nb->begin();
    EXPECT_TRUE(nb->is_recording());

    cd::rhi::Viewport vp { 0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f };
    nb->set_viewport(vp);
    nb->set_scissor(
        {
            { 0,    0   },
            { 1280, 720 }
    }
    );

    cd::rhi::RenderPassBeginInfo info {};
    info.render_area = {
        { 0,    0   },
        { 1280, 720 }
    };
    nb->begin_render_pass(info);
    nb->draw(3, 1, 0, 0);
    nb->draw_indexed(36, 100, 0, 0, 0);
    nb->end_render_pass();

    nb->dispatch(8, 8, 1);
    nb->push_debug_group("frame");
    nb->pop_debug_group();
    nb->end();

    EXPECT_FALSE(nb->is_recording());
    const auto& log = nb->log();
    EXPECT_EQ(log.begin_count, 1ULL);
    EXPECT_EQ(log.end_count, 1ULL);
    EXPECT_EQ(log.begin_pass_count, 1ULL);
    EXPECT_EQ(log.end_pass_count, 1ULL);
    EXPECT_EQ(log.draws, 1ULL);
    EXPECT_EQ(log.indexed_draws, 1ULL);
    EXPECT_EQ(log.dispatches, 1ULL);
    EXPECT_EQ(log.set_viewport, 1ULL);
    EXPECT_EQ(log.set_scissor, 1ULL);
    ASSERT_EQ(log.debug_groups.size(), 1u);
    EXPECT_EQ(log.debug_groups[0], "frame");
}

TEST(NullCommandBuffer, BarriersCounted)
{
    cd::rhi::NullDevice dev;
    auto cb = dev.create_command_buffer();
    auto* nb = dynamic_cast<cd::rhi::NullCommandBuffer*>(cb.get());
    ASSERT_NE(nb, nullptr);

    std::array<cd::rhi::BufferBarrier, 2> bb {};
    std::array<cd::rhi::TextureBarrier, 3> tb {};
    nb->barrier(bb, tb);
    EXPECT_EQ(nb->log().buffer_barriers, 2ULL);
    EXPECT_EQ(nb->log().texture_barriers, 3ULL);
}

TEST(NullDevice, SubmitIncrementsCount)
{
    cd::rhi::NullDevice dev;
    auto cb = dev.create_command_buffer();
    EXPECT_EQ(dev.submit_count(), 0u);
    dev.submit(*cb);
    dev.submit(*cb);
    EXPECT_EQ(dev.submit_count(), 2u);
}

TEST(BlendPresets, OpaqueIsBlendDisabled)
{
    const auto s = cd::rhi::blend_opaque();
    EXPECT_FALSE(s.blend_enable);
}

TEST(BlendPresets, AlphaUsesSrcAlphaOneMinusSrcAlpha)
{
    const auto s = cd::rhi::blend_alpha();
    EXPECT_TRUE(s.blend_enable);
    EXPECT_EQ(s.src_color, cd::rhi::BlendFactor::kSrcAlpha);
    EXPECT_EQ(s.dst_color, cd::rhi::BlendFactor::kOneMinusSrcAlpha);
    EXPECT_EQ(s.color_op,  cd::rhi::BlendOp::kAdd);
}

TEST(BlendPresets, AdditiveSumsSourceWithDest)
{
    const auto s = cd::rhi::blend_additive();
    EXPECT_TRUE(s.blend_enable);
    EXPECT_EQ(s.dst_color, cd::rhi::BlendFactor::kOne);
    EXPECT_EQ(s.color_op,  cd::rhi::BlendOp::kAdd);
}

TEST(BlendPresets, PremultipliedUsesOneAsSourceColor)
{
    const auto s = cd::rhi::blend_premultiplied();
    EXPECT_TRUE(s.blend_enable);
    EXPECT_EQ(s.src_color, cd::rhi::BlendFactor::kOne);
    EXPECT_EQ(s.dst_color, cd::rhi::BlendFactor::kOneMinusSrcAlpha);
}

TEST(DepthStencilPresets, DefaultIsLessWithWrite)
{
    const auto s = cd::rhi::depth_default();
    EXPECT_TRUE(s.depth_test);
    EXPECT_TRUE(s.depth_write);
    EXPECT_EQ(s.depth_compare, cd::rhi::CompareOp::kLess);
}

TEST(DepthStencilPresets, ReadonlyKeepsTestDisablesWrite)
{
    const auto s = cd::rhi::depth_readonly();
    EXPECT_TRUE(s.depth_test);
    EXPECT_FALSE(s.depth_write);
}

TEST(DepthStencilPresets, DisabledHasNoTestNorWrite)
{
    const auto s = cd::rhi::depth_disabled();
    EXPECT_FALSE(s.depth_test);
    EXPECT_FALSE(s.depth_write);
}

TEST(DepthStencilPresets, EqualForPrePass)
{
    const auto s = cd::rhi::depth_equal();
    EXPECT_EQ(s.depth_compare, cd::rhi::CompareOp::kEqual);
    EXPECT_FALSE(s.depth_write);
}

}  // namespace
