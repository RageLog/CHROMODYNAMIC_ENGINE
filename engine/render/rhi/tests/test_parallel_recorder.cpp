// =============================================================================
// CHROMODYNAMIC — test_parallel_recorder.cpp
// phase1115 (X1-FU-F step 1) — IDrawRecorder split + IParallelPassRecorder
// contract pins on the Null reference implementation.
//
// Pattern: Arrange / Act / Assert. Headless (NullCommandBuffer).
// =============================================================================
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>
#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// COMPILE-TIME pin: the type system forbids pass/lifecycle calls on a
// lane (the misuse class the X1FUF surface review flagged). If someone
// ever moves begin_render_pass/end/barrier onto IDrawRecorder, these
// trip immediately.
// ---------------------------------------------------------------------------
// (requires-expressions outside templates hard-error on bad lookups,
// so the probes are dependent via a template parameter.)
template <class R>
concept HasPassScope = requires(R& r, cd::rhi::RenderPassBeginInfo info) {
    r.begin_render_pass(info);
    r.end_render_pass();
};
template <class R>
concept HasLifecycle = requires(R& r) {
    r.begin();
    r.end();
};
template <class R>
concept HasDrawSubset = requires(R& r) {
    r.draw(1u, 1u, 0u, 0u);
    r.push_debug_group(std::string_view {});
};
static_assert(!HasPassScope<cd::rhi::IDrawRecorder>);
static_assert(!HasLifecycle<cd::rhi::IDrawRecorder>);
static_assert(HasDrawSubset<cd::rhi::IDrawRecorder>);
static_assert(HasPassScope<cd::rhi::ICommandBuffer>);
static_assert(HasLifecycle<cd::rhi::ICommandBuffer>);

// ---------------------------------------------------------------------------
// Default: a backend without an override reports "unsupported" via
// nullptr — callers fall back to the serial path.
// ---------------------------------------------------------------------------
class MinimalBuffer final : public cd::rhi::ICommandBuffer
{
public:
    void begin() override {}
    void end() override {}
    void begin_render_pass(const cd::rhi::RenderPassBeginInfo&) override {}
    void end_render_pass() override {}
    void bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle) override {}
    void bind_compute_pipeline(cd::rhi::ComputePipelineHandle) override {}
    void bind_descriptor_set(std::uint32_t, cd::rhi::DescriptorSetHandle) override {}
    void bind_vertex_buffer(std::uint32_t, cd::rhi::BufferHandle, std::uint64_t) override {}
    void bind_index_buffer(cd::rhi::BufferHandle, std::uint64_t, cd::rhi::IndexType) override {}
    void push_constants(cd::rhi::PipelineLayoutHandle, cd::rhi::ShaderStage,
                        std::uint32_t, std::uint32_t, const void*) override {}
    void set_viewport(const cd::rhi::Viewport&) override {}
    void set_scissor(const cd::rhi::Rect2D&) override {}
    void draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void draw_indexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t,
                      std::uint32_t) override {}
    void dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void copy_buffer(cd::rhi::BufferHandle, cd::rhi::BufferHandle,
                     std::span<const cd::rhi::BufferCopyRegion>) override {}
    void copy_buffer_to_image(cd::rhi::BufferHandle, cd::rhi::TextureHandle,
                              std::span<const cd::rhi::BufferImageCopyRegion>) override {}
    void copy_image_to_buffer(cd::rhi::TextureHandle, cd::rhi::BufferHandle,
                              std::span<const cd::rhi::BufferImageCopyRegion>) override {}
    void barrier(std::span<const cd::rhi::BufferBarrier>,
                 std::span<const cd::rhi::TextureBarrier>) override {}
    void push_debug_group(std::string_view) override {}
    void pop_debug_group() override {}
};

TEST(ParallelRecorder, DefaultBackendReportsUnsupported)
{
    MinimalBuffer cmd;
    auto rec = cmd.begin_parallel_render_pass({}, 4);
    EXPECT_EQ(rec, nullptr);
}

// ---------------------------------------------------------------------------
// Null reference: lanes record CONCURRENTLY; finish() merges in LANE
// ORDER regardless of which thread finished first, and closes the pass.
// ---------------------------------------------------------------------------
TEST(ParallelRecorder, LanesMergeDeterministicallyInLaneOrder)
{
    cd::rhi::NullCommandBuffer primary;
    primary.begin();

    constexpr std::uint32_t kLanes = 4;
    auto rec = primary.begin_parallel_render_pass({}, kLanes);
    ASSERT_NE(rec, nullptr);
    ASSERT_EQ(rec->lane_count(), kLanes);
    EXPECT_EQ(primary.log().begin_pass_count, 1u);
    EXPECT_EQ(primary.log().end_pass_count, 0u);

    {
        std::vector<std::jthread> workers;
        workers.reserve(kLanes);
        for (std::uint32_t i = 0; i < kLanes; ++i)
        {
            workers.emplace_back(
                [&rec, i]
                {
                    auto& lane = rec->lane(i);
                    lane.push_debug_group("lane" + std::to_string(i));
                    for (std::uint32_t d = 0; d <= i; ++d)
                        lane.draw(3, 1, 0, 0);
                    lane.pop_debug_group();
                });
        }
    }  // joined

    rec->finish();

    // Pass closed exactly once.
    EXPECT_EQ(primary.log().end_pass_count, 1u);
    // 1+2+3+4 draws summed across lanes.
    EXPECT_EQ(primary.log().draws, 10u);
    // Debug groups arrive IN LANE ORDER — the determinism contract.
    const std::vector<std::string> expected { "lane0", "lane1", "lane2", "lane3" };
    EXPECT_EQ(primary.log().debug_groups, expected);

    // finish() is idempotent.
    rec->finish();
    EXPECT_EQ(primary.log().end_pass_count, 1u);
}

TEST(ParallelRecorder, ZeroLaneRequestClampsToOne)
{
    cd::rhi::NullCommandBuffer primary;
    auto rec = primary.begin_parallel_render_pass({}, 0);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->lane_count(), 1u);
    rec->lane(0).draw(3, 1, 0, 0);
    rec->finish();
    EXPECT_EQ(primary.log().draws, 1u);
}

}  // namespace
