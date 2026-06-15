// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_msaa_parity.cpp
//
// parity1121 WAVE A1 / D1: D3D12 MSAA SampleCount parity vs the Vulkan
// reference.
//
// BACKGROUND. The D3D12 backend historically hardcoded 1x MSAA in THREE places
// that ignored the engine descriptor:
//   * create_texture          — rd.SampleDesc.Count = 1
//   * graphics PSO            — psd.SampleDesc.Count = 1 + MultisampleEnable=FALSE
//   * mesh-shader PSO         — sd.Count = 1 (+ no MultisampleEnable)
// The Vulkan reference honors TextureDesc.samples / GraphicsPipelineDesc.samples
// via map_samples (VulkanDevice.cpp:1295/1728/2084, 2/4/8/16x). So a caller that
// asked for 4x MSAA got a SILENT 1x render on D3D12 and a true 4x render on
// Vulkan — a cross-backend divergence.
//
// THE FIX adds a D3D12 `map_samples(SampleCount, DXGI_FORMAT)` helper that
// validates the (format, count) pair against the adapter's supported MSAA
// quality levels (CheckFeatureSupport(MULTISAMPLE_QUALITY_LEVELS), clamp+warn on
// unsupported) and wires it into create_texture + BOTH PSO builders, plus sets
// RasterizerState.MultisampleEnable when samples > 1.
//
// WHAT THIS TEST PROVES (real WARP, no Vulkan device needed):
//
//   D3D12 enforces — at command-list execution — that a graphics PSO's
//   SampleDesc.Count MATCHES the sample count of the bound render-target
//   resource. So the ONLY way a 4x-PSO + 4x-RTV render is a valid program is if
//   BOTH create_texture AND the graphics-PSO builder honored desc.samples = k4.
//
//   We create a 4x MSAA colour texture + RTV + a 4x graphics PSO through the
//   RHI, render one triangle into it, and submit via the Result-returning
//   SubmitDesc overload with a SIGNAL SEMAPHORE. With the fix the program is
//   valid: the submission + a wait on the signal succeeds. If EITHER fix is
//   temp-reverted, the texture is created 1x while the PSO declares 4x (or vice
//   versa) — a sample-count MISMATCH the WARP debug layer turns into a removed
//   device, so the queue Signal returns DXGI_ERROR_DEVICE_REMOVED and the
//   SubmitDesc Result is an error -> this test FAILS. (Detection is via the
//   Signal HRESULT, never an INFINITE fence wait, so a removed device fails the
//   test instead of hanging it.)
//
//   Plus a creation-level assertion: the 4x texture + 4x PSO must CREATE
//   successfully (path coverage of the create_texture + PSO map_samples wiring),
//   which the engine's clamp keeps true on any 4x-capable adapter (WARP is).
//
// Validation layer ON so WARP enforces the sample-count match. GTEST_SKIP when
// no adapter / no glslang / no dxcompiler.dll / 4x MSAA unsupported for the
// format on this adapter. Pattern: Arrange/Act/Assert.
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

constexpr const char* kVS = R"glsl(
#version 450
void main()
{
    vec2 verts[3] = vec2[3](vec2(0.0, -0.8), vec2(0.8, 0.8), vec2(-0.8, 0.8));
    gl_Position = vec4(verts[gl_VertexIndex], 0.0, 1.0);
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) out vec4 o;
void main() { o = vec4(1.0, 1.0, 1.0, 1.0); }
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_validated()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    // Validation ON: makes WARP ENFORCE the PSO/RTV sample-count match so a
    // temp-reverted fix removes the device (detected via the Signal HRESULT)
    // instead of silently producing undefined results.
    ci.enable_validation = true;
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

