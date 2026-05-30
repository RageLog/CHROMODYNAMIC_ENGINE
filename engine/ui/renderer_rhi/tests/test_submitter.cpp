// =============================================================================
// CHROMODYNAMIC — cd::ui::renderer_rhi tests
//
// Phase 1.2b. Validates the Submitter lifecycle + upload-overflow guard
// against NullDevice. End-to-end GPU correctness (pipeline + actual
// draws) is covered by the Phase 1.5 hello_ui sample on a real Vulkan
// device.
// =============================================================================
#include <cd/rhi/NullDevice.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>
#include <gtest/gtest.h>

namespace rh = cd::ui::renderer_rhi;
namespace ur = cd::ui::renderer;

TEST(Submitter, CreateAllocatesValidBuffers)
{
    cd::rhi::NullDevice dev;
    rh::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;

    auto r = rh::Submitter::create(dev, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;
    EXPECT_TRUE(sub.is_valid());
    EXPECT_EQ(sub.vertex_count(),  0U);
    EXPECT_EQ(sub.index_count(),   0U);
    EXPECT_EQ(sub.command_count(), 0U);
}

TEST(Submitter, ZeroCapacityRejected)
{
    cd::rhi::NullDevice dev;
    rh::SubmitterCreateInfo info {};
    info.max_vertices = 0U;
    info.max_indices  = 100U;
    auto r = rh::Submitter::create(dev, info);
    EXPECT_FALSE(r.has_value());
}

TEST(Submitter, DestroyIsIdempotent)
{
    cd::rhi::NullDevice dev;
    rh::SubmitterCreateInfo info {};
    info.max_vertices = 64U;
    info.max_indices  = 64U;
    auto r = rh::Submitter::create(dev, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    sub.destroy();
    EXPECT_FALSE(sub.is_valid());
    sub.destroy();  // second call is safe
    EXPECT_FALSE(sub.is_valid());
}

TEST(Submitter, UploadBelowCapacitySucceeds)
{
    cd::rhi::NullDevice dev;
    rh::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    auto r = rh::Submitter::create(dev, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    for (int i = 0; i < 5; ++i)
    {
        batcher.quad(static_cast<float>(i * 50), 10.0F, 40.0F, 40.0F,
                     ur::Color::white());
    }

    EXPECT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.vertex_count(),  20U);   // 5 quads × 4 verts
    EXPECT_EQ(sub.index_count(),   30U);   // 5 quads × 6 indices
    EXPECT_EQ(sub.command_count(), 1U);    // all merged (same state)
}

TEST(Submitter, UploadOverflowRejected)
{
    cd::rhi::NullDevice dev;
    rh::SubmitterCreateInfo info {};
    info.max_vertices = 12U;   // ~3 quads worth
    info.max_indices  = 18U;
    auto r = rh::Submitter::create(dev, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    for (int i = 0; i < 10; ++i)   // 40 verts, 60 indices — way over cap
    {
        batcher.quad(static_cast<float>(i * 50), 10.0F, 40.0F, 40.0F,
                     ur::Color::white());
    }

    EXPECT_FALSE(sub.upload(batcher));   // overflow returns false, no partial state
}

TEST(Submitter, RecordRunsWithoutCrashOnNullDevice)
{
    cd::rhi::NullDevice dev;
    rh::SubmitterCreateInfo info {};
    info.max_vertices = 256U;
    info.max_indices  = 512U;
    auto r = rh::Submitter::create(dev, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 100.0F, 100.0F, ur::Color::white());
    batcher.push_scissor(ur::ScissorRect { 0, 0, 200U, 200U });
    batcher.quad(10.0F, 10.0F, 50.0F, 50.0F, ur::Color::black());
    batcher.pop_scissor();
    EXPECT_TRUE(sub.upload(batcher));

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    sub.record(*cmd, { 800U, 600U });
    cmd->end();
    // No assertion on the recorded commands themselves — NullDevice
    // doesn't expose introspection. Smoke-tests that the call sequence
    // doesn't crash + the API surface is callable end-to-end.
}
