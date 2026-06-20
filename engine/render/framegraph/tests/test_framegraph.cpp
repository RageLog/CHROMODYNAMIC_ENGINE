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
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

TEST(FrameGraphCull, DiamondDependencyKeepsEveryPass)
{
    // root -> {left, right} -> sink. Every pass is on a live path to the
    // imported sink, so backward reachability must keep all four.
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
    auto base  = g.create_texture(td, "base");
    auto left  = g.create_texture(td, "left");
    auto right = g.create_texture(td, "right");
    ImportedTextureDesc id {};
    id.texture       = *sw_tex;
    id.initial_state = cd::rhi::ResourceState::kColorAttachment;
    id.final_state   = cd::rhi::ResourceState::kPresent;
    auto sink = g.import_texture(id, "sink");

    auto w = [](cd::framegraph::ResourceHandle h, cd::rhi::ResourceState s)
    { return PassResource { .resource = h, .state = s }; };

    std::vector<int> order;
    std::array<PassResource, 1> w_base { w(base, cd::rhi::ResourceState::kColorAttachment) };
    std::array<PassResource, 1> r_base { w(base, cd::rhi::ResourceState::kShaderResource) };
    std::array<PassResource, 1> w_left { w(left, cd::rhi::ResourceState::kColorAttachment) };
    std::array<PassResource, 1> w_right { w(right, cd::rhi::ResourceState::kColorAttachment) };
    std::array<PassResource, 2> r_lr {
        w(left, cd::rhi::ResourceState::kShaderResource),
        w(right, cd::rhi::ResourceState::kShaderResource)
    };
    std::array<PassResource, 1> w_sink { w(sink, cd::rhi::ResourceState::kColorAttachment) };

    PassDesc root {}; root.name = "root"; root.writes = w_base;
    root.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(0); };
    PassDesc pl {}; pl.name = "left"; pl.reads = r_base; pl.writes = w_left;
    pl.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(1); };
    PassDesc pr {}; pr.name = "right"; pr.reads = r_base; pr.writes = w_right;
    pr.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(2); };
    PassDesc psink {}; psink.name = "sink"; psink.reads = r_lr; psink.writes = w_sink;
    psink.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(3); };
    g.add_pass(root);
    g.add_pass(pl);
    g.add_pass(pr);
    g.add_pass(psink);

    ASSERT_TRUE(g.compile().has_value());
    EXPECT_EQ(g.culled_pass_count(), 0U);
    EXPECT_EQ(g.pass_count(), 4U);
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(order, (std::vector<int> { 0, 1, 2, 3 }));
    dev.destroy_texture(*sw_tex);
}

TEST(FrameGraphCull, MultiLevelDeadChainCulledTransitively)
{
    // dead_a -> dead_b (reads dead_a, writes dead_b); neither feeds a sink.
    // A separate live pass writes the swapchain. Both dead passes must drop.
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
    auto da = g.create_texture(td, "dead_a");
    auto db = g.create_texture(td, "dead_b");
    ImportedTextureDesc id {};
    id.texture       = *sw_tex;
    id.initial_state = cd::rhi::ResourceState::kColorAttachment;
    id.final_state   = cd::rhi::ResourceState::kPresent;
    auto sink = g.import_texture(id, "sink");

    auto mk = [](cd::framegraph::ResourceHandle h, cd::rhi::ResourceState s)
    { return PassResource { .resource = h, .state = s }; };
    std::array<PassResource, 1> w_da { mk(da, cd::rhi::ResourceState::kColorAttachment) };
    std::array<PassResource, 1> r_da { mk(da, cd::rhi::ResourceState::kShaderResource) };
    std::array<PassResource, 1> w_db { mk(db, cd::rhi::ResourceState::kColorAttachment) };
    std::array<PassResource, 1> w_sink { mk(sink, cd::rhi::ResourceState::kColorAttachment) };

    std::vector<int> order;
    PassDesc p0 {}; p0.name = "dead_a"; p0.writes = w_da;
    p0.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(10); };
    PassDesc p1 {}; p1.name = "dead_b"; p1.reads = r_da; p1.writes = w_db;
    p1.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(11); };
    PassDesc p2 {}; p2.name = "present"; p2.writes = w_sink;
    p2.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(1); };
    g.add_pass(p0);
    g.add_pass(p1);
    g.add_pass(p2);

    ASSERT_TRUE(g.compile().has_value());
    EXPECT_EQ(g.culled_pass_count(), 2U);  // both dead passes pruned
    EXPECT_EQ(g.pass_count(), 1U);
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(order, (std::vector<int> { 1 }));
    dev.destroy_texture(*sw_tex);
}

