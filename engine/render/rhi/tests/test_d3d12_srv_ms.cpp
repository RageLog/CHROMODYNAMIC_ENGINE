// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_srv_ms.cpp
//
// C-D3D12-FIXES / D-SRV-MS: a multisampled texture can be SAMPLED on D3D12 vs
// the Vulkan reference (the named A1 follow-up).
//
// BACKGROUND. create_texture_view's SRV path + the descriptor-set SRV writes all
// hardcoded D3D12_SRV_DIMENSION_TEXTURE2D, while the RTV / DSV paths already
// branched to *2DMS for a multisampled resource. So a 4x-MSAA texture could be a
// render target / depth target but could NOT be sampled: a TEXTURE2D SRV over a
// multisampled resource is an INVALID view (device-removal under the debug
// layer). On Vulkan the VkImageView automatically reflects the multisampled
// image, so the same RHI calls sample fine on Vulkan and break on D3D12. The fix
// adds an is_ms branch (sample_count > 1) -> D3D12_SRV_DIMENSION_TEXTURE2DMS at
// all five SRV sites.
//
// WHAT THIS TEST PROVES (real WARP, validation ON, end-to-end GPU):
//
//   Pass 1 renders a full-white triangle into a 4x-MSAA colour texture.
//   Pass 2 binds that MSAA texture as a kSampledImage and a fragment shader
//   resolves it via texelFetch(sampler2DMS, coord, 0..3) into a 1x output, which
//   is read back. The SRV the descriptor write creates is consumed by the
//   sampling draw, so its ViewDimension MUST match the resource: with the fix it
//   is TEXTURE2DMS (valid) and the program completes -> the submit succeeds and a
//   non-black pixel reads back. With the fix temp-reverted (TEXTURE2D SRV on the
//   MSAA resource) the WARP debug layer flags the invalid view at the sampling
//   draw and removes the device, so the queue Signal returns
//   DXGI_ERROR_DEVICE_REMOVED and the SubmitDesc Result is an error -> this test
//   FAILS. Detection is via the Signal HRESULT, never an INFINITE fence wait.
//
// GTEST_SKIP when no adapter / no glslang / no dxcompiler.dll / 4x MSAA
// unsupported. Pattern: Arrange/Act/Assert.
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
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#if defined(_WIN32)

namespace
{

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

// Pass 1: a full-screen white triangle into the MSAA target.
constexpr const char* kVS = R"glsl(
#version 460
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

constexpr const char* kFSWhite = R"glsl(
#version 460
layout(location = 0) out vec4 o;
void main() { o = vec4(1.0, 1.0, 1.0, 1.0); }
)glsl";

// Pass 2: resolve the 4x-MSAA texture by averaging its samples via texelFetch on
// a sampler2DMS — this is what requires a TEXTURE2DMS SRV (D-SRV-MS). A plain
// TEXTURE2D SRV over the MSAA resource is an invalid view the debug layer
// rejects when this draw consumes it.
constexpr const char* kFSResolve = R"glsl(
#version 460
layout(set = 0, binding = 0) uniform sampler2DMS cd_ms;  // MSAA SRV
layout(location = 0) out vec4 o;
void main()
{
    ivec2 c = ivec2(gl_FragCoord.xy);
    vec4 sum = vec4(0.0);
    for (int s = 0; s < 4; ++s)
        sum += texelFetch(cd_ms, c, s);
    o = sum * 0.25;
}
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_validated()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    // Validation ON so WARP ENFORCES the SRV ViewDimension / resource match: a
    // temp-reverted fix (TEXTURE2D SRV on the MSAA resource) removes the device,
    // detected via the Signal HRESULT, instead of silently sampling garbage.
    ci.enable_validation = true;
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
            *skip = true;
        return {};
    }
    return *r;
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

}  // namespace

