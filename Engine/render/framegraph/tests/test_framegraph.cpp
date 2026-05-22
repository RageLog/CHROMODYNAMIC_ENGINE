// =============================================================================
// CHROMODYNAMIC — cd::framegraph tests
//
// All tests use NullDevice + NullCommandBuffer so they run on any host with
// no GPU dependency. The frame graph's job is bookkeeping + barrier
// scheduling; verifying it against the NullCommandLog gives full coverage
// of compile / execute / state-tracking semantics.
// =============================================================================
#include <cd/framegraph/FrameGraph.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{

using cd::framegraph::FrameGraph;
using cd::framegraph::ImportedTextureDesc;
using cd::framegraph::PassDesc;
using cd::framegraph::PassResource;
using cd::framegraph::TransientTextureDesc;

TEST(FrameGraph, EmptyCompileAndExecuteAreClean)
{
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    ASSERT_TRUE(g.compile().has_value());
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(cb.log().texture_barriers, 0U);
    EXPECT_EQ(cb.log().begin_pass_count, 0U);
}

TEST(FrameGraph, ExecuteBeforeCompileRejected)
{
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    cd::rhi::NullCommandBuffer cb;
    auto r = g.execute(cb);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::framegraph::fg_errors::Code::kNotCompiled));
}

TEST(FrameGraph, DoubleCompileRejected)
{
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    ASSERT_TRUE(g.compile().has_value());
    auto r = g.compile();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::framegraph::fg_errors::Code::kAlreadyCompiled));
}

TEST(FrameGraph, TransientTextureMaterializedOnCompile)
{
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    TransientTextureDesc td {};
    td.extent = { 64, 64, 1 };
    auto rh = g.create_texture(td, "color");
    ASSERT_TRUE(rh.is_valid());
    ASSERT_FALSE(g.texture_handle(rh).is_valid());  // not yet allocated
    ASSERT_TRUE(g.compile().has_value());
    EXPECT_TRUE(g.texture_handle(rh).is_valid());
}

TEST(FrameGraph, SinglePassExecutes)
{
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    TransientTextureDesc td {};
    td.extent = { 32, 32, 1 };
    auto rh = g.create_texture(td);
    std::array<PassResource, 1> writes {
        PassResource { .resource = rh, .state = cd::rhi::ResourceState::kColorAttachment }
    };

    bool executed { false };
    PassDesc p {};
    p.name = "clear";
    p.writes = writes;
    p.execute = [&](cd::rhi::ICommandBuffer&)
    {
        executed = true;
    };
    g.add_pass(p);

    ASSERT_TRUE(g.compile().has_value());
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_TRUE(executed);
    // One barrier (kUndefined → kColorAttachment).
    EXPECT_EQ(cb.log().texture_barriers, 1U);
    EXPECT_EQ(g.current_state(rh), cd::rhi::ResourceState::kColorAttachment);
}

