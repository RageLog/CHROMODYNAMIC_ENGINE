// =============================================================================
// CHROMODYNAMIC -- cd::ui::renderer_rhi inline-shader Submitter tests
// Phase 554 / M3 W1A.
//
// These tests exercise the Route B factory
// (`Submitter::create_with_inline_shader`). The factory:
//   * compiles inline GLSL via cd::shader::ICompiler (glslang backend),
//   * builds shader modules + pipeline-layout + graphics-pipeline,
//   * leaves the ring vb/ib path untouched (delegated to `create()`).
//
// Both tests prefer a real Vulkan device (so the pipeline state goes
// through a true backend), but fall back to NullDevice with GTEST_SKIP
// when Vulkan is unavailable so CI hosts without a GPU still link the
// binary cleanly.
//
// The brief asked for "2 cases -- (a) create_with_inline_shader returns
// valid handle when given a real Vulkan device (GTEST_SKIP if no Vulkan),
// (b) record() emits expected draw-call count after 4 quad pushes". Both
// are implemented below; (a) checks `is_valid()` + zero frame state,
// (b) pumps 4 quads through the batcher and reads back the merged
// `command_count() == 1U` (all four share the same scissor/variant) plus
// the expected vertex/index totals.
// =============================================================================
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>

#include <gtest/gtest.h>

#include <memory>

namespace
{

namespace rh = cd::ui::renderer_rhi;
namespace ur = cd::ui::renderer;

// Try to bring up a real Vulkan device. Returns nullptr if the host has
// no Vulkan ICD (typical for headless CI). Tests gate on this and skip
// gracefully so the binary keeps compiling everywhere.
[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> try_make_vulkan()
{
    cd::rhi::vulkan::VulkanCreateInfo vci {};
    vci.app_name          = "test_submitter_inline";
    vci.enable_validation = false;  // CI hosts often lack the validation layer
    auto dr = cd::rhi::vulkan::create_vulkan_device(vci);
    if (!dr.has_value())
    {
        return nullptr;
    }
    return std::move(*dr);
}

}  // namespace

// (a) -- create_with_inline_shader builds a usable submitter when Vulkan
//        is available. The handle is valid, the inline pipeline is owned,
//        and the frame-local state is zero (no upload yet).
TEST(SubmitterInline, CreateWithInlineShaderProducesValidHandle)
{
    auto vk = try_make_vulkan();
    if (vk == nullptr)
    {
        GTEST_SKIP() << "no Vulkan ICD; inline-shader path needs a real device "
                        "to exercise the pipeline create call meaningfully";
    }

    rh::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    info.color_format = cd::rhi::Format::kBGRA8Unorm;

    auto r = rh::Submitter::create_with_inline_shader(*vk, info);
    ASSERT_TRUE(r.has_value())
        << "create_with_inline_shader failed: domain=" << r.error().domain
        << " code=" << r.error().code
        << " msg=" << std::string(r.error().message);
    auto& sub = *r;
    EXPECT_TRUE(sub.is_valid());
    EXPECT_EQ(sub.vertex_count(),  0U);
    EXPECT_EQ(sub.index_count(),   0U);
    EXPECT_EQ(sub.command_count(), 0U);
}

// (b) -- after 4 same-state quad pushes the batcher merges them into ONE
//        DrawCommand (same variant + same scissor) and the submitter
//        snapshots that count via `upload()`. The brief specifies "draw-
//        call count after 4 quad pushes"; the batcher exposes that count
//        via `command_count()` -- the figure that drives the per-command
//        scissor + draw_indexed loop in `record()`.
TEST(SubmitterInline, RecordEmitsExpectedDrawCallCount)
{
    auto vk = try_make_vulkan();
    if (vk == nullptr)
    {
        GTEST_SKIP() << "no Vulkan ICD; record() path is best validated on the "
                        "real backend (NullDevice draws are no-op)";
    }

    rh::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    info.color_format = cd::rhi::Format::kBGRA8Unorm;
    auto r = rh::Submitter::create_with_inline_shader(*vk, info);
    ASSERT_TRUE(r.has_value())
        << "create_with_inline_shader failed: domain=" << r.error().domain
        << " code=" << r.error().code
        << " msg=" << std::string(r.error().message);
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    for (int i = 0; i < 4; ++i)
    {
        batcher.quad(static_cast<float>(i * 32), 8.0F, 24.0F, 24.0F,
                     ur::Color::white());
    }

    EXPECT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.vertex_count(),  16U);  // 4 quads * 4 verts
    EXPECT_EQ(sub.index_count(),   24U);  // 4 quads * 6 indices
    // All four share the default scissor + kSolid variant -> single
    // batched draw command. This is the count `record()` iterates.
    EXPECT_EQ(sub.command_count(), 1U);

