// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_sampler_bind_parity.cpp
//
// FINISH-ALL-BACKEND Wave A2 / D2: D3D12 CUSTOM-SAMPLER bindability parity vs
// the Vulkan reference.
//
// THE BUG (pre-fix). create_sampler fully mapped filter / address / compare /
// border / LOD into a D3D12_SAMPLER_DESC, BUT:
//   * that descriptor landed in a NON-shader-visible heap referenced by nothing,
//   * update_descriptor_set's kSampler case was an explicit no-op, and
//   * bind_descriptor_set always bound a bindless sampler heap pre-filled with a
//     single default LINEAR-clamp sampler.
// Net: EVERY dynamically-created sampler was silently ignored and all shader
// sampling resolved to LINEAR-clamp — the opposite of VulkanDevice, which
// writes the real VkDescriptorImageInfo.sampler into the descriptor set
// (VulkanDevice.cpp update_descriptor_set kSampler / kCombinedImageSampler).
//
// THE FIX mirrors Vulkan in the D3D12 binding model: dynamic samplers live in a
// SHADER-VISIBLE D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ring; the kSampler write
// copies the SamplerRecord's descriptor into the set's region of that ring;
// create_pipeline_layout exposes a per-set SAMPLER descriptor table root param;
// bind_descriptor_set binds the sampler ring + points the sampler table at the
// set's region (instead of the always-default bindless sampler heap).
//
// BIDIRECTIONAL PROOF (phase1204/A1 pattern). We sample a sharp 2x2 checkerboard
// at the dead-centre UV via a SEPARATE texture2D + sampler (Vulkan-style: the
// sampler half is a real `kSampler` binding, the EXACT descriptor the bug
// ignored). We render twice:
//   * a POINT(nearest) sampler  -> the centre UV snaps to ONE texel (saturated).
//   * a LINEAR sampler          -> the centre UV blends the four texels (mid).
// We assert the two readbacks DIFFER, and that the POINT result matches the
// analytic single-texel expectation (NOT the LINEAR-clamp default). With the
// bug, BOTH samplers resolve to the baked LINEAR-clamp default -> the two
// readbacks are IDENTICAL and the POINT-vs-analytic assert fails. Temporarily
// reverting the fix (kSampler write back to a no-op, or binding the bindless
// default heap) therefore makes this test FAIL — the revert-proof.
//
// A second case proves the ADDRESS mode is honoured (WRAP vs CLAMP at UV=1.5),
// locking the rest of the SamplerDesc->D3D12_SAMPLER_DESC mapping into the
// actually-bound path.
//
// Real D3D12 backend (WARP if no hardware adapter). GTEST_SKIP when no adapter /
// no glslang / no dxcompiler.dll. Pattern: Arrange / Act / Assert.
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
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <vector>

#if defined(_WIN32)

