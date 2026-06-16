// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_mip_state.cpp
//
// C-D3D12-FIXES / D-MIPSTATE: per-subresource resource-state tracking vs the
// Vulkan reference.
//
// BACKGROUND. TextureRecord.state was a SINGLE whole-resource D3D12_RESOURCE_
// STATES and barrier() ALWAYS used D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
// IGNORING the TextureBarrier's subresource range. So a per-mip transition (read
// mip N as a shader resource while writing mip N+1 as a UAV — the mip-generation
// pattern) was impossible: one barrier flipped the WHOLE resource, so the
// not-meant-to-move mip silently changed state too. Vulkan tracks layout per
// VkImageSubresourceRange, so the same RHI calls keep the two mips in different
// states on Vulkan and collapse them on D3D12. The fix adds per-subresource
// state (indexed by mip + layer*mip_levels) and emits DISTINCT per-subresource
// barriers for a subset range.
//
// WHAT THIS TEST PROVES (real WARP, deterministic — no validation-strictness
// dependency):
//
//   A 2-mip storage texture is moved whole-resource to UNORDERED_ACCESS, then a
//   SUBSET barrier moves ONLY mip 0 to PIXEL_SHADER_RESOURCE. The backend's
//   tracked state for mip 0 and mip 1 is then read via
//   debug_texture_subresource_state. With the fix they DIFFER (mip 0 = shader-
//   resource, mip 1 = unordered-access — the subset barrier emitted a DISTINCT
//   barrier for mip 0 only). With the fix temp-reverted (ALL_SUBRESOURCES), the
//   mip-0 barrier drags mip 1 along, so BOTH mips report the SAME state and the
//   EXPECT_NE FAILS. This is the "transition mip 1 while mip 0 stays in a
//   different state, assert distinct subresource barriers" scenario the brief
//   calls for, asserted deterministically at the state-tracking surface.
//
//   A functional half also dispatches a UAV write to mip 1 (valid only if mip 1
//   is still UNORDERED_ACCESS) and submits — proving the divergent state is a
//   real program, not just bookkeeping.
//
// GTEST_SKIP when no adapter / no glslang / no dxcompiler.dll / compute PSO
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

constexpr std::uint32_t kSize = 8;  // mip 0 = 8x8, mip 1 = 4x4.

// Compute shader: write a known value into the bound mip-1 UAV (image2D maps to
// RWTexture2D — the bound view is mip 1, so this addresses mip 1 directly).
constexpr const char* kCS = R"glsl(
#version 450
layout(local_size_x = 1, local_size_y = 1) in;
layout(binding = 0, rgba8) uniform writeonly image2D u_mip1;
void main()
{
    imageStore(u_mip1, ivec2(gl_GlobalInvocationID.xy), vec4(1.0));
}
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_validated()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
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

}  // namespace

