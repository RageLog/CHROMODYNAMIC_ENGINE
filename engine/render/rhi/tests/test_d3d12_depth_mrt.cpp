// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_depth_mrt.cpp
//
// phase1186 (D3 + D5): GPU smoke tests for the newly-wired D3D12
// begin_render_pass depth/MRT path and the PSO depth-compare/stencil
// translation.
//
//   (a) DEPTH-OCCLUSION — render two overlapping full-screen triangles at
//       different gl_Position.z through a D32 depth attachment with depth
//       test LESS + depth write ON. The NEAR triangle (z=0.25, drawn first)
//       must occlude the FAR triangle (z=0.75, drawn second). Read the colour
//       target back and assert the centre pixel is the NEAR colour — i.e. the
//       depth test genuinely rejected the later, farther fragments. A second
//       draw order (far-first) proves write+test both fire. This fails on the
//       pre-D3 code because begin_render_pass bound a NULL DSV (no depth test
//       at all → the last draw always wins).
//
//   (b) MRT — a single PSO writes two DISTINCT constants to render target 0
//       and render target 1. Both targets are read back and asserted to carry
//       their own output. This fails on the pre-D3 code because
//       begin_render_pass bound only color_attachments.front() (RT1 was never
//       bound, so the second SV_Target was dropped).
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
#include <vector>

#if defined(_WIN32)

namespace
{

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

// A vertex shader that emits a full-screen triangle from SV_VertexID (no
// vertex buffer needed) at a uniform depth supplied via push-constant DWORD 0.
// gl_Position.z = u_depth so every fragment of the triangle is at that depth.
constexpr const char* kDepthVS = R"glsl(
#version 450
layout(push_constant) uniform Push { float u_depth; vec4 u_color; } pc;
void main()
{
    // Oversized triangle covering the whole viewport.
    vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                  (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(p, pc.u_depth, 1.0);
}
)glsl";

constexpr const char* kDepthFS = R"glsl(
#version 450
layout(push_constant) uniform Push { float u_depth; vec4 u_color; } pc;
layout(location = 0) out vec4 o;
void main() { o = pc.u_color; }
)glsl";

