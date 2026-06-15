// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_backend_pixel_parity.cpp
//
// D16 — the cross-backend PIXEL-PARITY capstone. Proves the D3D12 backend
// renders the SAME image as the Vulkan reference for a representative scene,
// from ONE engine GLSL source authored ONCE.
//
// What the per-feature WARP smokes (depth/MRT, blend/stride, bindless/sampler)
// could NOT catch is INTER-backend agreement: each of those asserts a single
// backend behaves; none renders the same draw on BOTH devices and diffs the
// pixels. A UV-origin flip, an NDC-Y handedness mismatch, an sRGB encode/decode
// asymmetry, or a winding/cull divergence would pass every single-backend smoke
// yet make the two backends disagree. This test is that integration net.
//
// SCENE (authored once, compiled to SPIR-V for Vulkan + DXIL for D3D12 via the
//        SAME create_shader_module(kGlsl) call on each device):
//   * A depth-tested pair of TEXTURED, LIT triangles forming a quad.
//   * The texture is a procedural 8x8 checker (uploaded with the SAME pixels to
//     both devices) so a UV-origin flip would show as a mirrored checker.
//   * A constant directional light: the fragment modulates the sampled texel by
//     N·L with a per-vertex world normal, so the lit gradient differs across the
//     quad — any handedness/interp mismatch shows structurally.
//   * Two quads at different gl_Position.z with depth-test LESS: the NEAR quad
//     occludes the FAR one, so the depth path participates in the parity image.
//   * Source-over BLEND is enabled on the PSO (opaque alpha here, so the blend
//     math is identity — it proves the blend STATE translates the same, while
//     keeping the reference image deterministic).
//   Rendered to a 256x256 RGBA8Unorm (LINEAR, no sRGB) offscreen target, read
//   back, and the two backends' readbacks are diffed.
//
// PARITY METRIC + TOLERANCE (justified):
//   Vulkan vs D3D12 are NOT bit-exact — different rasterizer fill rules at
//   triangle edges, different sub-texel sample snap, and independent float
//   rounding in the shader ALU. So we compute, over the readback:
//     (1) max per-channel abs diff on INTERIOR pixels (away from the quad edge),
//     (2) the % of ALL pixels whose max-channel diff exceeds a small threshold.
//   Interior pixels (texture + light + depth, no edge AA) must agree to within
//   kInteriorTol = 4/255 — that bounds unorm rounding (±1) plus checker-filter
//   and N·L ALU rounding (a few LSBs). The fraction of pixels exceeding
//   kPixelThresh = 4/255 must stay under kMaxOutlierFrac = 3% — that envelope
//   covers only the thin diagonal quad-edge seam where the two rasterizers'
//   coverage rules legitimately differ by at most one pixel row. A LARGE or
//   STRUCTURED diff (a flipped checker, an inverted light gradient, a wholesale
//   colour-space shift) blows through both bounds and FAILS — which is the point.
//
// GTEST_SKIP when either device is unavailable (no Vulkan ICD / no D3D12
// adapter / no dxcompiler.dll / device lacks the needed support).
//
// This is a CROSS-BACKEND test (not the flaky cd_test_rhi_vulkan stress binary),
// so it legitimately drives BOTH the Vulkan AND the D3D12 device in one process.
// Pattern: Arrange / Act / Assert.
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
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{

// Fixed offscreen size for the parity render.
constexpr std::uint32_t kW = 256;
constexpr std::uint32_t kH = 256;

// ---- The ONE engine GLSL source pair (authored once) -----------------------
//
// Vertex: a vertex buffer of (vec3 pos, vec3 normal, vec2 uv). Two quads are
// supplied with different z so the depth test participates. The shader writes
// clip-space directly (positions are already in NDC -1..1) and forwards normal
// + uv. NDC Z is 0..1 on both backends; both viewports are positive-height so
// NDC +Y maps to the same framebuffer direction on Vulkan and D3D12.
constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_uv;
void main()
{
    v_normal = in_normal;
    v_uv     = in_uv;
    gl_Position = vec4(in_pos, 1.0);
}
)glsl";

