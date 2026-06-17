// =============================================================================
// CHROMODYNAMIC — cd::framegraph tests
//
// All tests use NullDevice + NullCommandBuffer so they run on any host with
// no GPU dependency. The frame graph's job is bookkeeping + barrier
// scheduling; verifying it against the NullCommandLog gives full coverage
// of compile / execute / state-tracking semantics.
// =============================================================================
#include <cd/framegraph/FrameGraph.hpp>
#include <cd/framegraph/PassTopology.hpp>
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

// ---------------------------------------------------------------------------
// Dead-pass culling (band2-render-core feature). Default OFF must reproduce
// the v1 "execute every registered pass" contract byte-for-byte; ON prunes
// passes whose writes never reach a sink.
// ---------------------------------------------------------------------------

TEST(FrameGraphCull, DefaultOffKeepsEveryPass)
{
    // Same shape as ManyPassesBatchAndOrder but with culling left at its
    // default (off): pass 2 writes 'b' which nobody reads, yet it must still
    // run — proving the default contract is unchanged.
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    EXPECT_FALSE(g.dead_pass_culling_enabled());

    TransientTextureDesc td {};
    td.extent = { 16, 16, 1 };
    auto a = g.create_texture(td, "a");
    auto b = g.create_texture(td, "b");

    std::vector<int> order;
    std::array<PassResource, 1> wa {
        PassResource { .resource = a, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    std::array<PassResource, 1> wb {
        PassResource { .resource = b, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    PassDesc p0 {}; p0.name = "writes_a"; p0.writes = wa;
    p0.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(0); };
    PassDesc p1 {}; p1.name = "writes_b_unread"; p1.writes = wb;
    p1.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(1); };
    g.add_pass(p0);
    g.add_pass(p1);

    ASSERT_TRUE(g.compile().has_value());
    EXPECT_EQ(g.culled_pass_count(), 0U);
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(order, (std::vector<int> { 0, 1 }));  // both ran
}

TEST(FrameGraphCull, PrunesPassWritingUnconsumedTransient)
{
    // Pass 0 writes transient 'dead' that NO later pass reads and that is NOT
    // a sink → culled. Pass 1 writes an imported swapchain (final=PRESENT) →
    // live. With culling ON only pass 1 executes.
    cd::rhi::NullDevice dev;
    cd::rhi::TextureDesc swd {};
    swd.extent = { 8, 8, 1 };
    swd.format = cd::rhi::Format::kRGBA8Unorm;
    auto sw_tex = dev.create_texture(swd);
    ASSERT_TRUE(sw_tex.has_value());

    FrameGraph g { dev };
    g.set_dead_pass_culling(true);
    EXPECT_TRUE(g.dead_pass_culling_enabled());

    TransientTextureDesc td {};
    td.extent = { 8, 8, 1 };
    auto dead = g.create_texture(td, "dead");

    ImportedTextureDesc id {};
    id.texture       = *sw_tex;
    id.initial_state = cd::rhi::ResourceState::kColorAttachment;
    id.final_state   = cd::rhi::ResourceState::kPresent;
    auto sw = g.import_texture(id, "swapchain");

    std::vector<int> order;
    std::array<PassResource, 1> w_dead {
        PassResource { .resource = dead, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    std::array<PassResource, 1> w_sw {
        PassResource { .resource = sw, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    PassDesc pdead {}; pdead.name = "dead_pass"; pdead.writes = w_dead;
    pdead.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(99); };
    PassDesc plive {}; plive.name = "present_pass"; plive.writes = w_sw;
    plive.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(1); };
    g.add_pass(pdead);
    g.add_pass(plive);

    ASSERT_TRUE(g.compile().has_value());
    EXPECT_EQ(g.culled_pass_count(), 1U);
    EXPECT_EQ(g.pass_count(), 1U);
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(order, (std::vector<int> { 1 }));  // only the live pass ran
    dev.destroy_texture(*sw_tex);
}

TEST(FrameGraphCull, KeepsProducerChainFeedingSink)
{
    // Producer (writes transient t) -> Consumer (reads t, writes sink). Both
    // are live by backward reachability even though the producer's output is
    // not itself a sink.
    cd::rhi::NullDevice dev;
    cd::rhi::TextureDesc swd {};
    swd.extent = { 8, 8, 1 };
    swd.format = cd::rhi::Format::kRGBA8Unorm;
    auto sw_tex = dev.create_texture(swd);
    ASSERT_TRUE(sw_tex.has_value());

    FrameGraph g { dev };
    g.set_dead_pass_culling(true);

    TransientTextureDesc td {};
    td.extent = { 8, 8, 1 };
    auto t = g.create_texture(td, "t");
    ImportedTextureDesc id {};
    id.texture       = *sw_tex;
    id.initial_state = cd::rhi::ResourceState::kColorAttachment;
    id.final_state   = cd::rhi::ResourceState::kPresent;
    auto sink = g.import_texture(id, "sink");

    std::vector<int> order;
    std::array<PassResource, 1> w_t {
        PassResource { .resource = t, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    std::array<PassResource, 1> r_t {
        PassResource { .resource = t, .state = cd::rhi::ResourceState::kShaderResource }
    };
    std::array<PassResource, 1> w_sink {
        PassResource { .resource = sink, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    PassDesc producer {}; producer.name = "producer"; producer.writes = w_t;
    producer.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(0); };
    PassDesc consumer {}; consumer.name = "consumer";
    consumer.reads = r_t; consumer.writes = w_sink;
    consumer.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(1); };
    g.add_pass(producer);
    g.add_pass(consumer);

    ASSERT_TRUE(g.compile().has_value());
    EXPECT_EQ(g.culled_pass_count(), 0U);
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(order, (std::vector<int> { 0, 1 }));
    dev.destroy_texture(*sw_tex);
}

TEST(FrameGraphCull, WriteOnlyNoSinkPassWithoutWritesIsKept)
{
    // A pass with NO declared writes is always kept (untracked side effects in
    // its execute callback). Culling ON must not drop it even though it has no
    // outputs the analysis can see.
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    g.set_dead_pass_culling(true);

    bool ran = false;
    PassDesc side_effect {};
    side_effect.name = "barrier_only";
    side_effect.execute = [&ran](cd::rhi::ICommandBuffer&) { ran = true; };
    g.add_pass(side_effect);

    ASSERT_TRUE(g.compile().has_value());
    EXPECT_EQ(g.culled_pass_count(), 0U);
    EXPECT_EQ(g.pass_count(), 1U);
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_TRUE(ran);
}

}  // namespace

TEST(PassTopology, LinearChainPreservesOrder)
{
    using cd::framegraph::topo_sort;
    auto r = topo_sort(4, { {0, 1}, {1, 2}, {2, 3} });
    EXPECT_FALSE(r.has_cycle);
    ASSERT_EQ(r.order.size(), 4u);
    EXPECT_EQ(r.order[0], 0u);
    EXPECT_EQ(r.order[3], 3u);
}

TEST(PassTopology, IndependentNodesAllAppear)
{
    using cd::framegraph::topo_sort;
    auto r = topo_sort(3, {});
    EXPECT_FALSE(r.has_cycle);
    EXPECT_EQ(r.order.size(), 3u);
}

TEST(PassTopology, DiamondPattern)
{
    using cd::framegraph::topo_sort;
    // 0 → 1 → 3; 0 → 2 → 3
    auto r = topo_sort(4, { {0, 1}, {0, 2}, {1, 3}, {2, 3} });
    EXPECT_FALSE(r.has_cycle);
    ASSERT_EQ(r.order.size(), 4u);
    EXPECT_EQ(r.order.front(), 0u);
    EXPECT_EQ(r.order.back(),  3u);
}

TEST(PassTopology, CycleDetected)
{
    using cd::framegraph::topo_sort;
    auto r = topo_sort(3, { {0, 1}, {1, 2}, {2, 0} });
    EXPECT_TRUE(r.has_cycle);
    EXPECT_LT(r.order.size(), 3u);
}

TEST(PassTopology, OutOfRangeEdgesIgnored)
{
    using cd::framegraph::topo_sort;
    auto r = topo_sort(2, { {0, 5} });  // 5 invalid → silently dropped
    EXPECT_FALSE(r.has_cycle);
    EXPECT_EQ(r.order.size(), 2u);
}