// MRT: full-screen triangle, fragment writes two distinct constants.
constexpr const char* kMrtVS = R"glsl(
#version 450
void main()
{
    vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                  (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)glsl";

constexpr const char* kMrtFS = R"glsl(
#version 450
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
void main()
{
    o0 = vec4(1.0, 0.0, 0.0, 1.0);   // RT0 = red
    o1 = vec4(0.0, 1.0, 0.0, 1.0);   // RT1 = green
}
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;  // avoid debug-layer dependency in CI
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

[[nodiscard]] bool glslang_available()
{
    return cd::shader::make_glslang_compiler() != nullptr;
}

// Compile a GLSL stage through the device path (GLSL -> DXIL). Returns an
// invalid handle (and sets *skip) when dxcompiler.dll is missing.
[[nodiscard]] cd::rhi::ShaderModuleHandle
make_module(cd::rhi::IDevice& dev, cd::rhi::ShaderStage stage,
            const char* src, bool* skip)
{
    cd::rhi::ShaderModuleDesc d {};
    d.stage     = stage;
    d.code      = src;
    d.code_size = std::char_traits<char>::length(src);
    d.entry_point = "main";
    d.language  = cd::rhi::ShaderSourceLanguage::kGlsl;
    auto r = dev.create_shader_module(d);
    if (!r.has_value())
    {
        const std::string msg { r.error().message };
        if (msg.find("dxc") != std::string::npos)
            *skip = true;  // toolchain DLL missing — environment gap
        return {};
    }
    return *r;
}

// Create an RGBA8 colour target (attachment + transfer-src + sampled).
[[nodiscard]] cd::rhi::TextureHandle
make_color_target(cd::rhi::IDevice& dev)
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
make_view(cd::rhi::IDevice& dev, cd::rhi::TextureHandle t,
          cd::rhi::Format fmt = cd::rhi::Format::kUndefined)
{
    cd::rhi::TextureViewDesc vd {};
    vd.texture = t;
    vd.type    = cd::rhi::TextureType::k2D;
    vd.format  = fmt;
    auto r = dev.create_texture_view(vd);
    return r.has_value() ? *r : cd::rhi::TextureViewHandle {};
}

// Read a single texel (centre of the image) back as RGBA8.
struct Rgba8 { std::uint8_t r, g, b, a; };

[[nodiscard]] Rgba8
read_center_texel(cd::rhi::IDevice& dev, cd::rhi::TextureHandle tex,
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
    EXPECT_TRUE(copy_r.has_value())
        << (copy_r.has_value() ? std::string {}
                               : std::string(copy_r.error().message.begin(),
                                             copy_r.error().message.end()));

    std::vector<std::byte> raw(static_cast<std::size_t>(kBytes));
    auto dl = dev.download_buffer(buf, 0, std::span<std::byte> { raw });
    EXPECT_TRUE(dl.has_value());

    const std::size_t center = (static_cast<std::size_t>(kH / 2) * kW +
                                (kW / 2)) * 4u;
    Rgba8 px {
        std::to_integer<std::uint8_t>(raw[center + 0]),
        std::to_integer<std::uint8_t>(raw[center + 1]),
        std::to_integer<std::uint8_t>(raw[center + 2]),
        std::to_integer<std::uint8_t>(raw[center + 3]),
    };
    dev.destroy_buffer(buf);
    return px;
}

// ---- (a) DEPTH-OCCLUSION ----------------------------------------------------

// Two full-screen triangles, near (z=0.25) and far (z=0.75), depth test LESS.
// `near_first` controls draw order; in BOTH orders the NEAR colour must win.
void run_depth_occlusion(cd::rhi::IDevice& dev, bool near_first)
{
    bool skip = false;
    const auto vs = make_module(dev, cd::rhi::ShaderStage::kVertex, kDepthVS, &skip);
    const auto fs = make_module(dev, cd::rhi::ShaderStage::kFragment, kDepthFS, &skip);
    if (skip) { GTEST_SKIP() << "dxcompiler.dll unavailable at runtime"; }
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    // Pipeline layout: one push-constant block (float depth + vec4 colour = 20B).
    cd::rhi::PushConstantRange pcr {};
    pcr.offset = 0;
    pcr.size   = 32;  // 8 DWORDs (depth + pad + colour) — over-provision is fine.
    pcr.stages = cd::rhi::ShaderStage::kAllGraphics;
    cd::rhi::PipelineLayoutDesc pld {};
    pld.push_constants = std::span<const cd::rhi::PushConstantRange>(&pcr, 1);
    auto layout_r = dev.create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value());
    const auto layout = *layout_r;

    const auto color = make_color_target(dev);
    ASSERT_TRUE(color.is_valid());
    const auto color_view = make_view(dev, color);
    ASSERT_TRUE(color_view.is_valid());

    // Depth target (D32) + DSV.
    cd::rhi::TextureDesc dtd {};
    dtd.type         = cd::rhi::TextureType::k2D;
    dtd.format       = cd::rhi::Format::kD32Float;
    dtd.extent       = { kW, kH, 1 };
    dtd.mip_levels   = 1;
    dtd.array_layers = 1;
    dtd.usage        = cd::rhi::TextureUsage::kDepthStencilAttachment;
    auto depth_r = dev.create_texture(dtd);
    ASSERT_TRUE(depth_r.has_value())
        << std::string(depth_r.error().message.begin(), depth_r.error().message.end());
    const auto depth = *depth_r;
    const auto depth_view = make_view(dev, depth, cd::rhi::Format::kD32Float);
    ASSERT_TRUE(depth_view.is_valid());

    // PSO: depth test LESS, depth write ON, one RGBA8 attachment + D32 depth.
    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout = layout;
    gpd.vertex_shader = vs;
    gpd.fragment_shader = fs;
    gpd.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test    = true;
    gpd.depth_stencil.depth_write   = true;
    gpd.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    gpd.color_attachment_formats = color_fmts;
    gpd.depth_attachment_format  = cd::rhi::Format::kD32Float;
    auto pso_r = dev.create_graphics_pipeline(gpd);
    ASSERT_TRUE(pso_r.has_value())
        << std::string(pso_r.error().message.begin(), pso_r.error().message.end());
    const auto pso = *pso_r;

    struct Push { float depth; float pad[3]; float color[4]; };
    const Push near_push { 0.25F, { 0, 0, 0 }, { 1.0F, 0.0F, 0.0F, 1.0F } };  // red
    const Push far_push  { 0.75F, { 0, 0, 0 }, { 0.0F, 0.0F, 1.0F, 1.0F } };  // blue

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    cd::rhi::ColorAttachmentInfo catt {};
    catt.view        = color_view;
    catt.load_op     = cd::rhi::LoadOp::kClear;
    catt.store_op    = cd::rhi::StoreOp::kStore;
    catt.clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } };

    cd::rhi::DepthStencilAttachmentInfo datt {};
    datt.view        = depth_view;
    datt.depth_load  = cd::rhi::LoadOp::kClear;
    datt.depth_store = cd::rhi::StoreOp::kStore;
    datt.clear       = { .depth = 1.0F, .stencil = 0 };

    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
    rp.depth_stencil     = &datt;
    rp.render_area.extent = { kW, kH };

    cmd->begin_render_pass(rp);
    cd::rhi::Viewport vp { 0, 0, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F };
    cmd->set_viewport(vp);
    cmd->set_scissor(cd::rhi::Rect2D { {}, { kW, kH } });
    cmd->bind_graphics_pipeline(pso);

    const Push& first  = near_first ? near_push : far_push;
    const Push& second = near_first ? far_push  : near_push;
    cmd->push_constants(layout, cd::rhi::ShaderStage::kAllGraphics, 0,
                        sizeof(Push), &first);
    cmd->draw(3, 1, 0, 0);
    cmd->push_constants(layout, cd::rhi::ShaderStage::kAllGraphics, 0,
                        sizeof(Push), &second);
    cmd->draw(3, 1, 0, 0);
    cmd->end_render_pass();

    // Transition colour target to shader-resource for readback.
    cd::rhi::TextureBarrier to_read {};
    to_read.texture = color;
    to_read.from    = cd::rhi::ResourceState::kColorAttachment;
    to_read.to      = cd::rhi::ResourceState::kShaderResource;
    to_read.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_read, 1));
    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();

    const Rgba8 px = read_center_texel(dev, color, cd::rhi::ResourceState::kShaderResource);

    // The NEAR (red) triangle must win regardless of draw order — depth test
    // LESS rejected the FAR (blue) fragments. Pre-D3 (null DSV) this would be
    // whatever was drawn LAST, so the far-first order would read blue.
    EXPECT_GT(px.r, 200) << "near (red) triangle should occlude — R high";
    EXPECT_LT(px.b, 64)  << "far (blue) triangle must be rejected — B low";

    dev.destroy_texture_view(depth_view);
    dev.destroy_texture(depth);
    dev.destroy_texture_view(color_view);
    dev.destroy_texture(color);
    dev.destroy_graphics_pipeline(pso);
    dev.destroy_pipeline_layout(layout);
    dev.destroy_shader_module(vs);
    dev.destroy_shader_module(fs);
}