// Fragment: sample the checker texture, modulate by a constant directional
// light's N·L (clamped, with a small ambient term), output linear RGBA8.
// The texture is a combined sampler2D at set 0 binding 0 (D3D12 backs s0 with a
// static sampler; Vulkan uses an immutable/regular sampler — same GLSL).
constexpr const char* kFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D cd_tex;
layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o;
void main()
{
    vec3 n = normalize(v_normal);
    vec3 l = normalize(vec3(0.4, 0.5, 0.768));   // fixed directional light dir
    float ndotl = max(dot(n, l), 0.0);
    float lit = 0.25 + 0.75 * ndotl;             // ambient + diffuse
    vec3 albedo = texture(cd_tex, v_uv).rgb;
    o = vec4(albedo * lit, 1.0);
}
)glsl";

// ---- Device factories -------------------------------------------------------

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_vulkan_device_or_null()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    return r.has_value() ? std::move(*r) : nullptr;
}

#if defined(_WIN32)
[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
}
#endif

[[nodiscard]] bool glslang_available()
{
    return cd::shader::make_glslang_compiler() != nullptr;
}

// Compile a GLSL stage through the device path (kGlsl). On Vulkan this yields
// SPIR-V; on D3D12 it routes GLSL -> SPIR-V -> HLSL -> DXIL. SAME call, SAME
// source. *skip is set when the D3D12 toolchain DLL (dxc) is missing.
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

// ---- Procedural checker texture (the SAME bytes both backends) -------------
//
// 8x8 RGBA8 checker of two distinct colours so a UV-origin flip (top-left vs
// bottom-left) would mirror the pattern and break parity structurally.
constexpr std::uint32_t kTexN = 8;

[[nodiscard]] std::vector<std::uint8_t> make_checker_pixels()
{
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kTexN) * kTexN * 4u);
    for (std::uint32_t y = 0; y < kTexN; ++y)
        for (std::uint32_t x = 0; x < kTexN; ++x)
        {
            const bool a = ((x ^ y) & 1u) == 0u;
            const std::size_t o = (static_cast<std::size_t>(y) * kTexN + x) * 4u;
            if (a) { px[o + 0] = 230; px[o + 1] =  60; px[o + 2] =  40; }  // warm
            else   { px[o + 0] =  40; px[o + 1] = 110; px[o + 2] = 220; }  // cool
            px[o + 3] = 255;
        }
    return px;
}

// Upload a solid checker into a sampled texture via a staging buffer.
[[nodiscard]] cd::rhi::TextureHandle
make_checker_texture(cd::rhi::IDevice& dev, const std::vector<std::uint8_t>& px)
{
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { kTexN, kTexN, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kSampled |
                      cd::rhi::TextureUsage::kTransferDst;
    auto tr = dev.create_texture(td);
    if (!tr.has_value())
        return {};
    const auto tex = *tr;

    cd::rhi::BufferDesc bd {};
    bd.size   = px.size();
    bd.usage  = cd::rhi::BufferUsage::kTransferSrc;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto br = dev.create_buffer(bd);
    if (!br.has_value())
        return {};
    const auto buf = *br;
    (void)dev.upload_buffer(buf, 0,
        std::span<const std::byte> {
            reinterpret_cast<const std::byte*>(px.data()), px.size() });

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    cmd->begin();
    cd::rhi::TextureBarrier to_dst {};
    to_dst.texture = tex;
    to_dst.from    = cd::rhi::ResourceState::kUndefined;
    to_dst.to      = cd::rhi::ResourceState::kTransferDst;
    to_dst.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_dst, 1));
    cd::rhi::BufferImageCopyRegion region {};
    region.image_extent = { kTexN, kTexN, 1 };
    cmd->copy_buffer_to_image(buf, tex,
        std::span<const cd::rhi::BufferImageCopyRegion>(&region, 1));
    cd::rhi::TextureBarrier to_srv {};
    to_srv.texture = tex;
    to_srv.from    = cd::rhi::ResourceState::kTransferDst;
    to_srv.to      = cd::rhi::ResourceState::kShaderResource;
    to_srv.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_srv, 1));
    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();
    dev.destroy_buffer(buf);
    return tex;
}

// ---- Resources --------------------------------------------------------------

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

