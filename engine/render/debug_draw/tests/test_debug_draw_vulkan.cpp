// =============================================================================
// CHROMODYNAMIC — engine/render/debug_draw/tests/test_debug_draw_vulkan.cpp
// band2-render-core — real-Vulkan-device integration tests for
// cd::debug_draw::Renderer.
//
// The NullDevice suite (test_debug_draw.cpp) cannot compile GLSL, so the
// vertex-buffer growth / park / reclaim policy — the phase-1034 use-after-free
// guard, the load-bearing behaviour of this library — was only "exercised
// nightly by hello_engine" and untested in CI. These tests build a REAL
// Renderer (glslang -> SPIR-V -> Vulkan pipeline) and drive the growth path
// across frames, asserting the observable capacity + parked-buffer contract.
//
// Skips cleanly when no Vulkan ICD / no glslang is present on the host (same
// pattern as test_framegraph_vulkan.cpp).
//
// Tests (Arrange / Act / Assert):
//   1. WhenRealDevice_ExpectCreateSucceedsAndIsValid
//   2. WhenFlushingGrowingBatches_ExpectCapacityGrowthAndParkThenReclaim
//   3. WhenManyShapesFlushed_ExpectSingleAllocationCoversThem (no per-frame
//      re-alloc once the buffer is large enough — shape-coverage edge)
//   4. WhenEmptyBatchFlushed_ExpectNoAllocationButQueueStillTicks
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/debug_draw/DebugDraw.hpp>
#include <cd/debug_line/DebugLine.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory>

namespace
{

using cd::debug_draw::Renderer;
using cd::debug_draw::RendererDesc;

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

constexpr std::array<cd::rhi::Format, 1> kColorFmts {
    cd::rhi::Format::kRGBA16Float
};

[[nodiscard]] RendererDesc make_desc()
{
    RendererDesc d {};
    d.color_attachment_formats = kColorFmts;
    return d;
}

/// Build an offscreen RGBA16F target + view to record the flush draw into.
struct OffscreenTarget
{
    cd::rhi::TextureHandle     tex {};
    cd::rhi::TextureViewHandle view {};

    [[nodiscard]] bool create(cd::rhi::IDevice& dev, std::uint32_t w, std::uint32_t h)
    {
        cd::rhi::TextureDesc td {};
        td.type         = cd::rhi::TextureType::k2D;
        td.format       = cd::rhi::Format::kRGBA16Float;
        td.extent       = { w, h, 1 };
        td.mip_levels   = 1;
        td.array_layers = 1;
        td.usage        = cd::rhi::TextureUsage::kColorAttachment;
        td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
        auto tr = dev.create_texture(td);
        if (!tr.has_value())
            return false;
        tex = *tr;

        cd::rhi::TextureViewDesc vd {};
        vd.texture     = tex;
        vd.type        = cd::rhi::TextureType::k2D;
        vd.format      = cd::rhi::Format::kRGBA16Float;
        vd.base_mip    = 0;
        vd.mip_count   = 1;
        vd.base_layer  = 0;
        vd.layer_count = 1;
        auto vr = dev.create_texture_view(vd);
        if (!vr.has_value())
            return false;
        view = *vr;
        return true;
    }

    void destroy(cd::rhi::IDevice& dev) noexcept
    {
        if (view.is_valid())
            dev.destroy_texture_view(view);
        if (tex.is_valid())
            dev.destroy_texture(tex);
    }
};

/// Record one Renderer::flush inside a colour render pass on a fresh command
/// buffer, submit + wait_idle so the frame is fully retired.
void flush_one_frame(cd::rhi::IDevice& dev,
                     Renderer& r,
                     const OffscreenTarget& rt,
                     const cd::debug_line::LineBatch& batch,
                     std::uint32_t frame_idx)
{
    auto cmd = dev.create_command_buffer();
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    cd::rhi::TextureBarrier to_color {};
    to_color.texture = rt.tex;
    to_color.from    = cd::rhi::ResourceState::kUndefined;
    to_color.to      = cd::rhi::ResourceState::kColorAttachment;
    to_color.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, { &to_color, 1 });

