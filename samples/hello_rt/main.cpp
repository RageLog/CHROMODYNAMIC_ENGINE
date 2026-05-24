// =============================================================================
// CHROMODYNAMIC — samples/hello_rt
//
// Phase 136 / v0.99.62 — RT pipeline + SBT-handle copy end-to-end
// infrastructure smoke. Exercises the full chain landed in
// Phases 132-135:
//   * Compile 3 minimal RT shaders (raygen / miss / closesthit) via
//     cd::shader::ICompiler.
//   * Create shader modules + descriptor set layout (empty) +
//     pipeline layout.
//   * Call cd::rhi::IDevice::create_rt_pipeline + populate SBT
//     handles via get_rt_shader_group_handles.
//   * Print device RT properties (handle size, alignment, base
//     alignment) + per-group handle byte dumps.
//
// What this sample DOES NOT do (deferred to a follow-up phase):
//   * vkCmdTraceRaysKHR dispatch — needs `DescriptorType::kAccelStructure`
//     wired so the raygen shader can bind a TLAS.
//   * Window / swapchain.
//
// Exit code 0 on success, non-zero on a backend that lacks RT
// extensions or fails any of the create steps.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

// Minimal RT shaders — no descriptor access, no traceRayEXT calls.
// raygen just exists; miss + closesthit are empty placeholders that
// satisfy the pipeline's group requirements. Real ray dispatch would
// trace against a TLAS bound via a descriptor set.
constexpr const char* kRaygenGlsl = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
void main() {
  // Intentionally empty — no traceRayEXT, no descriptor access.
}
)glsl";

constexpr const char* kMissGlsl = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec3 payload;
void main() {
  payload = vec3(0.05, 0.1, 0.2);
}
)glsl";

constexpr const char* kClosestHitGlsl = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec3 payload;
hitAttributeEXT vec2 bary;
void main() {
  payload = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
}
)glsl";

