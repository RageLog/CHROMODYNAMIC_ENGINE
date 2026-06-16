// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_rhi_bindpoint.cpp
//
// A-BINDPOINT (Backend-to-100 Wave 3c): EXPLICIT per-command-buffer bind point.
//
// BACKGROUND. The cmd-buffer used to infer the descriptor bind point from
// "which native pipeline-layout handle is non-null". A compute dispatch recorded
// AFTER a graphics render pass would then silently pick the GRAPHICS bind point
// (the stale graphics layout was still set) and the compute descriptor set never
// reached the dispatch — the DDGI bug class phase1213 papered over with an
// end_render_pass layout reset. Wave 3c replaces the heuristic with an explicit
// BindPoint{kGraphics,kCompute,kRayTracing} tracked per command buffer, reset off
// graphics at end_render_pass, so the post-pass compute route is robust by
// construction (and RT aliases compute for descriptor binding).
//
// WHAT THIS TEST PROVES (Vulkan lavapipe/RTX 3080 + D3D12 WARP):
//   A graphics render pass (clear a colour target) is recorded, end_render_pass
//   resets the bind point, THEN a compute pipeline with ITS OWN descriptor set
//   (a storage buffer) is bound + dispatched to write a KNOWN sentinel value via
//   that set. Readback of the storage buffer == the sentinel proves the compute
//   descriptor reached the dispatch through the COMPUTE bind point — i.e. the
//   render pass did not leave the cmd buffer routing compute binds to graphics.
//   This regression-locks the DDGI bug class: temp-reverting end_render_pass's
//   bind-point reset (or the bind_descriptor_set explicit-point routing) makes
//   the compute write land via the wrong root and the sentinel never appears.
//
// On Vulkan the device is created WITH validation so a mis-routed bind also trips
// a validation error; the assert is on the readback value (the decisive signal).
//
// SKIPs cleanly when no device / no glslang / no DXC DLL / compute unsupported.
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
#include <cd/rhi/NullCommandBuffer.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace
{

// One thread writes a known sentinel into a storage buffer through the compute
// descriptor set's binding 0. If the set reaches the dispatch, slot 0 == sentinel.
constexpr std::uint32_t kSentinel = 0xC0FFEE42u;
constexpr const char* kCS = R"glsl(
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(std430, binding = 0) buffer Out { uint v[]; } b;
void main()
{
    b.v[0] = 0xC0FFEE42u;
}
)glsl";

// A trivial graphics pipeline with ITS OWN descriptor set (a UBO at set 0,
// binding 0). Binding this pipeline + its set inside the render pass sets the
// cmd buffer's GRAPHICS layout — the precondition for the phase1213 DDGI bug
// (a stale graphics layout poisoning the follow-up compute descriptor bind).
constexpr const char* kVS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform U { vec4 tint; } u;
void main()
{
    vec2 p = vec2((gl_VertexIndex == 2) ? 3.0 : -1.0,
                  (gl_VertexIndex == 1) ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0) + vec4(u.tint.x) * 0.0;
}
)glsl";
constexpr const char* kFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform U { vec4 tint; } u;
layout(location = 0) out vec4 o;
void main() { o = u.tint; }
)glsl";

[[nodiscard]] bool glslang_available()
{
    return cd::shader::make_glslang_compiler() != nullptr;
}

[[nodiscard]] cd::rhi::ShaderModuleHandle
make_module(cd::rhi::IDevice& dev, cd::rhi::ShaderStage stage, const char* src, bool* skip)
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

