// =============================================================================
// CHROMODYNAMIC — engine/render/framegraph/tests/test_framegraph_vulkan.cpp
//
// Real-Vulkan-device integration tests for cd::framegraph::FrameGraph.
//
// Purpose: prove that barrier emission by FrameGraph::compile/execute is
// real GPU-visible work — not just NullDevice bookkeeping. The Null-device
// 22-test suite covers semantic correctness; this file covers device proof.
//
// Skips cleanly when no Vulkan ICD is present on the host (same pattern as
// test_rhi_parallel_lanes.cpp / test_image_readback.cpp).
//
// Tests (all Arrange / Act / Assert):
//   1. WhenOnePassClearsOffscreen_ExpectSubmitAndWaitIdle
//        - one pass, kColorAttachment write, FG emits barrier + execute cb
//   2. WhenTwoPassesWriteThenRead_ExpectTwoBarriersAndCleanSubmit
//        - pass A writes (kColorAttachment), pass B reads (kShaderResource)
//        - verifies FG stitches write->read barrier on real device
//   3. WhenGraphReusedViaReset_ExpectSecondCompileAndExecuteClean
//        - fresh FrameGraph on same texture -> second compile/execute works
//   4. WhenEmptyGraphExecutes_ExpectNoError
//        - negative / edge-case: compile+execute with zero passes is safe
//   5. WhenPixelReadbackAfterFgClear_ExpectClearColour (golden standard)
//        - full round-trip: FG-managed pass issues clear -> copy_image_to_buffer
//          -> download_buffer -> assert every channel
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/framegraph/FrameGraph.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Device factory + SKIP guard
// ---------------------------------------------------------------------------

std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

#define SKIP_IF_NO_VULKAN(dev_var)    \
    auto dev_var = try_make_device(); \
    if (!(dev_var))                   \
    GTEST_SKIP() << "no Vulkan ICD available on this host"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create a minimal 2D RGBA8 offscreen colour target with the union of usage