[[nodiscard]] cd::rhi::TextureHandle
make_msaa_color_target(cd::rhi::IDevice& dev, cd::rhi::SampleCount samples)
{
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { kW, kH, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.samples      = samples;
    td.usage        = cd::rhi::TextureUsage::kColorAttachment |
                      cd::rhi::TextureUsage::kTransferSrc;
    auto r = dev.create_texture(td);
    return r.has_value() ? *r : cd::rhi::TextureHandle {};
}

// Returns true when copy_image_to_buffer SUCCEEDS for `tex`. D3D12 forbids a
// direct CopyTextureRegion from a MULTISAMPLED source (it requires a Resolve
// first), so on a genuinely 4x texture the one-shot copy hits a removed device
// and the Result is an error -> false. On a 1x texture the copy is valid ->
// true. This is the decisive descriptor-level proof that create_texture honored
// TextureDesc.samples: a silently-1x texture would copy fine.
[[nodiscard]] bool copy_succeeds(cd::rhi::IDevice& dev, cd::rhi::TextureHandle tex)
{
    constexpr std::uint64_t kBytes = std::uint64_t { kW } * kH * 4u;
    cd::rhi::BufferDesc bd {};
    bd.size   = kBytes;
    bd.usage  = cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto buf_r = dev.create_buffer(bd);
    if (!buf_r.has_value())
        return false;
    const auto buf = *buf_r;

    cd::rhi::IDevice::ImageRegion region {};
    region.width     = kW;
    region.height    = kH;
    region.src_state = cd::rhi::ResourceState::kColorAttachment;
    const auto r = dev.copy_image_to_buffer(tex, buf, 0, region);
    dev.destroy_buffer(buf);
    return r.has_value();
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

// ---- D1: a 4x-MSAA texture + 4x PSO is a valid render program ---------------
TEST(D3D12MsaaParity, FourXMsaaTextureAndPsoRenderIsValid)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_validated();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    constexpr auto kSamples = cd::rhi::SampleCount::k4;

    // --- Creation-level (path coverage of create_texture + PSO map_samples) ---
    const auto color = make_msaa_color_target(d, kSamples);
    ASSERT_TRUE(color.is_valid())
        << "4x MSAA colour texture creation failed (create_texture must honor "
           "TextureDesc.samples)";
    const auto color_view = make_view(d, color);
    ASSERT_TRUE(color_view.is_valid());

    bool skip = false;
    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex, kVS, &skip);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kFS, &skip);
    if (skip)
        GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    cd::rhi::PipelineLayoutDesc pld {};
    auto layout_r = d.create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value())
        << "pipeline layout creation failed: "
        << (layout_r.has_value() ? std::string {}
                                 : std::string(layout_r.error().message.begin(),
                                               layout_r.error().message.end()));
    const auto layout = *layout_r;

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout          = layout;
    gpd.vertex_shader   = vs;
    gpd.fragment_shader = fs;
    gpd.topology        = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull     = cd::rhi::CullMode::kNone;
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    gpd.samples                   = kSamples;  // 4x PSO — must match the RTV.
    gpd.color_attachment_formats  = color_fmts;
    auto pso_r = d.create_graphics_pipeline(gpd);
    ASSERT_TRUE(pso_r.has_value())
        << "4x MSAA graphics PSO creation failed: "
        << (pso_r.has_value() ? std::string {}
                              : std::string(pso_r.error().message.begin(),
                                            pso_r.error().message.end()));
    const auto pso = *pso_r;

    // --- Render-completes round trip (the decisive parity proof) -------------
    auto cmd = d.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
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
    cmd->draw(3, 1, 0, 0);
    cmd->end_render_pass();
    cmd->end();

    // Submit via the Result-returning overload WITH a signal semaphore. On a
    // sample-count mismatch (temp-reverted fix) WARP removes the device and the
    // queue Signal returns DXGI_ERROR_DEVICE_REMOVED -> kDeviceLost -> the
    // Result is an error. Never an INFINITE fence wait.
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
        << "BUG: 4x-MSAA texture + 4x PSO render was REJECTED by D3D12 — the "
           "texture and PSO sample counts disagree, which means create_texture "
           "or a PSO builder is not honoring desc.samples (the silent-1x bug "
           "Vulkan does not have): "
        << (submit_r.has_value() ? std::string {}
                                 : std::string(submit_r.error().message.begin(),
                                               submit_r.error().message.end()));

    d.wait_idle();

    d.destroy_semaphore(sem);
    d.destroy_texture_view(color_view);
    d.destroy_texture(color);
    d.destroy_graphics_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

// ---- D1 (decisive, bidirectional): create_texture honors TextureDesc.samples
//
// A 4x texture is genuinely MULTISAMPLED, so a direct CopyTextureRegion from it
// is invalid in D3D12 -> copy_image_to_buffer fails. A 1x texture copies fine.
// If create_texture silently ignored desc.samples (the pre-fix bug), the "4x"
// texture would actually be 1x and its copy would SUCCEED -> this test FAILS.
// So the assertion flips when the create_texture sample-count fix is reverted.
// No shaders needed; runs wherever a 4x-MSAA-capable D3D12 device exists.
TEST(D3D12MsaaParity, FourXTextureIsMultisampledOneXIsNot)
{
    auto dev = make_d3d12_device_validated();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    // Positive control: a 1x texture is single-sampled -> its copy SUCCEEDS.
    const auto one_x = make_msaa_color_target(d, cd::rhi::SampleCount::k1);
    ASSERT_TRUE(one_x.is_valid());
    EXPECT_TRUE(copy_succeeds(d, one_x))
        << "a 1x (non-MSAA) texture must be directly copyable";
    d.destroy_texture(one_x);

    // The decisive check: a 4x texture is multisampled -> its DIRECT copy FAILS.
    const auto four_x = make_msaa_color_target(d, cd::rhi::SampleCount::k4);
    ASSERT_TRUE(four_x.is_valid())
        << "4x MSAA texture creation failed (create_texture must honor samples)";
    EXPECT_FALSE(copy_succeeds(d, four_x))
        << "BUG: a 4x-MSAA texture was DIRECTLY copyable — it is actually 1x, so "
           "create_texture is NOT honoring TextureDesc.samples (the silent-1x "
           "parity bug Vulkan does not have).";
    d.destroy_texture(four_x);
}

#endif  // _WIN32
