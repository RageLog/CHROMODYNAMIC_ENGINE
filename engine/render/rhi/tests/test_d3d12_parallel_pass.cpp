// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_parallel_pass.cpp
//
// D11 (phase1191): GPU correctness test for the D3D12
// begin_parallel_render_pass / IParallelPassRecorder lane path.
//
// THE PROOF: render the SAME scene two ways and assert BYTE-IDENTICAL pixels.
//
//   (a) SERIAL — one command buffer opens the render pass and draws K colored
//       quads (full-screen triangle clipped by a per-quad scissor tile, colour
//       via push-constant) in order 0..K-1.
//   (b) PARALLEL — begin_parallel_render_pass splits the SAME K draws across
//       L lanes (quad q goes to lane q % L). Each lane RECORDS independently
//       (here from worker threads to exercise thread-confined recording);
//       finish() joins the lanes onto the primary IN LANE ORDER.
//
// Because the lanes replay in lane order and each lane re-binds the pass RTV +
// viewport + scissor before its draws (D3D12 lists inherit no state), the
// emitted command stream is identical to the serial path — so the two readback
// images MUST be byte-for-byte identical. A regression that mis-orders lanes,
// drops a lane, or fails to propagate the render-target/viewport/scissor state
// into a lane changes at least one tile and trips the memcmp.
//
// To make lane ORDER observable, the quads OVERLAP: every quad draws the FULL
// viewport (no scissor on the last) so the LAST-drawn colour wins each pixel —
// any lane-order scramble changes the final colour. A second sub-test uses
// disjoint scissor tiles so a DROPPED lane leaves a tile at the clear colour.
//
// Real D3D12 backend (WARP if no hardware adapter). GTEST_SKIP when no adapter
// or when dxcompiler.dll is unavailable at runtime. Pattern: Arrange/Act/Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/shader/Compiler.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)