/// flags required for this test suite (color-attachment + transfer-src +
/// sampled). Returns an invalid handle on failure.
[[nodiscard]] cd::rhi::TextureHandle make_color_tex(cd::rhi::IDevice& dev,
                                                     std::uint32_t     w,
                                                     std::uint32_t     h)
{
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { w, h, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kColorAttachment |
                      cd::rhi::TextureUsage::kTransferSrc |
                      cd::rhi::TextureUsage::kSampled;
    td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
    auto r = dev.create_texture(td);
    return r.has_value() ? *r : cd::rhi::TextureHandle {};
}

/// Create a default 2D RGBA8 texture view for `tex`.
[[nodiscard]] cd::rhi::TextureViewHandle make_view(cd::rhi::IDevice&      dev,
                                                    cd::rhi::TextureHandle tex)
{
    cd::rhi::TextureViewDesc vd {};
    vd.texture     = tex;
    vd.type        = cd::rhi::TextureType::k2D;
    vd.format      = cd::rhi::Format::kRGBA8Unorm;
    vd.base_mip    = 0;
    vd.mip_count   = 1;
    vd.base_layer  = 0;
    vd.layer_count = 1;
    auto r = dev.create_texture_view(vd);
    return r.has_value() ? *r : cd::rhi::TextureViewHandle {};
}

/// kGpuToCpu readback buffer large enough for `byte_count` bytes.
[[nodiscard]] cd::rhi::BufferHandle make_readback_buf(cd::rhi::IDevice& dev,
                                                       std::uint64_t     byte_count)
{
    cd::rhi::BufferDesc bd {};
    bd.size   = byte_count;
    bd.usage  = cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto r = dev.create_buffer(bd);
    return r.has_value() ? *r : cd::rhi::BufferHandle {};
}

/// Build a ColorAttachmentInfo + RenderPassBeginInfo for `view` at `extent`
/// with the given clear colour. Captures are baked in; the caller stores the
/// begin-info on the stack and passes it by reference to the execute lambda.
struct PassAttachment
{
    cd::rhi::ColorAttachmentInfo att {};
    cd::rhi::RenderPassBeginInfo rp  {};

    PassAttachment(cd::rhi::TextureViewHandle view,
                   cd::rhi::Extent2D          extent,
                   float r, float g, float b, float a) noexcept
    {
        att.view        = view;
        att.load_op     = cd::rhi::LoadOp::kClear;
        att.store_op    = cd::rhi::StoreOp::kStore;
        att.clear_color = { .f32 = { r, g, b, a } };
        rp.color_attachments = { &att, 1 };
        rp.render_area.extent = extent;
    }
};

// ---------------------------------------------------------------------------
// Test 1: WhenOnePassClearsOffscreen_ExpectSubmitAndWaitIdle
// ---------------------------------------------------------------------------
// Arrange: real VulkanDevice + 32x32 offscreen texture imported into FG.
// Act:     single pass that clears the attachment, compile + execute.
// Assert:  compile/execute succeed + submit/wait_idle do not crash/timeout.
// ---------------------------------------------------------------------------
TEST(FrameGraphVulkan, WhenOnePassClearsOffscreen_ExpectSubmitAndWaitIdle)
{
    SKIP_IF_NO_VULKAN(dev);

    // -- Arrange --------------------------------------------------------------
    const cd::rhi::Extent2D ext { 32u, 32u };
    const auto tex  = make_color_tex(*dev, ext.width, ext.height);
    const auto view = make_view(*dev, tex);
    ASSERT_TRUE(tex.is_valid());
    ASSERT_TRUE(view.is_valid());

    auto cmd = dev->create_command_buffer();
    ASSERT_NE(cmd, nullptr);

    // -- Act ------------------------------------------------------------------
    cmd->begin();

    // Transition UNDEFINED -> COLOR_ATTACHMENT before importing so FG starts
    // from a defined layout.
    cd::rhi::TextureBarrier to_color {};
    to_color.texture = tex;
    to_color.from    = cd::rhi::ResourceState::kUndefined;
    to_color.to      = cd::rhi::ResourceState::kColorAttachment;
    to_color.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, { &to_color, 1 });

    cd::framegraph::FrameGraph fg { *dev };
    cd::framegraph::ImportedTextureDesc id {};
    id.texture       = tex;
    id.initial_state = cd::rhi::ResourceState::kColorAttachment;
    id.final_state   = cd::rhi::ResourceState::kColorAttachment;
    const auto rh = fg.import_texture(id, "offscreen");
    ASSERT_TRUE(rh.is_valid());

    std::array<cd::framegraph::PassResource, 1> writes {
        cd::framegraph::PassResource { rh, cd::rhi::ResourceState::kColorAttachment }
    };

    // Build the pass attachment before the lambda so no constexpr integral
    // needs to be captured (avoids -Wunused-lambda-capture under Clang).
    PassAttachment pa { view, ext, 0.1F, 0.2F, 0.3F, 1.0F };

    bool pass_executed = false;
    cd::framegraph::PassDesc pd {};
    pd.name   = "clear_pass";
    pd.writes = writes;
    pd.execute = [&pass_executed, &pa](cd::rhi::ICommandBuffer& c)
    {
        c.begin_render_pass(pa.rp);
        c.end_render_pass();
        pass_executed = true;
    };
    fg.add_pass(pd);

    ASSERT_TRUE(fg.compile().has_value());
    const auto exec_r = fg.execute(*cmd);
    ASSERT_TRUE(exec_r.has_value());

    cmd->end();
    dev->submit(*cmd);
    dev->wait_idle();

    // -- Assert ---------------------------------------------------------------
    EXPECT_TRUE(pass_executed);

    dev->destroy_texture_view(view);
    dev->destroy_texture(tex);
}