TEST(FrameGraphCull, ReadModifyWriteChainPromotesEarlierWriter)
{
    // Pass A writes 't' (color). Pass B reads-modify-writes 't' (writes color).
    // Pass C reads 't' and writes the sink. The cull must keep A because B's
    // RMW depends on A's accumulated content, even though A is not read directly
    // by the sink-feeding consumer.
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

    auto mk = [](cd::framegraph::ResourceHandle h, cd::rhi::ResourceState s)
    { return PassResource { .resource = h, .state = s }; };
    std::array<PassResource, 1> w_t { mk(t, cd::rhi::ResourceState::kColorAttachment) };
    std::array<PassResource, 1> r_t { mk(t, cd::rhi::ResourceState::kShaderResource) };
    std::array<PassResource, 1> w_sink { mk(sink, cd::rhi::ResourceState::kColorAttachment) };

    std::vector<int> order;
    PassDesc a {}; a.name = "init_t"; a.writes = w_t;
    a.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(0); };
    PassDesc b {}; b.name = "accumulate_t"; b.writes = w_t;  // RMW: re-write 't'
    b.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(1); };
    PassDesc c {}; c.name = "consume"; c.reads = r_t; c.writes = w_sink;
    c.execute = [&order](cd::rhi::ICommandBuffer&) { order.push_back(2); };
    g.add_pass(a);
    g.add_pass(b);
    g.add_pass(c);

    ASSERT_TRUE(g.compile().has_value());
    EXPECT_EQ(g.culled_pass_count(), 0U);  // all three live
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(order, (std::vector<int> { 0, 1, 2 }));
    dev.destroy_texture(*sw_tex);
}

// ---------------------------------------------------------------------------
// FrameGraph — handle-generation + barrier-elision negative coverage.
// ---------------------------------------------------------------------------

TEST(FrameGraph, InvalidHandleInPassEmitsNoBarrier)
{
    // A pass referencing a default-constructed (invalid) handle must be skipped
    // by the barrier pass without crashing or emitting spurious barriers.
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    std::array<PassResource, 1> bad {
        PassResource { .resource = {}, .state = cd::rhi::ResourceState::kColorAttachment }
    };
    bool ran = false;
    PassDesc p {};
    p.name = "uses_invalid";
    p.writes = bad;
    p.execute = [&ran](cd::rhi::ICommandBuffer&) { ran = true; };
    g.add_pass(p);

    ASSERT_TRUE(g.compile().has_value());
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_TRUE(ran);
    EXPECT_EQ(cb.log().texture_barriers, 0U);  // invalid handle → no barrier
}

TEST(FrameGraph, StaleGenerationHandleResolvesToNothing)
{
    // After reset(), an old handle's generation no longer matches; introspection
    // must reject it (defensive against use-after-reset).
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    TransientTextureDesc td {};
    td.extent = { 16, 16, 1 };
    auto rh = g.create_texture(td, "color");
    ASSERT_TRUE(g.compile().has_value());
    EXPECT_TRUE(g.texture_handle(rh).is_valid());

    g.reset();  // resources cleared; rh now dangles
    EXPECT_FALSE(g.texture_handle(rh).is_valid());
    EXPECT_EQ(g.current_state(rh), cd::rhi::ResourceState::kUndefined);
}

TEST(FrameGraph, ImportedResourceWithUndefinedFinalStateEmitsNoFinalize)
{
    // An imported texture left at final=kUndefined must NOT generate a finalize
    // barrier at end of execute (only meaningful final states transition).
    cd::rhi::NullDevice dev;
    cd::rhi::TextureDesc td {};
    td.extent = { 16, 16, 1 };
    td.format = cd::rhi::Format::kRGBA8Unorm;
    auto tex_r = dev.create_texture(td);
    ASSERT_TRUE(tex_r.has_value());

    FrameGraph g { dev };
    ImportedTextureDesc id {};
    id.texture       = *tex_r;
    id.initial_state = cd::rhi::ResourceState::kColorAttachment;
    id.final_state   = cd::rhi::ResourceState::kUndefined;  // no finalize wanted
    [[maybe_unused]] auto rh = g.import_texture(id, "no_finalize");

    ASSERT_TRUE(g.compile().has_value());
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(cb.log().texture_barriers, 0U);
    dev.destroy_texture(*tex_r);
}

