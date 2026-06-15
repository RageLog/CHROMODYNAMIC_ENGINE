// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_face_cull_parity.cpp
//
// STRAND 2 (parity1121): D3D12 FACE-CULL parity under the NEGATIVE-HEIGHT
// viewport (phase1196).
//
// BACKGROUND. The engine authors clip space in the Vulkan +Y-down NDC
// convention; the reference Vulkan backend uses a positive-height viewport so
// NDC +Y maps straight to framebuffer-down. D3D12's NDC is +Y-up, so
// set_viewport maps with a NEGATIVE height (TopLeftY = y+height, Height =
// -height) to match Vulkan pixel-for-pixel (D16 byte-exact). That Y-axis flip
// in the NDC->window transform INVERTS the window-space signed area the
// rasterizer uses for front/back-face determination — so the SAME clip-space
// triangle arrives at the D3D12 rasterizer with the OPPOSITE apparent winding
// versus Vulkan. If D3D12_RASTERIZER_DESC::FrontCounterClockwise honored the
// engine descriptor DIRECTLY (the pre-fix behavior), a triangle the engine
// intends as FRONT (front_face=kClockwise) would be classified BACK by D3D12
// and CULLED by cull=kBack — invisible on D3D12, visible on Vulkan. That is a
// face-cull parity bug. The fix INVERTS FrontCounterClockwise to compensate
// the viewport flip (D3D12Device.cpp graphics + mesh PSO sites).
//
// WHAT THIS TEST PROVES (real WARP render, no Vulkan device needed — the bug
// lives entirely on the D3D12 flip side, so an intra-backend invariant pins it
// and remains stable on hosts without a Vulkan ICD):
//
//   The engine ships front_face = kClockwise as the default precisely so that
//   back-face culling WORKS for engine-authored geometry. So for ANY triangle:
//     * cull = kNone  -> visible            (baseline: the triangle covers the
//                                             centre pixel at all)
//     * cull = kBack  -> visible IFF the triangle is FRONT-facing
//     * cull = kFront -> visible IFF the triangle is BACK-facing
//   and {kBack culled} XOR {kFront culled} must hold (exactly one of the two
//   single-sided modes culls a given non-degenerate triangle).
//
//   We author ONE triangle that is FRONT-facing under the engine convention
//   and assert: visible under kNone AND kBack, culled under kFront. Then a
//   REVERSED-winding triangle: visible under kNone AND kFront, culled under
//   kBack. With the pre-fix (un-compensated) FrontCounterClockwise the kBack /
//   kFront roles SWAP — the front triangle vanishes under kBack — so this test
//   FAILS on the bug and PASSES on the fix.
//
// The "front-facing under the engine convention" triangle is derived by the
// SAME logic the engine uses: vertices wound clockwise in the Vulkan-NDC
// (+Y-down) clip space, which after the neg-height D3D12 viewport flip lands as
// the front face when FrontCounterClockwise is correctly inverted.
//
// Real D3D12 backend (WARP if no hardware adapter). GTEST_SKIP when no adapter
// / no glslang / no dxcompiler.dll. Pattern: Arrange/Act/Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Barriers.hpp>
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

// A vertex shader that emits a SMALL screen-centred triangle from
// gl_VertexIndex, with the winding selected by push-constant DWORD 0
// (u_reverse: 0 = front-wound under the engine convention, 1 = reversed).
//
// The three positions span the centre of clip space so the rasterized triangle
// covers the centre pixel (which we read back). The winding here is authored in
// the Vulkan-NDC (+Y-down) clip space the engine targets; when u_reverse != 0
// we swap two vertices to flip the winding.
constexpr const char* kCullVS = R"glsl(
#version 450
layout(push_constant) uniform Push { uint u_reverse; } pc;
void main()
{
    // Three clip-space positions forming a centred triangle. v0/v1/v2 wound so
    // that, in the Vulkan +Y-down NDC the engine authors in, the triangle is
    // FRONT under front_face = kClockwise (the engine default).
    vec2 verts[3] = vec2[3](
        vec2( 0.0,  0.8),   // top    (NDC +Y is DOWN, so this is screen-bottom)
        vec2( 0.8, -0.8),   // right-up
        vec2(-0.8, -0.8)    // left-up
    );
    // Reverse winding by swapping v1 and v2 when requested.
    int idx = int(gl_VertexIndex);
    if (pc.u_reverse != 0u)
    {
        if (idx == 1) idx = 2;
        else if (idx == 2) idx = 1;
    }
    gl_Position = vec4(verts[idx], 0.0, 1.0);
}
)glsl";