[[nodiscard]] cd::rhi::BufferHandle
make_vertex_buffer(cd::rhi::IDevice& dev, const void* data, std::uint64_t bytes)
{
    cd::rhi::BufferDesc bd {};
    bd.size   = bytes;
    bd.usage  = cd::rhi::BufferUsage::kVertex;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto r = dev.create_buffer(bd);
    if (!r.has_value())
        return {};
    const auto h = *r;
    (void)dev.upload_buffer(h, 0,
        std::span<const std::byte>(static_cast<const std::byte*>(data),
                                   static_cast<std::size_t>(bytes)));
    return h;
}

// ---- The vertex data (two depth-separated quads) ---------------------------
//
// Vertex = (vec3 pos, vec3 normal, vec2 uv), stride 32.
struct V { float pos[3]; float nrm[3]; float uv[2]; };
static_assert(sizeof(V) == 32, "vertex must be 32 bytes");

// Build a quad of 2 triangles covering a sub-rect of NDC at depth z, with a
// per-quad normal and a 0..1 UV mapping over the rect. The normal is chosen so
// the N·L term yields a non-trivial, non-flat lit value (parity-sensitive).
void append_quad(std::vector<V>& out, float x0, float y0, float x1, float y1,
                 float z, const float nrm[3])
{
    const V tl { { x0, y1, z }, { nrm[0], nrm[1], nrm[2] }, { 0.0F, 0.0F } };
    const V tr { { x1, y1, z }, { nrm[0], nrm[1], nrm[2] }, { 1.0F, 0.0F } };
    const V bl { { x0, y0, z }, { nrm[0], nrm[1], nrm[2] }, { 0.0F, 1.0F } };
    const V br { { x1, y0, z }, { nrm[0], nrm[1], nrm[2] }, { 1.0F, 1.0F } };
    out.push_back(tl); out.push_back(bl); out.push_back(tr);   // tri 0
    out.push_back(tr); out.push_back(bl); out.push_back(br);   // tri 1
}

[[nodiscard]] std::vector<V> build_scene_vertices()
{
    std::vector<V> v;
    // FAR quad (z = 0.7), large, slightly tilted normal (light from upper-right).
    const float far_n[3] { 0.0F, 0.0F, 1.0F };
    append_quad(v, -0.9F, -0.9F, 0.9F, 0.9F, 0.7F, far_n);
    // NEAR quad (z = 0.3), smaller, different normal -> different N·L band, must
    // occlude the FAR quad where they overlap (depth test LESS).
    const float near_n[3] { 0.3F, 0.2F, 0.93F };
    append_quad(v, -0.5F, -0.5F, 0.5F, 0.5F, 0.3F, near_n);
    return v;
}

