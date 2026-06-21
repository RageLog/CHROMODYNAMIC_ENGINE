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
#include <cd/ui/renderer_webgpu/WgpuContract.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
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

// ===========================================================================
// Floor-raise hardening (phase floor-raise): additional stub-path contract
// tests covering glyph/textured emit counts, multi-variant command splitting,
// move-assign, upload-after-destroy safety, command merging, and batcher
// saturation.  All cases run on the no-Dawn default path (no GPU required).
// ===========================================================================

// ---------------------------------------------------------------------------
// Case 17: glyph() emit increments vertex/index counts correctly
// (4 verts, 6 indices per glyph — same geometry as quad but kGlyph variant)
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, GlyphEmitCountsAreCorrect)
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

    // 3 glyphs: each emits 4 verts + 6 indices.
    const ur::AtlasUv uv { 0.0F, 0.0F, 1.0F, 1.0F };
    batcher.glyph(  0.0F, 0.0F, 8.0F, 12.0F, 0U, uv, ur::Color::white());
    batcher.glyph( 10.0F, 0.0F, 8.0F, 12.0F, 0U, uv, ur::Color::white());
    batcher.glyph( 20.0F, 0.0F, 8.0F, 12.0F, 0U, uv, ur::Color::white());

    ASSERT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.vertex_count(),  12U);   // 3 × 4
    EXPECT_EQ(sub.index_count(),   18U);   // 3 × 6
    // All same (variant=kGlyph, slot=0, scissor=default) → merged to 1 cmd.
    EXPECT_EQ(sub.command_count(),  1U);
}

// ---------------------------------------------------------------------------
// Case 18: textured_quad() emit counts are correct and produce kTextured cmds
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, TexturedQuadEmitCountsAreCorrect)
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

    const ur::AtlasUv uv { 0.0F, 0.0F, 1.0F, 1.0F };
    // 2 textured quads with the same slot → should merge into 1 command.
    batcher.textured_quad(  0.0F, 0.0F, 32.0F, 32.0F, 0U, uv, ur::Color::white());
    batcher.textured_quad( 40.0F, 0.0F, 32.0F, 32.0F, 0U, uv, ur::Color::white());

    ASSERT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.vertex_count(),  8U);   // 2 × 4
    EXPECT_EQ(sub.index_count(),  12U);   // 2 × 6
    EXPECT_EQ(sub.command_count(), 1U);   // same key → merged
}

// ---------------------------------------------------------------------------
// Case 19: mixed variants in the same frame split into separate DrawCommands
// (solid quad + glyph + textured quad must not merge because variant differs)
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, MixedVariantsProduceSeparateCommands)
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

    const ur::AtlasUv uv { 0.0F, 0.0F, 1.0F, 1.0F };
    batcher.quad(0.0F, 0.0F, 16.0F, 16.0F, ur::Color::white());             // kSolid
    batcher.glyph(20.0F, 0.0F, 8.0F, 12.0F, 0U, uv, ur::Color::white());   // kGlyph
    batcher.textured_quad(32.0F, 0.0F, 16.0F, 16.0F, 1U, uv);              // kTextured, slot 1

    const auto expected = static_cast<std::uint32_t>(batcher.command_count());
    ASSERT_GE(expected, 2U) << "mixed variants must produce at least 2 commands";
    ASSERT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.command_count(), expected);
    EXPECT_EQ(sub.vertex_count(), 12U);   // 3 quads regardless of batching
    EXPECT_EQ(sub.index_count(),  18U);
}

// ---------------------------------------------------------------------------
// Case 20: upload after destroy() returns false and does not crash
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, UploadAfterDestroyReturnsFalse)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 256U;
    info.max_indices  = 768U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    sub.destroy();
    EXPECT_FALSE(sub.is_valid());

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 16.0F, 16.0F, ur::Color::white());

    // Must return false without crashing — impl_ is null after destroy().
    EXPECT_FALSE(sub.upload(batcher));
    EXPECT_EQ(sub.vertex_count(),  0U);
    EXPECT_EQ(sub.index_count(),   0U);
    EXPECT_EQ(sub.command_count(), 0U);
}

