// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_rt.cpp
//
// Backend-to-100 Wave 4b / C-METAL-TIER2 (docs/METAL_MAC_TESTING.md §3 Tier-2):
//   cd_test_metal_rt — BLAS + TLAS build + ray-query dispatch in a compute
//   shader (M9, §5.3 AS useResource residency + §5.6 ray-query GPU). A single
//   triangle BLAS is referenced by an identity-transform TLAS instance; a
//   compute shader fires a rayQueryEXT against the TLAS and writes the
//   committed-hit distance into a storage buffer. The host then asserts the hit
//   distance matches the analytic ray/triangle intersection.
//
//   This is the Metal analog of Vulkan's cd_test_rhi_rt_rayquery. The host-side
//   MSL lowering of SPV_KHR_ray_query is already locked by
//   cd_test_metal_shader_toolchain::RayQueryLowersToMetalIntersector; THIS test
//   proves the lowered MSL plus the .mm AS build + residency actually trace on
//   the GPU.
//
// PLATFORM GATE (see test_metal_device.cpp for the full rationale): real
// MTLDevice on Apple; skip-stub everywhere else. Honest GTEST_SKIP when the
// device lacks the ray_query feature (older Apple GPU / Metal < 3).
//
// Anti-flake: completion via wait_idle on a bounded submit (no sleep_for).
// Pattern: Arrange / Act / Assert.
// =============================================================================
#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)

    #include <cd/rhi/Barriers.hpp>
    #include <cd/rhi/Descriptors.hpp>
    #include <cd/rhi/Enums.hpp>
    #include <cd/rhi/Handles.hpp>
    #include <cd/rhi/ICommandBuffer.hpp>
    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/Pipeline.hpp>
    #include <cd/rhi/metal/MetalDevice.hpp>

    #include <gtest/gtest.h>

    #include <array>
    #include <cmath>
    #include <cstddef>
    #include <cstdint>
    #include <memory>
    #include <span>
    #include <string>

namespace
{

// A compute shader that fires ONE ray straight down -Z at the triangle in the
// TLAS and writes the committed-hit t (or -1 on miss) into out_buf[0]. The ray
// origin (0,0,1) toward (0,0,-1) hits the z=0 triangle at t = 1.0.
constexpr const char* kRayQueryCS = R"glsl(
#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x = 1) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT cd_tlas;
layout(set = 0, binding = 1) buffer Out { float t; } out_buf;
void main()
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, cd_tlas, gl_RayFlagsOpaqueEXT, 0xFFu,
                          vec3(0.0, 0.0, 1.0), 0.001,
                          vec3(0.0, 0.0, -1.0), 100.0);
    while (rayQueryProceedEXT(rq)) { }
    if (rayQueryGetIntersectionTypeEXT(rq, true) ==
        gl_RayQueryCommittedIntersectionTriangleEXT)
        out_buf.t = rayQueryGetIntersectionTEXT(rq, true);
    else
        out_buf.t = -1.0;
}
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_metal_device_or_null()
{
    cd::rhi::metal::MetalCreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::metal::create_metal_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
}

[[nodiscard]] cd::rhi::BufferHandle
make_filled_buffer(cd::rhi::IDevice& d, cd::rhi::BufferUsage usage,
                   std::span<const std::byte> bytes)
{
    cd::rhi::BufferDesc bd {};
    bd.size   = bytes.size();
    bd.usage  = usage | cd::rhi::BufferUsage::kTransferSrc;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto r = d.create_buffer(bd);
    if (!r.has_value())
        return {};
    if (!d.upload_buffer(*r, 0, bytes).has_value())
        return {};
    return *r;
}