// ---- Render the parity scene + read back the RGBA8 image -------------------
//
// Returns the full kW*kH*4 readback, or std::nullopt + sets *skip when the
// device can't compile/build the scene (toolchain / feature gap).
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
render_scene(cd::rhi::IDevice& dev, bool* skip)
{
    const auto step_failed = [](const char* what) -> std::nullopt_t
    {
        std::cerr << "[parity] render_scene failed at: " << what << "\n";
        return std::nullopt;
    };

    const auto vs = make_module(dev, cd::rhi::ShaderStage::kVertex,   kVS, skip);
    const auto fs = make_module(dev, cd::rhi::ShaderStage::kFragment, kFS, skip);
    if (*skip) return std::nullopt;
    if (!vs.is_valid() || !fs.is_valid()) return step_failed("shader module");

    // set 0: one combined sampler2D (t0/space0). D3D12 backs s0 with a static
    // sampler (B1b); Vulkan binds a regular sampler via the descriptor write.
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> set0_bindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
    };
    cd::rhi::DescriptorSetLayoutDesc set0_desc {};
    set0_desc.bindings = set0_bindings;
    auto set0 = dev.create_descriptor_set_layout(set0_desc);
    if (!set0.has_value()) return step_failed("create_descriptor_set_layout");

    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sets { *set0 };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = sets;
    auto layout_r = dev.create_pipeline_layout(pld);
    if (!layout_r.has_value()) return step_failed("create_pipeline_layout");
    const auto layout = *layout_r;

    // Checker texture (identical bytes both backends).
    const auto px  = make_checker_pixels();
    const auto tex = make_checker_texture(dev, px);
    if (!tex.is_valid()) return step_failed("make_checker_texture");
    const auto tex_view = make_view(dev, tex);
    if (!tex_view.is_valid()) return step_failed("checker texture view");

    // A sampler bound through the descriptor set. The combined-sampler write
    // carries a sampler on Vulkan; on D3D12 the static sampler at s0 is used and
    // the write supplies the SRV. Use a default (linear / clamp) sampler.
    cd::rhi::SamplerDesc sd {};
    sd.min_filter  = cd::rhi::SamplerFilter::kLinear;
    sd.mag_filter  = cd::rhi::SamplerFilter::kLinear;
    sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    sd.address_u   = cd::rhi::SamplerAddressMode::kClampToEdge;
    sd.address_v   = cd::rhi::SamplerAddressMode::kClampToEdge;
    sd.address_w   = cd::rhi::SamplerAddressMode::kClampToEdge;
    auto samp_r = dev.create_sampler(sd);
    if (!samp_r.has_value()) return step_failed("create_sampler");
    const auto sampler = *samp_r;

    auto set_r = dev.allocate_descriptor_set(*set0);
    if (!set_r.has_value()) return step_failed("allocate_descriptor_set");
    const auto set = *set_r;
    cd::rhi::DescriptorWrite dw {};
    dw.binding = 0;
    dw.type    = cd::rhi::DescriptorType::kCombinedImageSampler;
    dw.view    = tex_view;
    dw.sampler = sampler;
    (void)dev.update_descriptor_set(set,
        std::span<const cd::rhi::DescriptorWrite>(&dw, 1));

    // Colour + depth targets.
    const auto color = make_color_target(dev);
    if (!color.is_valid()) return step_failed("create_color_target");
    const auto color_view = make_view(dev, color);
    if (!color_view.is_valid()) return step_failed("color view");

    cd::rhi::TextureDesc dtd {};
    dtd.type         = cd::rhi::TextureType::k2D;
    dtd.format       = cd::rhi::Format::kD32Float;
    dtd.extent       = { kW, kH, 1 };
    dtd.mip_levels   = 1;
    dtd.array_layers = 1;
    dtd.usage        = cd::rhi::TextureUsage::kDepthStencilAttachment;
    auto depth_r = dev.create_texture(dtd);
    if (!depth_r.has_value()) return step_failed("create_depth_texture");
    const auto depth = *depth_r;
    const auto depth_view = make_view(dev, depth, cd::rhi::Format::kD32Float);
    if (!depth_view.is_valid()) return step_failed("depth view");

    // Vertex buffer: two quads.
    const auto verts = build_scene_vertices();
    const auto vb = make_vertex_buffer(dev, verts.data(), sizeof(V) * verts.size());
    if (!vb.is_valid()) return step_failed("vertex buffer");

    // PSO: textured/lit, depth test LESS + write, blend enabled (source-over),
    // cull NONE (winding-agnostic so the parity image is purely raster+shade).
    const cd::rhi::VertexBinding binding {
        .binding = 0, .stride = 32, .per_instance = false };
    const std::array<cd::rhi::VertexAttribute, 3> attrs {
        cd::rhi::VertexAttribute { .location = 0, .binding = 0,
            .format = cd::rhi::Format::kRGB32Float, .offset = 0  },
        cd::rhi::VertexAttribute { .location = 1, .binding = 0,
            .format = cd::rhi::Format::kRGB32Float, .offset = 12 },
        cd::rhi::VertexAttribute { .location = 2, .binding = 0,
            .format = cd::rhi::Format::kRG32Float,  .offset = 24 },
    };
    cd::rhi::BlendAttachmentState blend {};
    blend.blend_enable     = true;
    blend.src_color        = cd::rhi::BlendFactor::kSrcAlpha;
    blend.dst_color        = cd::rhi::BlendFactor::kOneMinusSrcAlpha;
    blend.color_op         = cd::rhi::BlendOp::kAdd;
    blend.src_alpha        = cd::rhi::BlendFactor::kOne;
    blend.dst_alpha        = cd::rhi::BlendFactor::kZero;
    blend.alpha_op         = cd::rhi::BlendOp::kAdd;
    blend.color_write_mask = 0xF;

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout            = layout;
    gpd.vertex_shader     = vs;
    gpd.fragment_shader   = fs;
    gpd.topology          = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull       = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test    = true;
    gpd.depth_stencil.depth_write   = true;
    gpd.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    gpd.vertex_bindings   = std::span<const cd::rhi::VertexBinding>(&binding, 1);
    gpd.vertex_attributes = attrs;
    gpd.color_attachment_formats = color_fmts;
    gpd.depth_attachment_format  = cd::rhi::Format::kD32Float;
    gpd.blend_attachments = std::span<const cd::rhi::BlendAttachmentState>(&blend, 1);
    auto pso_r = dev.create_graphics_pipeline(gpd);
    if (!pso_r.has_value()) return step_failed("create_graphics_pipeline");
    const auto pso = *pso_r;

    // Record: clear -> draw both quads -> barrier to shader-resource.
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    cmd->begin();

    cd::rhi::ColorAttachmentInfo catt {};
    catt.view        = color_view;
    catt.load_op     = cd::rhi::LoadOp::kClear;
    catt.store_op    = cd::rhi::StoreOp::kStore;
    catt.clear_color = { .f32 = { 0.05F, 0.05F, 0.07F, 1.0F } };

    cd::rhi::DepthStencilAttachmentInfo datt {};
    datt.view        = depth_view;
    datt.depth_load  = cd::rhi::LoadOp::kClear;
    datt.depth_store = cd::rhi::StoreOp::kStore;
    datt.clear       = { .depth = 1.0F, .stencil = 0 };

    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
    rp.depth_stencil      = &datt;
    rp.render_area.extent = { kW, kH };

    cmd->begin_render_pass(rp);
    cmd->set_viewport({ 0, 0, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F });
    cmd->set_scissor(cd::rhi::Rect2D { {}, { kW, kH } });
    cmd->bind_graphics_pipeline(pso);
    cmd->bind_descriptor_set(0, set);
    cmd->bind_vertex_buffer(0, vb, 0);
    cmd->draw(static_cast<std::uint32_t>(verts.size()), 1, 0, 0);
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

    // Read back the whole 256x256 RGBA8 image.
    constexpr std::uint64_t kBytes = std::uint64_t { kW } * kH * 4u;
    cd::rhi::BufferDesc rbd {};
    rbd.size   = kBytes;
    rbd.usage  = cd::rhi::BufferUsage::kTransferDst;
    rbd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto rb_r = dev.create_buffer(rbd);
    if (!rb_r.has_value()) return step_failed("readback buffer");
    const auto rb = *rb_r;

    cd::rhi::IDevice::ImageRegion ir {};
    ir.width     = kW;
    ir.height    = kH;
    ir.src_state = cd::rhi::ResourceState::kShaderResource;
    auto copy_r = dev.copy_image_to_buffer(color, rb, 0, ir);
    if (!copy_r.has_value()) return step_failed("copy_image_to_buffer");

    std::vector<std::byte> raw(static_cast<std::size_t>(kBytes));
    auto dl = dev.download_buffer(rb, 0, std::span<std::byte> { raw });
    if (!dl.has_value()) return step_failed("download_buffer");

    std::vector<std::uint8_t> out(static_cast<std::size_t>(kBytes));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = std::to_integer<std::uint8_t>(raw[i]);

    // Teardown.
    dev.destroy_buffer(rb);
    dev.destroy_graphics_pipeline(pso);
    dev.destroy_buffer(vb);
    dev.destroy_texture_view(depth_view);
    dev.destroy_texture(depth);
    dev.destroy_texture_view(color_view);
    dev.destroy_texture(color);
    dev.destroy_descriptor_set(set);
    dev.destroy_sampler(sampler);
    dev.destroy_texture_view(tex_view);
    dev.destroy_texture(tex);
    dev.destroy_pipeline_layout(layout);
    dev.destroy_descriptor_set_layout(*set0);
    dev.destroy_shader_module(vs);
    dev.destroy_shader_module(fs);
    return out;
}

