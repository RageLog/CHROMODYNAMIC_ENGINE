// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_cobind_heap.cpp
//
// phase1190 B6 follow-up — UNIFIED descriptor-heap CO-BIND smoke for the D3D12
// backend. WARP-capable (SM6 descriptor-indexing / DESCRIPTORS_VOLATILE
// unbounded SRV arrays). Real D3D12 backend; honest GTEST_SKIP when no adapter,
// no DXC DLL, or the feature is unsupported on this WARP.
//
// THE BUG THIS TEST LOCKS: D3D12 allows exactly ONE CBV/SRV/UAV shader-visible
// heap bound per draw. Before the fix the per-set ring (gpu_heap_) and the
// persistent bindless pool (bindless_heap_) were SEPARATE heaps, so a single
// draw that called both bind_descriptor_set(set0) and
// bind_bindless_texture_array(set1) silently unbound whichever heap was set
// last -> the other table sampled garbage. The fix sub-allocates BOTH the ring
// and the bindless pool from ONE unified shader-visible CBV/SRV/UAV heap, so a
// co-bind keeps both regions live.
//
// THE TEST: one pipeline layout with a CLASSIC set-0 (combined sampler2D at
// t0/space0 + static sampler) AND the BINDLESS set-1 array (sampler2D[] at
// t0/space1). One draw binds set-0 (classic), set-1 (bindless), and
// dynamic-indexes the bindless array by a push-constant index while ALSO
// sampling the classic resource. The FS outputs classic in .r and bindless in
// .g; on read-back we assert BOTH the classic value AND the bindless-indexed
// value are correct — proving both heap regions resolve in ONE draw. If only
// one heap survived (the pre-fix bug) one channel would be wrong.
//
// Pattern: Arrange / Act / Assert. Windows + DXC only.
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

struct Rgba8 { std::uint8_t r, g, b, a; };