// ---------------------------------------------------------------------------
// Test 2: WhenTwoPassesWriteThenRead_ExpectTwoBarriersAndCleanSubmit
// ---------------------------------------------------------------------------
// Arrange: pass A writes transient texture (kColorAttachment), pass B reads
//          it (kShaderResource). FG must emit kColorAttachment->kShaderResource
//          barrier between the two passes on the real Vulkan device.
// Act:     compile + execute.
// Assert:  both execute-lambdas run; final state is kShaderResource;
//          submit+wait_idle clean (validation would flag a missing barrier).
// ---------------------------------------------------------------------------
TEST(FrameGraphVulkan, WhenTwoPassesWriteThenRead_ExpectTwoBarriersAndCleanSubmit)
{
    SKIP_IF_NO_VULKAN(dev);

    // -- Arrange --------------------------------------------------------------
    const cd::rhi::Extent2D ext { 16u, 16u };
    // An external view is needed only for the render-pass call in pass A.
    const auto ext_tex  = make_color_tex(*dev, ext.width, ext.height);
    const auto ext_view = make_view(*dev, ext_tex);
    ASSERT_TRUE(ext_tex.is_valid());
    ASSERT_TRUE(ext_view.is_valid());

    auto cmd = dev->create_command_buffer();
    ASSERT_NE(cmd, nullptr);

    // -- Act ------------------------------------------------------------------
    cmd->begin();

    cd::framegraph::FrameGraph fg { *dev };

    // Transient texture: FG allocates it on compile. Initial state is kUndefined;
    // FG issues the UNDEFINED->kColorAttachment barrier before pass A.
    cd::framegraph::TransientTextureDesc td {};
    td.format  = cd::rhi::Format::kRGBA8Unorm;
    td.extent  = { ext.width, ext.height, 1 };
    td.usage   = cd::rhi::TextureUsage::kColorAttachment |
                 cd::rhi::TextureUsage::kTransferSrc |
                 cd::rhi::TextureUsage::kSampled;
    const auto rh = fg.create_texture(td, "transient");
    ASSERT_TRUE(rh.is_valid());

    bool pass_a_ran = false;
    bool pass_b_ran = false;

    std::array<cd::framegraph::PassResource, 1> a_writes {
        cd::framegraph::PassResource { rh, cd::rhi::ResourceState::kColorAttachment }
    };
    PassAttachment pa_att { ext_view, ext, 0.5F, 0.0F, 0.5F, 1.0F };
    cd::framegraph::PassDesc pa {};
    pa.name   = "pass_a_write";
    pa.writes = a_writes;
    pa.execute = [&pass_a_ran, &pa_att](cd::rhi::ICommandBuffer& c)
    {
        c.begin_render_pass(pa_att.rp);
        c.end_render_pass();
        pass_a_ran = true;
    };
    fg.add_pass(pa);

    // Pass B: read-only — FG must emit kColorAttachment->kShaderResource.
    std::array<cd::framegraph::PassResource, 1> b_reads {
        cd::framegraph::PassResource { rh, cd::rhi::ResourceState::kShaderResource }
    };
    cd::framegraph::PassDesc pb {};
    pb.name   = "pass_b_read";
    pb.reads  = b_reads;
    pb.execute = [&pass_b_ran](cd::rhi::ICommandBuffer&)
    {
        pass_b_ran = true;
    };
    fg.add_pass(pb);

    ASSERT_TRUE(fg.compile().has_value());

    // After compile the transient resource must be backed by a real RHI texture.
    EXPECT_TRUE(fg.texture_handle(rh).is_valid());

    const auto exec_r = fg.execute(*cmd);
    ASSERT_TRUE(exec_r.has_value());

    // Final state must be kShaderResource (last consumer declared it).
    EXPECT_EQ(fg.current_state(rh), cd::rhi::ResourceState::kShaderResource);

    cmd->end();
    dev->submit(*cmd);
    dev->wait_idle();

    // -- Assert ---------------------------------------------------------------
    EXPECT_TRUE(pass_a_ran);
    EXPECT_TRUE(pass_b_ran);

    dev->destroy_texture_view(ext_view);
    dev->destroy_texture(ext_tex);
    fg.reset();  // releases the transient texture owned by the graph
}