// ---- Diff metric ------------------------------------------------------------

struct DiffStats
{
    int           max_interior_diff = 0;   // max per-channel abs diff, interior
    double        outlier_frac      = 0.0; // fraction of pixels over threshold
    std::uint32_t outlier_count     = 0;
    std::uint32_t worst_x           = 0;
    std::uint32_t worst_y           = 0;
};

// A pixel is "edge" (excluded from the interior-tolerance assert) when it sits
// within kEdgeBand of either quad's NDC rectangle boundary, mapped to pixels —
// that is where the two rasterizers' coverage rules legitimately differ. The
// outlier-fraction assert still covers the WHOLE image so a wholesale shift
// can't hide in the edge band.
constexpr int kEdgeBand = 2;

[[nodiscard]] bool near_edge(std::uint32_t x, std::uint32_t y)
{
    // The two quads span NDC [-0.9,0.9] and [-0.5,0.5]. NDC->pixel with a
    // positive-height viewport: px = (ndc*0.5+0.5)*size. Compute the four
    // boundary pixel coords for each quad and flag a band around any of them.
    const auto ndc_to_px_x = [](float n) { return (n * 0.5F + 0.5F) * static_cast<float>(kW); };
    const auto ndc_to_px_y = [](float n) { return (n * 0.5F + 0.5F) * static_cast<float>(kH); };
    const std::array<float, 4> bx {
        ndc_to_px_x(-0.9F), ndc_to_px_x(0.9F), ndc_to_px_x(-0.5F), ndc_to_px_x(0.5F) };
    const std::array<float, 4> by {
        ndc_to_px_y(-0.9F), ndc_to_px_y(0.9F), ndc_to_px_y(-0.5F), ndc_to_px_y(0.5F) };
    const auto fx = static_cast<float>(x);
    const auto fy = static_cast<float>(y);
    const auto in_band = [](float v, float b)
    { return v >= b - kEdgeBand && v <= b + kEdgeBand; };
    if (std::ranges::any_of(bx, [&](float b) { return in_band(fx, b); })) return true;
    if (std::ranges::any_of(by, [&](float b) { return in_band(fy, b); })) return true;
    return false;
}