TEST(FrameGraph, InstrumentationCallbackInvokedPerExecutedPass)
{
    // The per-pass timing hook must fire once per pass, in registration order,
    // with monotonically-increasing pass_index.
    cd::rhi::NullDevice dev;
    FrameGraph g { dev };
    TransientTextureDesc td {};
    td.extent = { 8, 8, 1 };
    auto a = g.create_texture(td, "a");
    auto b = g.create_texture(td, "b");

    auto mk = [](cd::framegraph::ResourceHandle h)
    { return PassResource { .resource = h, .state = cd::rhi::ResourceState::kColorAttachment }; };
    std::array<PassResource, 1> wa { mk(a) };
    std::array<PassResource, 1> wb { mk(b) };
    PassDesc p0 {}; p0.name = "p0"; p0.writes = wa;
    p0.execute = [](cd::rhi::ICommandBuffer&) {};
    PassDesc p1 {}; p1.name = "p1"; p1.writes = wb;
    p1.execute = [](cd::rhi::ICommandBuffer&) {};
    g.add_pass(p0);
    g.add_pass(p1);

    std::vector<std::uint32_t> seen_indices;
    std::vector<std::string>   seen_names;
    g.set_instrumentation_callback(
        [&](std::string_view name, double start_ms, double dur_ms, std::uint32_t idx)
        {
            EXPECT_GE(start_ms, 0.0);
            EXPECT_GE(dur_ms, 0.0);
            seen_names.emplace_back(name);
            seen_indices.push_back(idx);
        });

    ASSERT_TRUE(g.compile().has_value());
    cd::rhi::NullCommandBuffer cb;
    ASSERT_TRUE(g.execute(cb).has_value());
    EXPECT_EQ(seen_indices, (std::vector<std::uint32_t> { 0u, 1u }));
    EXPECT_EQ(seen_names, (std::vector<std::string> { "p0", "p1" }));
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

// ---------------------------------------------------------------------------
// PassTopology — edge / negative coverage (to-100). topo_sort is a pure
// host-side utility (not wired into FrameGraph scheduling), so these add no
// render-output risk.
// ---------------------------------------------------------------------------

TEST(PassTopology, EmptyGraphSchedulesNothing)
{
    using cd::framegraph::topo_sort;
    auto r = topo_sort(0, {});  // no nodes, no edges
    EXPECT_FALSE(r.has_cycle);
    EXPECT_TRUE(r.order.empty());
}

TEST(PassTopology, SingleNodeNoEdges)
{
    using cd::framegraph::topo_sort;
    auto r = topo_sort(1, {});
    EXPECT_FALSE(r.has_cycle);
    ASSERT_EQ(r.order.size(), 1u);
    EXPECT_EQ(r.order[0], 0u);
}

TEST(PassTopology, SelfLoopIsACycle)
{
    using cd::framegraph::topo_sort;
    // A node depending on itself can never reach in-degree 0.
    auto r = topo_sort(2, { {0, 1}, {1, 1} });
    EXPECT_TRUE(r.has_cycle);
    // Node 0 still schedules (it has no predecessor); node 1 is stuck.
    ASSERT_EQ(r.order.size(), 1u);
    EXPECT_EQ(r.order[0], 0u);
}

TEST(PassTopology, DuplicateEdgesStillOrderConsistently)
{
    using cd::framegraph::topo_sort;
    // Two identical edges 0->1: in-degree of 1 becomes 2, decremented twice as
    // node 0's two successor entries are processed → still resolves cleanly.
    auto r = topo_sort(2, { {0, 1}, {0, 1} });
    EXPECT_FALSE(r.has_cycle);
    ASSERT_EQ(r.order.size(), 2u);
    EXPECT_EQ(r.order[0], 0u);
    EXPECT_EQ(r.order[1], 1u);
}

TEST(PassTopology, FifoTieBreakIsDeterministic)
{
    using cd::framegraph::topo_sort;
    // 3 independent roots feeding one sink. All roots are in-degree-0 and are
    // pushed in ascending index order, so FIFO must pop them 0,1,2 then 3.
    auto r = topo_sort(4, { {0, 3}, {1, 3}, {2, 3} });
    EXPECT_FALSE(r.has_cycle);
    ASSERT_EQ(r.order.size(), 4u);
    EXPECT_EQ(r.order[0], 0u);
    EXPECT_EQ(r.order[1], 1u);
    EXPECT_EQ(r.order[2], 2u);
    EXPECT_EQ(r.order[3], 3u);
}

TEST(PassTopology, CyclicNodesEmptyWhenAcyclic)
{
    using cd::framegraph::topo_cyclic_nodes;
    using cd::framegraph::topo_sort;
    auto r = topo_sort(4, { {0, 1}, {1, 2}, {2, 3} });
    EXPECT_TRUE(topo_cyclic_nodes(4, r).empty());
}

TEST(PassTopology, CyclicNodesIdentifiesOffendingSet)
{
    using cd::framegraph::topo_cyclic_nodes;
    using cd::framegraph::topo_sort;
    // Node 0 is independent (schedules); 1<->2<->3 form a cycle (all stuck).
    auto r = topo_sort(4, { {1, 2}, {2, 3}, {3, 1} });
    EXPECT_TRUE(r.has_cycle);
    const auto stuck = topo_cyclic_nodes(4, r);
    EXPECT_EQ(stuck, (std::vector<std::uint32_t> { 1u, 2u, 3u }));
}

TEST(PassTopology, CyclicNodesIncludesDominatedNodes)
{
    using cd::framegraph::topo_cyclic_nodes;
    using cd::framegraph::topo_sort;
    // 0<->1 cycle; node 2 depends on the cyclic node 1, so it is also stuck.
    auto r = topo_sort(3, { {0, 1}, {1, 0}, {1, 2} });
    EXPECT_TRUE(r.has_cycle);
    const auto stuck = topo_cyclic_nodes(3, r);
    EXPECT_EQ(stuck, (std::vector<std::uint32_t> { 0u, 1u, 2u }));
}