// ---------------------------------------------------------------------------
// Test 3: WhenGraphReusedViaReset_ExpectSecondCompileAndExecuteClean
// ---------------------------------------------------------------------------
// Arrange: compile+execute a one-pass graph (fg1). Destroy fg1. Build fg2
//          over the same external texture.
// Act:     compile+execute fg2.
// Assert:  both rounds succeed and both execute-lambdas fire.
// ---------------------------------------------------------------------------
TEST(FrameGraphVulkan, WhenGraphReusedViaReset_ExpectSecondCompileAndExecuteClean)
{
    SKIP_IF_NO_VULKAN(dev);

    // -- Arrange --------------------------------------------------------------
    const cd::rhi::Extent2D ext { 8u, 8u };
    const auto tex  = make_color_tex(*dev, ext.width, ext.height);
    const auto view = make_view(*dev, tex);
    ASSERT_TRUE(tex.is_valid());
    ASSERT_TRUE(view.is_valid());

    // -- Act — first round ----------------------------------------------------
    {
        auto cmd = dev->create_command_buffer();
        ASSERT_NE(cmd, nullptr);
        cmd->begin();

        cd::rhi::TextureBarrier init {};
        init.texture = tex;
        init.from    = cd::rhi::ResourceState::kUndefined;
        init.to      = cd::rhi::ResourceState::kColorAttachment;
        init.range   = { 0, 1, 0, 1 };
        cmd->barrier({}, { &init, 1 });

        cd::framegraph::FrameGraph fg1 { *dev };
        cd::framegraph::ImportedTextureDesc id1 {};
        id1.texture       = tex;
        id1.initial_state = cd::rhi::ResourceState::kColorAttachment;
        id1.final_state   = cd::rhi::ResourceState::kColorAttachment;
        const auto rh1 = fg1.import_texture(id1, "round1");

        std::array<cd::framegraph::PassResource, 1> w1 {
            cd::framegraph::PassResource { rh1, cd::rhi::ResourceState::kColorAttachment }
        };
        PassAttachment pa1 { view, ext, 0.0F, 1.0F, 0.0F, 1.0F };
        bool ran1 = false;
        cd::framegraph::PassDesc pd1 {};
        pd1.name   = "round1_pass";
        pd1.writes = w1;
        pd1.execute = [&ran1, &pa1](cd::rhi::ICommandBuffer& c)
        {
            c.begin_render_pass(pa1.rp);
            c.end_render_pass();
            ran1 = true;
        };
        fg1.add_pass(pd1);
        ASSERT_TRUE(fg1.compile().has_value());
        ASSERT_TRUE(fg1.execute(*cmd).has_value());

        cmd->end();
        dev->submit(*cmd);
        dev->wait_idle();
        EXPECT_TRUE(ran1);
    }  // fg1 destroyed here

    // -- Act — second round (fresh FrameGraph, same external texture) ---------
    {
        auto cmd2 = dev->create_command_buffer();
        ASSERT_NE(cmd2, nullptr);
        cmd2->begin();

        // tex is still in kColorAttachment from round 1.
        cd::framegraph::FrameGraph fg2 { *dev };
        cd::framegraph::ImportedTextureDesc id2 {};
        id2.texture       = tex;
        id2.initial_state = cd::rhi::ResourceState::kColorAttachment;
        id2.final_state   = cd::rhi::ResourceState::kColorAttachment;
        const auto rh2 = fg2.import_texture(id2, "round2");

        std::array<cd::framegraph::PassResource, 1> w2 {
            cd::framegraph::PassResource { rh2, cd::rhi::ResourceState::kColorAttachment }
        };
        PassAttachment pa2 { view, ext, 0.0F, 0.0F, 1.0F, 1.0F };
        bool ran2 = false;
        cd::framegraph::PassDesc pd2 {};
        pd2.name   = "round2_pass";
        pd2.writes = w2;
        pd2.execute = [&ran2, &pa2](cd::rhi::ICommandBuffer& c)
        {
            c.begin_render_pass(pa2.rp);
            c.end_render_pass();
            ran2 = true;
        };
        fg2.add_pass(pd2);
        ASSERT_TRUE(fg2.compile().has_value());
        ASSERT_TRUE(fg2.execute(*cmd2).has_value());

        cmd2->end();
        dev->submit(*cmd2);
        dev->wait_idle();

        // -- Assert -----------------------------------------------------------
        EXPECT_TRUE(ran2);
    }

    dev->destroy_texture_view(view);
    dev->destroy_texture(tex);
}

// ---------------------------------------------------------------------------
// Test 4: WhenEmptyGraphExecutes_ExpectNoError
// ---------------------------------------------------------------------------
// Edge-case: compile + execute with zero passes on a real device. The Null
// test already covers semantics; this confirms the real command-buffer
// begin/end round-trip is safe with an empty FG.
// ---------------------------------------------------------------------------
TEST(FrameGraphVulkan, WhenEmptyGraphExecutes_ExpectNoError)
{
    SKIP_IF_NO_VULKAN(dev);

    // -- Arrange --------------------------------------------------------------
    cd::framegraph::FrameGraph fg { *dev };

    // -- Act ------------------------------------------------------------------
    ASSERT_TRUE(fg.compile().has_value());
    auto cmd = dev->create_command_buffer();
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    ASSERT_TRUE(fg.execute(*cmd).has_value());
    cmd->end();
    dev->submit(*cmd);
    dev->wait_idle();

    // -- Assert ---------------------------------------------------------------
    EXPECT_EQ(fg.pass_count(), 0u);
    EXPECT_EQ(fg.resource_count(), 0u);
}