// ---- D-MIPSTATE: a subset barrier leaves the other mip in its prior state ----
TEST(D3D12MipState, SubsetBarrierKeepsOtherMipInItsState)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_validated();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { kSize, kSize, 1 };
    td.mip_levels   = 2;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kStorage |
                      cd::rhi::TextureUsage::kSampled;
    auto tex_r = d.create_texture(td);
    ASSERT_TRUE(tex_r.has_value())
        << "2-mip storage texture creation failed: "
        << (tex_r.has_value() ? std::string {}
                              : std::string(tex_r.error().message.begin(),
                                            tex_r.error().message.end()));
    const auto tex = *tex_r;

    // A UAV view of MIP 1 (base_mip = 1).
    cd::rhi::TextureViewDesc vd {};
    vd.texture   = tex;
    vd.type      = cd::rhi::TextureType::k2D;
    vd.format    = cd::rhi::Format::kUndefined;
    vd.base_mip  = 1;
    vd.mip_count = 1;
    auto view_r = d.create_texture_view(vd);
    ASSERT_TRUE(view_r.has_value());
    const auto mip1_view = *view_r;

    bool skip = false;
    const auto cs = make_module(d, cd::rhi::ShaderStage::kCompute, kCS, &skip);
    if (skip)
        GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(cs.is_valid());

    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kStorageImage,
            .count = 1, .stages = cd::rhi::ShaderStage::kCompute },
    };
    cd::rhi::DescriptorSetLayoutDesc sld {};
    sld.bindings = bindings;
    auto set_layout_r = d.create_descriptor_set_layout(sld);
    ASSERT_TRUE(set_layout_r.has_value()) << set_layout_r.error().message;
    const auto set_layout = *set_layout_r;

    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sets { set_layout };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = sets;
    auto layout_r = d.create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value()) << layout_r.error().message;
    const auto layout = *layout_r;

    cd::rhi::ComputePipelineDesc cpd {};
    cpd.layout = layout;
    cpd.shader = cs;
    auto pso_r = d.create_compute_pipeline(cpd);
    if (!pso_r.has_value())
        GTEST_SKIP() << "compute PSO unsupported on this adapter: "
                     << std::string(pso_r.error().message.begin(),
                                    pso_r.error().message.end());
    const auto pso = *pso_r;

    auto set_r = d.allocate_descriptor_set(set_layout);
    ASSERT_TRUE(set_r.has_value()) << set_r.error().message;
    const auto set = *set_r;

    cd::rhi::DescriptorWrite dw {};
    dw.binding = 0;
    dw.type    = cd::rhi::DescriptorType::kStorageImage;
    dw.view    = mip1_view;
    auto upd = d.update_descriptor_set(
        set, std::span<const cd::rhi::DescriptorWrite>(&dw, 1));
    ASSERT_TRUE(upd.has_value()) << upd.error().message;

    auto cmd = d.create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    // 1) whole-resource COMMON -> UNORDERED_ACCESS (both mips UA).
    cd::rhi::TextureBarrier to_ua {};
    to_ua.texture = tex;
    to_ua.from    = cd::rhi::ResourceState::kCommon;
    to_ua.to      = cd::rhi::ResourceState::kUnorderedAccess;
    to_ua.range   = { 0, 2, 0, 1 };  // whole resource (both mips)
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_ua, 1));

    // 2) SUBSET barrier — move ONLY mip 0 to shader-resource.
    cd::rhi::TextureBarrier mip0_to_sr {};
    mip0_to_sr.texture = tex;
    mip0_to_sr.from    = cd::rhi::ResourceState::kUnorderedAccess;
    mip0_to_sr.to      = cd::rhi::ResourceState::kShaderResource;
    mip0_to_sr.range   = { 0, 1, 0, 1 };  // mip 0 ONLY
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&mip0_to_sr, 1));

    // --- DECISIVE (deterministic): the two mips now hold DIFFERENT states. ----
    // With the fix, the subset barrier emitted a DISTINCT barrier for mip 0
    // only, so mip 0 = shader-resource while mip 1 = unordered-access. With the
    // bug (ALL_SUBRESOURCES) the mip-0 barrier dragged mip 1 along, so both
    // report the SAME state and this EXPECT_NE FAILS.
    const std::uint32_t mip0_state = d.debug_texture_subresource_state(tex, 0, 0);
    const std::uint32_t mip1_state = d.debug_texture_subresource_state(tex, 1, 0);
    EXPECT_NE(mip0_state, mip1_state)
        << "BUG: after a SUBSET barrier on mip 0 only, mip 0 and mip 1 hold the "
           "SAME tracked state — the barrier used ALL_SUBRESOURCES and dragged "
           "mip 1 along instead of emitting a distinct per-subresource barrier "
           "(the per-mip tracking Vulkan has via VkImageSubresourceRange). "
           "mip0=0x" << std::hex << mip0_state << " mip1=0x" << mip1_state;

    // 3) Functional half: dispatch a UAV write to mip 1 (valid only if mip 1 is
    //    still UNORDERED_ACCESS) and submit. Proves the divergent state is a
    //    real program. A removed device (the reverted bug path) is caught via
    //    the Signal HRESULT, never an infinite wait.
    cmd->bind_compute_pipeline(pso);
    cmd->bind_descriptor_set(0, set);
    cmd->dispatch(kSize / 2, kSize / 2, 1);  // mip 1 is 4x4
    cmd->end();

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
        << "writing mip 1 as a UAV after moving ONLY mip 0 to shader-resource "
           "must be a valid program (mip 1 is still unordered-access): "
        << (submit_r.has_value() ? std::string {}
                                 : std::string(submit_r.error().message.begin(),
                                               submit_r.error().message.end()));
    d.wait_idle();

    d.destroy_semaphore(sem);
    d.destroy_compute_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_descriptor_set_layout(set_layout);
    d.destroy_texture_view(mip1_view);
    d.destroy_texture(tex);
    d.destroy_shader_module(cs);
}

#endif  // _WIN32
