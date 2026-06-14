// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_blend_stride.cpp
//
// phase1187 (D4 + D6): GPU smoke tests for the newly-wired D3D12 graphics-PSO
// per-attachment blend translation (D4) and the per-binding vertex-stride
// lookup in bind_vertex_buffer (D6).
//
//   (a) BLEND — clear the colour target to a KNOWN background (0.25 grey),
//       then draw a full-screen quad whose fragment outputs a half-alpha
//       (a=0.5) bright colour through a PSO with blend ENABLED
//       (src = SRC_ALPHA, dst = INV_SRC_ALPHA, op = ADD). The read-back centre
//       pixel must equal the *blended* math — src*0.5 + dst*0.5 — and must be
//       neither the opaque source nor the untouched clear. This fails on the
//       pre-D4 code because the PSO hardcoded BlendEnable=FALSE, so the source
//       overwrote the destination opaquely.
//
//   (b) STRIDE — a pipeline whose vertex layout has stride 28 (vec3 pos at
//       offset 0 + vec4 colour at offset 12), NOT the legacy hardcoded 24. The
//       vertex buffer packs three vertices at the 28-byte stride. The colour of
//       vertices 1 and 2 can only be fetched correctly if the VBV stride is 28;
//       a stride of 24 reads each later vertex's colour from the wrong offset
//       (overlapping the previous vertex's data), yielding the WRONG colour at
//       the read-back centre. Asserting the exact authored colour proves the
//       stride is honoured. Also covers a second pipeline at stride 16
//       (vec2 pos + vec2 data) to prove multiple distinct strides work.
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
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#if defined(_WIN32)

namespace
{

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

// ---- Shaders ---------------------------------------------------------------

// BLEND: full-screen triangle from SV_VertexID, fragment emits a constant
// half-alpha colour supplied via push constant (rgba).
constexpr const char* kBlendVS = R"glsl(
#version 450
void main()
{
    vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                  (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)glsl";

constexpr const char* kBlendFS = R"glsl(
#version 450
layout(push_constant) uniform Push { vec4 u_color; } pc;
layout(location = 0) out vec4 o;
void main() { o = pc.u_color; }
)glsl";

// STRIDE: a real vertex buffer with (vec3 pos, vec4 color). The colour comes
// from the per-vertex attribute (location 1), so it is ONLY correct when the
// VBV stride matches the authored 28-byte packing.
constexpr const char* kStrideVS = R"glsl(
#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 v_color;
void main()
{
    v_color = in_color;
    gl_Position = vec4(in_pos, 1.0);
}
)glsl";

constexpr const char* kStrideFS = R"glsl(
#version 450
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 o;
void main() { o = v_color; }
)glsl";

// STRIDE-16: (vec2 pos, vec2 data). The fragment reconstructs a colour from
// the 2-component data attribute fetched at offset 8 with a 16-byte stride.
constexpr const char* kStride16VS = R"glsl(
#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_data;   // (r, g) packed
layout(location = 0) out vec4 v_color;
void main()
{
    v_color = vec4(in_data, 0.0, 1.0);
    gl_Position = vec4(in_pos, 0.0, 1.0);
}
)glsl";

// ---- Helpers (mirrors test_d3d12_depth_mrt.cpp) ----------------------------

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
    EXPECT_TRUE(copy_r.has_value())
        << (copy_r.has_value() ? std::string {}
                               : std::string(copy_r.error().message.begin(),
                                             copy_r.error().message.end()));

    std::vector<std::byte> raw(static_cast<std::size_t>(kBytes));
    auto dl = dev.download_buffer(buf, 0, std::span<std::byte> { raw });
    EXPECT_TRUE(dl.has_value());

    const std::size_t center = (static_cast<std::size_t>(kH / 2) * kW + (kW / 2)) * 4u;
    Rgba8 px {
        std::to_integer<std::uint8_t>(raw[center + 0]),
        std::to_integer<std::uint8_t>(raw[center + 1]),
        std::to_integer<std::uint8_t>(raw[center + 2]),
        std::to_integer<std::uint8_t>(raw[center + 3]),
    };
    dev.destroy_buffer(buf);
    return px;
}