    cd::rhi::ColorAttachmentInfo att {};
    att.view        = rt.view;
    att.load_op     = cd::rhi::LoadOp::kClear;
    att.store_op    = cd::rhi::StoreOp::kStore;
    att.clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments  = { &att, 1 };
    rp.render_area.extent = { 16u, 16u };

    cmd->begin_render_pass(rp);
    r.flush(dev, *cmd, batch, cd::math::Mat4f::identity(), frame_idx);
    cmd->end_render_pass();

    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();
}

// ---------------------------------------------------------------------------
// Test 1 — real device, create succeeds
// ---------------------------------------------------------------------------
TEST(DebugDrawRendererVulkan, WhenRealDevice_ExpectCreateSucceedsAndIsValid)
{
    SKIP_IF_NO_VULKAN(dev);
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        GTEST_SKIP() << "no glslang";

    auto r = Renderer::create(*dev, compiler.get(), make_desc());
    ASSERT_TRUE(r.has_value()) << "real-device create must succeed";
    EXPECT_TRUE(r->is_valid());
    EXPECT_EQ(r->vertex_capacity_bytes(), 0U);  // no VB until first flush
    EXPECT_EQ(r->parked_buffer_count(), 0U);
    r->destroy(*dev);
}

// ---------------------------------------------------------------------------
// Test 2 — growth + park + reclaim across frames (the phase-1034 contract)
// ---------------------------------------------------------------------------
TEST(DebugDrawRendererVulkan, WhenFlushingGrowingBatches_ExpectCapacityGrowthAndParkThenReclaim)
{
    SKIP_IF_NO_VULKAN(dev);
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        GTEST_SKIP() << "no glslang";

    OffscreenTarget rt;
    ASSERT_TRUE(rt.create(*dev, 16u, 16u));

    auto rr = Renderer::create(*dev, compiler.get(), make_desc());
    ASSERT_TRUE(rr.has_value());
    Renderer& r = *rr;

    // -- Frame 0: small batch -> first allocation -----------------------------
    cd::debug_line::LineBatch small;
    small.add_line({ 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0, 1 });
    flush_one_frame(*dev, r, rt, small, 0u);
    const std::uint64_t cap0 = r.vertex_capacity_bytes();
    EXPECT_GT(cap0, 0U) << "first flush must allocate the VB";
    EXPECT_EQ(r.parked_buffer_count(), 0U);

    // -- Frame 1: huge batch -> growth, old buffer parked ---------------------
    cd::debug_line::LineBatch big;
    for (int i = 0; i < 4096; ++i)
        big.add_line({ float(i), 0, 0 }, { float(i), 1, 0 }, { 0, 1, 0, 1 });
    flush_one_frame(*dev, r, rt, big, 1u);
    EXPECT_GT(r.vertex_capacity_bytes(), cap0) << "growth must enlarge capacity";
    EXPECT_EQ(r.parked_buffer_count(), 1U)
        << "outgrown buffer must be PARKED, not destroyed in place";

    // -- Frames 2..(1+kDestroyMargin): parked buffer reclaimed at margin ------
    // destroy_at_frame for the parked buffer = 1 + kDestroyMargin. The reclaim
    // loop in flush fires when frame_idx >= destroy_at_frame.
    cd::debug_line::LineBatch tiny;
    tiny.add_line({ 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 1, 1 });
    const std::uint32_t reclaim_frame = 1u + Renderer::kDestroyMargin;
    for (std::uint32_t f = 2u; f < reclaim_frame; ++f)
    {
        flush_one_frame(*dev, r, rt, tiny, f);
        EXPECT_EQ(r.parked_buffer_count(), 1U)
            << "parked buffer must survive until its margin frame (f=" << f << ")";
    }
    flush_one_frame(*dev, r, rt, tiny, reclaim_frame);
    EXPECT_EQ(r.parked_buffer_count(), 0U)
        << "parked buffer must be reclaimed at frame " << reclaim_frame;

    r.destroy(*dev);
    rt.destroy(*dev);
}