TEST(FrameGraph, TwoDependentPassesInsertChainBarrier)
{
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    TransientTextureDesc td {};
    td.extent = { 32, 32, 1 };
    auto rh = g.create_texture(td);

    // Pass A: write as color attachment.
    std::array<PassResource, 1> a_writes {
        PassResource { .resource = rh, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    PassDesc a {};
    a.name = "write";
    a.writes = a_writes;
    a.execute = [](cd::rhi::ICommandBuffer&)
    {
    };
    g.add_pass(a);

    // Pass B: read as shader resource — must transition COLOR → SHADER_READ.
    std::array<PassResource, 1> b_reads {
        PassResource { .resource = rh, .state = cd::rhi::ResourceState::kShaderResource }
    };
    PassDesc b {};
    b.name = "read";
    b.reads = b_reads;
    b.execute = [](cd::rhi::ICommandBuffer&)
    {
    };
    g.add_pass(b);

    ASSERT_TRUE(g.compile().has_value());
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    // Pass A: kUndefined → kColorAttachment (1 barrier).
    // Pass B: kColorAttachment → kShaderResource (1 barrier).
    EXPECT_EQ(cb.log().texture_barriers, 2U);
    EXPECT_EQ(g.current_state(rh), cd::rhi::ResourceState::kShaderResource);
}

TEST(FrameGraph, IdentityTransitionElided)
{
    // Two consecutive writes in the same state should NOT emit a barrier
    // between them (kColorAttachment → kColorAttachment is identity).
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    TransientTextureDesc td {};
    td.extent = { 32, 32, 1 };
    auto rh = g.create_texture(td);
    std::array<PassResource, 1> w {
        PassResource { .resource = rh, .state = cd::rhi::ResourceState::kColorAttachment }
    };

    PassDesc a {};
    a.writes = w;
    a.execute = [](cd::rhi::ICommandBuffer&)
    {
    };
    g.add_pass(a);
    PassDesc b {};
    b.writes = w;
    b.execute = [](cd::rhi::ICommandBuffer&)
    {
    };
    g.add_pass(b);

    ASSERT_TRUE(g.compile().has_value());
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(cb.log().texture_barriers, 1U);  // only the first transition
}

TEST(FrameGraph, ImportedResourceFinalStateTransition)
{
    // Imported swapchain-style texture: initial=UNDEFINED, final=PRESENT.
    // The graph must emit the final transition even if no pass touches it.
    cd::rhi::NullDevice dev;
    // Allocate an actual NullDevice texture to import.
    cd::rhi::TextureDesc td {};
    td.extent = { 32, 32, 1 };
    td.format = cd::rhi::Format::kRGBA8Unorm;
    auto tex_r = dev.create_texture(td);
    ASSERT_TRUE(tex_r.has_value());

    FrameGraph g { dev };
    ImportedTextureDesc id {};
    id.texture = *tex_r;
    id.initial_state = cd::rhi::ResourceState::kColorAttachment;
    id.final_state = cd::rhi::ResourceState::kPresent;
    auto rh = g.import_texture(id, "swapchain");

    ASSERT_TRUE(g.compile().has_value());
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(cb.log().texture_barriers, 1U);  // COLOR → PRESENT
    EXPECT_EQ(g.current_state(rh), cd::rhi::ResourceState::kPresent);
    dev.destroy_texture(*tex_r);
}

TEST(FrameGraph, ResetReleasesTransientResources)
{
    cd::rhi::NullDevice dev;
    {
        FrameGraph g { dev };
        TransientTextureDesc td {};
        td.extent = { 64, 64, 1 };
        [[maybe_unused]] auto h1 = g.create_texture(td);
        [[maybe_unused]] auto h2 = g.create_texture(td);
        ASSERT_TRUE(g.compile().has_value());
        EXPECT_EQ(dev.live_texture_count(), 2U);
        g.reset();
        EXPECT_EQ(dev.live_texture_count(), 0U);
    }
    EXPECT_EQ(dev.live_texture_count(), 0U);
}

TEST(FrameGraph, ManyPassesBatchAndOrder)
{
    // Stress: 3 transient textures, 4 passes, verifies the execute callbacks
    // run in registration order and total barrier count is correct.
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    TransientTextureDesc td {};
    td.extent = { 16, 16, 1 };
    auto a = g.create_texture(td, "a");
    auto b = g.create_texture(td, "b");
    auto c = g.create_texture(td, "c");

    std::vector<int> order;
    auto pass = [&](int id, std::span<const PassResource> w)
    {
        PassDesc p {};
        p.writes = w;
        p.execute = [&order, id](cd::rhi::ICommandBuffer&)
        {
            order.push_back(id);
        };
        g.add_pass(p);
    };
    std::array<PassResource, 1> wa {
        PassResource { .resource = a, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    std::array<PassResource, 1> wb {
        PassResource { .resource = b, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    std::array<PassResource, 1> wc {
        PassResource { .resource = c, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    std::array<PassResource, 1> wa2 {
        PassResource { .resource = a, .state = cd::rhi::ResourceState::kShaderResource }
    };
    pass(1, wa);
    pass(2, wb);
    pass(3, wc);
    pass(4, wa2);

    ASSERT_TRUE(g.compile().has_value());
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(order, (std::vector<int> { 1, 2, 3, 4 }));
    // 4 barriers: a→COLOR, b→COLOR, c→COLOR, a→SHADER (the 5th write to a
    // would be elided because a is already in COLOR after pass 1, but pass 4
    // moves it to SHADER which is a real transition).
    EXPECT_EQ(cb.log().texture_barriers, 4U);
}

}  // namespace