// Upload-heap vertex buffer holding `bytes` of `data`.
[[nodiscard]] cd::rhi::BufferHandle
make_vertex_buffer(cd::rhi::IDevice& dev, const void* data, std::uint64_t bytes)
{
    cd::rhi::BufferDesc bd {};
    bd.size   = bytes;
    bd.usage  = cd::rhi::BufferUsage::kVertex;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;  // UPLOAD heap → upload_buffer ok
    auto r = dev.create_buffer(bd);
    if (!r.has_value())
        return {};
    const auto h = *r;
    const auto up = dev.upload_buffer(
        h, 0, std::span<const std::byte>(static_cast<const std::byte*>(data),
                                         static_cast<std::size_t>(bytes)));
    EXPECT_TRUE(up.has_value());
    return h;
}

// Issue begin_render_pass(clear) -> bind pso (+ optional vb) -> draw -> barrier
// to shader-resource. Caller reads back the centre texel afterwards.
void draw_fullscreen(cd::rhi::IDevice& dev, cd::rhi::TextureHandle color,
                     cd::rhi::TextureViewHandle color_view,
                     cd::rhi::GraphicsPipelineHandle pso,
                     const std::array<float, 4>& clear,
                     cd::rhi::PipelineLayoutHandle pc_layout,
                     const void* pc_data, std::uint32_t pc_size,
                     cd::rhi::BufferHandle vb, std::uint32_t vertex_count)
{
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    cd::rhi::ColorAttachmentInfo catt {};
    catt.view        = color_view;
    catt.load_op     = cd::rhi::LoadOp::kClear;
    catt.store_op    = cd::rhi::StoreOp::kStore;
    catt.clear_color = { .f32 = { clear[0], clear[1], clear[2], clear[3] } };

    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
    rp.render_area.extent = { kW, kH };

    cmd->begin_render_pass(rp);
    cmd->set_viewport({ 0, 0, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F });
    cmd->set_scissor(cd::rhi::Rect2D { {}, { kW, kH } });
    cmd->bind_graphics_pipeline(pso);
    if (vb.is_valid())
        cmd->bind_vertex_buffer(0, vb, 0);
    if (pc_data != nullptr && pc_size > 0u)
        cmd->push_constants(pc_layout, cd::rhi::ShaderStage::kAllGraphics, 0,
                            pc_size, pc_data);
    cmd->draw(vertex_count, 1, 0, 0);
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
}

// ---- (a) BLEND --------------------------------------------------------------

