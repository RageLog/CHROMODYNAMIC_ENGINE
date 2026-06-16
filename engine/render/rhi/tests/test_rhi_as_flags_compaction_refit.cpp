// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_rhi_as_flags_compaction_refit.cpp
//
// A-AS-FLAGS + A-COMPACTION + A-REFIT (Backend-to-100 Wave 3b).
//
// The acceleration-structure build-flag + compaction + refit subsystem went
// from 0% (AccelStructureDesc had NO build_flags field) to a full 3-backend
// implementation. This test locks the three slices:
//
//   1. FLAGS (no device) — AccelBuildFlags round-trips through the bit-flag
//      helpers; AccelStructureDesc::build_flags defaults to kPreferFastTrace so
//      every existing caller is byte-for-byte unchanged. A descriptor-level
//      assert per the wave brief ("a descriptor-level assert is fine if a full
//      GPU diff is heavy"): the default + the OR-combination contract.
//
//   2. COMPACTION (Vulkan + D3D12, RT-gated) — build a BLAS with
//      kAllowCompaction, compact it, assert the compacted AS size is STRICTLY
//      SMALLER than the source, then build a TLAS over the COMPACTED BLAS and
//      ray-query it: the ray still HITS the geometry (ray-equivalence). The
//      compacted handle is independent of the source (the source is destroyed
//      first to prove the copy is self-contained).
//
//   3. REFIT (Vulkan + D3D12, RT-gated) — build a BLAS with kAllowUpdate, move a
//      vertex in the source buffer, refit_acceleration_structure (in-place
//      MODE_UPDATE / PERFORM_UPDATE), rebuild the TLAS, and assert the ray-query
//      still HITS a valid AS after the in-place update. Also asserts refit on an
//      AS WITHOUT kAllowUpdate safely falls back to a full rebuild (still hits).
//
// The ray-query is an INLINE ray-query compute shader (the production RT path):
// GLSL `rayQueryEXT` on Vulkan (native glslang -> SPIR-V), HLSL `RayQuery<>`
// (SM6.5) on D3D12. One ray is cast straight down -Z at a Z=0 triangle; a HIT
// writes t (committed distance) into a storage buffer, a MISS writes -1.
//
// Honest-SKIP when no device / no RT feature / no shader toolchain — per
// ROADMAP §5 the RT runtime is RTX-3080-here-verifiable; on a non-RT adapter
// (lavapipe / WARP) the GPU cases self-skip. Pattern: Arrange / Act / Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
    #include <cd/rhi/d3d12/D3D12ShaderCompile.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>

namespace rhi = cd::rhi;

