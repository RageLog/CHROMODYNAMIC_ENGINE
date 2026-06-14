// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_bindless_sampler.cpp
//
// D10 (bindless runtime) + B1b (sampler-half) GPU smoke tests for the D3D12
// backend. WARP-capable (SM6 descriptor-indexing / DESCRIPTORS_VOLATILE
// unbounded SRV arrays). Real D3D12 backend; honest GTEST_SKIP when no adapter,
// no DXC DLL, or the feature is unsupported on this WARP.
//
//   (a) BINDLESS smoke — three 1x1 textures (red / green / blue) are uploaded
//       and written to bindless slots 0/1/2 of a dedicated bindless array
//       (set 1 = register(t0, space1)[]). A full-screen-triangle shader
//       dynamic-indexes the array by a push-constant index and samples slot
//       [index]. For each of index=0/1/2 we render + read back the centre
//       pixel and assert it matches that slot's colour — proving the SRV
//       written by write_bindless_texture_slot landed at the right heap offset
//       and the shader's register(t0, space1)[index] resolved to it.
//
//   (b) SAMPLER smoke — a classic textured shader (combined sampler2D at set 0)
//       samples a uniformly-coloured texture through the now-BACKED static
//       sampler (B1b: NumStaticSamplers was 0 before, so any Sample() had no
//       sampler register). Read back + assert the sampled colour.
//
//   (c) STRUCTURAL fallback — when the dynamic-index path is unsupported on
//       this WARP build, the heap/write-slot structural correctness is still
//       asserted: features().bindless_resources is gated on the binding tier,
//       create_bindless_texture_array + write_bindless_texture_slot succeed,
//       and out-of-range / unknown-handle writes are rejected. These run with
//       NO shader/GPU draw so they verify the runtime even on a draw-less host.
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

// ---- (a) BINDLESS shaders ---------------------------------------------------
// Full-screen triangle from SV_VertexID. No vertex buffer needed.
constexpr const char* kBindlessVS = R"glsl(
#version 460
void main()
{
    vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                  (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)glsl";

// Dynamic-index the dedicated bindless set (set 1, binding 0 -> t0/space1).
// The slot index arrives via a push-constant (b0/space1 per the ADR).
constexpr const char* kBindlessFS = R"glsl(
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(push_constant) uniform PC { uint idx; } pc;
layout(set = 1, binding = 0) uniform sampler2D cd_bindless[];
layout(location = 0) out vec4 o;
void main()
{
    o = texture(cd_bindless[nonuniformEXT(pc.idx)], vec2(0.5));
}
)glsl";

// ---- (b) classic textured shader (set 0 combined sampler) -------------------
constexpr const char* kTexVS = kBindlessVS;
constexpr const char* kTexFS = R"glsl(
#version 460
layout(set = 0, binding = 0) uniform sampler2D cd_tex;  // t0/space0 + static s0
layout(location = 0) out vec4 o;
void main() { o = texture(cd_tex, vec2(0.5)); }
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

    // Staging upload buffer.
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

// ---- (c) STRUCTURAL: feature gate + write-slot validation (no GPU draw) -----

TEST(D3D12Bindless, FeatureGateAndWriteSlotStructuralCorrectness)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    // The feature must be gated on the resource-binding tier. If the device
    // does not advertise it, create_bindless_texture_array must honestly decline.
    if (!d.features().bindless_resources)
    {
        auto bad = d.create_bindless_texture_array(cd::rhi::BindlessTextureArrayDesc {});
        EXPECT_FALSE(bad.has_value())
            << "a device without bindless_resources must decline the array";
        GTEST_SKIP() << "device resource-binding tier < 2 — bindless unsupported";
    }

    // Allocate a real bindless array.
    cd::rhi::BindlessTextureArrayDesc adesc {};
    adesc.slot_count = 8;
    auto arr_r = d.create_bindless_texture_array(adesc);
    ASSERT_TRUE(arr_r.has_value()) << arr_r.error().message;
    const auto arr = *arr_r;

    // Seed one slot with a real texture view → must succeed.
    const auto tex = make_solid_texture(d, { 10, 20, 30, 255 });
    ASSERT_TRUE(tex.is_valid());
    const auto view = make_view(d, tex);
    ASSERT_TRUE(view.is_valid());
    auto ok = d.write_bindless_texture_slot(arr, 0, view);
    EXPECT_TRUE(ok.has_value()) << "slot 0 write must succeed: "
        << (ok.has_value() ? std::string {} : std::string { ok.error().message });

    // Negative: slot out of range, and an unknown view handle, must be rejected.
    auto oob = d.write_bindless_texture_slot(arr, 99, view);
    EXPECT_FALSE(oob.has_value()) << "out-of-range slot must be rejected";
    auto bad_view = d.write_bindless_texture_slot(arr, 1, cd::rhi::TextureViewHandle {});
    EXPECT_FALSE(bad_view.has_value()) << "unknown view handle must be rejected";

    d.destroy_bindless_texture_array(arr);
    d.destroy_texture_view(view);
    d.destroy_texture(tex);
}

// ---- (a) BINDLESS dynamic-index round-trip ---------------------------------

