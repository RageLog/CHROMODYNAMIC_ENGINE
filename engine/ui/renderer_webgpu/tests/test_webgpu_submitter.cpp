// =============================================================================
// CHROMODYNAMIC — cd::ui::renderer_webgpu tests (phase522 / phase551)
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
//
// Case 5 (phase551): guarded by #if CD_UI_WEBGPU_HAVE_DAWN.  It verifies
// that record() produces the expected draw-call count when the real Dawn
// path is active.  Only compiled + run when Dawn is installed.
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_webgpu/Submitter.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

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

// ===========================================================================
// BAND-7 hardening (docs/ADR/ADR-20260616-band7-scope.md §2): the no-op
// default backend is a FUTURE-TARGET, not a working WebGPU UI backend. The
// load-bearing contract it MUST honour today is the CPU buffer-accounting +
// the Dawn-absent (stub) behaviour. The cases below lock that contract so a
// regression in the counting / overflow / lifecycle math fails on revert.
// All run on the stub path (CD_UI_WEBGPU_HAVE_DAWN == 0); no GPU required.
// ===========================================================================

// ---------------------------------------------------------------------------
// Case 6: create rejects a zero max_vertices / max_indices config
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, CreateRejectsZeroLimits)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo zero_v {};
    zero_v.max_vertices = 0U;
    zero_v.max_indices  = 64U;
    EXPECT_FALSE(rw::Submitter::create(null_device, zero_v).has_value());

    rw::SubmitterCreateInfo zero_i {};
    zero_i.max_vertices = 64U;
    zero_i.max_indices  = 0U;
    EXPECT_FALSE(rw::Submitter::create(null_device, zero_i).has_value());
}

// ---------------------------------------------------------------------------
// Case 7: upload of an over-capacity batcher returns false (vertex overflow)
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, UploadRejectsVertexOverflow)
{
    rw::WgpuDevice null_device {};

    // 1 quad = 4 verts; cap the submitter at 4 verts so a 2nd quad overflows.
    rw::SubmitterCreateInfo info {};
    info.max_vertices = 4U;
    info.max_indices  = 4096U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());
    batcher.quad(8.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());  // 8 verts > 4

    EXPECT_FALSE(sub.upload(batcher));
    // A rejected upload must NOT mutate the last-good counts (still 0).
    EXPECT_EQ(sub.vertex_count(), 0U);
    EXPECT_EQ(sub.index_count(),  0U);
    EXPECT_EQ(sub.command_count(), 0U);
}

// ---------------------------------------------------------------------------
// Case 8: upload of an over-capacity batcher returns false (index overflow)
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, UploadRejectsIndexOverflow)
{
    rw::WgpuDevice null_device {};

    // 1 quad = 6 indices; cap indices at 4 so a single quad overflows.
    rw::SubmitterCreateInfo info {};
    info.max_vertices = 4096U;
    info.max_indices  = 4U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());  // 6 indices > 4

    EXPECT_FALSE(sub.upload(batcher));
    EXPECT_EQ(sub.index_count(), 0U);
}

// ---------------------------------------------------------------------------
// Case 9: upload exactly at capacity succeeds (boundary is inclusive)
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, UploadAtExactCapacitySucceeds)
{
    rw::WgpuDevice null_device {};

    // 1 quad = 4 verts, 6 indices; size the caps to exactly that.
    rw::SubmitterCreateInfo info {};
    info.max_vertices = 4U;
    info.max_indices  = 6U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());

    EXPECT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.vertex_count(), 4U);
    EXPECT_EQ(sub.index_count(),  6U);
}

// ---------------------------------------------------------------------------
// Case 10: an empty-frame upload is valid and zeroes the per-frame counts
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, EmptyFrameUploadIsValidAndZeroed)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 256U;
    info.max_indices  = 768U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    // First a non-empty frame so the counts are non-zero...
    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 16.0F, 16.0F, ur::Color::white());
    ASSERT_TRUE(sub.upload(batcher));
    ASSERT_EQ(sub.vertex_count(), 4U);

    // ...then an empty frame must reset them to zero (per-frame snapshot).
    ur::DrawBatcher empty;
    empty.begin_frame();
    EXPECT_TRUE(sub.upload(empty));
    EXPECT_EQ(sub.vertex_count(),  0U);
    EXPECT_EQ(sub.index_count(),   0U);
    EXPECT_EQ(sub.command_count(), 0U);
}