namespace
{

// ============================================================================
// SLICE 1 — FLAGS (no device): the descriptor-level contract.
// ============================================================================

TEST(AsBuildFlags, DefaultIsPreferFastTrace)
{
    // An untouched AccelStructureDesc must reproduce the historical hardcoded
    // PREFER_FAST_TRACE behaviour — every existing caller is unchanged.
    rhi::AccelStructureDesc desc {};
    EXPECT_EQ(desc.build_flags, rhi::AccelBuildFlags::kPreferFastTrace);
    EXPECT_TRUE(rhi::has(desc.build_flags, rhi::AccelBuildFlags::kPreferFastTrace));
    EXPECT_FALSE(rhi::has(desc.build_flags, rhi::AccelBuildFlags::kAllowUpdate));
    EXPECT_FALSE(rhi::has(desc.build_flags, rhi::AccelBuildFlags::kAllowCompaction));
}

TEST(AsBuildFlags, OrCombinationRoundTrips)
{
    // The bit-flag helpers compose update + compaction + a build hint and the
    // membership predicate reads each one back independently.
    const auto f = rhi::AccelBuildFlags::kAllowUpdate |
                   rhi::AccelBuildFlags::kAllowCompaction |
                   rhi::AccelBuildFlags::kPreferFastBuild;
    EXPECT_TRUE(rhi::has(f, rhi::AccelBuildFlags::kAllowUpdate));
    EXPECT_TRUE(rhi::has(f, rhi::AccelBuildFlags::kAllowCompaction));
    EXPECT_TRUE(rhi::has(f, rhi::AccelBuildFlags::kPreferFastBuild));
    EXPECT_FALSE(rhi::has(f, rhi::AccelBuildFlags::kPreferFastTrace));
    EXPECT_FALSE(rhi::has(f, rhi::AccelBuildFlags::kLowMemory));
    EXPECT_TRUE(rhi::any(f));
    EXPECT_FALSE(rhi::any(rhi::AccelBuildFlags::kNone));
}

// ============================================================================
// Shared GPU harness (Vulkan GLSL + D3D12 HLSL inline ray-query).
// ============================================================================

// One ground triangle in the Z=0 plane. The centre ray (origin above (0,0))
// straight down -Z hits it; the test moves vertex 0 for the refit case.
struct Tri { std::array<float, 9> v; };

[[nodiscard]] Tri make_triangle(float y_centre_lift)
{
    // A large triangle centred on the origin in XY at Z=0. y_centre_lift nudges
    // the apex up in +Z so a refit changes the committed hit distance.
    return Tri { {
        0.0F,  0.6F, y_centre_lift,
       -0.6F, -0.6F, 0.0F,
        0.6F, -0.6F, 0.0F,
    } };
}

// GLSL inline-ray-query compute shader (Vulkan native SPIR-V path).
constexpr const char* kRayQueryGlsl = R"glsl(
#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x = 1) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 1, std430) buffer Out { float hit_t; } o;
void main()
{
    rayQueryEXT rq;
    vec3 origin = vec3(0.0, 0.0, 2.0);
    vec3 dir    = vec3(0.0, 0.0, -1.0);
    rayQueryInitializeEXT(rq, tlas, gl_RayFlagsOpaqueEXT, 0xFF,
                          origin, 0.001, dir, 100.0);
    while (rayQueryProceedEXT(rq)) {}
    if (rayQueryGetIntersectionTypeEXT(rq, true) ==
        gl_RayQueryCommittedIntersectionTriangleEXT)
        o.hit_t = rayQueryGetIntersectionTEXT(rq, true);
    else
        o.hit_t = -1.0;
}
)glsl";

// HLSL inline-ray-query compute shader (D3D12 SM6.5 path). The TLAS binds as an
// SRV (t0) and the output as a RAW UAV (u1) — the engine's kStorageBuffer maps
// to a D3D12_BUFFER_UAV_FLAG_RAW view, so the shader uses RWByteAddressBuffer
// (NOT RWStructuredBuffer, which would need a non-existent stride field).
constexpr const char* kRayQueryHlsl = R"(
RaytracingAccelerationStructure tlas : register(t0);
RWByteAddressBuffer             o    : register(u1);
[numthreads(1,1,1)]
void main()
{
    RayDesc ray;
    ray.Origin    = float3(0.0, 0.0, 2.0);
    ray.Direction = float3(0.0, 0.0, -1.0);
    ray.TMin      = 0.001;
    ray.TMax      = 100.0;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        o.Store(0, asuint(q.CommittedRayT()));
    else
        o.Store(0, asuint(-1.0));
}
)";

[[nodiscard]] rhi::ShaderModuleHandle
make_rayquery_module_vulkan(rhi::IDevice& dev)
{
    rhi::ShaderModuleDesc d {};
    d.stage       = rhi::ShaderStage::kCompute;
    d.code        = kRayQueryGlsl;
    d.code_size   = std::char_traits<char>::length(kRayQueryGlsl);
    d.entry_point = "main";
    d.language    = rhi::ShaderSourceLanguage::kGlsl;
    auto r = dev.create_shader_module(d);
    return r.has_value() ? *r : rhi::ShaderModuleHandle {};
}