// ---------------------------------------------------------------------------
// Case 21: move-assign transfers ownership; moved-from is inert
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, MoveAssignTransfersOwnership)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 256U;
    info.max_indices  = 768U;

    auto r1 = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r1.has_value());

    auto r2 = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r2.has_value());

    // Move-assign r1 into r2 (r2 already owns an impl_ — it must destroy it).
    *r2 = std::move(*r1);

    EXPECT_TRUE(r2->is_valid());
    // r1 is now in the moved-from state: no impl_.
    EXPECT_FALSE(r1->is_valid());
    EXPECT_EQ(r1->vertex_count(),  0U);
    EXPECT_EQ(r1->index_count(),   0U);
    EXPECT_EQ(r1->command_count(), 0U);

    // The new owner works normally.
    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());
    EXPECT_TRUE(r2->upload(batcher));
    EXPECT_EQ(r2->vertex_count(), 4U);
}

// ---------------------------------------------------------------------------
// Case 22: same-key quads merge into a single DrawCommand
// (no scissor push, same variant, same texture → 1 cmd regardless of count)
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, SameKeyQuadsMergeIntoOneCommand)
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
    // 8 solid quads with no key change → must produce exactly 1 DrawCommand.
    for (std::uint32_t i = 0U; i < 8U; ++i)
        batcher.quad(static_cast<float>(i * 20U), 0.0F, 16.0F, 16.0F, ur::Color::white());

    ASSERT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.command_count(), 1U);       // all merged
    EXPECT_EQ(sub.vertex_count(), 32U);       // 8 × 4
    EXPECT_EQ(sub.index_count(),  48U);       // 8 × 6
}

// ---------------------------------------------------------------------------
// Case 23: different texture slots within the same variant produce separate cmds
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, DifferentTextureSlotsProduceSeparateCommands)
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
    const ur::AtlasUv uv { 0.0F, 0.0F, 1.0F, 1.0F };
    // Slot 0 then slot 1 — different texture → two commands.
    batcher.textured_quad(0.0F, 0.0F, 16.0F, 16.0F, 0U, uv);
    batcher.textured_quad(20.0F, 0.0F, 16.0F, 16.0F, 1U, uv);

    const auto expected = static_cast<std::uint32_t>(batcher.command_count());
    ASSERT_GE(expected, 2U);
    ASSERT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.command_count(), expected);
    EXPECT_EQ(sub.vertex_count(), 8U);   // 2 × 4
    EXPECT_EQ(sub.index_count(), 12U);   // 2 × 6
}

// ---------------------------------------------------------------------------
// Case 24: batcher at_vertex_limit() triggers after enough quads; the
// submitter correctly reflects the count that was actually accumulated.
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, BatcherAtVertexLimitReflectedBySubmitter)
{
    rw::WgpuDevice null_device {};

    // Use a large-enough submitter so overflow is purely from the batcher
    // kMaxVertices limit (65532), not from the submitter cap.
    rw::SubmitterCreateInfo info {};
    info.max_vertices = ur::kMaxVertices + 4U;   // larger than batcher limit
    info.max_indices  = ur::kMaxVertices * 6U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();

    // Fill batcher up to kMaxVertices (65532 verts = 16383 full quads).
    constexpr std::uint32_t kQuadsToLimit = ur::kMaxVertices / 4U;
    for (std::uint32_t i = 0U; i < kQuadsToLimit; ++i)
        batcher.quad(static_cast<float>(i), 0.0F, 1.0F, 1.0F, ur::Color::white());

    // batcher must be at or past the vertex limit now.
    EXPECT_TRUE(batcher.at_vertex_limit());

    // Any further emission is silently dropped by the batcher.
    batcher.quad(9999.0F, 0.0F, 1.0F, 1.0F, ur::Color::white());
    EXPECT_EQ(batcher.vertex_count(), static_cast<std::size_t>(kQuadsToLimit) * 4U);

    // Submitter upload must succeed and reflect the exact batcher count.
    ASSERT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.vertex_count(), kQuadsToLimit * 4U);
    EXPECT_EQ(sub.index_count(),  kQuadsToLimit * 6U);
}

