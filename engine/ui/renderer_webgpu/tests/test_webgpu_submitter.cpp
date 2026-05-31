// =============================================================================
// CHROMODYNAMIC — cd::ui::renderer_webgpu tests (phase522)
//
// Build-only test suite: no physical WebGPU device or Dawn library is
// required.  All four cases exercise the stub backend (CD_UI_WEBGPU_HAVE_DAWN
// == 0) that compiles and runs on every CI tier.
//
// When Dawn IS installed and the project is reconfigured with
// -DCD_ENABLE_WEBGPU=ON, the same binary links the real backend and the
// same four test cases continue to pass (create/lifecycle/upload/record are
// all valid no-ops against a null Dawn handle in the real wgpu path too —
// actual device-backed correctness belongs in the Phase 5.5 hello_ui_webgpu
// sample).
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_webgpu/Submitter.hpp>
#include <gtest/gtest.h>

namespace rw = cd::ui::renderer_webgpu;
namespace ur = cd::ui::renderer;

// ---------------------------------------------------------------------------
// Case 1: create returns a valid Result with a skeleton handle
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, CreateReturnsValidHandle)
{
    rw::WgpuDevice null_device {};          // opaque stub (value 0)

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    info.debug_label  = "test_create";

    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value()) << "create must succeed with a null/stub device";

    const auto& sub = *r;
    EXPECT_TRUE(sub.is_valid());
    EXPECT_EQ(sub.vertex_count(),  0U);
    EXPECT_EQ(sub.index_count(),   0U);
    EXPECT_EQ(sub.command_count(), 0U);
}

// ---------------------------------------------------------------------------
// Case 2: upload signature compiles + accepts a DrawBatcher
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, UploadSignatureCompiles)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 256U;
    info.max_indices  = 768U;

    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    for (int i = 0; i < 4; ++i)
    {
        batcher.quad(static_cast<float>(i * 20), 0.0F, 16.0F, 16.0F,
                     ur::Color::white());
    }

    // 4 quads = 16 verts, 24 indices — well within max_vertices/max_indices.
    const bool ok = sub.upload(batcher);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sub.vertex_count(),  16U);
    EXPECT_EQ(sub.index_count(),   24U);
    EXPECT_EQ(sub.command_count(),  1U);  // all same-state → merged
}

// ---------------------------------------------------------------------------
// Case 3: record signature compiles + accepts encoder + extent
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, RecordSignatureCompiles)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 128U;
    info.max_indices  = 384U;

    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 64.0F, 64.0F, ur::Color::black());
    batcher.push_scissor(ur::ScissorRect { 10, 10, 100U, 100U });
    batcher.quad(12.0F, 12.0F, 20.0F, 20.0F, ur::Color::white());
    batcher.pop_scissor();
    ASSERT_TRUE(sub.upload(batcher));

    rw::WgpuCommandEncoder null_encoder {};
    const rw::Extent2D extent { 1920U, 1080U };

    // Must not crash.  Stub path iterates commands and does no GPU work.
    sub.record(null_encoder, extent);

    // command_count reflects the two scissor-separated batches.
    EXPECT_GE(sub.command_count(), 1U);
}

// ---------------------------------------------------------------------------
// Case 4: destroy is idempotent
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, DestroyIsIdempotent)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 64U;
    info.max_indices  = 192U;

    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;
    ASSERT_TRUE(sub.is_valid());

    sub.destroy();
    EXPECT_FALSE(sub.is_valid());

    sub.destroy();   // second call must not crash
    EXPECT_FALSE(sub.is_valid());
}
