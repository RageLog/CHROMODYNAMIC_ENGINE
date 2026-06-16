// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_face_cull_parity.cpp
//
// Backend-to-100 Wave 4b / C-METAL-CULL: Metal FACE-CULL parity under the
// negative-height / flipped-viewport convention (phase1209) — the Metal analog
// of test_d3d12_face_cull_parity.cpp (phase1196).
//
// BACKGROUND (mirrors the D3D12 story, CORRECTED by parity1224). The engine
// authors clip space in the Vulkan +Y-down NDC convention. Metal's NDC is +Y-up
// (like D3D12), so the Metal backend applies a NEGATIVE-HEIGHT viewport
// (MetalCommandBuffer.mm, originY=y+h, height=-h) so the SAME source geometry
// lands at BYTE-IDENTICAL window pixels as Vulkan's +Y-down NDC + positive
// viewport. Because the window pixels are identical, the window-space signed
// area — and therefore the facing the rasterizer computes — is IDENTICAL to
// Vulkan. So MTLWinding must HONOR front_face DIRECTLY (kClockwise ->
// MTLWindingClockwise), exactly like Vulkan VkFrontFace and the corrected D3D12
// FrontCounterClockwise. NO inversion. The earlier phase1209 inversion (mirroring
// the D3D12 phase1204 inversion) was disproven: parity1224's empirical RTX-3080
// RCA showed the analogous D3D12 inversion MIS-culled real engine geometry; the
// inversion was removed on D3D12 and the identical fix applies to Metal (same
// negative-height mechanism). See MetalPipeline.mm to_winding +
// ADR-20260615-ndc-y-handedness.
//
// WHAT THIS TEST PROVES (Metal cull facing matches the cross-backend convention;
// run on Apple hardware — C-METAL-CULL):
//   For the engine default front_face = kClockwise, ANY non-degenerate triangle:
//     * cull = kNone  -> visible (covers the centre pixel at all)
//     * cull = kBack  -> visible IFF the triangle is FRONT-facing
//     * cull = kFront -> visible IFF the triangle is BACK-facing
//   and {kBack culled} XOR {kFront culled} holds. The raw winding
//   {(0,0.8),(0.8,-0.8),(-0.8,-0.8)} (u_reverse=0) is the BACK face under the
//   engine convention (Vulkan culls it under cull=kBack — Metal now matches);
//   the reversed winding (u_reverse=1) is the FRONT face. Re-introducing the
//   phase1209 MTLWinding inversion SWAPS the kBack/kFront roles -> this FAILS on
//   the inversion and PASSES honoring the descriptor directly.
//
// PLATFORM GATE (see test_metal_device.cpp for the full rationale): real
// MTLDevice on Apple; skip-stub everywhere else. GLSL is cross-compiled to MSL
// in-device via create_shader_module(kGlsl).
//
// Pattern: Arrange / Act / Assert.
// =============================================================================
#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)

    #include <cd/rhi/Barriers.hpp>
    #include <cd/rhi/Descriptors.hpp>
    #include <cd/rhi/Enums.hpp>
    #include <cd/rhi/Format.hpp>
    #include <cd/rhi/Handles.hpp>
    #include <cd/rhi/ICommandBuffer.hpp>
    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/Pipeline.hpp>
    #include <cd/rhi/metal/MetalDevice.hpp>

    #include <gtest/gtest.h>

    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <memory>
    #include <span>
    #include <string>
    #include <vector>