namespace
{

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

struct Rgba8 { std::uint8_t r, g, b, a; };

// Full-screen triangle (no vertex buffer) — same gl_VertexIndex trick the other
// D3D12 GPU smokes use.
constexpr const char* kVS = R"glsl(
#version 460
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

// SEPARATE texture + sampler (Vulkan-style). SPIRV-Cross emits:
//   texture2D  -> Texture2D    : register(t0, space0)   (kSampledImage)
//   sampler    -> SamplerState : register(s1, space0)   (kSampler  <-- the bug)
// We sample at a fixed fractional UV (push-constant) so a single draw controls
// where the filter is exercised. With a POINT sampler the centre UV snaps to one
// texel; with LINEAR it blends — the result DIFFERS only if the custom sampler
// is actually bound.
constexpr const char* kFS = R"glsl(
#version 460
layout(set = 0, binding = 0) uniform texture2D cd_tex;   // t0/space0
layout(set = 0, binding = 1) uniform sampler   cd_samp;  // s1/space0  (kSampler)
layout(push_constant) uniform PC { vec2 uv; } pc;
layout(location = 0) out vec4 o;
void main() { o = texture(sampler2D(cd_tex, cd_samp), pc.uv); }
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;  // avoid debug-layer dependency in CI
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
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

// A 2x2 RGBA8 checkerboard: a sharp feature whose POINT vs LINEAR results
// differ at the dead-centre UV. Diagonal (0,0)=(1,1)=A, (1,0)=(0,1)=B.
[[nodiscard]] cd::rhi::TextureHandle
make_checker_2x2(cd::rhi::IDevice& dev, Rgba8 a, Rgba8 b)
{
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { 2, 2, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kSampled |
                      cd::rhi::TextureUsage::kTransferDst;
    auto tr = dev.create_texture(td);
    if (!tr.has_value())
        return {};
    const auto tex = *tr;

    // Tightly-packed 2x2 RGBA8 (16 bytes); the D3D12 backend re-pitches to the
    // 256-aligned staging. Row 0: (0,0)=a, (1,0)=b. Row 1: (0,1)=b, (1,1)=a.
    const std::array<std::uint8_t, 16> px {
        a.r, a.g, a.b, a.a,  b.r, b.g, b.b, b.a,
        b.r, b.g, b.b, b.a,  a.r, a.g, a.b, a.a,
    };
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
    cd::rhi::BufferImageCopyRegion region {};
    region.image_extent = { 2, 2, 1 };
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

[[nodiscard]] cd::rhi::TextureViewHandle
make_view(cd::rhi::IDevice& dev, cd::rhi::TextureHandle t)
{
    cd::rhi::TextureViewDesc vd {};
    vd.texture = t;
    vd.type    = cd::rhi::TextureType::k2D;
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
    (void)dev.copy_image_to_buffer(tex, buf, 0, region);

    std::vector<std::byte> raw(static_cast<std::size_t>(kBytes));
    (void)dev.download_buffer(buf, 0, std::span<std::byte> { raw });
    const std::size_t c = (static_cast<std::size_t>(kH / 2) * kW + (kW / 2)) * 4u;
    const Rgba8 px {
        std::to_integer<std::uint8_t>(raw[c + 0]),
        std::to_integer<std::uint8_t>(raw[c + 1]),
        std::to_integer<std::uint8_t>(raw[c + 2]),
        std::to_integer<std::uint8_t>(raw[c + 3]),
    };
    dev.destroy_buffer(buf);
    return px;
}

// One textured draw: the checkerboard sampled through `samp` at `uv`. Returns
// the centre pixel of the readback (every fragment samples the SAME uv, so all
// pixels carry the same colour). `samp` is bound as a real `kSampler` write —
// the descriptor the bug ignored.
[[nodiscard]] Rgba8
sample_through(cd::rhi::IDevice& dev,
               cd::rhi::ShaderModuleHandle vs,
               cd::rhi::ShaderModuleHandle fs,
               cd::rhi::TextureViewHandle tex_view,
               cd::rhi::SamplerHandle samp,
               float u, float v)
{
    // set 0 — separate texture (binding 0) + sampler (binding 1).
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 2> kSet0 {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kSampledImage,
            .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 1, .type = cd::rhi::DescriptorType::kSampler,
            .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
    };
    cd::rhi::DescriptorSetLayoutDesc set0_desc {};
    set0_desc.bindings = kSet0;
    auto set0 = dev.create_descriptor_set_layout(set0_desc);
    EXPECT_TRUE(set0.has_value()) << (set0.has_value() ? std::string {}
                                                       : std::string { set0.error().message });
    if (!set0.has_value())
        return {};

    cd::rhi::PushConstantRange pcr {};
    pcr.offset = 0;
    pcr.size   = 16;  // vec2 uv, padded to 16B
    pcr.stages = cd::rhi::ShaderStage::kAllGraphics;
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sets { *set0 };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts    = sets;
    pld.push_constants = std::span<const cd::rhi::PushConstantRange>(&pcr, 1);
    auto layout_r = dev.create_pipeline_layout(pld);
    EXPECT_TRUE(layout_r.has_value()) << (layout_r.has_value() ? std::string {}
                                                               : std::string { layout_r.error().message });
    if (!layout_r.has_value())
        return {};
    const auto layout = *layout_r;

    auto set = dev.allocate_descriptor_set(*set0);
    EXPECT_TRUE(set.has_value());
    if (!set.has_value())
        return {};
    const std::array<cd::rhi::DescriptorWrite, 2> dws {
        cd::rhi::DescriptorWrite {
            .binding = 0, .type = cd::rhi::DescriptorType::kSampledImage,
            .view = tex_view },
        cd::rhi::DescriptorWrite {
            .binding = 1, .type = cd::rhi::DescriptorType::kSampler,
            .sampler = samp },
    };
    auto upd = dev.update_descriptor_set(*set, std::span<const cd::rhi::DescriptorWrite>(dws));
    EXPECT_TRUE(upd.has_value()) << (upd.has_value() ? std::string {}
                                                     : std::string { upd.error().message });

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout          = layout;
    gpd.vertex_shader   = vs;
    gpd.fragment_shader = fs;
    gpd.raster.cull     = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    gpd.color_attachment_formats  = color_fmts;
    auto pso_r = dev.create_graphics_pipeline(gpd);
    EXPECT_TRUE(pso_r.has_value())
        << (pso_r.has_value() ? std::string {}
                              : std::string(pso_r.error().message.begin(),
                                            pso_r.error().message.end()));
    if (!pso_r.has_value())
        return {};
    const auto pso = *pso_r;

    const auto color = make_color_target(dev);
    const auto color_view = make_view(dev, color);

    const std::array<float, 4> uv { u, v, 0.0F, 0.0F };

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
    cmd->bind_descriptor_set(0, *set);
    cmd->push_constants(layout, cd::rhi::ShaderStage::kAllGraphics, 0,
                        static_cast<std::uint32_t>(sizeof(float) * 4), uv.data());
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
    dev.destroy_descriptor_set(*set);
    dev.destroy_graphics_pipeline(pso);
    dev.destroy_pipeline_layout(layout);
    dev.destroy_descriptor_set_layout(*set0);
    return px;
}

[[nodiscard]] cd::rhi::SamplerHandle
make_sampler(cd::rhi::IDevice& dev, cd::rhi::SamplerFilter filter,
             cd::rhi::SamplerAddressMode addr)
{
    cd::rhi::SamplerDesc sd {};
    sd.min_filter  = filter;
    sd.mag_filter  = filter;
    sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kNearest;
    sd.address_u   = addr;
    sd.address_v   = addr;
    sd.address_w   = addr;
    auto r = dev.create_sampler(sd);
    return r.has_value() ? *r : cd::rhi::SamplerHandle {};
}

// ---- FILTER parity: POINT vs LINEAR over a checkerboard --------------------
//
// The decisive bidirectional assertion. A POINT sampler and a LINEAR sampler
// over the SAME sharp 2x2 checkerboard, sampled at the dead-centre UV, must
// produce DIFFERENT colours (POINT = a single saturated texel, LINEAR = the
// four-texel blend). With the D2 bug both resolve to the baked LINEAR-clamp
// default -> identical -> this FAILS. With the fix the dynamic sampler is
// actually bound and they differ -> PASS.
TEST(D3D12SamplerBindParity, PointVsLinearOverCheckerboardDiffer)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    bool skip = false;
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex,   kVS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kFS, &skip);
    if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    // Diagonal checkerboard: A = red, B = blue.
    const Rgba8 kA { 255, 0, 0, 255 };
    const Rgba8 kB { 0, 0, 255, 255 };
    const auto tex = make_checker_2x2(d, kA, kB);
    ASSERT_TRUE(tex.is_valid());
    const auto tex_view = make_view(d, tex);
    ASSERT_TRUE(tex_view.is_valid());

    const auto point  = make_sampler(d, cd::rhi::SamplerFilter::kNearest,
                                     cd::rhi::SamplerAddressMode::kClampToEdge);
    const auto linear = make_sampler(d, cd::rhi::SamplerFilter::kLinear,
                                     cd::rhi::SamplerAddressMode::kClampToEdge);
    ASSERT_TRUE(point.is_valid());
    ASSERT_TRUE(linear.is_valid());

    // Sample just toward texel (0,0)=A=red so POINT deterministically snaps to
    // red (avoids the exact 0.5 texel-boundary tie). LINEAR blends all four
    // texels toward purple.
    const Rgba8 p_point  = sample_through(d, vs, fs, tex_view, point,  0.49F, 0.49F);
    const Rgba8 p_linear = sample_through(d, vs, fs, tex_view, linear, 0.49F, 0.49F);

    // (1) The two custom samplers MUST produce different results — the core
    //     bidirectional proof. Identical results == the bug (both default LINEAR).
    const int dr = std::abs(static_cast<int>(p_point.r) - static_cast<int>(p_linear.r));
    const int db = std::abs(static_cast<int>(p_point.b) - static_cast<int>(p_linear.b));
    EXPECT_GT(dr + db, 60)
        << "BUG: POINT and LINEAR samplers produced near-identical results — the "
           "dynamically-created sampler is being ignored and both resolve to the "
           "baked LINEAR-clamp default (D2). POINT=("
        << +p_point.r << "," << +p_point.g << "," << +p_point.b << ") LINEAR=("
        << +p_linear.r << "," << +p_linear.g << "," << +p_linear.b << ")";

    // (2) The POINT result must match the ANALYTIC single-texel expectation
    //     (red, not a blend) — proving the bound sampler is genuinely POINT,
    //     not the LINEAR-clamp default that would give a purple-ish blend.
    EXPECT_GT(p_point.r, 200) << "POINT sample at a red texel must be red-dominant";
    EXPECT_LT(p_point.b, 80)  << "POINT sample at a red texel must have little blue";

    // (3) The LINEAR result must be a genuine blend (both R and B present),
    //     distinguishing it from POINT's saturated single texel.
    EXPECT_GT(p_linear.r, 40) << "LINEAR sample must carry red from the blend";
    EXPECT_GT(p_linear.b, 40) << "LINEAR sample must carry blue from the blend";

    d.destroy_sampler(point);
    d.destroy_sampler(linear);
    d.destroy_texture_view(tex_view);
    d.destroy_texture(tex);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

// ---- ADDRESS parity: WRAP vs CLAMP at UV out of [0,1] ----------------------
//
// Locks the address-mode half of the SamplerDesc->D3D12 mapping into the
// actually-bound path. Sampling at UV=1.5 with POINT filtering:
//   * WRAP  -> 1.5 wraps to 0.5 -> samples the texel at u=0.5 (texel column 1
//              on row 1.5->wrap 0.5 -> row 0): (1,0)=B=blue.
//   * CLAMP -> 1.5 clamps to the edge texel column 1: (1,0)=B=blue too at row 0.
// To make WRAP vs CLAMP DISAGREE we sample at u=1.25, v=0.25 (POINT):
//   * WRAP  -> u=1.25 wraps to 0.25 -> texel col 0; v=0.25 -> row 0 -> (0,0)=A=red.
//   * CLAMP -> u=1.25 clamps to 1.0 edge -> texel col 1; v=0.25 -> row 0 -> (1,0)=B=blue.
// So WRAP gives red, CLAMP gives blue — they differ ONLY if the address mode of
// the bound sampler is honoured (the bug's default is CLAMP for both).
TEST(D3D12SamplerBindParity, WrapVsClampAddressModeHonoured)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    bool skip = false;
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex,   kVS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kFS, &skip);
    if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    const Rgba8 kA { 255, 0, 0, 255 };  // red  @ (0,0),(1,1)
    const Rgba8 kB { 0, 0, 255, 255 };  // blue @ (1,0),(0,1)
    const auto tex = make_checker_2x2(d, kA, kB);
    ASSERT_TRUE(tex.is_valid());
    const auto tex_view = make_view(d, tex);
    ASSERT_TRUE(tex_view.is_valid());