namespace
{

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

// Full-screen triangle from SV_VertexID; fragment writes a push-constant colour.
constexpr const char* kVS = R"glsl(
#version 450
void main()
{
    vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                  (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(push_constant) uniform Push { vec4 u_color; } pc;
layout(location = 0) out vec4 o;
void main() { o = pc.u_color; }
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

[[nodiscard]] bool glslang_available()
{
    return cd::shader::make_glslang_compiler() != nullptr;
}

[[nodiscard]] cd::rhi::ShaderModuleHandle
make_module(cd::rhi::IDevice& dev, cd::rhi::ShaderStage stage,
            const char* src, bool* skip)
{
    cd::rhi::ShaderModuleDesc d {};
    d.stage       = stage;
    d.code        = src;
    d.code_size   = std::char_traits<char>::length(src);
    d.entry_point = "main";
    d.language    = cd::rhi::ShaderSourceLanguage::kGlsl;
    auto r = dev.create_shader_module(d);
    if (!r.has_value())
    {
        const std::string msg { r.error().message };
        if (msg.find("dxc") != std::string::npos)
            *skip = true;
        return {};
    }
    return *r;
}

[[nodiscard]] cd::rhi::TextureHandle make_color_target(cd::rhi::IDevice& dev)
{
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { kW, kH, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kColorAttachment |
                      cd::rhi::TextureUsage::kTransferSrc |
                      cd::rhi::TextureUsage::kSampled;
    auto r = dev.create_texture(td);
    return r.has_value() ? *r : cd::rhi::TextureHandle {};
}

[[nodiscard]] cd::rhi::TextureViewHandle
make_view(cd::rhi::IDevice& dev, cd::rhi::TextureHandle t)
{
    cd::rhi::TextureViewDesc vd {};
    vd.texture = t;
    vd.type    = cd::rhi::TextureType::k2D;
    vd.format  = cd::rhi::Format::kUndefined;
    auto r = dev.create_texture_view(vd);
    return r.has_value() ? *r : cd::rhi::TextureViewHandle {};
}

// Read the whole RGBA8 image back into a tightly-packed byte vector.
[[nodiscard]] std::vector<std::byte>
read_image(cd::rhi::IDevice& dev, cd::rhi::TextureHandle tex,
           cd::rhi::ResourceState current_state)
{
    constexpr std::uint64_t kBytes = std::uint64_t { kW } * kH * 4u;
    cd::rhi::BufferDesc bd {};
    bd.size   = kBytes;
    bd.usage  = cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto buf_r = dev.create_buffer(bd);
    if (!buf_r.has_value())
        return {};
    const auto buf = *buf_r;

    cd::rhi::IDevice::ImageRegion region {};
    region.width     = kW;
    region.height    = kH;
    region.src_state = current_state;
    const auto copy_r = dev.copy_image_to_buffer(tex, buf, 0, region);
    EXPECT_TRUE(copy_r.has_value());

    std::vector<std::byte> raw(static_cast<std::size_t>(kBytes));
    auto dl = dev.download_buffer(buf, 0, std::span<std::byte> { raw });
    EXPECT_TRUE(dl.has_value());
    dev.destroy_buffer(buf);
    return raw;
}

struct QuadPush { float color[4]; };

// One full-screen quad: optional scissor tile + push-constant colour. Records
// the draw onto whichever IDrawRecorder it is handed (the primary in the serial
// path, a lane recorder in the parallel path).
struct Quad
{
    cd::rhi::Rect2D scissor;
    QuadPush        push;
};

// Build the K quads. Each gets a DISTINCT colour. `tiled` controls whether the
// quads are disjoint scissor tiles (drop-a-lane detector) or all full-screen
// and overlapping (lane-ORDER detector — last colour wins every pixel).
[[nodiscard]] std::vector<Quad> build_quads(std::uint32_t k, bool tiled)
{
    std::vector<Quad> quads;
    quads.reserve(k);
    const std::uint32_t tile_w = std::max<std::uint32_t>(1u, kW / k);
    for (std::uint32_t i = 0; i < k; ++i)
    {
        Quad q {};
        if (tiled)
        {
            const std::uint32_t x0 = i * tile_w;
            const std::uint32_t w  = (i + 1u == k) ? (kW - x0) : tile_w;
            q.scissor = cd::rhi::Rect2D { { static_cast<std::int32_t>(x0), 0 },
                                          { w, kH } };
        }
        else
        {
            q.scissor = cd::rhi::Rect2D { {}, { kW, kH } };
        }
        // Distinct colour per quad: vary R and B so order/identity is visible.
        const float r = static_cast<float>(i + 1u) / static_cast<float>(k);
        q.push = QuadPush { { r, 0.25F, 1.0F - r, 1.0F } };
        quads.push_back(q);
    }
    return quads;
}

// Record one quad's state + draw onto a draw-recorder (lane or primary).
void record_quad(cd::rhi::IDrawRecorder& rec, const Quad& q,
                 cd::rhi::GraphicsPipelineHandle pso,
                 cd::rhi::PipelineLayoutHandle layout)
{
    rec.bind_graphics_pipeline(pso);
    rec.set_viewport({ 0, 0, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F });
    rec.set_scissor(q.scissor);
    rec.push_constants(layout, cd::rhi::ShaderStage::kAllGraphics, 0,
                       sizeof(QuadPush), &q.push);
    rec.draw(3, 1, 0, 0);
}

// Shared fixture: shaders, layout, PSO. Returns false (via *skip) if DXC missing.
struct Fixture
{
    cd::rhi::ShaderModuleHandle    vs, fs;
    cd::rhi::PipelineLayoutHandle  layout;
    cd::rhi::GraphicsPipelineHandle pso;

    void destroy(cd::rhi::IDevice& d) const
    {
        d.destroy_graphics_pipeline(pso);
        d.destroy_pipeline_layout(layout);
        d.destroy_shader_module(vs);
        d.destroy_shader_module(fs);
    }
};

[[nodiscard]] bool make_fixture(cd::rhi::IDevice& d, Fixture* out, bool* skip)
{
    out->vs = make_module(d, cd::rhi::ShaderStage::kVertex, kVS, skip);
    out->fs = make_module(d, cd::rhi::ShaderStage::kFragment, kFS, skip);
    if (*skip) return false;
    if (!out->vs.is_valid() || !out->fs.is_valid()) return false;

    cd::rhi::PushConstantRange pcr {};
    pcr.offset = 0;
    pcr.size   = sizeof(QuadPush);
    pcr.stages = cd::rhi::ShaderStage::kAllGraphics;
    cd::rhi::PipelineLayoutDesc pld {};
    pld.push_constants = std::span<const cd::rhi::PushConstantRange>(&pcr, 1);
    auto lr = d.create_pipeline_layout(pld);
    if (!lr.has_value()) return false;
    out->layout = *lr;

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout          = out->layout;
    gpd.vertex_shader   = out->vs;
    gpd.fragment_shader = out->fs;
    gpd.topology        = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull     = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    gpd.color_attachment_formats  = color_fmts;
    auto pr = d.create_graphics_pipeline(gpd);
    if (!pr.has_value()) return false;
    out->pso = *pr;
    return true;
}

// Render K quads SERIALLY into a fresh colour target; return the readback bytes.
[[nodiscard]] std::vector<std::byte>
render_serial(cd::rhi::IDevice& dev, const Fixture& fx,
              const std::vector<Quad>& quads)
{
    const auto color = make_color_target(dev);
    const auto view  = make_view(dev, color);
    EXPECT_TRUE(color.is_valid());
    EXPECT_TRUE(view.is_valid());

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    EXPECT_NE(cmd, nullptr);
    cmd->begin();

    cd::rhi::ColorAttachmentInfo catt {};
    catt.view        = view;
    catt.load_op     = cd::rhi::LoadOp::kClear;
    catt.store_op    = cd::rhi::StoreOp::kStore;
    catt.clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
    rp.render_area.extent = { kW, kH };

    cmd->begin_render_pass(rp);
    for (const auto& q : quads)
        record_quad(*cmd, q, fx.pso, fx.layout);
    cmd->end_render_pass();

    cd::rhi::TextureBarrier to_read {};
    to_read.texture = color;
    to_read.from    = cd::rhi::ResourceState::kColorAttachment;
    to_read.to      = cd::rhi::ResourceState::kShaderResource;
    to_read.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_read, 1));
    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();

    auto bytes = read_image(dev, color, cd::rhi::ResourceState::kShaderResource);
    dev.destroy_texture_view(view);
    dev.destroy_texture(color);
    return bytes;
}

// Render the SAME K quads via begin_parallel_render_pass across L lanes (quad q
// -> lane q % L). Lanes are filled from worker threads to exercise concurrent,
// thread-confined recording; finish() joins them in lane order.
[[nodiscard]] std::vector<std::byte>
render_parallel(cd::rhi::IDevice& dev, const Fixture& fx,
                const std::vector<Quad>& quads, std::uint32_t lanes,
                bool* unsupported)
{
    *unsupported = false;
    const auto color = make_color_target(dev);
    const auto view  = make_view(dev, color);
    EXPECT_TRUE(color.is_valid());
    EXPECT_TRUE(view.is_valid());

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    EXPECT_NE(cmd, nullptr);
    cmd->begin();

    cd::rhi::ColorAttachmentInfo catt {};
    catt.view        = view;
    catt.load_op     = cd::rhi::LoadOp::kClear;
    catt.store_op    = cd::rhi::StoreOp::kStore;
    catt.clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
    rp.render_area.extent = { kW, kH };

    auto rec = cmd->begin_parallel_render_pass(rp, lanes);
    if (rec == nullptr)
    {
        // Backend reports no parallel support — caller falls back to serial.
        *unsupported = true;
        dev.destroy_texture_view(view);
        dev.destroy_texture(color);
        return {};
    }
    EXPECT_EQ(rec->lane_count(), lanes);

    // Partition the quads round-robin across lanes, preserving per-lane order.
    std::vector<std::vector<const Quad*>> lane_quads(lanes);
    for (std::uint32_t i = 0; i < quads.size(); ++i)
        lane_quads[i % lanes].push_back(&quads[i]);

    {
        std::vector<std::jthread> workers;
        workers.reserve(lanes);
        for (std::uint32_t l = 0; l < lanes; ++l)
        {
            workers.emplace_back(
                [&rec, &fx, &lane_quads, l]
                {
                    auto& lane = rec->lane(l);
                    for (const Quad* q : lane_quads[l])
                        record_quad(lane, *q, fx.pso, fx.layout);
                });
        }
    }  // all workers joined here — happens-before finish()
    rec->finish();

    cd::rhi::TextureBarrier to_read {};
    to_read.texture = color;
    to_read.from    = cd::rhi::ResourceState::kColorAttachment;
    to_read.to      = cd::rhi::ResourceState::kShaderResource;
    to_read.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_read, 1));
    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();

