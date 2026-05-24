// =============================================================================
// CHROMODYNAMIC — samples/hello_rt
//
// Phase 141 / v0.99.71 — REAL ray dispatch (BLAS + TLAS + storage image +
// descriptor set + SBT + vkCmdTraceRaysKHR).
//
// Builds on Phases 132-136 (BLAS/TLAS create + build, RT pipeline + SBT
// handle copy) by adding the missing pieces:
//   * Phase 140's DescriptorType::kAccelerationStructure used to bind
//     the TLAS into the raygen shader.
//   * BufferUsage::kShaderBindingTable + a real SBT buffer carrying
//     the raygen / miss / hit handles.
//   * Storage image output (256x256 RGBA8 unorm) bound at descriptor
//     binding 1 — raygen writes a color per pixel through imageStore.
//   * Command-buffer record: bind_rt_pipeline → bind_descriptor_set →
//     dispatch_rays(width, height, 1) → submit → wait_idle.
//
// Validation layers (enabled by default on debug builds via
// VulkanCreateInfo::enable_validation) are the success oracle: the
// program returns 0 only after the dispatch returns cleanly with no
// validation error.
//
// What this sample still DOES NOT do:
//   * Readback + on-disk PNG of the storage image — the descriptor
//     wiring is the primary deliverable; visual verification is a
//     follow-up phase once the cd_cook_texture pipeline gains a PNG
//     writer.
//   * Window / swapchain — keeps the sample headless.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