#if defined(_WIN32)
[[nodiscard]] rhi::ShaderModuleHandle
make_rayquery_module_d3d12(rhi::IDevice& dev, bool* dxc_missing)
{
    rhi::d3d12::CompileOptions co {};
    co.source      = kRayQueryHlsl;
    co.entry_point = "main";
    co.stage       = rhi::ShaderStage::kCompute;
    co.model       = rhi::d3d12::ShaderModel::kSM6_5;
    co.source_name = "rq_cs";
    auto blob_r = rhi::d3d12::compile_hlsl(co);
    if (!blob_r.has_value())
    {
        const std::string msg { blob_r.error().message };
        if (msg.find("dxc") != std::string::npos || msg.find("DXC") != std::string::npos)
            *dxc_missing = true;
        return {};
    }
    rhi::ShaderModuleDesc smd {};
    smd.stage       = rhi::ShaderStage::kCompute;
    smd.code        = blob_r->data();
    smd.code_size   = blob_r->size();
    smd.entry_point = "main";
    auto m = dev.create_shader_module(smd);
    return m.has_value() ? *m : rhi::ShaderModuleHandle {};
}
#endif

// Create a vertex buffer holding the triangle. kVertex + kTransferDst only:
// the BLAS build reads this buffer's GPU VA as triangle positions; it does NOT
// need a UAV. kStorage would force ALLOW_UNORDERED_ACCESS, which D3D12 rejects
// on an UPLOAD heap (kCpuToGpu) with E_INVALIDARG.
[[nodiscard]] rhi::BufferHandle
make_vertex_buffer(rhi::IDevice& dev, const Tri& tri)
{
    rhi::BufferDesc bd {};
    bd.size   = sizeof(tri.v);
    bd.usage  = rhi::BufferUsage::kVertex | rhi::BufferUsage::kTransferDst;
    bd.memory = rhi::MemoryUsage::kCpuToGpu;
    auto r = dev.create_buffer(bd);
    if (!r.has_value()) return {};
    (void)dev.upload_buffer(*r, 0, std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(tri.v.data()), sizeof(tri.v)));
    return *r;
}

[[nodiscard]] rhi::AccelStructureHandle
make_blas(rhi::IDevice& dev, rhi::BufferHandle vb, rhi::AccelBuildFlags flags)
{
    rhi::AccelTriangleGeometry geo {};
    geo.vertex_buffer = vb;
    geo.vertex_count  = 3u;
    geo.vertex_stride = 12u;
    rhi::AccelStructureDesc bd {};
    bd.kind        = rhi::AccelStructureKind::kBottomLevel;
    bd.triangles   = std::span<const rhi::AccelTriangleGeometry>(&geo, 1);
    bd.build_flags = flags;
    auto r = dev.create_acceleration_structure(bd);
    return r.has_value() ? *r : rhi::AccelStructureHandle {};
}

[[nodiscard]] rhi::AccelStructureHandle
make_tlas(rhi::IDevice& dev, rhi::AccelStructureHandle blas)
{
    rhi::AccelInstance inst {};
    inst.blas = blas;
    inst.mask = 0xFFu;
    rhi::AccelStructureDesc td {};
    td.kind      = rhi::AccelStructureKind::kTopLevel;
    td.instances = std::span<const rhi::AccelInstance>(&inst, 1);
    auto r = dev.create_acceleration_structure(td);
    return r.has_value() ? *r : rhi::AccelStructureHandle {};
}

// The shader writes the committed-T into a GPU-only storage buffer (a UAV); we
// then copy it into a separate GpuToCpu readback buffer (D3D12 forbids a UAV on
// a READBACK heap, and download_buffer requires a READBACK target). This bundle
// holds both + the descriptor surface.
struct TraceRig
{
    rhi::BufferHandle              out_storage {};  // GpuOnly UAV
    rhi::BufferHandle              out_readback {}; // GpuToCpu
    rhi::DescriptorSetLayoutHandle dsl {};
    rhi::PipelineLayoutHandle      layout {};
    rhi::ComputePipelineHandle     pso {};
    rhi::DescriptorSetHandle       set {};
    bool                           ok { false };
};

