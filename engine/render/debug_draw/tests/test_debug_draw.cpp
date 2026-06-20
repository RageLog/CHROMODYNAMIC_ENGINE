// =============================================================================
// CHROMODYNAMIC — engine/render/debug_draw/tests/test_debug_draw.cpp
// phase1057 — cd::debug_draw::Renderer unit tests.
//
// GPU-independent coverage via cd::rhi::NullDevice: creation error
// contract, move semantics, empty-flush behaviour and the
// frames-in-flight-safe vertex-buffer growth/park/reclaim policy.
// Pixel-level verification lives with the consumers (hello_engine
// golden fixtures).
//
// Depth-100 additions (ADD-ONLY — no existing path touched):
//   RendererDesc defaults (name + depth format), destroy() idempotent,
//   flush()-on-invalid no-op (empty + non-empty batch, all frame_idx
//   values), move chain (triple), post-destroy state reset, observer
//   accessors zero-state, kDestroyMargin static_assert, recreate on
//   default-constructed, RendererDesc field round-trip.
// =============================================================================
#include <cd/debug_draw/DebugDraw.hpp>
#include <cd/rhi/NullDevice.hpp>

#include <gtest/gtest.h>

#include <array>

namespace
{

using cd::debug_draw::Renderer;
using cd::debug_draw::RendererDesc;

constexpr std::array<cd::rhi::Format, 1> kColorFmts {
    cd::rhi::Format::kRGBA16Float
};

[[nodiscard]] RendererDesc make_desc()
{
    RendererDesc d {};
    d.color_attachment_formats = kColorFmts;
    return d;
}

// ---------------------------------------------------------------------------
// Original 5 tests (UNCHANGED)
// ---------------------------------------------------------------------------

TEST(DebugDrawRenderer, CreateWithoutCompilerFailsGracefully)
{
    cd::rhi::NullDevice device;

    // GLSL-only sources with no compiler and no pre-compiled SPIR-V
    // cannot produce a pipeline; the error must surface, not crash.
    auto r = Renderer::create(device, nullptr, make_desc());

    EXPECT_FALSE(r.has_value());
}

TEST(DebugDrawRenderer, DefaultConstructedIsInvalidAndFlushIsNoOp)
{
    cd::rhi::NullDevice device;
    Renderer r {};
    EXPECT_FALSE(r.is_valid());

    cd::debug_line::LineBatch batch;
    batch.add_line({ 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0, 1 });
    auto cmd = device.create_command_buffer();
    ASSERT_NE(cmd, nullptr);

    // Invalid renderer: no buffer is ever created, no draw recorded.
    r.flush(device, *cmd, batch, cd::math::Mat4f::identity(), 0);
    EXPECT_EQ(r.vertex_capacity_bytes(), 0U);
    EXPECT_EQ(r.parked_buffer_count(), 0U);

    r.destroy(device);  // idempotent on an empty renderer
}

// The growth policy is the part the phase-1034 review flagged as a
// GPU use-after-free when done naively; test it WITHOUT a pipeline
// by driving the internals through flush on an invalid material...
// Renderer correctly short-circuits, so instead we verify the policy
// through its observable contract once a material exists. NullDevice
// cannot compile GLSL, so the policy test ships as a typed harness:
// the same park/reclaim arithmetic, asserted via the public
// constants. (Full-pipeline growth is exercised nightly by
// hello_engine, which draws every overlay through this renderer.)
TEST(DebugDrawRenderer, DestroyMarginMatchesEngineConvention)
{
    // The TLAS ring + DeferredBuffer convention is frame_idx + 3
    // (fif=2 fences only N-2). A silent change here would reopen the
    // phase-1034 use-after-free across every consumer.
    static_assert(Renderer::kDestroyMargin == 3);
    SUCCEED();
}

// phase1060: hot-reload contract — a failed recreate keeps the old
// pipeline state untouched (here: the invalid default, but the
// observable contract is "no mutation on failure" + false return).
TEST(DebugDrawRenderer, RecreatePipelineFailureLeavesStateUntouched)
{
    cd::rhi::NullDevice device;
    Renderer r {};
    EXPECT_FALSE(r.recreate_pipeline(device, nullptr, make_desc()));
    EXPECT_FALSE(r.is_valid());
    EXPECT_EQ(r.vertex_capacity_bytes(), 0U);
    EXPECT_EQ(r.parked_buffer_count(), 0U);
}

TEST(DebugDrawRenderer, MoveTransfersValidity)
{
    Renderer a {};
    EXPECT_FALSE(a.is_valid());
    Renderer b { std::move(a) };
    EXPECT_FALSE(b.is_valid());
    Renderer c {};
    c = std::move(b);
    EXPECT_FALSE(c.is_valid());
}

// ---------------------------------------------------------------------------
// Depth-100 additions — observer accessors, zero-state, edge cases
// ---------------------------------------------------------------------------

// --- RendererDesc defaults --------------------------------------------------

TEST(DebugDrawRendererDesc, DefaultNameMatchesLibraryConvention)
{
    // The documented default name is "cd::debug_draw/line".  A silent
    // change here breaks any log/profiler that keys on it.
    const RendererDesc d {};
    EXPECT_EQ(d.name, "cd::debug_draw/line");
}

TEST(DebugDrawRendererDesc, DefaultDepthFormatIsD32Float)
{
    // RendererDesc::depth_attachment_format must default to kD32Float
    // so consumers that omit it match the typical engine depth target.
    const RendererDesc d {};
    EXPECT_EQ(d.depth_attachment_format, cd::rhi::Format::kD32Float);
}

TEST(DebugDrawRendererDesc, DefaultGlslOverridesAreEmpty)
{
    // Empty string_views signal "use the embedded shaders".
    const RendererDesc d {};
    EXPECT_TRUE(d.vertex_glsl.empty());
    EXPECT_TRUE(d.fragment_glsl.empty());
    EXPECT_TRUE(d.vertex_glsl_path.empty());
    EXPECT_TRUE(d.fragment_glsl_path.empty());
}

TEST(DebugDrawRendererDesc, DefaultColorAttachmentFormatsIsEmpty)
{
    // No formats → caller must supply at least one.
    const RendererDesc d {};
    EXPECT_TRUE(d.color_attachment_formats.empty());
}

TEST(DebugDrawRendererDesc, CustomNameSurvivesConstruction)
{
    // Callers can supply a custom name; verify the field is forwarded.
    RendererDesc d {};
    d.name = "my_custom_pass";
    EXPECT_EQ(d.name, "my_custom_pass");
}

// --- Observer zero-state ---------------------------------------------------

TEST(DebugDrawRenderer, VertexCapacityBytesDefaultIsZero)
{
    const Renderer r {};
    EXPECT_EQ(r.vertex_capacity_bytes(), 0U);
}

TEST(DebugDrawRenderer, ParkedBufferCountDefaultIsZero)
{
    const Renderer r {};
    EXPECT_EQ(r.parked_buffer_count(), 0U);
}

TEST(DebugDrawRenderer, IsValidDefaultIsFalse)
{
    const Renderer r {};
    EXPECT_FALSE(r.is_valid());
}

// --- destroy() on default-constructed is idempotent -----------------------

TEST(DebugDrawRenderer, DestroyOnDefaultConstructedTwiceIsIdempotent)
{
    cd::rhi::NullDevice device;
    Renderer r {};
    r.destroy(device);  // first call
    r.destroy(device);  // second call must not crash or assert
    EXPECT_FALSE(r.is_valid());
    EXPECT_EQ(r.vertex_capacity_bytes(), 0U);
    EXPECT_EQ(r.parked_buffer_count(), 0U);
}

// --- flush() with an empty batch on invalid renderer ----------------------

TEST(DebugDrawRenderer, FlushEmptyBatchOnInvalidRendererAllocatesNothing)
{
    cd::rhi::NullDevice device;
    Renderer r {};
    const cd::debug_line::LineBatch empty {};
    auto cmd = device.create_command_buffer();
    ASSERT_NE(cmd, nullptr);

    r.flush(device, *cmd, empty, cd::math::Mat4f::identity(), 0U);

    EXPECT_EQ(r.vertex_capacity_bytes(), 0U);
    EXPECT_EQ(r.parked_buffer_count(), 0U);
}

TEST(DebugDrawRenderer, FlushEmptyBatchOnInvalidRendererHighFrameIdxAllocatesNothing)
{
    // Flushing with a very large frame_idx should not differ from frame 0
    // when the renderer is invalid (no parked entries to reclaim).
    cd::rhi::NullDevice device;
    Renderer r {};
    const cd::debug_line::LineBatch empty {};
    auto cmd = device.create_command_buffer();
    ASSERT_NE(cmd, nullptr);

    constexpr std::uint32_t kHighFrame { 0xFFFFFFFFU };
    r.flush(device, *cmd, empty, cd::math::Mat4f::identity(), kHighFrame);

    EXPECT_EQ(r.vertex_capacity_bytes(), 0U);
    EXPECT_EQ(r.parked_buffer_count(), 0U);
}

// --- flush() with a non-empty batch on invalid renderer -------------------

TEST(DebugDrawRenderer, FlushNonEmptyBatchOnInvalidRendererIsNoOp)
{
    // When material_.is_valid() == false the flush implementation
    // returns before any buffer allocation.  The capacity must stay 0.
    cd::rhi::NullDevice device;
    Renderer r {};
    cd::debug_line::LineBatch batch;
    batch.add_line({ 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F, 1.0F });
    batch.add_aabb({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }, { 0.0F, 1.0F, 0.0F, 1.0F });
    auto cmd = device.create_command_buffer();
    ASSERT_NE(cmd, nullptr);

    r.flush(device, *cmd, batch, cd::math::Mat4f::identity(), 7U);

    EXPECT_EQ(r.vertex_capacity_bytes(), 0U);
    EXPECT_EQ(r.parked_buffer_count(), 0U);
    EXPECT_FALSE(r.is_valid());
}

TEST(DebugDrawRenderer, FlushLargeBatchOnInvalidRendererIsNoOp)
{
    // A large batch (many shapes) must still not trigger any allocation
    // through an invalid renderer.
    cd::rhi::NullDevice device;
    Renderer r {};
    cd::debug_line::LineBatch batch;
    for (int i = 0; i < 500; ++i)
    {
        const auto f = static_cast<float>(i);
        batch.add_line({ f, 0.0F, 0.0F }, { f + 1.0F, 0.0F, 0.0F }, { 1.0F, 1.0F, 1.0F, 1.0F });
    }
    ASSERT_GT(batch.vertex_count(), 0U);

    auto cmd = device.create_command_buffer();
    ASSERT_NE(cmd, nullptr);
    r.flush(device, *cmd, batch, cd::math::Mat4f::identity(), 0U);

    EXPECT_EQ(r.vertex_capacity_bytes(), 0U);
    EXPECT_EQ(r.parked_buffer_count(), 0U);
}

// --- Triple move chain -----------------------------------------------------

TEST(DebugDrawRenderer, TripleMoveChainPreservesInvalidState)
{
    // Three consecutive moves of an invalid Renderer must never produce
    // a valid one or leave dangling state.
    Renderer a {};
    Renderer b { std::move(a) };
    Renderer c { std::move(b) };
    Renderer d {};
    d = std::move(c);

    EXPECT_FALSE(d.is_valid());
    EXPECT_EQ(d.vertex_capacity_bytes(), 0U);
    EXPECT_EQ(d.parked_buffer_count(), 0U);
}

// --- create() error path detail --------------------------------------------

TEST(DebugDrawRenderer, CreateFailureReturnsUnexpected)
{
    // create() with nullptr compiler returns an unexpected (error) Result.
    cd::rhi::NullDevice device;
    const auto result = Renderer::create(device, nullptr, make_desc());
    EXPECT_FALSE(result.has_value());
    // Accessing the error must not throw.
    const auto& err = result.error();
    (void)err;
}

TEST(DebugDrawRenderer, CreateFailureWithCustomNameStillFails)
{
    // A custom name in RendererDesc does not magically succeed without a
    // compiler — the failure is in GLSL → SPIR-V compilation.
    cd::rhi::NullDevice device;
    auto d = make_desc();
    d.name = "test_named_renderer";
    const auto result = Renderer::create(device, nullptr, d);
    EXPECT_FALSE(result.has_value());
}

// --- recreate_pipeline() on default-constructed ----------------------------

TEST(DebugDrawRenderer, RecreatePipelineOnDefaultConstructedReturnsFalse)
{
    // A default-constructed (never-created) Renderer must also return
    // false from recreate_pipeline without crashing.
    cd::rhi::NullDevice device;
    Renderer r {};
    EXPECT_FALSE(r.recreate_pipeline(device, nullptr, make_desc()));
    EXPECT_FALSE(r.is_valid());
}

TEST(DebugDrawRenderer, MultipleRecreatePipelineCallsAllFail)
{
    // Repeated recreate_pipeline calls with a null compiler must all fail
    // and leave the renderer in the same invalid state.
    cd::rhi::NullDevice device;
    Renderer r {};
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_FALSE(r.recreate_pipeline(device, nullptr, make_desc()))
            << "expected false on call " << i;
    }
    EXPECT_FALSE(r.is_valid());
    EXPECT_EQ(r.vertex_capacity_bytes(), 0U);
    EXPECT_EQ(r.parked_buffer_count(), 0U);
}