// ---- D-SRV-MS: a 4x-MSAA texture is sampled through a TEXTURE2DMS SRV --------
TEST(D3D12SrvMs, MultisampledTextureIsSampleable)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_validated();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    // 4x-MSAA colour texture, also Sampled (so an SRV can be created over it).
    cd::rhi::TextureDesc msd {};
    msd.type         = cd::rhi::TextureType::k2D;
    msd.format       = cd::rhi::Format::kRGBA8Unorm;
    msd.extent       = { kW, kH, 1 };
    msd.mip_levels   = 1;
    msd.array_layers = 1;
    msd.samples      = cd::rhi::SampleCount::k4;
    msd.usage        = cd::rhi::TextureUsage::kColorAttachment |
                       cd::rhi::TextureUsage::kSampled;
    auto ms_r = d.create_texture(msd);
    ASSERT_TRUE(ms_r.has_value())
        << "4x-MSAA sampled texture creation failed: "
        << (ms_r.has_value() ? std::string {}
                             : std::string(ms_r.error().message.begin(),
                                           ms_r.error().message.end()));
    const auto ms_tex = *ms_r;
    const auto ms_view = make_view(d, ms_tex);
    ASSERT_TRUE(ms_view.is_valid());

    // 1x resolve output (colour attachment + transfer-src for readback).
    cd::rhi::TextureDesc od {};
    od.type         = cd::rhi::TextureType::k2D;
    od.format       = cd::rhi::Format::kRGBA8Unorm;
    od.extent       = { kW, kH, 1 };
    od.mip_levels   = 1;
    od.array_layers = 1;
    od.usage        = cd::rhi::TextureUsage::kColorAttachment |
                      cd::rhi::TextureUsage::kTransferSrc;
    auto out_r = d.create_texture(od);
    ASSERT_TRUE(out_r.has_value());
    const auto out_tex = *out_r;
    const auto out_view = make_view(d, out_tex);
    ASSERT_TRUE(out_view.is_valid());

    bool skip = false;
    const auto vs       = make_module(d, cd::rhi::ShaderStage::kVertex, kVS, &skip);
    const auto fs_white = make_module(d, cd::rhi::ShaderStage::kFragment, kFSWhite, &skip);
    const auto fs_res   = make_module(d, cd::rhi::ShaderStage::kFragment, kFSResolve, &skip);
    if (skip)
        GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(vs.is_valid() && fs_white.is_valid() && fs_res.is_valid());

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };

    // Pass-1 PSO: 4x, no descriptors.
    cd::rhi::PipelineLayoutDesc pld0 {};
    auto layout0_r = d.create_pipeline_layout(pld0);
    ASSERT_TRUE(layout0_r.has_value());
    const auto layout0 = *layout0_r;

    cd::rhi::GraphicsPipelineDesc g0 {};
    g0.layout                  = layout0;
    g0.vertex_shader           = vs;
    g0.fragment_shader         = fs_white;
    g0.topology                = cd::rhi::PrimitiveTopology::kTriangleList;
    g0.raster.cull             = cd::rhi::CullMode::kNone;
    g0.depth_stencil.depth_test  = false;
    g0.depth_stencil.depth_write = false;
    g0.samples                 = cd::rhi::SampleCount::k4;
    g0.color_attachment_formats = color_fmts;
    auto pso0_r = d.create_graphics_pipeline(g0);
    if (!pso0_r.has_value())
        GTEST_SKIP() << "4x MSAA PSO unsupported: "
                     << std::string(pso0_r.error().message.begin(),
                                    pso0_r.error().message.end());
    const auto pso0 = *pso0_r;

    // Pass-2 layout: one combined-image-sampler binding for the MSAA SRV.
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
    };
    cd::rhi::DescriptorSetLayoutDesc sld {};
    sld.bindings = bindings;
    auto set_layout_r = d.create_descriptor_set_layout(sld);
    ASSERT_TRUE(set_layout_r.has_value()) << set_layout_r.error().message;
    const auto set_layout = *set_layout_r;

    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sets { set_layout };
    cd::rhi::PipelineLayoutDesc pld1 {};
    pld1.set_layouts = sets;
    auto layout1_r = d.create_pipeline_layout(pld1);
    ASSERT_TRUE(layout1_r.has_value());
    const auto layout1 = *layout1_r;

    cd::rhi::GraphicsPipelineDesc g1 {};
    g1.layout                  = layout1;
    g1.vertex_shader           = vs;
    g1.fragment_shader         = fs_res;
    g1.topology                = cd::rhi::PrimitiveTopology::kTriangleList;
    g1.raster.cull             = cd::rhi::CullMode::kNone;
    g1.depth_stencil.depth_test  = false;
    g1.depth_stencil.depth_write = false;
    g1.samples                 = cd::rhi::SampleCount::k1;  // 1x resolve target
    g1.color_attachment_formats = color_fmts;
    auto pso1_r = d.create_graphics_pipeline(g1);
    ASSERT_TRUE(pso1_r.has_value())
        << "resolve PSO creation failed: "
        << (pso1_r.has_value() ? std::string {}
                               : std::string(pso1_r.error().message.begin(),
                                             pso1_r.error().message.end()));
    const auto pso1 = *pso1_r;

    auto set_r = d.allocate_descriptor_set(set_layout);
    ASSERT_TRUE(set_r.has_value()) << set_r.error().message;
    const auto set = *set_r;

    cd::rhi::DescriptorWrite dw {};
    dw.binding = 0;
    dw.type    = cd::rhi::DescriptorType::kCombinedImageSampler;
    dw.view    = ms_view;  // the MSAA view -> the SRV must be TEXTURE2DMS.
    auto upd = d.update_descriptor_set(
        set, std::span<const cd::rhi::DescriptorWrite>(&dw, 1));
    ASSERT_TRUE(upd.has_value()) << upd.error().message;

    // --- Record both passes + readback the resolve output --------------------
    auto cmd = d.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    // Pass 1: render white into the MSAA target.
    cd::rhi::ColorAttachmentInfo c0 {};
    c0.view        = ms_view;
    c0.load_op     = cd::rhi::LoadOp::kClear;
    c0.store_op    = cd::rhi::StoreOp::kStore;
    c0.clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } };
    cd::rhi::RenderPassBeginInfo rp0 {};
    rp0.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&c0, 1);
    rp0.render_area.extent = { kW, kH };
    cmd->begin_render_pass(rp0);
    cmd->set_viewport({ 0, 0, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F });
    cmd->set_scissor(cd::rhi::Rect2D { {}, { kW, kH } });
    cmd->bind_graphics_pipeline(pso0);
    cmd->draw(3, 1, 0, 0);
    cmd->end_render_pass();

    // MSAA target: RENDER_TARGET -> SHADER_RESOURCE for the sampling pass.
    cd::rhi::TextureBarrier to_srv {};
    to_srv.texture = ms_tex;
    to_srv.from    = cd::rhi::ResourceState::kColorAttachment;
    to_srv.to      = cd::rhi::ResourceState::kShaderResource;
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_srv, 1));

    // Pass 2: sample the MSAA texture into the 1x resolve output.
    cd::rhi::ColorAttachmentInfo c1 {};
    c1.view        = out_view;
    c1.load_op     = cd::rhi::LoadOp::kClear;
    c1.store_op    = cd::rhi::StoreOp::kStore;
    c1.clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } };
    cd::rhi::RenderPassBeginInfo rp1 {};
    rp1.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&c1, 1);
    rp1.render_area.extent = { kW, kH };
    cmd->begin_render_pass(rp1);
    cmd->set_viewport({ 0, 0, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F });
    cmd->set_scissor(cd::rhi::Rect2D { {}, { kW, kH } });
    cmd->bind_graphics_pipeline(pso1);
    cmd->bind_descriptor_set(0, set);
    cmd->draw(3, 1, 0, 0);
    cmd->end_render_pass();
    cmd->end();

    // Submit with a signal semaphore — a removed device (the reverted bug)
    // turns the queue Signal into an error, never an infinite wait.
    auto sem_r = d.create_semaphore();
    ASSERT_TRUE(sem_r.has_value());
    const auto sem = *sem_r;

    std::array<cd::rhi::ICommandBuffer*, 1> cmds { cmd.get() };
    const cd::rhi::SemaphoreSubmit signal { sem };
    cd::rhi::SubmitDesc sd {};
    sd.command_buffers   = std::span<cd::rhi::ICommandBuffer* const>(cmds.data(), 1);
    sd.signal_semaphores = std::span<const cd::rhi::SemaphoreSubmit>(&signal, 1);

    const auto submit_r = d.submit(sd);
    EXPECT_TRUE(submit_r.has_value())
        << "BUG: sampling a 4x-MSAA texture was REJECTED by D3D12 — the SRV "
           "ViewDimension is TEXTURE2D over a multisampled resource (an invalid "
           "view) instead of TEXTURE2DMS (the D-SRV-MS fix Vulkan does not need): "
        << (submit_r.has_value() ? std::string {}
                                 : std::string(submit_r.error().message.begin(),
                                               submit_r.error().message.end()));
    d.wait_idle();

    // If the program was valid, the resolve output is non-black (the MSAA target
    // was full white). Read back the centre pixel.
    if (submit_r.has_value())
    {
        constexpr std::uint64_t kBytes = std::uint64_t { kW } * kH * 4u;
        cd::rhi::BufferDesc bd {};
        bd.size   = kBytes;
        bd.usage  = cd::rhi::BufferUsage::kTransferDst;
        bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
        auto buf_r = d.create_buffer(bd);
        ASSERT_TRUE(buf_r.has_value());
        const auto buf = *buf_r;

        cd::rhi::IDevice::ImageRegion region {};
        region.width     = kW;
        region.height    = kH;
        region.src_state = cd::rhi::ResourceState::kColorAttachment;
        auto cp = d.copy_image_to_buffer(out_tex, buf, 0, region);
        ASSERT_TRUE(cp.has_value());

        std::array<std::byte, static_cast<std::size_t>(kW) * kH * 4u> raw {};
        auto dl = d.download_buffer(buf, 0, std::span<std::byte> { raw });
        ASSERT_TRUE(dl.has_value());
        const std::size_t centre = (static_cast<std::size_t>(kH / 2) * kW + kW / 2) * 4u;
        EXPECT_GT(std::to_integer<std::uint8_t>(raw[centre]), 200u)
            << "resolved MSAA sample should be near-white (the MSAA target was "
               "full white) — a near-black result means the texelFetch read "
               "nothing";
        d.destroy_buffer(buf);
    }

    d.destroy_semaphore(sem);
    d.destroy_graphics_pipeline(pso1);
    d.destroy_graphics_pipeline(pso0);
    d.destroy_pipeline_layout(layout1);
    d.destroy_pipeline_layout(layout0);
    d.destroy_descriptor_set_layout(set_layout);
    d.destroy_texture_view(out_view);
    d.destroy_texture(out_tex);
    d.destroy_texture_view(ms_view);
    d.destroy_texture(ms_tex);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs_white);
    d.destroy_shader_module(fs_res);
}

#endif  // _WIN32