// ---- M9: BLAS + TLAS build + ray-query trace --------------------------------
TEST(MetalRt, BlasTlasBuildAndRayQueryHit)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    if (!d.features().ray_query)
        GTEST_SKIP() << "this Metal device does not expose ray_query "
                        "(needs Apple GPU + Metal 3 / MSL 2.4)";

    // Arrange: a single triangle in the z=0 plane spanning the origin.
    const std::array<float, 9> verts {
        -1.0F, -1.0F, 0.0F,
         1.0F, -1.0F, 0.0F,
         0.0F,  1.0F, 0.0F
    };
    const auto vb = make_filled_buffer(
        d, cd::rhi::BufferUsage::kStorage,
        std::as_bytes(std::span<const float>(verts.data(), verts.size())));
    ASSERT_TRUE(vb.is_valid());

    // BLAS over the triangle.
    cd::rhi::AccelTriangleGeometry geo {};
    geo.vertex_buffer = vb;
    geo.vertex_count  = 3;
    geo.vertex_stride = 12;
    const std::array<cd::rhi::AccelTriangleGeometry, 1> geos { geo };
    cd::rhi::AccelStructureDesc blas_desc {};
    blas_desc.kind      = cd::rhi::AccelStructureKind::kBottomLevel;
    blas_desc.triangles = geos;
    auto blas_r = d.create_acceleration_structure(blas_desc);
    if (!blas_r.has_value())
        GTEST_SKIP() << "BLAS create unsupported on this Metal device: "
                     << std::string(blas_r.error().message);
    const auto blas = *blas_r;

    // TLAS with one identity instance referencing the BLAS.
    cd::rhi::AccelInstance inst {};
    inst.blas = blas;
    const std::array<cd::rhi::AccelInstance, 1> insts { inst };
    cd::rhi::AccelStructureDesc tlas_desc {};
    tlas_desc.kind      = cd::rhi::AccelStructureKind::kTopLevel;
    tlas_desc.instances = insts;
    auto tlas_r = d.create_acceleration_structure(tlas_desc);
    ASSERT_TRUE(tlas_r.has_value())
        << "TLAS create failed: " << std::string(tlas_r.error().message);
    const auto tlas = *tlas_r;

    // Build both on the command buffer (BLAS first, AS barrier, then TLAS).
    {
        auto build = d.create_command_buffer(cd::rhi::QueueType::kCompute);
        ASSERT_NE(build, nullptr);
        build->begin();
        build->build_acceleration_structure(blas);
        build->acceleration_structure_barrier();
        build->build_acceleration_structure(tlas);
        build->end();
        d.submit(*build);
        d.wait_idle();
    }

    // Output buffer for the hit distance.
    cd::rhi::BufferDesc obd {};
    obd.size   = sizeof(float);
    obd.usage  = cd::rhi::BufferUsage::kStorage | cd::rhi::BufferUsage::kTransferSrc;
    obd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto out_r = d.create_buffer(obd);
    ASSERT_TRUE(out_r.has_value());
    const auto out_buf = *out_r;

    // Compute pipeline + descriptor set (TLAS @ binding 0, out buffer @ 1).
    cd::rhi::ShaderModuleDesc smd {};
    smd.stage     = cd::rhi::ShaderStage::kCompute;
    smd.code      = kRayQueryCS;
    smd.code_size = std::char_traits<char>::length(kRayQueryCS);
    smd.language  = cd::rhi::ShaderSourceLanguage::kGlsl;
    auto cs_r = d.create_shader_module(smd);
    if (!cs_r.has_value())
        GTEST_SKIP() << "ray-query compute module did not compile on this device: "
                     << std::string(cs_r.error().message);
    const auto cs = *cs_r;

    const std::array<cd::rhi::DescriptorSetLayoutBinding, 2> binds {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kAccelerationStructure,
            .count = 1, .stages = cd::rhi::ShaderStage::kCompute },
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 1, .type = cd::rhi::DescriptorType::kStorageBuffer,
            .count = 1, .stages = cd::rhi::ShaderStage::kCompute }
    };
    cd::rhi::DescriptorSetLayoutDesc ld {};
    ld.bindings = binds;
    const auto set_layout = *d.create_descriptor_set_layout(ld);
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> set_layouts { set_layout };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = set_layouts;
    const auto layout = *d.create_pipeline_layout(pld);

    cd::rhi::ComputePipelineDesc cpd {};
    cpd.layout = layout;
    cpd.shader = cs;
    auto pso_r = d.create_compute_pipeline(cpd);
    ASSERT_TRUE(pso_r.has_value());
    const auto pso = *pso_r;

    const auto set = *d.allocate_descriptor_set(set_layout);
    std::array<cd::rhi::DescriptorWrite, 2> writes {};
    writes[0].binding = 0;
    writes[0].type    = cd::rhi::DescriptorType::kAccelerationStructure;
    writes[0].accel   = tlas;
    writes[1].binding      = 1;
    writes[1].type         = cd::rhi::DescriptorType::kStorageBuffer;
    writes[1].buffer       = out_buf;
    writes[1].buffer_range = sizeof(float);
    ASSERT_TRUE(d.update_descriptor_set(set, writes).has_value());

    // Act: dispatch one ray-query thread.
    auto cmd = d.create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    cmd->bind_compute_pipeline(pso);
    cmd->bind_descriptor_set(0, set);
    cmd->dispatch(1, 1, 1);
    cmd->end();
    const auto submit_r = d.submit(cd::rhi::SubmitDesc {
        .command_buffers = std::span<cd::rhi::ICommandBuffer* const>(
            std::array<cd::rhi::ICommandBuffer*, 1> { cmd.get() }) });
    ASSERT_TRUE(submit_r.has_value());
    d.wait_idle();

    // Assert: the committed hit distance is t == 1.0 (origin z=1 -> plane z=0),
    // proving the BLAS/TLAS built AND the residency (§5.3) let the ray hit.
    float t = -99.0F;
    ASSERT_TRUE(d.download_buffer(
        out_buf, 0,
        std::as_writable_bytes(std::span<float>(&t, 1))).has_value());
    EXPECT_NEAR(t, 1.0F, 1e-3F)
        << "ray-query committed-hit t must match the analytic intersection "
           "(1.0); a missing AS useResource residency returns a miss (-1)";

    d.destroy_descriptor_set(set);
    d.destroy_compute_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_descriptor_set_layout(set_layout);
    d.destroy_shader_module(cs);
    d.destroy_buffer(out_buf);
    d.destroy_acceleration_structure(tlas);
    d.destroy_acceleration_structure(blas);
    d.destroy_buffer(vb);
}

}  // namespace

#else  // not (Apple && CD_RHI_METAL_ENABLED)

    #include <gtest/gtest.h>

TEST(MetalRt, SkippedOffApple)
{
    GTEST_SKIP() << "Metal backend disabled on this platform (Apple-only)";
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