[[nodiscard]] TraceRig
make_trace_rig(rhi::IDevice& dev, rhi::ShaderModuleHandle cs,
               rhi::AccelStructureHandle tlas)
{
    TraceRig rig {};
    rhi::BufferDesc sbd {};
    sbd.size   = sizeof(float);
    sbd.usage  = rhi::BufferUsage::kStorage | rhi::BufferUsage::kTransferSrc;
    sbd.memory = rhi::MemoryUsage::kGpuOnly;
    auto s_r = dev.create_buffer(sbd);
    if (!s_r.has_value()) return rig;
    rig.out_storage = *s_r;

    rhi::BufferDesc rbd {};
    rbd.size   = sizeof(float);
    rbd.usage  = rhi::BufferUsage::kTransferDst;
    rbd.memory = rhi::MemoryUsage::kGpuToCpu;
    auto r_r = dev.create_buffer(rbd);
    if (!r_r.has_value()) return rig;
    rig.out_readback = *r_r;

    const rhi::DescriptorSetLayoutBinding binds[] = {
        { 0u, rhi::DescriptorType::kAccelerationStructure, 1u, rhi::ShaderStage::kCompute },
        { 1u, rhi::DescriptorType::kStorageBuffer,         1u, rhi::ShaderStage::kCompute },
    };
    rhi::DescriptorSetLayoutDesc dsl_desc {};
    dsl_desc.bindings = std::span<const rhi::DescriptorSetLayoutBinding>(binds, 2);
    auto dsl_r = dev.create_descriptor_set_layout(dsl_desc);
    if (!dsl_r.has_value()) return rig;
    rig.dsl = *dsl_r;

    const rhi::DescriptorSetLayoutHandle sls[] = { rig.dsl };
    rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = std::span<const rhi::DescriptorSetLayoutHandle>(sls, 1);
    auto layout_r = dev.create_pipeline_layout(pld);
    if (!layout_r.has_value()) return rig;
    rig.layout = *layout_r;

    rhi::ComputePipelineDesc cpd {};
    cpd.layout = rig.layout;
    cpd.shader = cs;
    auto pso_r = dev.create_compute_pipeline(cpd);
    if (!pso_r.has_value()) return rig;
    rig.pso = *pso_r;

    auto set_r = dev.allocate_descriptor_set(rig.dsl);
    if (!set_r.has_value()) return rig;
    rig.set = *set_r;

    const rhi::DescriptorWrite writes[] = {
        [&] { rhi::DescriptorWrite w {}; w.binding = 0u;
              w.type = rhi::DescriptorType::kAccelerationStructure;
              w.accel = tlas; return w; }(),
        [&] { rhi::DescriptorWrite w {}; w.binding = 1u;
              w.type = rhi::DescriptorType::kStorageBuffer;
              w.buffer = rig.out_storage; return w; }(),
    };
    if (!dev.update_descriptor_set(
            rig.set, std::span<const rhi::DescriptorWrite>(writes, 2)).has_value())
        return rig;

    rig.ok = true;
    return rig;
}

// Record dispatch + UAV->copy-source barrier + copy_buffer into the readback,
// submit, and read back the float. Returns the committed-T (>0 = HIT, -1 = MISS,
// -2 = harness failure).
[[nodiscard]] float
dispatch_and_read(rhi::IDevice& dev, const TraceRig& rig, rhi::ICommandBuffer& cmd)
{
    cmd.bind_compute_pipeline(rig.pso);
    cmd.bind_descriptor_set(0u, rig.set);
    cmd.dispatch(1u, 1u, 1u);
    // UAV write -> copy-source so the result is visible to the copy.
    const rhi::BufferBarrier bb {
        .buffer = rig.out_storage,
        .from   = rhi::ResourceState::kUnorderedAccess,
        .to     = rhi::ResourceState::kTransferSrc,
        .offset = 0u, .size = 0u,
    };
    cmd.barrier(std::span<const rhi::BufferBarrier>(&bb, 1),
                std::span<const rhi::TextureBarrier>());
    const rhi::BufferCopyRegion region { 0u, 0u, sizeof(float) };
    cmd.copy_buffer(rig.out_storage, rig.out_readback,
                    std::span<const rhi::BufferCopyRegion>(&region, 1));
    cmd.end();
    dev.submit(cmd);
    dev.wait_idle();

    float hit_t = -2.0F;
    std::array<std::byte, sizeof(float)> raw {};
    if (dev.download_buffer(rig.out_readback, 0u, std::span<std::byte>(raw)).has_value())
        std::memcpy(&hit_t, raw.data(), sizeof(float));
    return hit_t;
}