[[nodiscard]] DiffStats
diff_images(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
            int pixel_thresh)
{
    DiffStats s {};
    const std::size_t n = std::min(a.size(), b.size());
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x)
        {
            const std::size_t o = (static_cast<std::size_t>(y) * kW + x) * 4u;
            if (o + 3 >= n) continue;
            int pix_max = 0;
            for (int c = 0; c < 3; ++c)  // RGB; A is constant 255
            {
                const int d = std::abs(static_cast<int>(a[o + static_cast<std::size_t>(c)]) -
                                       static_cast<int>(b[o + static_cast<std::size_t>(c)]));
                pix_max = std::max(pix_max, d);
            }
            if (!near_edge(x, y))
            {
                if (pix_max > s.max_interior_diff)
                {
                    s.max_interior_diff = pix_max;
                    s.worst_x = x;
                    s.worst_y = y;
                }
            }
            if (pix_max > pixel_thresh)
                ++s.outlier_count;
        }
    s.outlier_frac = static_cast<double>(s.outlier_count) /
                     (static_cast<double>(kW) * static_cast<double>(kH));
    return s;
}

// ---- The parity TEST --------------------------------------------------------

TEST(BackendPixelParity, VulkanVsD3D12TexturedLitDepthQuad)
{
#if !defined(_WIN32)
    GTEST_SKIP() << "D3D12 backend is Windows-only — cross-backend parity needs both";
#else
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG — no GLSL front-end";

    auto vk = make_vulkan_device_or_null();
    if (vk == nullptr)
        GTEST_SKIP() << "no Vulkan ICD available on this host";
    auto dx = make_d3d12_device_or_null();
    if (dx == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";

    bool vk_skip = false;
    auto vk_img = render_scene(*vk, &vk_skip);
    if (vk_skip)
        GTEST_SKIP() << "Vulkan could not build the parity scene (toolchain gap)";
    ASSERT_TRUE(vk_img.has_value()) << "Vulkan parity scene failed to render";

    bool dx_skip = false;
    auto dx_img = render_scene(*dx, &dx_skip);
    if (dx_skip)
        GTEST_SKIP() << "dxcompiler.dll unavailable at runtime — DXIL path absent";
    ASSERT_TRUE(dx_img.has_value()) << "D3D12 parity scene failed to render";

    // Bind checked references (the ASSERT_TRUEs above guarantee both engaged).
    const std::vector<std::uint8_t>& vk_px = *vk_img;
    const std::vector<std::uint8_t>& dx_px = *dx_img;

    // --- Tolerance (justified in the file header) ---
    constexpr int    kInteriorTol     = 4;     // max per-channel abs diff, interior
    constexpr int    kPixelThresh     = 4;     // outlier threshold (per-channel)
    constexpr double kMaxOutlierFrac  = 0.03;  // <=3% of pixels may be edge-seam

    const DiffStats st = diff_images(vk_px, dx_px, kPixelThresh);

    // Diagnostic — print the measured numbers so a structured regression is
    // legible in the test log even when it passes.
    std::cout << "[parity] max interior per-channel diff = " << st.max_interior_diff
              << "/255 (worst at " << st.worst_x << "," << st.worst_y << "); "
              << "outliers > " << kPixelThresh << "/255 = " << st.outlier_count
              << " (" << (st.outlier_frac * 100.0) << "%)\n";

    // SENSITIVITY CHECK: a VERTICAL flip of the D3D12 image must NOT match
    // Vulkan. Before the D16 fix (D3D12 negative-height viewport) the backends
    // disagreed by a pure Y mirror — the direct diff was ~75% and THIS flipped
    // diff was ~0%. Now they are reversed. Asserting the flipped image diverges
    // proves (a) the gap is genuinely fixed at the raster level (not masked by a
    // symmetric scene) and (b) the metric is sensitive to a one-axis mirror, so
    // a future viewport regression re-fails the direct assert below.
    {
        std::vector<std::uint8_t> dx_flip(dx_px.size());
        for (std::uint32_t y = 0; y < kH; ++y)
            for (std::uint32_t x = 0; x < kW; ++x)
            {
                const std::size_t src = (static_cast<std::size_t>(kH - 1 - y) * kW + x) * 4u;
                const std::size_t dst = (static_cast<std::size_t>(y) * kW + x) * 4u;
                for (int c = 0; c < 4; ++c)
                    dx_flip[dst + static_cast<std::size_t>(c)] =
                        dx_px[src + static_cast<std::size_t>(c)];
            }
        const DiffStats fs = diff_images(vk_px, dx_flip, kPixelThresh);
        std::cout << "[parity] (sensitivity) Vulkan vs Y-FLIPPED D3D12: max interior diff = "
                  << fs.max_interior_diff << "/255, outliers = "
                  << (fs.outlier_frac * 100.0) << "%\n";
        EXPECT_GT(fs.outlier_frac, kMaxOutlierFrac)
            << "a Y-flipped D3D12 image must NOT match Vulkan — the scene is "
               "asymmetric and the metric must be flip-sensitive (else the direct "
               "parity pass below could be a false positive on a symmetric image)";
    }

    // A sanity floor: the image must NOT be uniformly the clear colour (that
    // would mean nothing drew on one side and the diff would be trivially small
    // for the wrong reason). Require the scene to have textured/lit content.
    bool vk_has_content = false;
    for (std::size_t i = 0; i + 3 < vk_px.size(); i += 4)
        if (vk_px[i] > 80 || vk_px[i + 1] > 80 || vk_px[i + 2] > 80)
        { vk_has_content = true; break; }
    ASSERT_TRUE(vk_has_content) << "Vulkan image looks empty — scene did not render";

    EXPECT_LE(st.max_interior_diff, kInteriorTol)
        << "interior pixels (texture+light+depth) diverge beyond unorm/ALU "
           "rounding — a structured D3D12 parity gap (UV flip / NDC-Y / sRGB / "
           "winding) is likely. Worst pixel ("
        << st.worst_x << "," << st.worst_y << ").";
    EXPECT_LE(st.outlier_frac, kMaxOutlierFrac)
        << "too many pixels exceed " << kPixelThresh << "/255 ("
        << (st.outlier_frac * 100.0) << "%) — the divergence is wider than the "
           "thin quad-edge seam the two rasterizers legitimately disagree on.";
#endif
}

}  // namespace