// Full-screen triangle from SV_VertexID. No vertex buffer needed.
constexpr const char* kCoVS = R"glsl(
#version 460
void main()
{
    vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                  (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)glsl";

// CO-BIND fragment shader: samples the CLASSIC set-0 combined sampler AND the
// BINDLESS set-1 array (dynamic-indexed by a push-constant) in the SAME draw.
// Output: classic.r in .r, bindless.g in .g. Both must resolve for the pixel to
// be correct — if either heap region were unbound the matching channel would be
// wrong/garbage.
constexpr const char* kCoFS = R"glsl(
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(push_constant) uniform PC { uint idx; } pc;
layout(set = 0, binding = 0) uniform sampler2D cd_classic;     // t0/space0 + static s0
layout(set = 1, binding = 0) uniform sampler2D cd_bindless[];  // t0/space1[]
layout(location = 0) out vec4 o;
void main()
{
    float c = texture(cd_classic, vec2(0.5)).r;
    float b = texture(cd_bindless[nonuniformEXT(pc.idx)], vec2(0.5)).g;
    o = vec4(c, b, 0.0, 1.0);
}
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;
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

// A 1x1 RGBA8 sampled texture seeded with a solid colour via staging upload.
[[nodiscard]] cd::rhi::TextureHandle
make_solid_texture(cd::rhi::IDevice& dev, Rgba8 colour)
{
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { 1, 1, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kSampled |
                      cd::rhi::TextureUsage::kTransferDst;
    auto tr = dev.create_texture(td);
    if (!tr.has_value())
        return {};
    const auto tex = *tr;

    const std::array<std::uint8_t, 4> px { colour.r, colour.g, colour.b, colour.a };
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
    region.image_extent = { 1, 1, 1 };
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
    Rgba8 px {
        std::to_integer<std::uint8_t>(raw[c + 0]),
        std::to_integer<std::uint8_t>(raw[c + 1]),
        std::to_integer<std::uint8_t>(raw[c + 2]),
        std::to_integer<std::uint8_t>(raw[c + 3]),
    };
    dev.destroy_buffer(buf);
    return px;
}

// ---- CO-BIND: classic set-0 + bindless set-1 BOTH resolve in ONE draw -------

TEST(D3D12CoBindHeap, ClassicAndBindlessResolveInOneDraw)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;
    if (!d.features().bindless_resources)
        GTEST_SKIP() << "device resource-binding tier < 2 — bindless unsupported";

    bool skip = false;
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex,   kCoVS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kCoFS, &skip);
    if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    // set 0 — CLASSIC combined sampler2D (t0/space0 + static s0).
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kSet0 {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
    };
    cd::rhi::DescriptorSetLayoutDesc set0_desc {};
    set0_desc.bindings = kSet0;
    auto set0 = d.create_descriptor_set_layout(set0_desc);
    ASSERT_TRUE(set0.has_value()) << set0.error().message;

    // set 1 — BINDLESS sampler2D array (space1, MEMORY rule 9).
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kSet1 {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kBindlessSampledImage,
            .count = 256, .stages = cd::rhi::ShaderStage::kFragment,
            .bindless = true },
    };
    cd::rhi::DescriptorSetLayoutDesc set1_desc {};
    set1_desc.bindings = kSet1;
    auto set1 = d.create_descriptor_set_layout(set1_desc);
    ASSERT_TRUE(set1.has_value()) << set1.error().message;

    // Pipeline layout: set 0 (classic) + set 1 (bindless at space1) + push
    // constant for the bindless slot index.
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 2> sets { *set0, *set1 };
    cd::rhi::PushConstantRange pcr {};
    pcr.size   = 4;  // one uint
    pcr.stages = cd::rhi::ShaderStage::kAllGraphics;
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts    = sets;
    pld.push_constants = std::span<const cd::rhi::PushConstantRange>(&pcr, 1);
    auto layout_r = d.create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value()) << layout_r.error().message;
    const auto layout = *layout_r;

    // CLASSIC resource: solid red (R is the channel the classic Sample reads).
    constexpr std::uint8_t kClassicR = 200;
    const auto classic_tex = make_solid_texture(d, { kClassicR, 0, 0, 255 });
    ASSERT_TRUE(classic_tex.is_valid());
    const auto classic_view = make_view(d, classic_tex);
    ASSERT_TRUE(classic_view.is_valid());

    auto classic_set = d.allocate_descriptor_set(*set0);
    ASSERT_TRUE(classic_set.has_value()) << classic_set.error().message;
    cd::rhi::DescriptorWrite dw {};
    dw.binding = 0;
    dw.type    = cd::rhi::DescriptorType::kCombinedImageSampler;
    dw.view    = classic_view;
    auto upd = d.update_descriptor_set(*classic_set,
        std::span<const cd::rhi::DescriptorWrite>(&dw, 1));
    ASSERT_TRUE(upd.has_value()) << upd.error().message;

    // BINDLESS array: three slots whose G channels differ. We index slot 2 so a
    // pre-fix "ring stomps bindless" bug or a "bindless unbinds ring" bug both
    // surface as a wrong channel.
    const std::array<std::uint8_t, 3> kBindlessG { 60, 130, 240 };
    cd::rhi::BindlessTextureArrayDesc adesc {};
    adesc.slot_count = 256;
    auto arr_r = d.create_bindless_texture_array(adesc);
    ASSERT_TRUE(arr_r.has_value()) << arr_r.error().message;
    const auto arr = *arr_r;
    std::array<cd::rhi::TextureHandle, 3> btexs {};
    std::array<cd::rhi::TextureViewHandle, 3> bviews {};
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        btexs[i] = make_solid_texture(d, { 0, kBindlessG[i], 0, 255 });
        ASSERT_TRUE(btexs[i].is_valid());
        bviews[i] = make_view(d, btexs[i]);
        ASSERT_TRUE(bviews[i].is_valid());
        auto w = d.write_bindless_texture_slot(arr, i, bviews[i]);
        ASSERT_TRUE(w.has_value()) << w.error().message;
    }

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout = layout;
    gpd.vertex_shader = vs;
    gpd.fragment_shader = fs;
    gpd.raster.cull = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    gpd.color_attachment_formats  = color_fmts;
    auto pso_r = d.create_graphics_pipeline(gpd);
    if (!pso_r.has_value())
        GTEST_SKIP() << "co-bind PSO creation unsupported on this adapter "
                        "(SM6 descriptor-indexing / unbounded SRV array): "
                     << pso_r.error().message;
    const auto pso = *pso_r;

    // Render once for each bindless index; the classic channel must ALWAYS be
    // kClassicR and the bindless channel must be kBindlessG[idx] — both in the
    // SAME draw that co-binds set 0 (classic) and set 1 (bindless).
    for (std::uint32_t idx = 0; idx < 3; ++idx)
    {
        const auto color = make_color_target(d);
        ASSERT_TRUE(color.is_valid());
        const auto color_view = make_view(d, color);
        ASSERT_TRUE(color_view.is_valid());

        auto cmd = d.create_command_buffer(cd::rhi::QueueType::kGraphics);
        cmd->begin();
        cd::rhi::ColorAttachmentInfo catt {};
        catt.view = color_view; catt.load_op = cd::rhi::LoadOp::kClear;
        catt.store_op = cd::rhi::StoreOp::kStore;
        catt.clear_color = { .f32 = { 0, 0, 0, 1 } };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.color_attachments = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
        rp.render_area.extent = { kW, kH };
        cmd->begin_render_pass(rp);
        cmd->set_viewport({ 0, 0, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F });
        cmd->set_scissor(cd::rhi::Rect2D { {}, { kW, kH } });
        cmd->bind_graphics_pipeline(pso);
        // CO-BIND: classic set 0 AND bindless set 1 in the SAME draw.
        cmd->bind_descriptor_set(0, *classic_set);       // ring region heap
        cmd->bind_bindless_texture_array(1, arr);        // bindless region heap
        cmd->push_constants(layout, cd::rhi::ShaderStage::kAllGraphics, 0,
                            sizeof(std::uint32_t), &idx);
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

        const Rgba8 px = read_center_texel(d, color);
        EXPECT_NEAR(px.r, kClassicR, 8)
            << "idx " << idx << " — CLASSIC set-0 channel must survive the co-bind "
               "(R from the classic sampler in the unified heap's ring region)";
        EXPECT_NEAR(px.g, kBindlessG[idx], 8)
            << "idx " << idx << " — BINDLESS set-1 channel must survive the co-bind "
               "(G from cd_bindless[idx] in the unified heap's bindless region)";

        d.destroy_texture_view(color_view);
        d.destroy_texture(color);
    }

    for (std::uint32_t i = 0; i < 3; ++i)
    {
        d.destroy_texture_view(bviews[i]);
        d.destroy_texture(btexs[i]);
    }
    d.destroy_bindless_texture_array(arr);
    d.destroy_descriptor_set(*classic_set);
    d.destroy_texture_view(classic_view);
    d.destroy_texture(classic_tex);
    d.destroy_graphics_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

}  // namespace

#else  // !_WIN32

TEST(D3D12CoBindHeap, SkippedOffWindows)
{
    GTEST_SKIP() << "D3D12 co-bind heap smoke is Windows-only";
}

#endif  // _WIN32