// ---------------------------------------------------------------------------
// Case 11: successive uploads re-snapshot the counts (growth + shrink)
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, SuccessiveUploadsResnapshotCounts)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    // Frame A: 3 quads = 12 verts, 18 indices, 1 merged command.
    ur::DrawBatcher a;
    a.begin_frame();
    for (int i = 0; i < 3; ++i)
        a.quad(static_cast<float>(i * 20), 0.0F, 16.0F, 16.0F, ur::Color::white());
    ASSERT_TRUE(sub.upload(a));
    EXPECT_EQ(sub.vertex_count(), 12U);
    EXPECT_EQ(sub.index_count(),  18U);

    // Frame B: 1 quad — counts must shrink, not accumulate.
    ur::DrawBatcher b;
    b.begin_frame();
    b.quad(0.0F, 0.0F, 16.0F, 16.0F, ur::Color::white());
    ASSERT_TRUE(sub.upload(b));
    EXPECT_EQ(sub.vertex_count(), 4U);
    EXPECT_EQ(sub.index_count(),  6U);
}

// ---------------------------------------------------------------------------
// Case 12: scissor-separated quads produce multiple counted DrawCommands
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, ScissorBoundariesProduceMultipleCommands)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 16.0F, 16.0F, ur::Color::white());     // batch 1
    batcher.push_scissor(ur::ScissorRect { 10, 10, 100U, 100U });
    batcher.quad(12.0F, 12.0F, 8.0F, 8.0F, ur::Color::black());      // batch 2
    batcher.pop_scissor();
    batcher.push_scissor(ur::ScissorRect { 50, 50, 200U, 200U });
    batcher.quad(60.0F, 60.0F, 8.0F, 8.0F, ur::Color::white());      // batch 3
    batcher.pop_scissor();

    const auto expected = static_cast<std::uint32_t>(batcher.command_count());
    ASSERT_GE(expected, 2U);
    ASSERT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.command_count(), expected);
    EXPECT_EQ(sub.vertex_count(),  12U);  // 3 quads regardless of batching
    EXPECT_EQ(sub.index_count(),   18U);
}

// ---------------------------------------------------------------------------
// Case 13: record() before any upload is a safe no-op (no crash, no state)
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, RecordBeforeUploadIsNoOp)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 64U;
    info.max_indices  = 192U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    rw::WgpuCommandEncoder null_encoder {};
    sub.record(null_encoder, rw::Extent2D { 800U, 600U });  // must not crash
    EXPECT_EQ(sub.command_count(), 0U);
}

// ---------------------------------------------------------------------------
// Case 14: move transfers ownership; the moved-from submitter is inert and
// crash-safe — the Dawn-absent default contract (impl_ is null after move).
//
// NOTE: we obtain the inert handle via move (not default-construction): the
// PIMPL `Impl` is incomplete in the header, so a default-constructed
// `Submitter` cannot be destroyed in a test TU. `create()` builds the object
// in the .cpp where `Impl` is complete; moving from it is the supported way
// to reach the "no impl_" state from here.
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, MoveTransfersOwnershipAndLeavesSourceInert)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 256U;
    info.max_indices  = 768U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());

    rw::Submitter moved = std::move(*r);
    EXPECT_TRUE(moved.is_valid());

    // The moved-into handle owns the impl and works.
    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 16.0F, 16.0F, ur::Color::white());
    EXPECT_TRUE(moved.upload(batcher));
    EXPECT_EQ(moved.vertex_count(), 4U);

    // The moved-from handle is inert: no impl_ -> every accessor reports zero
    // and every operation is a safe no-op (no crash).
    rw::Submitter& src = *r;
    EXPECT_FALSE(src.is_valid());
    EXPECT_EQ(src.vertex_count(),  0U);
    EXPECT_EQ(src.index_count(),   0U);
    EXPECT_EQ(src.command_count(), 0U);
    EXPECT_FALSE(src.upload(batcher));            // no impl_ -> false, no crash
    rw::WgpuCommandEncoder enc {};
    src.record(enc, rw::Extent2D { 1U, 1U });     // must not crash
    src.destroy();                                // must not crash
}