constexpr const char* kCullFS = R"glsl(
#version 450
layout(location = 0) out vec4 o;
void main() { o = vec4(1.0, 1.0, 1.0, 1.0); }   // white where the triangle wins
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
            *skip = true;  // toolchain DLL missing — environment gap
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

struct Rgba8 { std::uint8_t r, g, b, a; };

[[nodiscard]] Rgba8
read_center_texel(cd::rhi::IDevice& dev, cd::rhi::TextureHandle tex)
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

    const std::size_t center =
        (static_cast<std::size_t>(kH / 2) * kW + (kW / 2)) * 4u;
    const Rgba8 px {
        std::to_integer<std::uint8_t>(raw[center + 0]),
        std::to_integer<std::uint8_t>(raw[center + 1]),
        std::to_integer<std::uint8_t>(raw[center + 2]),
        std::to_integer<std::uint8_t>(raw[center + 3]),
    };
    dev.destroy_buffer(buf);
    return px;
}

// Render the centred triangle with the given winding + cull mode; returns true
// if the centre pixel ended up WHITE (the triangle was rasterized, i.e. not
// culled), false if it stayed BLACK (culled or off-centre). front_face is the
// engine default kClockwise throughout (the convention the whole tree authors
// in), so this exercises the EXACT cull semantics the engine relies on.
[[nodiscard]] bool triangle_visible(cd::rhi::IDevice& dev,
                                    cd::rhi::CullMode cull,
                                    std::uint32_t reverse_winding,
                                    bool* skip)
{
    const auto vs = make_module(dev, cd::rhi::ShaderStage::kVertex, kCullVS, skip);
    const auto fs = make_module(dev, cd::rhi::ShaderStage::kFragment, kCullFS, skip);
    if (*skip)
        return false;
    EXPECT_TRUE(vs.is_valid());
    EXPECT_TRUE(fs.is_valid());

    cd::rhi::PushConstantRange pcr {};
    pcr.offset = 0;
    pcr.size   = 16;  // one uint, over-provisioned to a 16B alignment.
    pcr.stages = cd::rhi::ShaderStage::kAllGraphics;
    cd::rhi::PipelineLayoutDesc pld {};
    pld.push_constants = std::span<const cd::rhi::PushConstantRange>(&pcr, 1);
    const auto layout = *dev.create_pipeline_layout(pld);

    const auto color = make_color_target(dev);
    EXPECT_TRUE(color.is_valid());
    const auto color_view = make_view(dev, color);
    EXPECT_TRUE(color_view.is_valid());

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout          = layout;
    gpd.vertex_shader   = vs;
    gpd.fragment_shader = fs;
    gpd.topology        = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull     = cull;
    // front_face left at the engine default (kClockwise) — the convention the
    // whole tree authors in. This is the value the fix compensates for.
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    gpd.color_attachment_formats  = color_fmts;
    auto pso_r = dev.create_graphics_pipeline(gpd);
    EXPECT_TRUE(pso_r.has_value())
        << (pso_r.has_value() ? std::string {}
                              : std::string(pso_r.error().message.begin(),
                                            pso_r.error().message.end()));
    const auto pso = *pso_r;

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    cmd->begin();

    cd::rhi::ColorAttachmentInfo catt {};
    catt.view        = color_view;
    catt.load_op     = cd::rhi::LoadOp::kClear;
    catt.store_op    = cd::rhi::StoreOp::kStore;
    catt.clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } };  // black background

    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
    rp.render_area.extent = { kW, kH };

    cmd->begin_render_pass(rp);
    cmd->set_viewport({ 0, 0, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F });
    cmd->set_scissor(cd::rhi::Rect2D { {}, { kW, kH } });
    cmd->bind_graphics_pipeline(pso);
    cmd->push_constants(layout, cd::rhi::ShaderStage::kAllGraphics, 0,
                        sizeof(std::uint32_t), &reverse_winding);
    cmd->draw(3, 1, 0, 0);
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

    const Rgba8 px = read_center_texel(dev, color);

    dev.destroy_texture_view(color_view);
    dev.destroy_texture(color);
    dev.destroy_graphics_pipeline(pso);
    dev.destroy_pipeline_layout(layout);
    dev.destroy_shader_module(vs);
    dev.destroy_shader_module(fs);

    // White centre => triangle rasterized (not culled).
    return px.r > 200 && px.g > 200 && px.b > 200;
}