TEST(D3D12BlendStride, SrcAlphaBlendOverClear)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    bool skip = false;
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex, kBlendVS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kBlendFS, &skip);
    if (skip) { GTEST_SKIP() << "dxcompiler.dll unavailable at runtime"; }
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    cd::rhi::PushConstantRange pcr {};
    pcr.size   = 16;  // vec4
    pcr.stages = cd::rhi::ShaderStage::kAllGraphics;
    cd::rhi::PipelineLayoutDesc pld {};
    pld.push_constants = std::span<const cd::rhi::PushConstantRange>(&pcr, 1);
    auto layout_r = d.create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value());
    const auto layout = *layout_r;

    const auto color = make_color_target(d);
    ASSERT_TRUE(color.is_valid());
    const auto color_view = make_view(d, color);
    ASSERT_TRUE(color_view.is_valid());

    // PSO with blend ENABLED: src*SRC_ALPHA + dst*INV_SRC_ALPHA, op ADD.
    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::BlendAttachmentState blend {};
    blend.blend_enable     = true;
    blend.src_color        = cd::rhi::BlendFactor::kSrcAlpha;
    blend.dst_color        = cd::rhi::BlendFactor::kOneMinusSrcAlpha;
    blend.color_op         = cd::rhi::BlendOp::kAdd;
    blend.src_alpha        = cd::rhi::BlendFactor::kOne;
    blend.dst_alpha        = cd::rhi::BlendFactor::kZero;
    blend.alpha_op         = cd::rhi::BlendOp::kAdd;
    blend.color_write_mask = 0xF;

    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout          = layout;
    gpd.vertex_shader   = vs;
    gpd.fragment_shader = fs;
    gpd.topology        = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull     = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    gpd.color_attachment_formats  = color_fmts;
    gpd.blend_attachments = std::span<const cd::rhi::BlendAttachmentState>(&blend, 1);
    auto pso_r = d.create_graphics_pipeline(gpd);
    ASSERT_TRUE(pso_r.has_value())
        << std::string(pso_r.error().message.begin(), pso_r.error().message.end());
    const auto pso = *pso_r;

    // Background clear = 0.25 grey (opaque). Source = full red at alpha 0.5.
    // Expected blend over RGBA8: out = src.rgb*0.5 + dst.rgb*0.5.
    //   R: 1.0*0.5 + 0.25*0.5 = 0.625  -> ~159
    //   G: 0.0*0.5 + 0.25*0.5 = 0.125  -> ~32
    //   B: 0.0*0.5 + 0.25*0.5 = 0.125  -> ~32
    const std::array<float, 4> clear { 0.25F, 0.25F, 0.25F, 1.0F };
    const std::array<float, 4> src   { 1.0F, 0.0F, 0.0F, 0.5F };

    draw_fullscreen(d, color, color_view, pso, clear, layout,
                    src.data(), 16u, {}, 3u);

    const Rgba8 px = read_center_texel(d, color);

    // The result must be the BLENDED math: not the opaque source (R==255),
    // not the clear (R==64). R near 0.625 (~159), G/B near 0.125 (~32).
    EXPECT_NEAR(px.r, 159, 12) << "R must be the blended 0.625, not 255 (opaque src) nor 64 (clear)";
    EXPECT_NEAR(px.g, 32,  12) << "G must be the blended 0.125";
    EXPECT_NEAR(px.b, 32,  12) << "B must be the blended 0.125";
    EXPECT_LT(px.r, 230) << "blend must NOT have overwritten opaquely (pre-D4 bug)";
    EXPECT_GT(px.r, 100) << "blend must NOT have left the clear untouched";

    d.destroy_texture_view(color_view);
    d.destroy_texture(color);
    d.destroy_graphics_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

// ---- (b) STRIDE = 28 --------------------------------------------------------

TEST(D3D12BlendStride, VertexStride28FetchesCorrectColor)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    bool skip = false;
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex, kStrideVS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kStrideFS, &skip);
    if (skip) { GTEST_SKIP() << "dxcompiler.dll unavailable at runtime"; }
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    cd::rhi::PipelineLayoutDesc pld {};
    const auto layout = *d.create_pipeline_layout(pld);

    const auto color = make_color_target(d);
    const auto color_view = make_view(d, color);

    // Vertex layout: vec3 pos @0, vec4 color @12 -> stride 28 (NOT 24).
    const cd::rhi::VertexBinding binding { .binding = 0, .stride = 28, .per_instance = false };
    const std::array<cd::rhi::VertexAttribute, 2> attrs {
        cd::rhi::VertexAttribute { .location = 0, .binding = 0,
            .format = cd::rhi::Format::kRGB32Float,  .offset = 0  },
        cd::rhi::VertexAttribute { .location = 1, .binding = 0,
            .format = cd::rhi::Format::kRGBA32Float, .offset = 12 },
    };

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout            = layout;
    gpd.vertex_shader     = vs;
    gpd.fragment_shader   = fs;
    gpd.topology          = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull       = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    gpd.vertex_bindings   = std::span<const cd::rhi::VertexBinding>(&binding, 1);
    gpd.vertex_attributes = attrs;
    gpd.color_attachment_formats = color_fmts;
    auto pso_r = d.create_graphics_pipeline(gpd);
    ASSERT_TRUE(pso_r.has_value())
        << std::string(pso_r.error().message.begin(), pso_r.error().message.end());
    const auto pso = *pso_r;

    // Three vertices, oversized triangle covering the viewport, ALL coloured
    // green (0,1,0,1). Packed at 28-byte stride. With a wrong stride of 24 the
    // 2nd/3rd vertices' colour would be fetched 4 bytes early, landing in the
    // pos data (near-zero) -> the interpolated centre would NOT be pure green.
    struct V { float pos[3]; float col[4]; };
    static_assert(sizeof(V) == 28, "vertex must be 28 bytes");
    const std::array<V, 3> verts {
        V { { -1.0F, -1.0F, 0.0F }, { 0.0F, 1.0F, 0.0F, 1.0F } },
        V { {  3.0F, -1.0F, 0.0F }, { 0.0F, 1.0F, 0.0F, 1.0F } },
        V { { -1.0F,  3.0F, 0.0F }, { 0.0F, 1.0F, 0.0F, 1.0F } },
    };
    const auto vb = make_vertex_buffer(d, verts.data(), sizeof(verts));
    ASSERT_TRUE(vb.is_valid());

    const std::array<float, 4> clear { 0.0F, 0.0F, 0.0F, 1.0F };
    draw_fullscreen(d, color, color_view, pso, clear, {}, nullptr, 0u, vb, 3u);

    const Rgba8 px = read_center_texel(d, color);

    // Pure green from the per-vertex colour, fetched at the correct 28 stride.
    EXPECT_GT(px.g, 200) << "green must be high — colour fetched at stride 28";
    EXPECT_LT(px.r, 48)  << "red must be low — not garbled by a wrong (24) stride";
    EXPECT_LT(px.b, 48)  << "blue must be low — not garbled by a wrong (24) stride";

    d.destroy_buffer(vb);
    d.destroy_texture_view(color_view);
    d.destroy_texture(color);
    d.destroy_graphics_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