// Shared body: graphics render pass, then a compute dispatch with its own set.
void run_compute_after_graphics(cd::rhi::IDevice& dev)
{
    bool skip = false;
    const auto cs = make_module(dev, cd::rhi::ShaderStage::kCompute, kCS, &skip);
    const auto vs = make_module(dev, cd::rhi::ShaderStage::kVertex, kVS, &skip);
    const auto fs = make_module(dev, cd::rhi::ShaderStage::kFragment, kFS, &skip);
    if (skip)
        GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(cs.is_valid());
    ASSERT_TRUE(vs.is_valid());
    ASSERT_TRUE(fs.is_valid());

    // --- A 4x4 colour target for the (bind-point-poisoning) graphics pass. ---
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { 4, 4, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kColorAttachment;
    auto tex_r = dev.create_texture(td);
    ASSERT_TRUE(tex_r.has_value());
    const auto tex = *tex_r;
    cd::rhi::TextureViewDesc vd {};
    vd.texture = tex;
    auto view_r = dev.create_texture_view(vd);
    ASSERT_TRUE(view_r.has_value());
    const auto view = *view_r;

    // --- The graphics pipeline + its UBO descriptor set. Binding this inside
    //     the render pass is what sets the GRAPHICS layout (the bug precondition).
    cd::rhi::BufferDesc ubo_bd {};
    ubo_bd.size   = 16;  // vec4 tint
    ubo_bd.usage  = cd::rhi::BufferUsage::kUniform;
    ubo_bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto ubo_r = dev.create_buffer(ubo_bd);
    ASSERT_TRUE(ubo_r.has_value());
    const auto ubo = *ubo_r;

    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kGfxB {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kUniformBuffer,
            .count = 1, .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment },
    };
    cd::rhi::DescriptorSetLayoutDesc gfx_sld {};
    gfx_sld.bindings = kGfxB;
    auto gfx_sl_r = dev.create_descriptor_set_layout(gfx_sld);
    ASSERT_TRUE(gfx_sl_r.has_value()) << gfx_sl_r.error().message;
    const auto gfx_sl = *gfx_sl_r;
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> gfx_sets { gfx_sl };
    cd::rhi::PipelineLayoutDesc gfx_pld {};
    gfx_pld.set_layouts = gfx_sets;
    auto gfx_pl_r = dev.create_pipeline_layout(gfx_pld);
    ASSERT_TRUE(gfx_pl_r.has_value()) << gfx_pl_r.error().message;
    const auto gfx_pl = *gfx_pl_r;

    const std::array<cd::rhi::Format, 1> gfx_color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout = gfx_pl;
    gpd.vertex_shader = vs;
    gpd.fragment_shader = fs;
    gpd.color_attachment_formats = gfx_color_fmts;
    gpd.depth_stencil.depth_test = false;
    gpd.depth_stencil.depth_write = false;
    gpd.raster.cull = cd::rhi::CullMode::kNone;
    auto gpso_r = dev.create_graphics_pipeline(gpd);
    if (!gpso_r.has_value())
        GTEST_SKIP() << "graphics PSO unsupported on this adapter: "
                     << std::string(gpso_r.error().message.begin(),
                                    gpso_r.error().message.end());
    const auto gpso = *gpso_r;

    auto gfx_set_r = dev.allocate_descriptor_set(gfx_sl);
    ASSERT_TRUE(gfx_set_r.has_value()) << gfx_set_r.error().message;
    const auto gfx_set = *gfx_set_r;
    cd::rhi::DescriptorWrite gfx_dw {};
    gfx_dw.binding = 0;
    gfx_dw.type    = cd::rhi::DescriptorType::kUniformBuffer;
    gfx_dw.buffer  = ubo;
    auto gfx_upd = dev.update_descriptor_set(
        gfx_set, std::span<const cd::rhi::DescriptorWrite>(&gfx_dw, 1));
    ASSERT_TRUE(gfx_upd.has_value()) << gfx_upd.error().message;

    // --- The compute storage buffer + its descriptor set. ---
    constexpr std::uint64_t kBytes = 16;  // room for v[0..3]
    cd::rhi::BufferDesc sb_bd {};
    sb_bd.size   = kBytes;
    sb_bd.usage  = cd::rhi::BufferUsage::kStorage | cd::rhi::BufferUsage::kTransferSrc;
    sb_bd.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto sb_r = dev.create_buffer(sb_bd);
    ASSERT_TRUE(sb_r.has_value());
    const auto sb = *sb_r;

    cd::rhi::BufferDesc rb_bd {};
    rb_bd.size   = kBytes;
    rb_bd.usage  = cd::rhi::BufferUsage::kTransferDst;
    rb_bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto rb_r = dev.create_buffer(rb_bd);
    ASSERT_TRUE(rb_r.has_value());
    const auto rb = *rb_r;

    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kB {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kStorageBuffer,
            .count = 1, .stages = cd::rhi::ShaderStage::kCompute },
    };
    cd::rhi::DescriptorSetLayoutDesc sld {};
    sld.bindings = kB;
    auto sl_r = dev.create_descriptor_set_layout(sld);
    ASSERT_TRUE(sl_r.has_value()) << sl_r.error().message;
    const auto sl = *sl_r;

    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sets { sl };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = sets;
    auto pl_r = dev.create_pipeline_layout(pld);
    ASSERT_TRUE(pl_r.has_value()) << pl_r.error().message;
    const auto pl = *pl_r;

    cd::rhi::ComputePipelineDesc cpd {};
    cpd.layout = pl;
    cpd.shader = cs;
    auto pso_r = dev.create_compute_pipeline(cpd);
    if (!pso_r.has_value())
        GTEST_SKIP() << "compute PSO unsupported on this adapter: "
                     << std::string(pso_r.error().message.begin(),
                                    pso_r.error().message.end());
    const auto pso = *pso_r;

    auto set_r = dev.allocate_descriptor_set(sl);
    ASSERT_TRUE(set_r.has_value()) << set_r.error().message;
    const auto set = *set_r;
    cd::rhi::DescriptorWrite dw {};
    dw.binding = 0;
    dw.type    = cd::rhi::DescriptorType::kStorageBuffer;
    dw.buffer  = sb;
    auto upd = dev.update_descriptor_set(
        set, std::span<const cd::rhi::DescriptorWrite>(&dw, 1));
    ASSERT_TRUE(upd.has_value()) << upd.error().message;

    // --- Record: graphics pass (poisons the bind point), THEN compute. -------
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    cd::rhi::TextureBarrier to_color {};
    to_color.texture = tex;
    to_color.from    = cd::rhi::ResourceState::kUndefined;
    to_color.to      = cd::rhi::ResourceState::kColorAttachment;
    to_color.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_color, 1));

    cd::rhi::ColorAttachmentInfo ca {};
    ca.view     = view;
    ca.load_op  = cd::rhi::LoadOp::kClear;
    ca.store_op = cd::rhi::StoreOp::kStore;
    cd::rhi::RenderPassBeginInfo rp {};
    rp.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&ca, 1);
    rp.render_area.extent = { 4, 4 };
    cmd->begin_render_pass(rp);
    // Bind a graphics pipeline + ITS descriptor set + draw INSIDE the pass —
    // this is what sets the cmd buffer's GRAPHICS layout, the precondition for
    // the phase1213 bug (a stale graphics layout poisoning the post-pass compute
    // descriptor bind). A full-screen triangle covers the 4x4 target.
    cd::rhi::Viewport vp { 0.0F, 0.0F, 4.0F, 4.0F, 0.0F, 1.0F };
    cmd->set_viewport(vp);
    cmd->set_scissor(cd::rhi::Rect2D { { 0, 0 }, { 4, 4 } });
    cmd->bind_graphics_pipeline(gpso);
    cmd->bind_descriptor_set(0, gfx_set);
    cmd->draw(3, 1, 0, 0);
    cmd->end_render_pass();

    // The decisive sequence: compute pipeline + ITS set + dispatch. If the
    // bind point were still graphics, bind_descriptor_set would route the set
    // to the graphics root and the storage write would never land.
    cmd->bind_compute_pipeline(pso);
    cmd->bind_descriptor_set(0, set);
    cmd->dispatch(1, 1, 1);

    cd::rhi::BufferBarrier sb_to_src {};
    sb_to_src.buffer = sb;
    sb_to_src.from   = cd::rhi::ResourceState::kUnorderedAccess;
    sb_to_src.to     = cd::rhi::ResourceState::kTransferSrc;
    cmd->barrier(std::span<const cd::rhi::BufferBarrier>(&sb_to_src, 1), {});

    std::array<cd::rhi::BufferCopyRegion, 1> creg {
        cd::rhi::BufferCopyRegion { .src_offset = 0, .dst_offset = 0, .size = kBytes }
    };
    cmd->copy_buffer(sb, rb, creg);
    cmd->end();

    EXPECT_FALSE(cmd->recording_error())
        << "no recording call hit an invalid handle in this sequence";

    dev.submit(*cmd);
    dev.wait_idle();

    // --- Assert: the compute set reached the dispatch. -----------------------
    std::array<std::byte, kBytes> raw {};
    auto dl = dev.download_buffer(rb, 0, std::span<std::byte> { raw });
    ASSERT_TRUE(dl.has_value());
    std::uint32_t got = 0;
    std::memcpy(&got, raw.data(), sizeof(got));
    EXPECT_EQ(got, kSentinel)
        << "BUG: the compute descriptor set did NOT reach the dispatch after a "
           "graphics render pass — the bind point was poisoned to GRAPHICS (the "
           "phase1213 DDGI class). Got 0x" << std::hex << got;

    dev.destroy_buffer(rb);
    dev.destroy_buffer(sb);
    dev.destroy_compute_pipeline(pso);
    dev.destroy_pipeline_layout(pl);
    dev.destroy_descriptor_set_layout(sl);
    dev.destroy_graphics_pipeline(gpso);
    dev.destroy_pipeline_layout(gfx_pl);
    dev.destroy_descriptor_set_layout(gfx_sl);
    dev.destroy_buffer(ubo);
    dev.destroy_texture_view(view);
    dev.destroy_texture(tex);
    dev.destroy_shader_module(fs);
    dev.destroy_shader_module(vs);
    dev.destroy_shader_module(cs);
}

}  // namespace