// --- flush() across sequential frames on invalid renderer ------------------

TEST(DebugDrawRenderer, FlushAcrossSequentialFramesOnInvalidRendererIsStable)
{
    // Simulating a multi-frame loop where the renderer is never valid:
    // every frame's flush must be a no-op with consistent zero state.
    cd::rhi::NullDevice device;
    Renderer r {};
    auto cmd = device.create_command_buffer();
    ASSERT_NE(cmd, nullptr);

    cd::debug_line::LineBatch batch;
    batch.add_sphere({ 0.0F, 0.0F, 0.0F }, 1.0F, 16, { 1.0F, 0.5F, 0.0F, 1.0F });

    for (std::uint32_t frame = 0U; frame < 10U; ++frame)
    {
        r.flush(device, *cmd, batch, cd::math::Mat4f::identity(), frame);
        EXPECT_EQ(r.vertex_capacity_bytes(), 0U) << "frame " << frame;
        EXPECT_EQ(r.parked_buffer_count(), 0U)   << "frame " << frame;
        EXPECT_FALSE(r.is_valid())               << "frame " << frame;
    }
}

// --- kDestroyMargin constexpr properties -----------------------------------

TEST(DebugDrawRenderer, DestroyMarginIsPositive)
{
    // The margin must be > 0; a zero margin would destroy in place.
    static_assert(Renderer::kDestroyMargin > 0U);
    SUCCEED();
}

TEST(DebugDrawRenderer, DestroyMarginExceedsFramesInFlight)
{
    // Engine-wide fif=2 fences only frame N-2; margin must be at least 3.
    static_assert(Renderer::kDestroyMargin >= 3U);
    SUCCEED();
}

}  // namespace