    const auto wrap  = make_sampler(d, cd::rhi::SamplerFilter::kNearest,
                                    cd::rhi::SamplerAddressMode::kRepeat);
    const auto clamp = make_sampler(d, cd::rhi::SamplerFilter::kNearest,
                                    cd::rhi::SamplerAddressMode::kClampToEdge);
    ASSERT_TRUE(wrap.is_valid());
    ASSERT_TRUE(clamp.is_valid());

    const Rgba8 p_wrap  = sample_through(d, vs, fs, tex_view, wrap,  1.25F, 0.25F);
    const Rgba8 p_clamp = sample_through(d, vs, fs, tex_view, clamp, 1.25F, 0.25F);

    // WRAP -> red-dominant (wrapped back to texel (0,0)); CLAMP -> blue-dominant
    // (clamped to edge texel (1,0)). With the bug both default to CLAMP -> both
    // blue -> identical -> this FAILS.
    EXPECT_GT(p_wrap.r, p_wrap.b)
        << "BUG: WRAP address mode ignored — sample did not wrap to the red texel. "
           "WRAP=(" << +p_wrap.r << "," << +p_wrap.g << "," << +p_wrap.b << ")";
    EXPECT_GT(p_clamp.b, p_clamp.r)
        << "CLAMP sample at u>1 must clamp to the blue edge texel. CLAMP=("
        << +p_clamp.r << "," << +p_clamp.g << "," << +p_clamp.b << ")";
    const int diff =
        std::abs(static_cast<int>(p_wrap.r) - static_cast<int>(p_clamp.r)) +
        std::abs(static_cast<int>(p_wrap.b) - static_cast<int>(p_clamp.b));
    EXPECT_GT(diff, 120)
        << "WRAP and CLAMP must DIFFER — the bound sampler's address mode is honoured";

    d.destroy_sampler(wrap);
    d.destroy_sampler(clamp);
    d.destroy_texture_view(tex_view);
    d.destroy_texture(tex);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

}  // namespace

#else  // !_WIN32

TEST(D3D12SamplerBindParity, SkippedOffWindows)
{
    GTEST_SKIP() << "D3D12 custom-sampler bind parity is Windows-only";
}

#endif  // _WIN32