    auto bytes = read_image(dev, color, cd::rhi::ResourceState::kShaderResource);
    dev.destroy_texture_view(view);
    dev.destroy_texture(color);
    return bytes;
}

// ---- Core proof: serial vs parallel produce IDENTICAL pixels ----------------

void run_identity(bool tiled, std::uint32_t k, std::uint32_t lanes)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";

    Fixture fx {};
    bool skip = false;
    if (!make_fixture(*dev, &fx, &skip))
    {
        if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
        FAIL() << "fixture creation failed";
    }

    const auto quads = build_quads(k, tiled);

    const auto serial = render_serial(*dev, fx, quads);
    bool unsupported = false;
    const auto parallel = render_parallel(*dev, fx, quads, lanes, &unsupported);
    if (unsupported)
    {
        fx.destroy(*dev);
        FAIL() << "begin_parallel_render_pass returned nullptr — D11 not wired";
    }

    ASSERT_EQ(serial.size(), parallel.size());
    ASSERT_FALSE(serial.empty());
    // BYTE-IDENTICAL: lane ordering + per-lane state isolation reproduce the
    // exact single-threaded command stream.
    EXPECT_EQ(serial, parallel)
        << "parallel pass (" << lanes << " lanes, " << k
        << " quads, tiled=" << tiled
        << ") must be byte-identical to the serial render";

    fx.destroy(*dev);
}