TEST(RhiBindPoint, VulkanComputeAfterGraphicsReachesDispatch)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = true;  // a mis-routed bind also trips validation.
    auto dev_r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!dev_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD available on this host";
    run_compute_after_graphics(**dev_r);
}

#if defined(_WIN32)
TEST(RhiBindPoint, D3D12ComputeAfterGraphicsReachesDispatch)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev_r = cd::rhi::d3d12::create_d3d12_device({});
    if (!dev_r.has_value())
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    run_compute_after_graphics(**dev_r);
}
#endif

// ---- A-RESULT-DIAG: a recording call with an invalid handle latches the flag -
//
// Issue a bind_descriptor_set with a deliberately-invalid set handle on a real
// command buffer; recording_error() must become true. In a release build the
// call is a safe no-op (no crash) and the flag is the only observable signal;
// in debug the same call would also assert at the miss site (not exercised here
// because gtest runs the release-style path — the flag is the portable proof).

static void run_recording_error_latch(cd::rhi::IDevice& dev)
{
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    EXPECT_FALSE(cmd->recording_error()) << "begin() resets the flag";

    // This test DELIBERATELY feeds a bad handle; disable the debug assert around
    // the one call so the intentional miss latches the flag instead of aborting
    // (the assert is the "LOUD in debug" half of the contract for UNINTENDED
    // misses — see set_recording_error_assert_enabled). Re-enable after.
    cd::rhi::set_recording_error_assert_enabled(false);
    const cd::rhi::DescriptorSetHandle bad { 0xDEAD, 0 };
    cmd->bind_descriptor_set(0, bad);  // safe no-op + latches the flag.
    cd::rhi::set_recording_error_assert_enabled(true);

    EXPECT_TRUE(cmd->recording_error())
        << "a recording call with an invalid handle must latch recording_error()";

    cmd->end();
    // Do NOT submit — the buffer recorded nothing real; this is a CPU-side
    // contract assertion only.
}

TEST(RhiBindPoint, VulkanRecordingErrorLatchesOnBadHandle)
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto dev_r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!dev_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD available on this host";
    run_recording_error_latch(**dev_r);
}

#if defined(_WIN32)
TEST(RhiBindPoint, D3D12RecordingErrorLatchesOnBadHandle)
{
    auto dev_r = cd::rhi::d3d12::create_d3d12_device({});
    if (!dev_r.has_value())
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    run_recording_error_latch(**dev_r);
}
#endif

// The Null reference: base recording_error() default is false and the headless
// recorder never does handle lookups, so the flag stays false (documented).
TEST(RhiBindPoint, NullRecordingErrorDefaultsFalse)
{
    cd::rhi::NullCommandBuffer cb;
    cb.begin();
    cb.bind_descriptor_set(0, cd::rhi::DescriptorSetHandle { 0xDEAD, 0 });
    EXPECT_FALSE(cb.recording_error());
}