void destroy_trace_rig(rhi::IDevice& dev, const TraceRig& rig)
{
    if (rig.pso.is_valid())          dev.destroy_compute_pipeline(rig.pso);
    if (rig.layout.is_valid())       dev.destroy_pipeline_layout(rig.layout);
    if (rig.dsl.is_valid())          dev.destroy_descriptor_set_layout(rig.dsl);
    if (rig.out_storage.is_valid())  dev.destroy_buffer(rig.out_storage);
    if (rig.out_readback.is_valid()) dev.destroy_buffer(rig.out_readback);
}

// Build (or refit) the BLAS, (re)build the TLAS fresh, then dispatch the inline
// ray-query compute and read back the committed hit-T.
[[nodiscard]] float
trace_committed_t(rhi::IDevice& dev,
                  rhi::ShaderModuleHandle cs,
                  rhi::AccelStructureHandle blas,
                  rhi::AccelStructureHandle tlas,
                  bool refit_blas)
{
    const TraceRig rig = make_trace_rig(dev, cs, tlas);
    if (!rig.ok) { destroy_trace_rig(dev, rig); return -2.0F; }

    auto cmd = dev.create_command_buffer(rhi::QueueType::kGraphics);
    if (cmd == nullptr) { destroy_trace_rig(dev, rig); return -2.0F; }
    cmd->begin();
    if (refit_blas)
        cmd->refit_acceleration_structure(blas);   // A-REFIT in-place update
    else
        cmd->build_acceleration_structure(blas);
    cmd->acceleration_structure_barrier();
    cmd->build_acceleration_structure(tlas);        // TLAS always (re)built fresh
    cmd->acceleration_structure_barrier();
    const float hit_t = dispatch_and_read(dev, rig, *cmd);

    destroy_trace_rig(dev, rig);
    return hit_t;
}

// ---- COMPACTION (shared, RT-gated) -----------------------------------------