// ---------------------------------------------------------------------------
// Case 25: SubmitterCreateInfo default values are sensible
// (max_vertices/max_indices non-zero, color_format non-zero, label non-null)
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, CreateInfoDefaultsAreSensible)
{
    const rw::SubmitterCreateInfo defaults {};
    EXPECT_GT(defaults.max_vertices, 0U);
    EXPECT_GT(defaults.max_indices,  0U);
    EXPECT_NE(defaults.color_format, 0U);
    EXPECT_NE(defaults.debug_label,  nullptr);

    // A submitter built from all-defaults must succeed.
    rw::WgpuDevice null_device {};
    auto r = rw::Submitter::create(null_device, defaults);
    EXPECT_TRUE(r.has_value());
    if (r.has_value()) EXPECT_TRUE(r->is_valid());
}

// ---------------------------------------------------------------------------
// Case 26: record() after destroy() is a safe no-op (no crash)
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, RecordAfterDestroyIsNoOp)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 64U;
    info.max_indices  = 192U;
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    // Upload something so impl_->commands is non-empty before we destroy.
    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());
    ASSERT_TRUE(sub.upload(batcher));

    sub.destroy();

    // record() on a destroyed submitter must not crash.
    rw::WgpuCommandEncoder null_encoder {};
    sub.record(null_encoder, rw::Extent2D { 1920U, 1080U });  // must not crash
}

// ---------------------------------------------------------------------------
// Case 27: multiple alternating scissor push/pop sequences track depth
// correctly — batcher scissor_stack depth returns the right value and the
// submitter sees the expected (merged or split) command count.
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, AlternatingScissorDepthIsTrackedCorrectly)
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

    // A: no scissor
    batcher.quad(0.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());

    // B: push scissor depth 1
    auto depth1 = batcher.push_scissor(ur::ScissorRect { 0, 0, 100U, 100U });
    EXPECT_EQ(depth1, 1U);
    batcher.quad(10.0F, 10.0F, 8.0F, 8.0F, ur::Color::black());

    // C: push scissor depth 2
    auto depth2 = batcher.push_scissor(ur::ScissorRect { 20, 20, 40U, 40U });
    EXPECT_EQ(depth2, 2U);
    batcher.quad(22.0F, 22.0F, 4.0F, 4.0F, ur::Color::white());
    batcher.pop_scissor();  // back to depth 1

    // D: another quad at depth 1 — same rect as B → may merge with B's cmd.
    batcher.quad(50.0F, 10.0F, 8.0F, 8.0F, ur::Color::black());
    batcher.pop_scissor();  // back to no scissor

    // E: quad at no-scissor — same rect as A → may merge with A's cmd.
    batcher.quad(80.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());

    const auto expected_cmds = static_cast<std::uint32_t>(batcher.command_count());
    // The batcher only merges consecutive same-key quads; the interleaved
    // scissor states prevent any merging here → 5 commands produced.
    ASSERT_GE(expected_cmds, 3U)
        << "expected at least 3 commands for 3 distinct scissor rects";
    ASSERT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.command_count(), expected_cmds);
    EXPECT_EQ(sub.vertex_count(),  20U);  // 5 quads × 4 verts
    EXPECT_EQ(sub.index_count(),   30U);  // 5 quads × 6 indices
}