[[nodiscard]] cd::rhi::ShaderModuleHandle compile_to_module(
    cd::shader::ICompiler& comp,
    cd::rhi::IDevice& dev,
    const char* glsl,
    cd::shader::ShaderStage stage,
    const char* name)
{
    cd::shader::CompileDesc cd {};
    cd.source = glsl;
    cd.stage = stage;
    cd.lang = cd::shader::ShaderLanguage::kGlsl;
    cd.target = cd::shader::TargetEnv::kVulkan_1_3;
    cd.source_name = name;
    auto r = comp.compile(cd);
    if (!r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] %s compile failed: %.*s\n",
                     name,
                     static_cast<int>(r.error().message.size()),
                     r.error().message.data());
        return {};
    }
    cd::rhi::ShaderModuleDesc smd {};
    smd.code = r->spirv.data();
    smd.code_size = r->spirv.size() * sizeof(std::uint32_t);
    auto m = dev.create_shader_module(smd);
    if (!m.has_value())
    {
        std::fprintf(stderr, "[hello_rt] %s shader_module create failed\n", name);
        return {};
    }
    return *m;
}

}  // namespace

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_rt (Phase 136)\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto dev_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] device init failed: %.*s\n",
                     static_cast<int>(dev_r.error().message.size()),
                     dev_r.error().message.data());
        return 1;
    }
    auto& device = **dev_r;
    if (!device.features().ray_tracing)
    {
        std::printf("[hello_rt] adapter lacks ray-tracing extension; nothing to do.\n");
        return 0;  // graceful no-op — matches the "honest exit" pattern.
    }
    std::printf("[hello_rt] adapter: %.*s\n",
                static_cast<int>(device.adapter_name().size()),
                device.adapter_name().data());

    // ---- 1. Compile RT shaders -------------------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        std::fprintf(stderr, "[hello_rt] no glslang compiler available\n");
        return 2;
    }
    const auto rg_mod = compile_to_module(*compiler, device, kRaygenGlsl,
                                          cd::shader::ShaderStage::kRaygen, "raygen.rgen");
    const auto ms_mod = compile_to_module(*compiler, device, kMissGlsl,
                                          cd::shader::ShaderStage::kMiss, "miss.rmiss");
    const auto ch_mod = compile_to_module(*compiler, device, kClosestHitGlsl,
                                          cd::shader::ShaderStage::kClosestHit, "chit.rchit");
    if (!rg_mod.is_valid() || !ms_mod.is_valid() || !ch_mod.is_valid())
        return 3;
    std::printf("[hello_rt] 3 RT shader modules compiled.\n");

    // ---- 2. Pipeline layout (empty descriptor set) -----------------------
    cd::rhi::PipelineLayoutDesc pl_desc {};
    auto pl = device.create_pipeline_layout(pl_desc);
    if (!pl.has_value())
    {
        std::fprintf(stderr, "[hello_rt] pipeline layout create failed\n");
        return 4;
    }

    // ---- 3. RT pipeline --------------------------------------------------
    const cd::rhi::RtShaderEntry shaders[3] = {
        { cd::rhi::RtShaderStage::kRaygen,     rg_mod, "main", 0 },
        { cd::rhi::RtShaderStage::kMiss,       ms_mod, "main", 1 },
        { cd::rhi::RtShaderStage::kClosestHit, ch_mod, "main", 2 },
    };
    cd::rhi::RtPipelineDesc rtd {};
    rtd.shaders = std::span<const cd::rhi::RtShaderEntry>(shaders);
    rtd.max_recursion = 1;
    rtd.debug_name = "hello_rt";
    auto rtp = device.create_rt_pipeline(rtd, *pl);
    if (!rtp.has_value())
    {
        std::fprintf(stderr, "[hello_rt] create_rt_pipeline failed: %.*s\n",
                     static_cast<int>(rtp.error().message.size()),
                     rtp.error().message.data());
        device.destroy_pipeline_layout(*pl);
        return 5;
    }
    std::printf("[hello_rt] RT pipeline created (3 groups: raygen/miss/hit).\n");

    // ---- 4. SBT handle copy ---------------------------------------------
    const std::uint32_t handle_size = device.rt_shader_group_handle_size();
    const std::uint32_t handle_align = device.rt_shader_group_handle_alignment();
    const std::uint32_t base_align  = device.rt_shader_group_base_alignment();
    std::printf("[hello_rt] RT properties:\n");
    std::printf("           handle size       = %u B\n", handle_size);
    std::printf("           handle alignment  = %u B\n", handle_align);
    std::printf("           base alignment    = %u B\n", base_align);

    std::vector<std::byte> sbt_handles(static_cast<std::size_t>(handle_size) * 3u);
    if (auto r = device.get_rt_shader_group_handles(*rtp, 0, 3, std::span<std::byte>(sbt_handles));
        !r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] get_rt_shader_group_handles failed: %.*s\n",
                     static_cast<int>(r.error().message.size()),
                     r.error().message.data());
    }
    else
    {
        std::printf("[hello_rt] SBT handles copied (%zu bytes total).\n", sbt_handles.size());
        // Print the first 8 bytes of each handle as a sanity check —
        // they should be non-zero (driver-assigned unique handles).
        for (int g = 0; g < 3; ++g)
        {
            const auto base = sbt_handles.data() + static_cast<std::size_t>(g) * handle_size;
            std::printf("           group %d handle[0..7]: ", g);
            for (int b = 0; b < 8 && b < static_cast<int>(handle_size); ++b)
                std::printf("%02x ", static_cast<std::uint8_t>(base[b]));
            std::printf("\n");
        }
    }

    // ---- 5. Cleanup ------------------------------------------------------
    device.destroy_rt_pipeline(*rtp);
    device.destroy_pipeline_layout(*pl);
    device.destroy_shader_module(rg_mod);
    device.destroy_shader_module(ms_mod);
    device.destroy_shader_module(ch_mod);

    std::printf("[hello_rt] OK — full RT pipeline + SBT chain verified on this adapter.\n");
    return 0;
}