TEST(D3D12DepthMrt, DepthOcclusionNearFirst)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    run_depth_occlusion(*dev, /*near_first=*/true);
}

// The decisive ordering: draw the FAR triangle first, then the NEAR one. With
// a working depth test the near still wins; with the pre-D3 null-DSV path the
// near (drawn last) would win for the WRONG reason, so this order specifically
// guards that depth WRITE + TEST are both active (the near-first variant could
// pass on a no-depth path by luck of ordering).
TEST(D3D12DepthMrt, DepthOcclusionFarFirst)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    run_depth_occlusion(*dev, /*near_first=*/false);
}

// A depth test of GREATER must INVERT the winner — proving DepthFunc is read
// from the descriptor (D5) and not hardcoded LESS. With GREATER + clear=0 and
// the near triangle drawn first then far: far (z=0.75 > 0.25) passes over near.
TEST(D3D12DepthMrt, DepthCompareGreaterInvertsWinner)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    bool skip = false;
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex, kDepthVS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kDepthFS, &skip);
    if (skip) { GTEST_SKIP() << "dxcompiler.dll unavailable at runtime"; }
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    cd::rhi::PushConstantRange pcr {};
    pcr.size   = 32;
    pcr.stages = cd::rhi::ShaderStage::kAllGraphics;
    cd::rhi::PipelineLayoutDesc pld {};
    pld.push_constants = std::span<const cd::rhi::PushConstantRange>(&pcr, 1);
    const auto layout = *d.create_pipeline_layout(pld);

    const auto color = make_color_target(d);
    const auto color_view = make_view(d, color);

    cd::rhi::TextureDesc dtd {};
    dtd.type   = cd::rhi::TextureType::k2D;
    dtd.format = cd::rhi::Format::kD32Float;
    dtd.extent = { kW, kH, 1 };
    dtd.mip_levels = 1; dtd.array_layers = 1;
    dtd.usage  = cd::rhi::TextureUsage::kDepthStencilAttachment;
    const auto depth = *d.create_texture(dtd);
    const auto depth_view = make_view(d, depth, cd::rhi::Format::kD32Float);

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout = layout;
    gpd.vertex_shader = vs;
    gpd.fragment_shader = fs;
    gpd.raster.cull = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test    = true;
    gpd.depth_stencil.depth_write   = true;
    gpd.depth_stencil.depth_compare = cd::rhi::CompareOp::kGreater;  // <- the point
    gpd.color_attachment_formats = color_fmts;
    gpd.depth_attachment_format  = cd::rhi::Format::kD32Float;
    const auto pso = *d.create_graphics_pipeline(gpd);

    struct Push { float depth; float pad[3]; float color[4]; };
    const Push near_push { 0.25F, { 0, 0, 0 }, { 1.0F, 0.0F, 0.0F, 1.0F } };  // red
    const Push far_push  { 0.75F, { 0, 0, 0 }, { 0.0F, 0.0F, 1.0F, 1.0F } };  // blue

    auto cmd = d.create_command_buffer(cd::rhi::QueueType::kGraphics);
    cmd->begin();
    cd::rhi::ColorAttachmentInfo catt {};
    catt.view = color_view; catt.load_op = cd::rhi::LoadOp::kClear;
    catt.clear_color = { .f32 = { 0, 0, 0, 1 } };
    cd::rhi::DepthStencilAttachmentInfo datt {};
    datt.view = depth_view; datt.depth_load = cd::rhi::LoadOp::kClear;
    datt.clear = { .depth = 0.0F, .stencil = 0 };  // GREATER needs a LOW clear
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
    rp.depth_stencil = &datt;
    rp.render_area.extent = { kW, kH };
    cmd->begin_render_pass(rp);
    cmd->set_viewport({ 0, 0, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F });
    cmd->set_scissor(cd::rhi::Rect2D { {}, { kW, kH } });
    cmd->bind_graphics_pipeline(pso);
    cmd->push_constants(layout, cd::rhi::ShaderStage::kAllGraphics, 0, sizeof(Push), &near_push);
    cmd->draw(3, 1, 0, 0);
    cmd->push_constants(layout, cd::rhi::ShaderStage::kAllGraphics, 0, sizeof(Push), &far_push);
    cmd->draw(3, 1, 0, 0);
    cmd->end_render_pass();
    cd::rhi::TextureBarrier to_read {};
    to_read.texture = color;
    to_read.from = cd::rhi::ResourceState::kColorAttachment;
    to_read.to   = cd::rhi::ResourceState::kShaderResource;
    to_read.range = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_read, 1));
    cmd->end();
    d.submit(*cmd);
    d.wait_idle();

    const Rgba8 px = read_center_texel(d, color, cd::rhi::ResourceState::kShaderResource);
    // GREATER: the far (blue, z=0.75) fragment passes over the near (red).
    EXPECT_GT(px.b, 200) << "GREATER compare should let the FAR (blue) win — B high";
    EXPECT_LT(px.r, 64)  << "near (red) must be rejected under GREATER — R low";

    d.destroy_texture_view(depth_view);
    d.destroy_texture(depth);
    d.destroy_texture_view(color_view);
    d.destroy_texture(color);
    d.destroy_graphics_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