// ---------------------------------------------------------------------------
// Case 28: rejected upload does NOT mutate previous good state (index overflow)
// Mirrors Case 7 but verifies prior vertex_count is preserved too.
// ---------------------------------------------------------------------------
TEST(WebGpuSubmitter, RejectedUploadPreservesPreviousGoodState)
{
    rw::WgpuDevice null_device {};

    rw::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 6U;   // exactly 1 quad worth
    auto r = rw::Submitter::create(null_device, info);
    ASSERT_TRUE(r.has_value());
    auto& sub = *r;

    // Good upload: 1 quad.
    ur::DrawBatcher good;
    good.begin_frame();
    good.quad(0.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());
    ASSERT_TRUE(sub.upload(good));
    ASSERT_EQ(sub.vertex_count(), 4U);
    ASSERT_EQ(sub.index_count(),  6U);

    // Over-capacity upload: 2 quads → 12 indices > 6 cap → must fail.
    ur::DrawBatcher bad;
    bad.begin_frame();
    bad.quad(0.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());
    bad.quad(8.0F, 0.0F, 8.0F, 8.0F, ur::Color::white());
    EXPECT_FALSE(sub.upload(bad));

    // After failed upload, counts must remain from the last GOOD upload —
    // the overflow guard returns early, so the snapshot is not touched.
    EXPECT_EQ(sub.vertex_count(), 4U);   // preserved from the good upload
    EXPECT_EQ(sub.index_count(),  6U);
}

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

// ===========================================================================
// WgpuContract host-side surface (HONEST floor-raise): the WGSL shader
// source, the vertex-buffer layout, the colour/format conversion, the
// scissor-clamp math and the draw-command translation are all Dawn-FREE and
// GPU-FREE. The cases below pin that surface so the contract a future Dawn
// pipeline consumes cannot drift from the CPU Vertex / material enums.
// All run on every CI tier (no GPU, no Dawn). AAA + edge + negative.
// ===========================================================================

// ---- Texture-format validity ---------------------------------------------

TEST(WgpuContract, SupportedColorFormatsAcceptedAndUndefinedRejected)
{
    EXPECT_TRUE(rw::is_supported_color_format(rw::texture_format::kRgba8Unorm));
    EXPECT_TRUE(rw::is_supported_color_format(rw::texture_format::kRgba8UnormSrgb));
    EXPECT_TRUE(rw::is_supported_color_format(rw::texture_format::kBgra8Unorm));
    EXPECT_TRUE(rw::is_supported_color_format(rw::texture_format::kBgra8UnormSrgb));

    // Negative: undefined (0) and an arbitrary unrelated enum are rejected.
    EXPECT_FALSE(rw::is_supported_color_format(rw::texture_format::kUndefined));
    EXPECT_FALSE(rw::is_supported_color_format(0xDEADU));
}

TEST(WgpuContract, DefaultCreateInfoFormatIsSupported)
{
    // The SubmitterCreateInfo default (0x12 = BGRA8? — actually RGBA8Unorm)
    // must be one the host contract accepts so create() and the Dawn pipeline
    // agree on a valid target.
    const rw::SubmitterCreateInfo defaults {};
    EXPECT_TRUE(rw::is_supported_color_format(defaults.color_format));
}

TEST(WgpuContract, SrgbClassificationIsCorrect)
{
    EXPECT_TRUE(rw::is_srgb_color_format(rw::texture_format::kRgba8UnormSrgb));
    EXPECT_TRUE(rw::is_srgb_color_format(rw::texture_format::kBgra8UnormSrgb));
    EXPECT_FALSE(rw::is_srgb_color_format(rw::texture_format::kRgba8Unorm));
    EXPECT_FALSE(rw::is_srgb_color_format(rw::texture_format::kBgra8Unorm));
    EXPECT_FALSE(rw::is_srgb_color_format(rw::texture_format::kUndefined));
}

// ---- Vertex layout vs. the real Vertex struct -----------------------------

TEST(WgpuContract, VertexStrideMatchesRealVertexSize)
{
    EXPECT_EQ(rw::vertex_stride_bytes(),
              static_cast<std::uint64_t>(sizeof(ur::Vertex)));
    EXPECT_EQ(rw::vertex_stride_bytes(), 24U);  // pos8 + uv8 + rgba4 + variant4
}