TEST(D3D12Bindless, DynamicIndexSamplesCorrectSlot)
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
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex,   kBindlessVS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kBindlessFS, &skip);
    if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    // The shader declares the bindless array at GLSL `set = 1` -> space1 (MEMORY
    // rule 9). For the pipeline layout to place the bindless set at space1, it
    // must occupy ORDINAL 1 — so we pass a set-0 layout first (one CBV so it
    // contributes a real table and the set->root-param mapping stays aligned),
    // exactly as the engine pairs set 0 (classic) + set 1 (bindless). The shader
    // never references space0; the set-0 table is declared but unused.
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kSet0 {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kUniformBuffer,
            .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
    };
    cd::rhi::DescriptorSetLayoutDesc set0_desc {};
    set0_desc.bindings = kSet0;
    auto set0 = d.create_descriptor_set_layout(set0_desc);
    ASSERT_TRUE(set0.has_value()) << set0.error().message;

    // set 1 — dedicated bindless sampler2D array (space1, MEMORY rule 9).
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

    // Pipeline layout: set 0 (placeholder) + set 1 (bindless at space1) + a
    // push-constant for the slot index (-> b0/space1).
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

    // Three distinct 1x1 textures → bindless slots 0/1/2.
    const std::array<Rgba8, 3> slot_colours {
        Rgba8 { 255, 0, 0, 255 }, Rgba8 { 0, 255, 0, 255 }, Rgba8 { 0, 0, 255, 255 } };
    std::array<cd::rhi::TextureHandle, 3> texs {};
    std::array<cd::rhi::TextureViewHandle, 3> views {};
    cd::rhi::BindlessTextureArrayDesc adesc {};
    adesc.slot_count = 256;
    auto arr_r = d.create_bindless_texture_array(adesc);
    ASSERT_TRUE(arr_r.has_value()) << arr_r.error().message;
    const auto arr = *arr_r;
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        texs[i] = make_solid_texture(d, slot_colours[i]);
        ASSERT_TRUE(texs[i].is_valid());
        views[i] = make_view(d, texs[i]);
        ASSERT_TRUE(views[i].is_valid());
        auto w = d.write_bindless_texture_slot(arr, i, views[i]);
        ASSERT_TRUE(w.has_value()) << w.error().message;
    }

    // PSO: full-screen triangle, one RGBA8 attachment, no depth.
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
        GTEST_SKIP() << "bindless PSO creation unsupported on this adapter "
                        "(SM6 descriptor-indexing / unbounded SRV array): "
                     << pso_r.error().message;
    const auto pso = *pso_r;

    // Render once per slot index; the centre pixel must be that slot's colour.
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
        cmd->bind_bindless_texture_array(1, arr);  // set 1 (bindless at space1)
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
        const Rgba8 want = slot_colours[idx];
        EXPECT_NEAR(px.r, want.r, 8) << "slot " << idx << " R";
        EXPECT_NEAR(px.g, want.g, 8) << "slot " << idx << " G";
        EXPECT_NEAR(px.b, want.b, 8) << "slot " << idx << " B";

        d.destroy_texture_view(color_view);
        d.destroy_texture(color);
    }

    for (std::uint32_t i = 0; i < 3; ++i)
    {
        d.destroy_texture_view(views[i]);
        d.destroy_texture(texs[i]);
    }
    d.destroy_bindless_texture_array(arr);
    d.destroy_graphics_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

// ---- (b) SAMPLER smoke: classic textured shader via the static sampler ------

TEST(D3D12Bindless, ClassicSamplerSamplesViaStaticSampler)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    bool skip = false;
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex,   kTexVS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kTexFS, &skip);
    if (skip) GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    // set 0 — one combined sampler2D (t0/space0). create_pipeline_layout must
    // emit a static sampler at s0/space0 (B1b); without it the Sample() has no
    // sampler register and the draw produces garbage / a validation error.
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kSet0 {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
    };
    cd::rhi::DescriptorSetLayoutDesc set0_desc {};
    set0_desc.bindings = kSet0;
    auto set0 = d.create_descriptor_set_layout(set0_desc);
    ASSERT_TRUE(set0.has_value()) << set0.error().message;

    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sets { *set0 };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = sets;
    auto layout_r = d.create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value()) << layout_r.error().message;
    const auto layout = *layout_r;

    // Source texture: solid orange.
    const Rgba8 src_colour { 255, 128, 0, 255 };
    const auto tex = make_solid_texture(d, src_colour);
    ASSERT_TRUE(tex.is_valid());
    const auto tex_view = make_view(d, tex);
    ASSERT_TRUE(tex_view.is_valid());

    auto set = d.allocate_descriptor_set(*set0);
    ASSERT_TRUE(set.has_value()) << set.error().message;
    cd::rhi::DescriptorWrite dw {};
    dw.binding = 0;
    dw.type    = cd::rhi::DescriptorType::kCombinedImageSampler;
    dw.view    = tex_view;
    auto upd = d.update_descriptor_set(*set,
        std::span<const cd::rhi::DescriptorWrite>(&dw, 1));
    ASSERT_TRUE(upd.has_value()) << upd.error().message;

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
    ASSERT_TRUE(pso_r.has_value())
        << std::string(pso_r.error().message.begin(), pso_r.error().message.end());
    const auto pso = *pso_r;

    const auto color = make_color_target(d);
    const auto color_view = make_view(d, color);

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
    cmd->bind_descriptor_set(0, *set);
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
    EXPECT_NEAR(px.r, src_colour.r, 8) << "sampled R must match source — proves "
                                          "the static sampler backs s0";
    EXPECT_NEAR(px.g, src_colour.g, 8) << "sampled G must match source";
    EXPECT_NEAR(px.b, src_colour.b, 8) << "sampled B must match source";

    d.destroy_texture_view(color_view);
    d.destroy_texture(color);
    d.destroy_texture_view(tex_view);
    d.destroy_texture(tex);
    d.destroy_descriptor_set(*set);
    d.destroy_graphics_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

}  // namespace

#else  // !_WIN32

TEST(D3D12Bindless, SkippedOffWindows)
{
    GTEST_SKIP() << "D3D12 bindless/sampler smokes are Windows-only";
}

#endif  // _WIN32