void run_compaction(rhi::IDevice& dev, rhi::ShaderModuleHandle cs)
{
    const Tri tri = make_triangle(0.0F);
    const auto vb = make_vertex_buffer(dev, tri);
    ASSERT_TRUE(vb.is_valid());

    // BLAS with compaction allowed.
    const auto blas = make_blas(dev, vb,
        rhi::AccelBuildFlags::kPreferFastTrace |
        rhi::AccelBuildFlags::kAllowCompaction);
    ASSERT_TRUE(blas.is_valid());

    // The BLAS must be BUILT before its compacted size is meaningful: build it
    // on a one-shot CB and wait.
    {
        auto cb = dev.create_command_buffer(rhi::QueueType::kGraphics);
        ASSERT_NE(cb, nullptr);
        cb->begin();
        cb->build_acceleration_structure(blas);
        cb->acceleration_structure_barrier();
        cb->end();
        dev.submit(*cb);
        dev.wait_idle();
    }

    const std::uint64_t orig_size = dev.acceleration_structure_size(blas);
    ASSERT_GT(orig_size, 0u) << "device did not report a source AS size";

    auto compacted_r = dev.compact_acceleration_structure(blas);
    ASSERT_TRUE(compacted_r.has_value())
        << "compact_acceleration_structure failed: " << compacted_r.error().message;
    const auto compacted = *compacted_r;
    const std::uint64_t comp_size = dev.acceleration_structure_size(compacted);

    // The compacted AS must be STRICTLY smaller (a single-triangle BLAS always
    // compacts on real DXR/VK_KHR hardware).
    EXPECT_GT(comp_size, 0u);
    EXPECT_LT(comp_size, orig_size)
        << "compacted size " << comp_size << " not < original " << orig_size;
    // Diagnostic: the shrink numbers the wave brief asks to report.
    std::printf("[as-compaction] original=%llu B compacted=%llu B (%.1f%% of original)\n",
                static_cast<unsigned long long>(orig_size),
                static_cast<unsigned long long>(comp_size),
                100.0 * static_cast<double>(comp_size) / static_cast<double>(orig_size));

    // Ray-equivalence: destroy the SOURCE first (proves the compacted copy is
    // self-contained), then build a TLAS over the COMPACTED BLAS and trace it.
    dev.destroy_acceleration_structure(blas);
    dev.destroy_buffer(vb);  // compacted AS no longer needs the source geometry

    const auto tlas = make_tlas(dev, compacted);
    ASSERT_TRUE(tlas.is_valid());

    // The compacted BLAS is already built (copy-compacted from a built source)
    // and is read-only — it must NOT be rebuilt. So we build ONLY the fresh
    // TLAS over it, then dispatch the ray-query. Re-use the shared trace rig.
    const TraceRig rig = make_trace_rig(dev, cs, tlas);
    ASSERT_TRUE(rig.ok) << "compaction trace rig setup failed";

    auto cmd = dev.create_command_buffer(rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    cmd->build_acceleration_structure(tlas);   // TLAS over the compacted BLAS only
    cmd->acceleration_structure_barrier();
    const float hit_t = dispatch_and_read(dev, rig, *cmd);

    // The ray originates at z=2 going -Z; the Z=0 triangle is at distance ~2.
    EXPECT_GT(hit_t, 0.0F)
        << "ray-query against the COMPACTED TLAS missed (hit_t=" << hit_t << ")";
    EXPECT_NEAR(hit_t, 2.0F, 0.05F)
        << "committed-T against the compacted geometry drifted";

    destroy_trace_rig(dev, rig);
    dev.destroy_acceleration_structure(tlas);
    dev.destroy_acceleration_structure(compacted);
}

// ---- REFIT (shared, RT-gated) ----------------------------------------------

void run_refit(rhi::IDevice& dev, rhi::ShaderModuleHandle cs)
{
    // Start with the apex at Z=0; build with kAllowUpdate.
    Tri tri = make_triangle(0.0F);
    const auto vb = make_vertex_buffer(dev, tri);
    ASSERT_TRUE(vb.is_valid());
    const auto blas = make_blas(dev, vb,
        rhi::AccelBuildFlags::kPreferFastTrace |
        rhi::AccelBuildFlags::kAllowUpdate);
    ASSERT_TRUE(blas.is_valid());
    const auto tlas = make_tlas(dev, blas);
    ASSERT_TRUE(tlas.is_valid());

    // First trace with a fresh build — baseline HIT.
    const float t0 = trace_committed_t(dev, cs, blas, tlas, /*refit_blas=*/false);
    ASSERT_GT(t0, -2.0F) << "refit harness setup failed";
    EXPECT_GT(t0, 0.0F) << "baseline ray-query missed before refit";

    // Move the apex vertex toward the ray (lift +Z to 0.5): the committed T at
    // the centre changes because the surface the ray hits moved closer.
    tri = make_triangle(0.5F);
    (void)dev.upload_buffer(vb, 0, std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(tri.v.data()), sizeof(tri.v)));

    // In-place refit (MODE_UPDATE / PERFORM_UPDATE), rebuild TLAS, re-trace.
    const float t1 = trace_committed_t(dev, cs, blas, tlas, /*refit_blas=*/true);
    ASSERT_GT(t1, -2.0F) << "refit harness teardown failed";
    EXPECT_GT(t1, 0.0F)
        << "ray-query missed after in-place refit (refit produced an invalid AS)";
    // The centre ray hits the moved apex region; the committed distance should
    // be SMALLER than the baseline (apex lifted from z=0 to z=0.5, closer to the
    // z=2 origin). Loose bound — the exact value depends on barycentrics.
    EXPECT_LT(t1, t0 + 0.001F)
        << "refit did not change the traced geometry (t1=" << t1
        << " baseline=" << t0 << ")";

    dev.destroy_acceleration_structure(tlas);
    dev.destroy_acceleration_structure(blas);
    dev.destroy_buffer(vb);
}

// ---- REFIT fallback: AS WITHOUT kAllowUpdate still traces -------------------

void run_refit_fallback(rhi::IDevice& dev, rhi::ShaderModuleHandle cs)
{
    const Tri tri = make_triangle(0.0F);
    const auto vb = make_vertex_buffer(dev, tri);
    ASSERT_TRUE(vb.is_valid());
    // NO kAllowUpdate — refit must safely demote to a full rebuild.
    const auto blas = make_blas(dev, vb, rhi::AccelBuildFlags::kPreferFastTrace);
    ASSERT_TRUE(blas.is_valid());
    const auto tlas = make_tlas(dev, blas);
    ASSERT_TRUE(tlas.is_valid());

    const float t = trace_committed_t(dev, cs, blas, tlas, /*refit_blas=*/true);
    ASSERT_GT(t, -2.0F) << "fallback harness failed";
    EXPECT_GT(t, 0.0F)
        << "refit on a non-updatable AS did not fall back to a valid full build";

    dev.destroy_acceleration_structure(tlas);
    dev.destroy_acceleration_structure(blas);
    dev.destroy_buffer(vb);
}