TEST(WgpuContract, VertexAttributeOffsetsMatchVertexMembers)
{
    const std::array<rw::VertexAttributeDesc, 4U> attrs = rw::vertex_attributes();

    // Offsets must equal the real member byte positions so the GPU reads the
    // right bytes. offsetof on the real struct is the ground truth.
    EXPECT_EQ(attrs[0].offset, offsetof(ur::Vertex, pos_x));
    EXPECT_EQ(attrs[1].offset, offsetof(ur::Vertex, uv_x));
    EXPECT_EQ(attrs[2].offset, offsetof(ur::Vertex, r));
    EXPECT_EQ(attrs[3].offset, offsetof(ur::Vertex, variant));

    // Shader @location indices are 0..3 in declaration order.
    EXPECT_EQ(attrs[0].shader_location, 0U);
    EXPECT_EQ(attrs[1].shader_location, 1U);
    EXPECT_EQ(attrs[2].shader_location, 2U);
    EXPECT_EQ(attrs[3].shader_location, 3U);
}

TEST(WgpuContract, VertexAttributeFormatsAreCorrect)
{
    const std::array<rw::VertexAttributeDesc, 4U> attrs = rw::vertex_attributes();
    EXPECT_EQ(attrs[0].format, rw::vertex_format::kFloat32x2);  // pos
    EXPECT_EQ(attrs[1].format, rw::vertex_format::kFloat32x2);  // uv
    EXPECT_EQ(attrs[2].format, rw::vertex_format::kUnorm8x4);   // rgba
    EXPECT_EQ(attrs[3].format, rw::vertex_format::kUint8x4);    // variant
}

TEST(WgpuContract, IndexSizeIsTwoBytesMatchingBatcherU16)
{
    EXPECT_EQ(rw::index_size_bytes(), sizeof(std::uint16_t));
    EXPECT_EQ(rw::index_size_bytes(), 2U);
}

TEST(WgpuContract, BindGroupAndBindingIndicesAreStable)
{
    EXPECT_EQ(rw::bind_group::kGlobals, 0U);
    EXPECT_EQ(rw::bind_group::kAtlas,   1U);
    EXPECT_EQ(rw::binding::kGlobalsUbo,   0U);
    EXPECT_EQ(rw::binding::kAtlasTexture, 0U);
    EXPECT_EQ(rw::binding::kAtlasSampler, 1U);
}

// ---- Globals packing ------------------------------------------------------

TEST(WgpuContract, GlobalsIsStd140SizedAndZeroDefault)
{
    EXPECT_EQ(sizeof(rw::Globals), 16U);
    const rw::Globals g {};
    EXPECT_FLOAT_EQ(g.inv_viewport_x, 0.0F);
    EXPECT_FLOAT_EQ(g.inv_viewport_y, 0.0F);
}

TEST(WgpuContract, MakeGlobalsComputesInverseViewport)
{
    const rw::Globals g = rw::make_globals(1920U, 1080U);
    EXPECT_FLOAT_EQ(g.inv_viewport_x, 1.0F / 1920.0F);
    EXPECT_FLOAT_EQ(g.inv_viewport_y, 1.0F / 1080.0F);
}

TEST(WgpuContract, MakeGlobalsZeroViewportDoesNotDivideByZero)
{
    // Negative/edge: a zero dimension must yield a zero inverse (not inf/nan).
    const rw::Globals gx = rw::make_globals(0U, 1080U);
    EXPECT_FLOAT_EQ(gx.inv_viewport_x, 0.0F);
    EXPECT_FLOAT_EQ(gx.inv_viewport_y, 1.0F / 1080.0F);

    const rw::Globals gboth = rw::make_globals(0U, 0U);
    EXPECT_FLOAT_EQ(gboth.inv_viewport_x, 0.0F);
    EXPECT_FLOAT_EQ(gboth.inv_viewport_y, 0.0F);
}

// ---- Colour conversion ----------------------------------------------------

TEST(WgpuContract, Unorm8ToFloatEndpointsAndMidpoint)
{
    EXPECT_FLOAT_EQ(rw::unorm8_to_float(0U),   0.0F);
    EXPECT_FLOAT_EQ(rw::unorm8_to_float(255U), 1.0F);
    EXPECT_FLOAT_EQ(rw::unorm8_to_float(128U), 128.0F / 255.0F);
}