// Disjoint tiles: every quad colours its own slice. A dropped/aliased lane
// would leave a tile at the clear colour or the wrong colour -> memcmp fails.
TEST(D3D12ParallelPass, TiledSerialEqualsParallel_4quads_4lanes)
{
    run_identity(/*tiled=*/true, /*k=*/4, /*lanes=*/4);
}

// More quads than lanes: round-robin packs >1 quad per lane; per-lane order +
// lane order together must still reproduce the serial tiling.
TEST(D3D12ParallelPass, TiledSerialEqualsParallel_8quads_3lanes)
{
    run_identity(/*tiled=*/true, /*k=*/8, /*lanes=*/3);
}

// Overlapping full-screen quads: the LAST-drawn colour wins every pixel, so any
// lane-order scramble changes the final image. This is the strongest ordering
// guard — it fails the instant the join is not strictly lane-0..lane-(L-1).
TEST(D3D12ParallelPass, OverlapLastWriteWins_6quads_2lanes)
{
    run_identity(/*tiled=*/false, /*k=*/6, /*lanes=*/2);
}

// Single lane must equal serial trivially (degenerate partition).
TEST(D3D12ParallelPass, SingleLaneEqualsSerial)
{
    run_identity(/*tiled=*/true, /*k=*/4, /*lanes=*/1);
}

// Contract: zero lanes clamps to one (mirrors the Vulkan/Null reference).
TEST(D3D12ParallelPass, ZeroLanesClampsToOne)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";

    Fixture fx {};
    bool skip = false;
    if (!make_fixture(*dev, &fx, &skip))
    {
        if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
        FAIL() << "fixture creation failed";
    }

    const auto color = make_color_target(*dev);
    const auto view  = make_view(*dev, color);
    auto cmd = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    cmd->begin();
    cd::rhi::ColorAttachmentInfo catt {};
    catt.view = view; catt.load_op = cd::rhi::LoadOp::kClear;
    catt.clear_color = { .f32 = { 0, 0, 0, 1 } };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
    rp.render_area.extent = { kW, kH };
    auto rec = cmd->begin_parallel_render_pass(rp, 0);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->lane_count(), 1u);
    rec->finish();
    cmd->end();
    dev->submit(*cmd);
    dev->wait_idle();

    dev->destroy_texture_view(view);
    dev->destroy_texture(color);
    fx.destroy(*dev);
}

}  // namespace

#endif  // _WIN32