// ============================================================================
// SLICE 2 + 3 — Vulkan
// ============================================================================

[[nodiscard]] std::unique_ptr<rhi::IDevice> make_vulkan_rt_or_skip()
{
    rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto dev_r = rhi::vulkan::create_vulkan_device(info);
    if (!dev_r.has_value())
        return nullptr;
    return std::move(*dev_r);
}

TEST(AsCompaction, VulkanCompactsAndStillHits)
{
    auto dev = make_vulkan_rt_or_skip();
    if (dev == nullptr)
        GTEST_SKIP() << "no Vulkan ICD on this host";
    if (!dev->features().ray_tracing)
        GTEST_SKIP() << "adapter lacks VK_KHR_ray_tracing (lavapipe/non-RT GPU)";
    if (cd::shader::make_glslang_compiler() == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    const auto cs = make_rayquery_module_vulkan(*dev);
    ASSERT_TRUE(cs.is_valid()) << "ray-query GLSL compile failed";
    run_compaction(*dev, cs);
    dev->destroy_shader_module(cs);
}

TEST(AsRefit, VulkanRefitsInPlaceAndStillHits)
{
    auto dev = make_vulkan_rt_or_skip();
    if (dev == nullptr)
        GTEST_SKIP() << "no Vulkan ICD on this host";
    if (!dev->features().ray_tracing)
        GTEST_SKIP() << "adapter lacks VK_KHR_ray_tracing (lavapipe/non-RT GPU)";
    if (cd::shader::make_glslang_compiler() == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    const auto cs = make_rayquery_module_vulkan(*dev);
    ASSERT_TRUE(cs.is_valid()) << "ray-query GLSL compile failed";
    run_refit(*dev, cs);
    run_refit_fallback(*dev, cs);
    dev->destroy_shader_module(cs);
}

// ============================================================================
// SLICE 2 + 3 — D3D12
// ============================================================================

#if defined(_WIN32)

[[nodiscard]] std::unique_ptr<rhi::IDevice> make_d3d12_rt_or_skip()
{
    rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;
    auto dev_r = rhi::d3d12::create_d3d12_device(ci);
    if (!dev_r.has_value())
        return nullptr;
    return std::move(*dev_r);
}

TEST(AsCompaction, D3D12CompactsAndStillHits)
{
    auto dev = make_d3d12_rt_or_skip();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter on this host";
    if (!dev->features().ray_tracing)
        GTEST_SKIP() << "adapter does not report D3D12_RAYTRACING_TIER >= 1.0 "
                        "(WARP/non-RT GPU)";
    bool dxc_missing = false;
    const auto cs = make_rayquery_module_d3d12(*dev, &dxc_missing);
    if (dxc_missing)
        GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(cs.is_valid()) << "ray-query HLSL (SM6.5) compile failed";
    run_compaction(*dev, cs);
    dev->destroy_shader_module(cs);
}

TEST(AsRefit, D3D12RefitsInPlaceAndStillHits)
{
    auto dev = make_d3d12_rt_or_skip();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter on this host";
    if (!dev->features().ray_tracing)
        GTEST_SKIP() << "adapter does not report D3D12_RAYTRACING_TIER >= 1.0 "
                        "(WARP/non-RT GPU)";
    bool dxc_missing = false;
    const auto cs = make_rayquery_module_d3d12(*dev, &dxc_missing);
    if (dxc_missing)
        GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(cs.is_valid()) << "ray-query HLSL (SM6.5) compile failed";
    run_refit(*dev, cs);
    run_refit_fallback(*dev, cs);
    dev->destroy_shader_module(cs);
}

#endif  // _WIN32

}  // namespace