namespace
{

// Phase 141 — raygen now actually writes a per-pixel color via an
// imageStore into binding 1 (rgba8 storage image). Miss returns
// background, closest-hit returns barycentric debug coords.
constexpr const char* kRaygenGlsl = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(binding = 0, set = 0) uniform accelerationStructureEXT tlas;
layout(binding = 1, set = 0, rgba8) uniform image2D img;
layout(location = 0) rayPayloadEXT vec3 payload;
void main() {
  const ivec2 px  = ivec2(gl_LaunchIDEXT.xy);
  const vec2  uv  = (vec2(px) + 0.5) / vec2(gl_LaunchSizeEXT.xy);
  const vec3  org = vec3(uv * 2.0 - 1.0, -1.5);
  const vec3  dir = normalize(vec3(0.0, 0.0, 1.0));
  payload = vec3(0.0);
  traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF,
              /*sbtRecordOffset=*/0, /*sbtRecordStride=*/0,
              /*missIndex=*/0, org, 0.001, dir, 1000.0, 0);
  imageStore(img, px, vec4(payload, 1.0));
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

[[nodiscard]] std::uint64_t align_up(std::uint64_t v, std::uint64_t a) noexcept
{
    return (v + a - 1) & ~(a - 1);
}

}  // namespace

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_rt (Phase 141)\n",
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
        return 0;
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

    // ---- 2. Descriptor set layout (TLAS @ 0, storage image @ 1) ----------
    std::array<cd::rhi::DescriptorSetLayoutBinding, 2> dsl_bindings { {
        { 0, cd::rhi::DescriptorType::kAccelerationStructure, 1, cd::rhi::ShaderStage::kRayGen },
        { 1, cd::rhi::DescriptorType::kStorageImage,          1, cd::rhi::ShaderStage::kRayGen },
    } };
    cd::rhi::DescriptorSetLayoutDesc dsld {};
    dsld.bindings = std::span<const cd::rhi::DescriptorSetLayoutBinding>(dsl_bindings);
    auto dsl = device.create_descriptor_set_layout(dsld);
    if (!dsl.has_value())
    {
        std::fprintf(stderr, "[hello_rt] descriptor set layout create failed\n");
        return 4;
    }

    // ---- 3. Pipeline layout ----------------------------------------------
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> dsl_arr { *dsl };
    cd::rhi::PipelineLayoutDesc pl_desc {};
    pl_desc.set_layouts = std::span<const cd::rhi::DescriptorSetLayoutHandle>(dsl_arr);
    auto pl = device.create_pipeline_layout(pl_desc);
    if (!pl.has_value())
    {
        std::fprintf(stderr, "[hello_rt] pipeline layout create failed\n");
        return 5;
    }

    // ---- 4. RT pipeline --------------------------------------------------
    const std::array<cd::rhi::RtShaderEntry, 3> shaders { {
        { cd::rhi::RtShaderStage::kRaygen,     rg_mod, "main", 0 },
        { cd::rhi::RtShaderStage::kMiss,       ms_mod, "main", 1 },
        { cd::rhi::RtShaderStage::kClosestHit, ch_mod, "main", 2 },
    } };
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
        return 6;
    }
    std::printf("[hello_rt] RT pipeline created (3 groups: raygen/miss/hit).\n");

    // ---- 5. BLAS (single triangle) ---------------------------------------
    constexpr std::array<float, 9> kTriVerts { {
         0.0F,  0.6F, 0.0F,
        -0.6F, -0.6F, 0.0F,
         0.6F, -0.6F, 0.0F,
    } };
    cd::rhi::BufferDesc vb_desc {};
    vb_desc.size = sizeof(kTriVerts);
    vb_desc.usage = cd::rhi::BufferUsage::kStorage |
                    cd::rhi::BufferUsage::kTransferDst |
                    cd::rhi::BufferUsage::kVertex;
    vb_desc.memory = cd::rhi::MemoryUsage::kAuto;
    auto vb_r = device.create_buffer(vb_desc);
    if (!vb_r.has_value()) return 7;
    (void)device.upload_buffer(*vb_r, 0,
        std::span<const std::byte> {
            reinterpret_cast<const std::byte*>(kTriVerts.data()), sizeof(kTriVerts)
        });

    cd::rhi::AccelTriangleGeometry tri {};
    tri.vertex_buffer = *vb_r;
    tri.vertex_count  = 3;
    tri.vertex_stride = 12;
    std::array<cd::rhi::AccelTriangleGeometry, 1> tri_arr { tri };

    cd::rhi::AccelStructureDesc blas_desc {};
    blas_desc.kind      = cd::rhi::AccelStructureKind::kBottomLevel;
    blas_desc.triangles = std::span<const cd::rhi::AccelTriangleGeometry>(tri_arr);
    blas_desc.debug_name = "blas_tri";
    auto blas_r = device.create_acceleration_structure(blas_desc);
    if (!blas_r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] BLAS create failed: %.*s\n",
                     static_cast<int>(blas_r.error().message.size()),
                     blas_r.error().message.data());
        return 8;
    }

    // ---- 6. TLAS (one identity instance referencing BLAS) ----------------
    cd::rhi::AccelInstance inst {};
    inst.blas = *blas_r;
    inst.instance_id = 0;
    inst.mask = 0xFF;
    std::array<cd::rhi::AccelInstance, 1> inst_arr { inst };

    cd::rhi::AccelStructureDesc tlas_desc {};
    tlas_desc.kind      = cd::rhi::AccelStructureKind::kTopLevel;
    tlas_desc.instances = std::span<const cd::rhi::AccelInstance>(inst_arr);
    tlas_desc.debug_name = "tlas";
    auto tlas_r = device.create_acceleration_structure(tlas_desc);
    if (!tlas_r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] TLAS create failed: %.*s\n",
                     static_cast<int>(tlas_r.error().message.size()),
                     tlas_r.error().message.data());
        return 9;
    }

    // ---- 7. Storage image ------------------------------------------------
    constexpr std::uint32_t kImgW = 256;
    constexpr std::uint32_t kImgH = 256;
    cd::rhi::TextureDesc tex_desc {};
    tex_desc.type = cd::rhi::TextureType::k2D;
    tex_desc.format = cd::rhi::Format::kRGBA8Unorm;
    tex_desc.extent = { kImgW, kImgH, 1 };
    tex_desc.mip_levels = 1;
    tex_desc.array_layers = 1;
    tex_desc.usage = cd::rhi::TextureUsage::kStorage | cd::rhi::TextureUsage::kTransferSrc;
    tex_desc.memory = cd::rhi::MemoryUsage::kGpuOnly;
    tex_desc.debug_name = "rt_out_img";
    auto tex_r = device.create_texture(tex_desc);
    if (!tex_r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] storage image create failed\n");
        return 10;
    }
    cd::rhi::TextureViewDesc tv_desc {};
    tv_desc.texture = *tex_r;
    tv_desc.format = cd::rhi::Format::kRGBA8Unorm;
    auto tv_r = device.create_texture_view(tv_desc);
    if (!tv_r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] storage image view create failed\n");
        return 11;
    }

    // ---- 8. SBT buffer (raygen / miss / hit, base-aligned) ---------------
    const std::uint32_t handle_size = device.rt_shader_group_handle_size();
    const std::uint32_t handle_align = device.rt_shader_group_handle_alignment();
    const std::uint32_t base_align  = device.rt_shader_group_base_alignment();
    std::printf("[hello_rt] RT properties:\n");
    std::printf("           handle size       = %u B\n", handle_size);
    std::printf("           handle alignment  = %u B\n", handle_align);
    std::printf("           base alignment    = %u B\n", base_align);

    const std::uint64_t handle_stride =
        align_up(static_cast<std::uint64_t>(handle_size), handle_align);
    const std::uint64_t region_size =
        align_up(handle_stride, base_align);  // each region is one record, base-aligned
    const std::uint64_t sbt_size = region_size * 3u;

    std::vector<std::byte> handles(static_cast<std::size_t>(handle_size) * 3u);
    if (auto r = device.get_rt_shader_group_handles(*rtp, 0, 3, std::span<std::byte>(handles));
        !r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] get_rt_shader_group_handles failed\n");
        return 12;
    }

    std::vector<std::byte> sbt_bytes(static_cast<std::size_t>(sbt_size), std::byte { 0 });
    // Each region starts at offset N * region_size, with the handle at offset 0.
    for (std::uint32_t g = 0; g < 3; ++g)
    {
        std::memcpy(
            sbt_bytes.data() + g * static_cast<std::size_t>(region_size),
            handles.data() + g * static_cast<std::size_t>(handle_size),
            handle_size
        );
    }

    cd::rhi::BufferDesc sbt_desc {};
    sbt_desc.size = sbt_size;
    sbt_desc.usage = cd::rhi::BufferUsage::kShaderBindingTable |
                     cd::rhi::BufferUsage::kTransferDst;
    sbt_desc.memory = cd::rhi::MemoryUsage::kAuto;
    auto sbt_r = device.create_buffer(sbt_desc);
    if (!sbt_r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] SBT buffer create failed\n");
        return 13;
    }
    (void)device.upload_buffer(*sbt_r, 0,
        std::span<const std::byte>(sbt_bytes));

    // ---- 9. Descriptor set + writes --------------------------------------
    auto ds = device.allocate_descriptor_set(*dsl);
    if (!ds.has_value())
    {
        std::fprintf(stderr, "[hello_rt] allocate_descriptor_set failed\n");
        return 14;
    }

    cd::rhi::DescriptorWrite w_tlas {};
    w_tlas.binding = 0;
    w_tlas.type = cd::rhi::DescriptorType::kAccelerationStructure;
    w_tlas.accel = *tlas_r;

    cd::rhi::DescriptorWrite w_img {};
    w_img.binding = 1;
    w_img.type = cd::rhi::DescriptorType::kStorageImage;
    w_img.view = *tv_r;

    std::array<cd::rhi::DescriptorWrite, 2> writes { w_tlas, w_img };
    if (auto r = device.update_descriptor_set(*ds,
            std::span<const cd::rhi::DescriptorWrite>(writes));
        !r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] update_descriptor_set failed: %.*s\n",
                     static_cast<int>(r.error().message.size()),
                     r.error().message.data());
        return 15;
    }
    std::printf("[hello_rt] descriptor set updated (TLAS @ 0, storage image @ 1).\n");

    // ---- 10. Command buffer record + submit ------------------------------
    auto cmd_ptr = device.create_command_buffer();
    if (cmd_ptr == nullptr)
    {
        std::fprintf(stderr, "[hello_rt] command buffer create failed\n");
        return 16;
    }
    auto& cmd = *cmd_ptr;
    cmd.begin();

    // First, build BLAS + TLAS on the GPU. Vulkan validation requires
    // the build to happen before any TLAS descriptor binding is used.
    cmd.build_acceleration_structure(*blas_r);
    cmd.build_acceleration_structure(*tlas_r);

    // Transition the storage image into GENERAL layout so the raygen
    // can imageStore through binding 1. ResourceState::kUnorderedAccess
    // is the engine's "storage" state.
    std::array<cd::rhi::TextureBarrier, 1> tb { {
        cd::rhi::TextureBarrier {
            .texture = *tex_r,
            .from = cd::rhi::ResourceState::kUndefined,
            .to   = cd::rhi::ResourceState::kUnorderedAccess,
            .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
        }
    } };
    cmd.barrier({}, tb);

    cmd.bind_rt_pipeline(*rtp);
    cmd.bind_descriptor_set(0, *ds);

    cd::rhi::DispatchRaysDesc drd {};
    drd.width  = kImgW;
    drd.height = kImgH;
    drd.depth  = 1;
    drd.raygen = { *sbt_r, 0 * region_size, region_size, region_size };
    drd.miss   = { *sbt_r, 1 * region_size, region_size, region_size };
    drd.hit    = { *sbt_r, 2 * region_size, region_size, region_size };
    cmd.dispatch_rays(drd);

    cmd.end();

    cd::rhi::SubmitDesc sd {};
    std::array<cd::rhi::ICommandBuffer*, 1> cbs { &cmd };
    sd.command_buffers = cbs;
    if (auto r = device.submit(sd); !r.has_value())
    {
        std::fprintf(stderr, "[hello_rt] submit failed: %.*s\n",
                     static_cast<int>(r.error().message.size()),
                     r.error().message.data());
        return 17;
    }
    device.wait_idle();
    std::printf("[hello_rt] dispatch_rays(%u x %u) executed + waited.\n", kImgW, kImgH);

    // ---- 11. Cleanup -----------------------------------------------------
    device.destroy_descriptor_set(*ds);
    device.destroy_texture_view(*tv_r);
    device.destroy_texture(*tex_r);
    device.destroy_acceleration_structure(*tlas_r);
    device.destroy_acceleration_structure(*blas_r);
    device.destroy_buffer(*sbt_r);
    device.destroy_buffer(*vb_r);
    device.destroy_rt_pipeline(*rtp);
    device.destroy_pipeline_layout(*pl);
    device.destroy_descriptor_set_layout(*dsl);
    device.destroy_shader_module(rg_mod);
    device.destroy_shader_module(ms_mod);
    device.destroy_shader_module(ch_mod);

    std::printf("[hello_rt] OK — TLAS-bound real ray dispatch verified on this adapter.\n");
    return 0;
}