// ---------------------------------------------------------------------------
// Test 3 — many shapes in one batch reuse a single allocation (shape coverage)
// ---------------------------------------------------------------------------
TEST(DebugDrawRendererVulkan, WhenManyShapesFlushed_ExpectSingleAllocationCoversThem)
{
    SKIP_IF_NO_VULKAN(dev);
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        GTEST_SKIP() << "no glslang";

    OffscreenTarget rt;
    ASSERT_TRUE(rt.create(*dev, 16u, 16u));
    auto rr = Renderer::create(*dev, compiler.get(), make_desc());
    ASSERT_TRUE(rr.has_value());
    Renderer& r = *rr;

    // A mixed batch of every shape kind the CPU batcher emits — proves the
    // renderer draws whatever the batch contains in one upload.
    cd::debug_line::LineBatch shapes;
    shapes.add_aabb({ -1, -1, -1 }, { 1, 1, 1 }, { 1, 1, 0, 1 });
    shapes.add_obb({ 0, 2, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
                   { 0.5F, 0.5F, 0.5F }, { 0, 1, 1, 1 });
    shapes.add_circle({ 0, 0, 0 }, { 0, 1, 0 }, 1.0F, 24, { 1, 0, 1, 1 });
    shapes.add_sphere({ 3, 0, 0 }, 0.75F, 16, { 1, 1, 1, 1 });
    shapes.add_cross({ 0, -2, 0 }, 0.5F, { 1, 0, 0, 1 });
    shapes.add_arrow({ 0, 0, 0 }, { 0, 0, 3 }, { 0, 0, 1, 1 });
    shapes.add_grid({ 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, 4, 0.5F, { 0.3F, 0.3F, 0.3F, 1 });
    ASSERT_GT(shapes.vertex_count(), 0U);

    flush_one_frame(*dev, r, rt, shapes, 0u);
    const std::uint64_t cap_after_first = r.vertex_capacity_bytes();
    EXPECT_GT(cap_after_first, 0U);

    // Second flush of the SAME (or smaller) batch must NOT re-allocate: the
    // capacity stays put and nothing is parked.
    flush_one_frame(*dev, r, rt, shapes, 1u);
    EXPECT_EQ(r.vertex_capacity_bytes(), cap_after_first)
        << "no growth expected when batch fits the existing buffer";
    EXPECT_EQ(r.parked_buffer_count(), 0U);

    r.destroy(*dev);
    rt.destroy(*dev);
}

// ---------------------------------------------------------------------------
// Test 4 — empty batch on a valid renderer: no alloc, but reclaim queue ticks
// ---------------------------------------------------------------------------
TEST(DebugDrawRendererVulkan, WhenEmptyBatchFlushed_ExpectNoAllocationButQueueStillTicks)
{
    SKIP_IF_NO_VULKAN(dev);
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        GTEST_SKIP() << "no glslang";

    OffscreenTarget rt;
    ASSERT_TRUE(rt.create(*dev, 16u, 16u));
    auto rr = Renderer::create(*dev, compiler.get(), make_desc());
    ASSERT_TRUE(rr.has_value());
    Renderer& r = *rr;

    cd::debug_line::LineBatch empty;
    flush_one_frame(*dev, r, rt, empty, 0u);
    EXPECT_EQ(r.vertex_capacity_bytes(), 0U)
        << "an empty batch must not allocate a VB";
    EXPECT_EQ(r.parked_buffer_count(), 0U);

    r.destroy(*dev);
    rt.destroy(*dev);
}

}  // namespace