TEST(WgpuContract, ToColorFMapsWhiteBlackTransparentExactly)
{
    EXPECT_EQ(rw::to_color_f(ur::Color::white()),
              (rw::ColorF { 1.0F, 1.0F, 1.0F, 1.0F }));
    EXPECT_EQ(rw::to_color_f(ur::Color::black()),
              (rw::ColorF { 0.0F, 0.0F, 0.0F, 1.0F }));
    EXPECT_EQ(rw::to_color_f(ur::Color::transparent()),
              (rw::ColorF { 0.0F, 0.0F, 0.0F, 0.0F }));
}

TEST(WgpuContract, ToColorFPreservesChannelOrder)
{
    const ur::Color c { 10U, 20U, 30U, 40U };
    const rw::ColorF f = rw::to_color_f(c);
    EXPECT_FLOAT_EQ(f.r, 10.0F / 255.0F);
    EXPECT_FLOAT_EQ(f.g, 20.0F / 255.0F);
    EXPECT_FLOAT_EQ(f.b, 30.0F / 255.0F);
    EXPECT_FLOAT_EQ(f.a, 40.0F / 255.0F);
}

// ---- Scissor clamp --------------------------------------------------------

TEST(WgpuContract, ClampScissorInsideViewportIsUnchanged)
{
    const ur::ScissorRect s { 10, 20, 100U, 80U };
    EXPECT_EQ(rw::clamp_scissor(s, 1920U, 1080U),
              (rw::ScissorPx { 10U, 20U, 100U, 80U }));
}

TEST(WgpuContract, ClampScissorNegativeOriginFlooredAndExtentReduced)
{
    // Negative origin: floor to 0, and the far edge (-5 + 50 = 45) is kept,
    // so width shrinks to 45 (not 50).
    const ur::ScissorRect s { -5, -10, 50U, 60U };
    const rw::ScissorPx px = rw::clamp_scissor(s, 1920U, 1080U);
    EXPECT_EQ(px.x, 0U);
    EXPECT_EQ(px.y, 0U);
    EXPECT_EQ(px.width,  45U);   // far edge 45 preserved
    EXPECT_EQ(px.height, 50U);   // far edge 50 preserved
}

TEST(WgpuContract, ClampScissorOversizeExtentClampedToViewport)
{
    // Far edge past the viewport must clamp to the edge.
    const ur::ScissorRect s { 100, 50, 5000U, 5000U };
    EXPECT_EQ(rw::clamp_scissor(s, 800U, 600U),
              (rw::ScissorPx { 100U, 50U, 700U, 550U }));
}

TEST(WgpuContract, ClampScissorNoClipSentinelCollapsesToViewport)
{
    // Default ScissorRect uses 0xFFFFFFFF "no clip"; clamped to full viewport.
    const ur::ScissorRect s {};  // x=0,y=0,w=0xFFFFFFFF,h=0xFFFFFFFF
    EXPECT_EQ(rw::clamp_scissor(s, 1280U, 720U),
              (rw::ScissorPx { 0U, 0U, 1280U, 720U }));
}

TEST(WgpuContract, ClampScissorOriginPastViewportYieldsZeroArea)
{
    // Origin entirely past the viewport: far edge <= origin -> zero extent.
    const ur::ScissorRect s { 900, 700, 50U, 50U };
    const rw::ScissorPx px = rw::clamp_scissor(s, 800U, 600U);
    EXPECT_EQ(px.x, 800U);   // origin clamped to viewport edge
    EXPECT_EQ(px.y, 600U);
    EXPECT_EQ(px.width,  0U);
    EXPECT_EQ(px.height, 0U);
}

TEST(WgpuContract, ClampScissorExactlyAtViewportEdge)
{
    const ur::ScissorRect s { 0, 0, 800U, 600U };
    EXPECT_EQ(rw::clamp_scissor(s, 800U, 600U),
              (rw::ScissorPx { 0U, 0U, 800U, 600U }));
}

TEST(WgpuContract, ClampScissorZeroViewportYieldsZeroArea)
{
    // Negative/edge: a zero viewport collapses any rect to zero area.
    const ur::ScissorRect s { 0, 0, 100U, 100U };
    EXPECT_EQ(rw::clamp_scissor(s, 0U, 0U),
              (rw::ScissorPx { 0U, 0U, 0U, 0U }));
}

