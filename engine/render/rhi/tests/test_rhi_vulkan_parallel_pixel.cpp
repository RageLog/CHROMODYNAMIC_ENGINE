// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_rhi_vulkan_parallel_pixel.cpp
//
// C-VK-PARALLEL-PIXEL (Backend-to-100 Wave 3d).
//
// The Vulkan analogue of test_d3d12_parallel_pass: prove the Vulkan backend's
// begin_parallel_render_pass / IParallelPassRecorder TRUE-SECONDARY lane path
// renders the SAME pixels as the serial path, and joins lanes IN LANE ORDER.
// D3D12 has this test (sequential-replay lanes); Vulkan (real secondary command
// buffers) had NONE — this closes the cross-backend hole.
//
// THE PROOF: render the SAME scene two ways and assert BYTE-IDENTICAL pixels.
//   (a) SERIAL — one command buffer opens the pass and draws K colour quads in
//       order 0..K-1 (full-screen triangle clipped per-quad by scissor; colour
//       via push constant).
//   (b) PARALLEL — begin_parallel_render_pass splits the SAME K draws across L
//       lanes (quad q -> lane q % L). Each lane records from its OWN worker
//       thread into a secondary command buffer; finish() executes the secondaries
//       on the primary IN LANE ORDER.
//
// Two scene flavours, exactly as the D3D12 test:
//   * TILED (disjoint scissors): a dropped/aliased lane leaves a tile at the
//     clear colour -> memcmp fails.
//   * OVERLAP (full-screen, last-write-wins): any lane-order scramble changes the
//     final colour -> the strongest ordering guard.
//
// Real Vulkan backend (lavapipe in CI / RTX on the host). GTEST_SKIP when no ICD
// or no glslang. NO sleep_for — std::jthread join is the only synchronisation,
// and it happens-before finish() (the recorder lifetime contract).
// Pattern: Arrange / Act / Assert.
// =============================================================================
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

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

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_vulkan_or_null()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    return r.has_value() ? std::move(*r) : nullptr;
}

[[nodiscard]] bool glslang_available()
{
    return cd::shader::make_glslang_compiler() != nullptr;
}

[[nodiscard]] cd::rhi::ShaderModuleHandle
make_module(cd::rhi::IDevice& dev, cd::rhi::ShaderStage stage, const char* src)
{
    cd::rhi::ShaderModuleDesc d {};
    d.stage       = stage;
    d.code        = src;
    d.code_size   = std::char_traits<char>::length(src);
    d.entry_point = "main";
    d.language    = cd::rhi::ShaderSourceLanguage::kGlsl;
    auto r = dev.create_shader_module(d);
    return r.has_value() ? *r : cd::rhi::ShaderModuleHandle {};
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

[[nodiscard]] std::vector<std::byte>
read_image(cd::rhi::IDevice& dev, cd::rhi::TextureHandle tex)
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
    region.src_state = cd::rhi::ResourceState::kShaderResource;
    const auto copy_r = dev.copy_image_to_buffer(tex, buf, 0, region);
    EXPECT_TRUE(copy_r.has_value());

    std::vector<std::byte> raw(static_cast<std::size_t>(kBytes));
    auto dl = dev.download_buffer(buf, 0, std::span<std::byte> { raw });
    EXPECT_TRUE(dl.has_value());
    dev.destroy_buffer(buf);
    return raw;
}

struct QuadPush { float color[4]; };
struct Quad { cd::rhi::Rect2D scissor; QuadPush push; };

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
            q.scissor = cd::rhi::Rect2D { { static_cast<std::int32_t>(x0), 0 }, { w, kH } };
        }
        else
        {
            q.scissor = cd::rhi::Rect2D { {}, { kW, kH } };
        }
        const float r = static_cast<float>(i + 1u) / static_cast<float>(k);
        q.push = QuadPush { { r, 0.25F, 1.0F - r, 1.0F } };
        quads.push_back(q);
    }
    return quads;
}

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

struct Fixture
{
    cd::rhi::ShaderModuleHandle     vs, fs;
    cd::rhi::PipelineLayoutHandle   layout;
    cd::rhi::GraphicsPipelineHandle pso;

    void destroy(cd::rhi::IDevice& d) const
    {
        d.destroy_graphics_pipeline(pso);
        d.destroy_pipeline_layout(layout);
        d.destroy_shader_module(vs);
        d.destroy_shader_module(fs);
    }
};

[[nodiscard]] bool make_fixture(cd::rhi::IDevice& d, Fixture* out)
{
    out->vs = make_module(d, cd::rhi::ShaderStage::kVertex,   kVS);
    out->fs = make_module(d, cd::rhi::ShaderStage::kFragment, kFS);
    if (!out->vs.is_valid() || !out->fs.is_valid()) return false;

    cd::rhi::PushConstantRange pcr {};
    pcr.offset = 0; pcr.size = sizeof(QuadPush); pcr.stages = cd::rhi::ShaderStage::kAllGraphics;
    cd::rhi::PipelineLayoutDesc pld {};
    pld.push_constants = std::span<const cd::rhi::PushConstantRange>(&pcr, 1);
    auto lr = d.create_pipeline_layout(pld);
    if (!lr.has_value()) return false;
    out->layout = *lr;

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout = out->layout; gpd.vertex_shader = out->vs; gpd.fragment_shader = out->fs;
    gpd.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test = false; gpd.depth_stencil.depth_write = false;
    gpd.color_attachment_formats = color_fmts;
    auto pr = d.create_graphics_pipeline(gpd);
    if (!pr.has_value()) return false;
    out->pso = *pr;
    return true;
}