// ---- (b') STRIDE = 16 (a second, distinct stride) ---------------------------

TEST(D3D12BlendStride, VertexStride16FetchesCorrectData)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    bool skip = false;
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex, kStride16VS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kStrideFS, &skip);
    if (skip) { GTEST_SKIP() << "dxcompiler.dll unavailable at runtime"; }
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    cd::rhi::PipelineLayoutDesc pld {};
    const auto layout = *d.create_pipeline_layout(pld);

    const auto color = make_color_target(d);
    const auto color_view = make_view(d, color);

    // Vertex layout: vec2 pos @0, vec2 data @8 -> stride 16.
    const cd::rhi::VertexBinding binding { .binding = 0, .stride = 16, .per_instance = false };
    const std::array<cd::rhi::VertexAttribute, 2> attrs {
        cd::rhi::VertexAttribute { .location = 0, .binding = 0,
            .format = cd::rhi::Format::kRG32Float, .offset = 0 },
        cd::rhi::VertexAttribute { .location = 1, .binding = 0,
            .format = cd::rhi::Format::kRG32Float, .offset = 8 },
    };

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout            = layout;
    gpd.vertex_shader     = vs;
    gpd.fragment_shader   = fs;
    gpd.topology          = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull       = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    gpd.vertex_bindings   = std::span<const cd::rhi::VertexBinding>(&binding, 1);
    gpd.vertex_attributes = attrs;
    gpd.color_attachment_formats = color_fmts;
    auto pso_r = d.create_graphics_pipeline(gpd);
    ASSERT_TRUE(pso_r.has_value())
        << std::string(pso_r.error().message.begin(), pso_r.error().message.end());
    const auto pso = *pso_r;

    // data = (r, g) = (0.0, 1.0) for every vertex -> green. Stride 16.
    struct V { float pos[2]; float data[2]; };
    static_assert(sizeof(V) == 16, "vertex must be 16 bytes");
    const std::array<V, 3> verts {
        V { { -1.0F, -1.0F }, { 0.0F, 1.0F } },
        V { {  3.0F, -1.0F }, { 0.0F, 1.0F } },
        V { { -1.0F,  3.0F }, { 0.0F, 1.0F } },
    };
    const auto vb = make_vertex_buffer(d, verts.data(), sizeof(verts));
    ASSERT_TRUE(vb.is_valid());

    const std::array<float, 4> clear { 0.0F, 0.0F, 0.0F, 1.0F };
    draw_fullscreen(d, color, color_view, pso, clear, {}, nullptr, 0u, vb, 3u);

    const Rgba8 px = read_center_texel(d, color);
    EXPECT_GT(px.g, 200) << "green from data attr — fetched at stride 16";
    EXPECT_LT(px.r, 48)  << "red low — data.x = 0 fetched at the right offset";

    d.destroy_buffer(vb);
    d.destroy_texture_view(color_view);
    d.destroy_texture(color);
    d.destroy_graphics_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

}  // namespace

#endif  // _WIN32