// The decisive parity assertion. A FRONT-wound (engine convention) triangle
// must survive cull=kBack and be killed by cull=kFront; a REVERSED triangle
// the opposite. With the un-compensated FrontCounterClockwise (pre-fix) the
// neg-height viewport flips the winding the rasterizer sees, swapping the
// kBack/kFront roles — the front triangle vanishes under kBack and this test
// FAILS. With the compensating inversion it PASSES, matching Vulkan.
TEST(D3D12FaceCullParity, FrontFaceSurvivesBackCullUnderNegHeightViewport)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    bool skip = false;

    // Baseline: with NO cull, the front-wound triangle must cover the centre —
    // proves the geometry+viewport actually rasterize there (guards a degenerate
    // / off-screen authoring mistake from silently masking the cull assertions).
    const bool front_none = triangle_visible(d, cd::rhi::CullMode::kNone, 0u, &skip);
    if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    EXPECT_TRUE(front_none) << "front-wound triangle must cover the centre pixel under cull=kNone";

    // FRONT-wound triangle: survives kBack (it IS the front face), culled by kFront.
    const bool front_back  = triangle_visible(d, cd::rhi::CullMode::kBack,  0u, &skip);
    const bool front_front = triangle_visible(d, cd::rhi::CullMode::kFront, 0u, &skip);
    if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";

    EXPECT_TRUE(front_back)
        << "BUG: engine front_face=kClockwise triangle was CULLED by cull=kBack on D3D12 — "
           "FrontCounterClockwise is not compensated for the neg-height viewport flip "
           "(this is what Vulkan keeps VISIBLE).";
    EXPECT_FALSE(front_front)
        << "a front-facing triangle must be removed by cull=kFront";

    // Exactly one single-sided mode culls a given non-degenerate triangle.
    EXPECT_NE(front_back, front_front)
        << "kBack and kFront must disagree for a non-degenerate triangle";
}

// Mirror with a REVERSED-wound triangle: it is the BACK face under the engine
// convention, so kFront keeps it, kBack culls it. Locks the symmetry so a future
// regression that simply force-sets FrontCounterClockwise can't pass by luck.
TEST(D3D12FaceCullParity, ReversedTriangleIsBackFaceUnderNegHeightViewport)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    bool skip = false;

    const bool rev_none  = triangle_visible(d, cd::rhi::CullMode::kNone,  1u, &skip);
    if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    EXPECT_TRUE(rev_none) << "reversed triangle must still cover the centre under cull=kNone";

    const bool rev_back  = triangle_visible(d, cd::rhi::CullMode::kBack,  1u, &skip);
    const bool rev_front = triangle_visible(d, cd::rhi::CullMode::kFront, 1u, &skip);
    if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";

    EXPECT_FALSE(rev_back)
        << "a reversed (back-facing) triangle must be removed by cull=kBack";
    EXPECT_TRUE(rev_front)
        << "a reversed (back-facing) triangle must survive cull=kFront";
    EXPECT_NE(rev_back, rev_front)
        << "kBack and kFront must disagree for a non-degenerate triangle";
}

}  // namespace

#endif  // _WIN32
