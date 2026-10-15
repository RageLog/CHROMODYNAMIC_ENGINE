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

TEST(Submitter, MultiTextureSlotRegistrationAndAllMaterialVariantsRecord)
{
    cd::rhi::NullDevice dev;
    rh::SubmitterCreateInfo info {};
    info.max_vertices = 512U;
    info.max_indices  = 1024U;
    auto r = rh::Submitter::create(dev, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    cd::rhi::TextureDesc td {};
    td.extent = { 64U, 64U, 1U };
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.usage  = cd::rhi::TextureUsage::kSampled;
    auto tex0 = dev.create_texture(td);
    auto tex1 = dev.create_texture(td);
    ASSERT_TRUE(tex0.has_value() && tex1.has_value());

    cd::rhi::TextureViewDesc vd {};
    vd.texture = *tex0;
    vd.format  = td.format;
    auto view0 = dev.create_texture_view(vd);
    vd.texture = *tex1;
    auto view1 = dev.create_texture_view(vd);
    auto samp  = dev.create_sampler(cd::rhi::SamplerDesc {});
    ASSERT_TRUE(view0.has_value() && view1.has_value() && samp.has_value());

    EXPECT_TRUE(sub.set_texture_slot(0U, *view0, *samp));
    EXPECT_TRUE(sub.set_texture_slot(2U, *view1, *samp));
    EXPECT_EQ(sub.texture_slot_count(), 2U);
    EXPECT_TRUE(sub.has_texture_slot(0U));
    EXPECT_TRUE(sub.has_texture_slot(2U));
    EXPECT_FALSE(sub.has_texture_slot(1U));

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 50.0F, 50.0F, ur::Color::white());
    batcher.nine_patch(10.0F, 10.0F, 80.0F, 60.0F, 0U, ur::NinePatchDesc {});
    batcher.linear_gradient(0.0F, 0.0F, 50.0F, 50.0F, ur::Color::white(), ur::Color::black());
    batcher.backdrop_blur(0.0F, 0.0F, 50.0F, 50.0F, 8.0F);
    batcher.drop_shadow(10.0F, 10.0F, 50.0F, 50.0F, 6.0F);
    batcher.rounded_rect(10.0F, 10.0F, 50.0F, 50.0F, 4.0F, ur::Color::white());
    EXPECT_TRUE(sub.upload(batcher));

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    sub.record(*cmd, { 1280U, 720U });
    cmd->end();
}