[[nodiscard]] std::vector<std::byte>
render_serial(cd::rhi::IDevice& dev, const Fixture& fx, const std::vector<Quad>& quads)
{
    const auto color = make_color_target(dev);
    const auto view  = make_view(dev, color);
    EXPECT_TRUE(color.is_valid() && view.is_valid());

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    EXPECT_NE(cmd, nullptr);
    cmd->begin();
    cd::rhi::ColorAttachmentInfo catt {};
    catt.view = view; catt.load_op = cd::rhi::LoadOp::kClear; catt.store_op = cd::rhi::StoreOp::kStore;
    catt.clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
    rp.render_area.extent = { kW, kH };
    cmd->begin_render_pass(rp);
    for (const auto& q : quads)
        record_quad(*cmd, q, fx.pso, fx.layout);
    cmd->end_render_pass();

    cd::rhi::TextureBarrier to_read {};
    to_read.texture = color; to_read.from = cd::rhi::ResourceState::kColorAttachment;
    to_read.to = cd::rhi::ResourceState::kShaderResource; to_read.range = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_read, 1));
    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();

    auto bytes = read_image(dev, color);
    dev.destroy_texture_view(view);
    dev.destroy_texture(color);
    return bytes;
}

[[nodiscard]] std::vector<std::byte>
render_parallel(cd::rhi::IDevice& dev, const Fixture& fx, const std::vector<Quad>& quads,
                std::uint32_t lanes, bool* unsupported)
{
    *unsupported = false;
    const auto color = make_color_target(dev);
    const auto view  = make_view(dev, color);
    EXPECT_TRUE(color.is_valid() && view.is_valid());

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    EXPECT_NE(cmd, nullptr);
    cmd->begin();
    cd::rhi::ColorAttachmentInfo catt {};
    catt.view = view; catt.load_op = cd::rhi::LoadOp::kClear; catt.store_op = cd::rhi::StoreOp::kStore;
    catt.clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
    rp.render_area.extent = { kW, kH };

    auto rec = cmd->begin_parallel_render_pass(rp, lanes);
    if (rec == nullptr)
    {
        *unsupported = true;
        dev.destroy_texture_view(view);
        dev.destroy_texture(color);
        return {};
    }
    EXPECT_EQ(rec->lane_count(), lanes);

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
    to_read.texture = color; to_read.from = cd::rhi::ResourceState::kColorAttachment;
    to_read.to = cd::rhi::ResourceState::kShaderResource; to_read.range = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_read, 1));
    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();

    auto bytes = read_image(dev, color);
    dev.destroy_texture_view(view);
    dev.destroy_texture(color);
    return bytes;
}

void run_identity(bool tiled, std::uint32_t k, std::uint32_t lanes)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_vulkan_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Vulkan ICD on this host";

    Fixture fx {};
    if (!make_fixture(*dev, &fx))
        FAIL() << "Vulkan fixture creation failed";

    const auto quads = build_quads(k, tiled);

    const auto serial = render_serial(*dev, fx, quads);
    bool unsupported = false;
    const auto parallel = render_parallel(*dev, fx, quads, lanes, &unsupported);
    if (unsupported)
    {
        fx.destroy(*dev);
        FAIL() << "begin_parallel_render_pass returned nullptr — Vulkan secondary "
                  "lane path not wired";
    }

    ASSERT_EQ(serial.size(), parallel.size());
    ASSERT_FALSE(serial.empty());
    EXPECT_EQ(serial, parallel)
        << "Vulkan parallel pass (" << lanes << " lanes, " << k
        << " quads, tiled=" << tiled
        << ") must be byte-identical to the serial render";

    fx.destroy(*dev);
}

}  // namespace

// Disjoint tiles — a dropped lane leaves a tile at the clear colour.
TEST(VulkanParallelPixel, TiledSerialEqualsParallel_4quads_4lanes)
{
    run_identity(/*tiled=*/true, /*k=*/4, /*lanes=*/4);
}

// More quads than lanes — round-robin packing + per-lane order must hold.
TEST(VulkanParallelPixel, TiledSerialEqualsParallel_8quads_3lanes)
{
    run_identity(/*tiled=*/true, /*k=*/8, /*lanes=*/3);
}

// Overlapping full-screen quads — last-write-wins makes lane ORDER observable.
TEST(VulkanParallelPixel, OverlapLastWriteWins_6quads_2lanes)
{
    run_identity(/*tiled=*/false, /*k=*/6, /*lanes=*/2);
}

// Single lane must equal serial trivially.
TEST(VulkanParallelPixel, SingleLaneEqualsSerial)
{
    run_identity(/*tiled=*/true, /*k=*/4, /*lanes=*/1);
}