    // Smoke: record into a real command buffer. The Vulkan backend
    // returns a recordable buffer; we never submit (no swapchain image
    // bound), but the recording call itself must not crash and the
    // pipeline bind + push_constants + draw_indexed sequence must
    // round-trip the command buffer's begin/end lifecycle.
    auto cmd = vk->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    sub.record(*cmd, cd::rhi::Extent2D { 800U, 600U });
    cmd->end();
}

// (c) -- create_with_inline_shader with atlas_view + atlas_sampler compiles
//        kInlineFSWithAtlas, allocates descriptor set 0, supports secondary
//        texture slots via set_texture_slot(), and records kGlyph + kTextured
//        commands cleanly.
TEST(SubmitterInline, CreateWithInlineShaderAndAtlasBindsGlyphAndTextureSlots)
{
    auto vk = try_make_vulkan();
    if (vk == nullptr)
    {
        GTEST_SKIP() << "no Vulkan ICD; atlas inline shader test requires a real device";
    }

    cd::rhi::TextureDesc td {};
    td.type   = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { 16U, 16U, 1U };
    td.usage  = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    auto tex_r = vk->create_texture(td);
    ASSERT_TRUE(tex_r.has_value());
    auto view_r = vk->create_texture_view(cd::rhi::TextureViewDesc { *tex_r });
    ASSERT_TRUE(view_r.has_value());
    auto samp_r = vk->create_sampler(cd::rhi::SamplerDesc {});
    ASSERT_TRUE(samp_r.has_value());

    rh::SubmitterCreateInfo info {};
    info.max_vertices  = 1024U;
    info.max_indices   = 4096U;
    info.color_format  = cd::rhi::Format::kBGRA8Unorm;
    info.atlas_view    = *view_r;
    info.atlas_sampler = *samp_r;

    auto r = rh::Submitter::create_with_inline_shader(*vk, info);
    ASSERT_TRUE(r.has_value())
        << "create_with_inline_shader(atlas) failed: msg=" << std::string(r.error().message);
    auto& sub = *r;
    EXPECT_TRUE(sub.is_valid());
    EXPECT_TRUE(sub.set_texture_slot(1U, *view_r, *samp_r));
    EXPECT_TRUE(sub.has_texture_slot(1U));

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 32.0F, 32.0F, ur::Color::white());
    batcher.glyph(36.0F, 0.0F, 12.0F, 16.0F, 0U, ur::AtlasUv { 0.0F, 0.0F, 0.5F, 0.5F }, ur::Color::white());
    batcher.textured_quad(60.0F, 0.0F, 32.0F, 32.0F, 1U, ur::AtlasUv { 0.0F, 0.0F, 1.0F, 1.0F }, ur::Color::white());

    EXPECT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.command_count(), 3U);

    auto cmd = vk->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    sub.record(*cmd, cd::rhi::Extent2D { 800U, 600U });
    cmd->end();

    sub.destroy();
    vk->destroy_sampler(*samp_r);
    vk->destroy_texture_view(*view_r);
    vk->destroy_texture(*tex_r);
}