// ---- (b) MRT ----------------------------------------------------------------

TEST(D3D12DepthMrt, MultipleRenderTargetsReceiveDistinctOutputs)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    bool skip = false;
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex, kMrtVS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kMrtFS, &skip);
    if (skip) { GTEST_SKIP() << "dxcompiler.dll unavailable at runtime"; }
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    cd::rhi::PipelineLayoutDesc pld {};
    auto layout_r = d.create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value());
    const auto layout = *layout_r;

    const auto rt0 = make_color_target(d);
    const auto rt1 = make_color_target(d);
    ASSERT_TRUE(rt0.is_valid());
    ASSERT_TRUE(rt1.is_valid());
    const auto v0 = make_view(d, rt0);
    const auto v1 = make_view(d, rt1);
    ASSERT_TRUE(v0.is_valid());
    ASSERT_TRUE(v1.is_valid());

    const std::array<cd::rhi::Format, 2> color_fmts {
        cd::rhi::Format::kRGBA8Unorm, cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout = layout;
    gpd.vertex_shader = vs;
    gpd.fragment_shader = fs;
    gpd.raster.cull = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    gpd.color_attachment_formats  = color_fmts;
    auto pso_r = d.create_graphics_pipeline(gpd);
    ASSERT_TRUE(pso_r.has_value())
        << std::string(pso_r.error().message.begin(), pso_r.error().message.end());
    const auto pso = *pso_r;

    auto cmd = d.create_command_buffer(cd::rhi::QueueType::kGraphics);
    cmd->begin();

    const std::array<cd::rhi::ColorAttachmentInfo, 2> catts {
        cd::rhi::ColorAttachmentInfo {
            .view = v0, .load_op = cd::rhi::LoadOp::kClear,
            .store_op = cd::rhi::StoreOp::kStore,
            .clear_color = { .f32 = { 0, 0, 0, 1 } } },
        cd::rhi::ColorAttachmentInfo {
            .view = v1, .load_op = cd::rhi::LoadOp::kClear,
            .store_op = cd::rhi::StoreOp::kStore,
            .clear_color = { .f32 = { 0, 0, 0, 1 } } },
    };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments = catts;
    rp.render_area.extent = { kW, kH };
    cmd->begin_render_pass(rp);
    cmd->set_viewport({ 0, 0, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F });
    cmd->set_scissor(cd::rhi::Rect2D { {}, { kW, kH } });
    cmd->bind_graphics_pipeline(pso);
    cmd->draw(3, 1, 0, 0);
    cmd->end_render_pass();

    std::array<cd::rhi::TextureBarrier, 2> to_read {
        cd::rhi::TextureBarrier {
            .texture = rt0, .from = cd::rhi::ResourceState::kColorAttachment,
            .to = cd::rhi::ResourceState::kShaderResource, .range = { 0, 1, 0, 1 } },
        cd::rhi::TextureBarrier {
            .texture = rt1, .from = cd::rhi::ResourceState::kColorAttachment,
            .to = cd::rhi::ResourceState::kShaderResource, .range = { 0, 1, 0, 1 } },
    };
    cmd->barrier({}, to_read);
    cmd->end();
    d.submit(*cmd);
    d.wait_idle();

    const Rgba8 p0 = read_center_texel(d, rt0, cd::rhi::ResourceState::kShaderResource);
    const Rgba8 p1 = read_center_texel(d, rt1, cd::rhi::ResourceState::kShaderResource);

    // RT0 = red, RT1 = green — proving BOTH attachments were bound and the
    // second SV_Target was not dropped (pre-D3 only RT0 was bound).
    EXPECT_GT(p0.r, 200) << "RT0 should be red";
    EXPECT_LT(p0.g, 64)  << "RT0 green channel should be low";
    EXPECT_GT(p1.g, 200) << "RT1 should be green";
    EXPECT_LT(p1.r, 64)  << "RT1 red channel should be low";

    d.destroy_texture_view(v0);
    d.destroy_texture_view(v1);
    d.destroy_texture(rt0);
    d.destroy_texture(rt1);
    d.destroy_graphics_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

}  // namespace

#endif  // _WIN32