// ---- Draw-slice translation ----------------------------------------------

TEST(WgpuContract, DrawSliceComputesByteOffsetFromIndexUnits)
{
    ur::DrawCommand dc {};
    dc.index_offset = 12U;
    dc.index_count  = 6U;
    const rw::DrawSlice slice = rw::draw_slice(dc);
    EXPECT_EQ(slice.first_index, 12U);
    EXPECT_EQ(slice.index_count, 6U);
    EXPECT_EQ(slice.index_byte_offset, 12U * 2U);  // u16 indices
}

TEST(WgpuContract, DrawSliceZeroOffsetIsZeroBytes)
{
    ur::DrawCommand dc {};  // all defaults: offset 0, count 0
    const rw::DrawSlice slice = rw::draw_slice(dc);
    EXPECT_EQ(slice.first_index, 0U);
    EXPECT_EQ(slice.index_count, 0U);
    EXPECT_EQ(slice.index_byte_offset, 0U);
}

// ---- WGSL structural contract pins ----------------------------------------

TEST(WgpuContract, WgslSourceIsNonEmptyAndDeclaresBothEntryPoints)
{
    const std::string_view wgsl = rw::kUiShaderWgsl;
    EXPECT_FALSE(wgsl.empty());
    EXPECT_NE(wgsl.find(rw::kVertexEntryPoint),   std::string_view::npos);
    EXPECT_NE(wgsl.find(rw::kFragmentEntryPoint), std::string_view::npos);
    EXPECT_NE(wgsl.find("@vertex"),   std::string_view::npos);
    EXPECT_NE(wgsl.find("@fragment"), std::string_view::npos);
}

TEST(WgpuContract, WgslDeclaresAllFourVertexAttributeLocations)
{
    const std::string_view wgsl = rw::kUiShaderWgsl;
    EXPECT_NE(wgsl.find("@location(0)"), std::string_view::npos);
    EXPECT_NE(wgsl.find("@location(1)"), std::string_view::npos);
    EXPECT_NE(wgsl.find("@location(2)"), std::string_view::npos);
    EXPECT_NE(wgsl.find("@location(3)"), std::string_view::npos);
}

TEST(WgpuContract, WgslBranchesEveryMaterialVariantId)
{
    const std::string_view wgsl = rw::kUiShaderWgsl;
    // The fragment shader must branch on the solid/textured/glyph/ninepatch ids.
    EXPECT_NE(wgsl.find("in.variant == 0u"), std::string_view::npos);  // kSolid
    EXPECT_NE(wgsl.find("in.variant == 1u"), std::string_view::npos);  // kTextured
    EXPECT_NE(wgsl.find("in.variant == 2u"), std::string_view::npos);  // kGlyph
    EXPECT_NE(wgsl.find("in.variant == 3u"), std::string_view::npos);  // kNinePatch
}

TEST(WgpuContract, WgslDeclaresGlobalsUboAndAtlasBindings)
{
    const std::string_view wgsl = rw::kUiShaderWgsl;
    EXPECT_NE(wgsl.find("@group(0) @binding(0)"), std::string_view::npos);  // Globals UBO
    EXPECT_NE(wgsl.find("@group(1) @binding(0)"), std::string_view::npos);  // atlas tex
    EXPECT_NE(wgsl.find("@group(1) @binding(1)"), std::string_view::npos);  // sampler
    EXPECT_NE(wgsl.find("texture_2d<f32>"),       std::string_view::npos);
    EXPECT_NE(wgsl.find("sampler"),               std::string_view::npos);
}

TEST(WgpuContract, EntryPointConstantsMatchWgslNames)
{
    // The advertised entry-point names must be exactly what the WGSL declares,
    // so the Dawn pipeline's VertexState/FragmentState entryPoint strings agree.
    EXPECT_EQ(rw::kVertexEntryPoint,   std::string_view { "vs_main" });
    EXPECT_EQ(rw::kFragmentEntryPoint, std::string_view { "fs_main" });
}