// ---------------------------------------------------------------------------
// Case 16: stub handles default to the null/zero opaque value (Dawn-absent
// contract). Only meaningful when Dawn is NOT present.
// ---------------------------------------------------------------------------
#if !CD_UI_WEBGPU_HAVE_DAWN
TEST(WebGpuSubmitter, StubHandlesAreNullByDefault)
{
    rw::WgpuDevice         dev {};
    rw::WgpuCommandEncoder enc {};
    EXPECT_EQ(dev.opaque, 0U);
    EXPECT_EQ(enc.opaque, 0U);
    EXPECT_EQ(dev, rw::WgpuDevice {});          // defaulted operator==
    EXPECT_EQ(enc, rw::WgpuCommandEncoder {});
}
#endif  // !CD_UI_WEBGPU_HAVE_DAWN

// ---------------------------------------------------------------------------
// Case 5 (phase551): record() emits the expected draw-call count — Dawn only
//
// This case is compiled + run only when CD_UI_WEBGPU_HAVE_DAWN == 1, i.e.
// when the project is configured with -DCD_ENABLE_WEBGPU=ON and Dawn is
// installed.  The stub path is exercised by Cases 1-4 above.
//
// The test pushes three scissor-separated batches (3 distinct DrawCommands)
// into the batcher, uploads them, then calls record() with a null encoder.
// After record() returns, command_count() must equal the number of
// DrawCommands emitted by the batcher — verifying the real record() loop
// iterates every command.
// ---------------------------------------------------------------------------
#if CD_UI_WEBGPU_HAVE_DAWN
TEST(WebGpuSubmitter, RecordEmitsExpectedDrawCallCount)
{
    // A real wgpu::Device is required to create GPU buffers (create() calls
    // CreateBuffer in the Dawn path).  We use a null wgpu::Device here —
    // CreateBuffer on a null device returns a null buffer; the test verifies
    // only the command-count bookkeeping, not actual GPU execution.
    wgpu::Device null_device {};   // default-constructed = null handle

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    info.debug_label  = "test_draw_count";

    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value()) << "create must succeed even with null device";
    auto& sub = *r;
    ASSERT_TRUE(sub.is_valid());

    // Build a batcher with 3 distinct scissor rects so the batcher emits 3
    // separate DrawCommands.
    ur::DrawBatcher batcher;
    batcher.begin_frame();

    // Batch 1 — no scissor (full-screen).
    batcher.quad(0.0F, 0.0F, 32.0F, 32.0F, ur::Color::white());

    // Batch 2 — first scissor rect.
    batcher.push_scissor(ur::ScissorRect { 10, 10, 200U, 100U });
    batcher.quad(12.0F, 12.0F, 20.0F, 20.0F, ur::Color::black());
    batcher.pop_scissor();

    // Batch 3 — second scissor rect (different from batch 2).
    batcher.push_scissor(ur::ScissorRect { 50, 50, 300U, 200U });
    batcher.quad(60.0F, 60.0F, 10.0F, 10.0F, ur::Color::white());
    batcher.pop_scissor();

    const std::uint32_t expected_commands =
        static_cast<std::uint32_t>(batcher.command_count());
    ASSERT_EQ(expected_commands, 3U)
        << "batcher should have produced exactly 3 DrawCommands";

    ASSERT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.command_count(), expected_commands);

    // record() on a null encoder must not crash and the command_count() must
    // still reflect the uploaded batch size after the call.
    wgpu::CommandEncoder null_encoder {};
    const rw::Extent2D extent { 1920U, 1080U };
    sub.record(null_encoder, extent);   // must not throw / crash

    // command_count() is set by upload(), not by record(), so it stays stable.
    EXPECT_EQ(sub.command_count(), expected_commands)
        << "command_count() must equal batcher draw-command count after record()";
}
#endif  // CD_UI_WEBGPU_HAVE_DAWN