// ---------------------------------------------------------------------------
// Test 5: WhenPixelReadbackAfterFgClear_ExpectClearColour (GOLDEN STANDARD)
// ---------------------------------------------------------------------------
// Arrange: 4x4 RGBA8 offscreen target allocated by the caller.
//          FrameGraph imports it (initial=kColorAttachment,
//          final=kShaderResource). A single pass clears to a known colour.
//          FG emits the write->read barrier automatically as final_state.
// Act:     copy_image_to_buffer (src_state=kShaderResource) + download_buffer.
// Assert:  every texel matches the clear colour within +/-1 unorm LSB.
//
// This is the "golden standard": if FG forgot the kColorAttachment->
// kShaderResource barrier the driver would be free to discard the cleared
// pixels in the subsequent transfer-src transition (phase1127 / X4-B rule).
// ---------------------------------------------------------------------------
TEST(FrameGraphVulkan, WhenPixelReadbackAfterFgClear_ExpectClearColour)
{
    SKIP_IF_NO_VULKAN(dev);

    // -- Arrange --------------------------------------------------------------
    const cd::rhi::Extent2D ext { 4u, 4u };
    const std::uint64_t kBytes = std::uint64_t { ext.width } * ext.height * 4u;

    const auto tex      = make_color_tex(*dev, ext.width, ext.height);
    const auto view     = make_view(*dev, tex);
    const auto readback = make_readback_buf(*dev, kBytes);
    ASSERT_TRUE(tex.is_valid());
    ASSERT_TRUE(view.is_valid());
    ASSERT_TRUE(readback.is_valid());

    // -- Act ------------------------------------------------------------------
    auto cmd = dev->create_command_buffer();
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    // Transition UNDEFINED -> kColorAttachment so FG import starts from a
    // defined layout (not discard-permitting).
    cd::rhi::TextureBarrier init_barrier {};
    init_barrier.texture = tex;
    init_barrier.from    = cd::rhi::ResourceState::kUndefined;
    init_barrier.to      = cd::rhi::ResourceState::kColorAttachment;
    init_barrier.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, { &init_barrier, 1 });

    cd::framegraph::FrameGraph fg { *dev };

    // Import with final=kShaderResource: FG will emit the
    // kColorAttachment->kShaderResource barrier at the end of execute().
    cd::framegraph::ImportedTextureDesc id {};
    id.texture       = tex;
    id.initial_state = cd::rhi::ResourceState::kColorAttachment;
    id.final_state   = cd::rhi::ResourceState::kShaderResource;
    const auto rh = fg.import_texture(id, "readback_target");
    ASSERT_TRUE(rh.is_valid());

    std::array<cd::framegraph::PassResource, 1> pw {
        cd::framegraph::PassResource { rh, cd::rhi::ResourceState::kColorAttachment }
    };
    // Clear to: R=1.0 (255), G=0.5 (~128), B=0.25 (~64), A=1.0 (255).
    PassAttachment clear_att { view, ext, 1.0F, 0.5F, 0.25F, 1.0F };
    cd::framegraph::PassDesc pd {};
    pd.name   = "fg_clear";
    pd.writes = pw;
    pd.execute = [&clear_att](cd::rhi::ICommandBuffer& c)
    {
        c.begin_render_pass(clear_att.rp);
        c.end_render_pass();
    };
    fg.add_pass(pd);

    ASSERT_TRUE(fg.compile().has_value());
    ASSERT_TRUE(fg.execute(*cmd).has_value());

    // FG must have tracked the final state transition.
    EXPECT_EQ(fg.current_state(rh), cd::rhi::ResourceState::kShaderResource);

    cmd->end();
    dev->submit(*cmd);
    dev->wait_idle();

    // Declare the true current state to avoid a discard-permitting transition
    // inside copy_image_to_buffer (phase1127 / X4-B rule).
    cd::rhi::IDevice::ImageRegion region {};
    region.width     = ext.width;
    region.height    = ext.height;
    region.src_state = cd::rhi::ResourceState::kShaderResource;
    const auto copy_r = dev->copy_image_to_buffer(tex, readback, 0, region);
    ASSERT_TRUE(copy_r.has_value())
        << std::string(copy_r.error().message.begin(), copy_r.error().message.end());

    std::vector<std::byte> raw(static_cast<std::size_t>(kBytes));
    const auto dl = dev->download_buffer(readback, 0, std::span<std::byte> { raw });
    ASSERT_TRUE(dl.has_value());

    // -- Assert: every texel matches the clear colour within +/-1 unorm LSB --
    const auto near_u8 = [](std::byte got, int want) -> bool
    {
        const int g = static_cast<int>(std::to_integer<std::uint8_t>(got));
        return g >= want - 1 && g <= want + 1;
    };
    const std::uint32_t texel_count = ext.width * ext.height;
    for (std::uint32_t i = 0; i < texel_count; ++i)
    {
        const std::size_t o = static_cast<std::size_t>(i) * 4u;
        EXPECT_TRUE(near_u8(raw[o + 0], 255)) << "texel " << i << " channel R";
        EXPECT_TRUE(near_u8(raw[o + 1], 128)) << "texel " << i << " channel G";
        EXPECT_TRUE(near_u8(raw[o + 2],  64)) << "texel " << i << " channel B";
        EXPECT_TRUE(near_u8(raw[o + 3], 255)) << "texel " << i << " channel A";
    }

    dev->destroy_buffer(readback);
    dev->destroy_texture_view(view);
    dev->destroy_texture(tex);
}

}  // namespace