namespace
{

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

// Centred triangle wound CW under the Vulkan +Y-down NDC the engine authors in;
// push-constant DWORD 0 (u_reverse) swaps two vertices to flip the winding.
constexpr const char* kCullVS = R"glsl(
#version 450
layout(push_constant) uniform Push { uint u_reverse; } pc;
void main()
{
    vec2 verts[3] = vec2[3](
        vec2( 0.0,  0.8),
        vec2( 0.8, -0.8),
        vec2(-0.8, -0.8)
    );
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
void main() { o = vec4(1.0, 1.0, 1.0, 1.0); }
)glsl";

struct Rgba8 { std::uint8_t r, g, b, a; };

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_metal_device_or_null()
{
    cd::rhi::metal::MetalCreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::metal::create_metal_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
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
    EXPECT_TRUE(dev.copy_image_to_buffer(tex, buf, 0, region).has_value());

    std::vector<std::byte> raw(static_cast<std::size_t>(kBytes));
    EXPECT_TRUE(dev.download_buffer(buf, 0, std::span<std::byte> { raw }).has_value());

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
// when the centre pixel ended up WHITE (rasterized, not culled). front_face is
// the engine default kClockwise throughout.
[[nodiscard]] bool triangle_visible(cd::rhi::IDevice& dev,
                                    cd::rhi::CullMode cull,
                                    std::uint32_t reverse_winding)
{
    const auto vs = make_module(dev, cd::rhi::ShaderStage::kVertex, kCullVS);
    const auto fs = make_module(dev, cd::rhi::ShaderStage::kFragment, kCullFS);
    EXPECT_TRUE(vs.is_valid());
    EXPECT_TRUE(fs.is_valid());

    cd::rhi::PushConstantRange pcr {};
    pcr.offset = 0;
    pcr.size   = 16;
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
    gpd.layout                    = layout;
    gpd.vertex_shader             = vs;
    gpd.fragment_shader           = fs;
    gpd.topology                  = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull               = cull;
    // front_face left at the engine default (kClockwise) — the value the
    // Metal-side winding-inversion fix compensates for.
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    gpd.color_attachment_formats  = color_fmts;
    const auto pso = *dev.create_graphics_pipeline(gpd);

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    cmd->begin();

    cd::rhi::ColorAttachmentInfo catt {};
    catt.view        = color_view;
    catt.load_op     = cd::rhi::LoadOp::kClear;
    catt.store_op    = cd::rhi::StoreOp::kStore;
    catt.clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } };

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

    return px.r > 200 && px.g > 200 && px.b > 200;
}

// ---- The decisive parity assertion: the raw winding is the BACK face ----------
// (u_reverse=0 = {(0,0.8),(0.8,-0.8),(-0.8,-0.8)} = BACK face under front_face=
// kClockwise; Vulkan culls it under cull=kBack — Metal now matches honoring the
// descriptor directly. Re-introducing the phase1209 inversion swaps the roles.)
TEST(MetalFaceCullParity, RawWindingIsBackFaceUnderFlippedViewport)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    // Baseline: with NO cull the raw-wound triangle covers the centre.
    EXPECT_TRUE(triangle_visible(d, cd::rhi::CullMode::kNone, 0u))
        << "raw-wound triangle must cover the centre under cull=kNone";

    const bool back_back  = triangle_visible(d, cd::rhi::CullMode::kBack,  0u);
    const bool back_front = triangle_visible(d, cd::rhi::CullMode::kFront, 0u);

    EXPECT_FALSE(back_back)
        << "the raw winding is the BACK face (Vulkan convention) — must be CULLED "
           "by cull=kBack on Metal; if VISIBLE, the phase1209 MTLWinding inversion "
           "has crept back (it mis-matches the corrected D3D12/Vulkan polarity).";
    EXPECT_TRUE(back_front)
        << "a back-facing triangle must survive cull=kFront";
    EXPECT_NE(back_back, back_front)
        << "kBack and kFront must disagree for a non-degenerate triangle";
}

// Mirror with the REVERSED-wound triangle (the FRONT face under the engine
// convention): visible under kBack, culled by kFront.
TEST(MetalFaceCullParity, ReversedWindingIsFrontFaceUnderFlippedViewport)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    EXPECT_TRUE(triangle_visible(d, cd::rhi::CullMode::kNone, 1u))
        << "reversed triangle must still cover the centre under cull=kNone";

    const bool rev_back  = triangle_visible(d, cd::rhi::CullMode::kBack,  1u);
    const bool rev_front = triangle_visible(d, cd::rhi::CullMode::kFront, 1u);

    EXPECT_TRUE(rev_back)
        << "the reversed winding is the FRONT face — must be VISIBLE under cull=kBack";
    EXPECT_FALSE(rev_front)
        << "a front-facing triangle must be removed by cull=kFront";
    EXPECT_NE(rev_back, rev_front)
        << "kBack and kFront must disagree for a non-degenerate triangle";
}

}  // namespace

#else  // not (Apple && CD_RHI_METAL_ENABLED)

    #include <gtest/gtest.h>

TEST(MetalFaceCullParity, SkippedOffApple)
{
    GTEST_SKIP() << "Metal backend disabled on this platform (Apple-only)";
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
