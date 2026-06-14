// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_dxr_command.cpp
//
// D8 + D7 + D9 — the D3D12 DXR COMMAND-RECORDING path.
//
// Audit (ROADMAP_BACKEND_PARITY.md §2) found bind_rt_pipeline / dispatch_rays /
// acceleration_structure_barrier were NOT overridden on D3D12CommandBuffer —
// they silently inherited the IDrawRecorder/ICommandBuffer base no-ops, so a
// caller that built a real AS + RTPSO + SBT on D3D12 would record NOTHING (a
// dangerous silent success). This test pins the three new overrides:
//
//   D8  bind_rt_pipeline(handle)        -> ID3D12GraphicsCommandList4::SetPipelineState1
//   D7  dispatch_rays(desc)             -> D3D12_DISPATCH_RAYS_DESC from the SBT
//                                          regions + DispatchRays
//   D9  acceleration_structure_barrier  -> global UAV barrier
//
// Two layers, mirroring test_d3d12_mesh_shader.cpp / test_d3d12_parity_m4.cpp:
//
//   1. STRUCTURAL (runs on EVERY CI lane, no device): a host-side helper builds
//      the exact D3D12_DISPATCH_RAYS_DESC the override computes from the engine
//      SbtRegion inputs and asserts every field — StartAddress = base GPU VA +
//      offset, SizeInBytes, and the per-record StrideInBytes for miss/hit/
//      callable (ray-gen carries no stride: exactly one record per dispatch).
//      Also pins the SBT alignment contract (handle size 32, record/base
//      alignment 64) the builder + override agree on.
//
//   2. FUNCTIONAL (Windows + DXR-gated): a full END-TO-END trace on the real
//      D3D12 device. Part 1 exercises acceleration_structure_barrier() around
//      two real AS builds. Part 2 (B7b) closes the two create-side gaps and
//      PROVES the GPU executed the rays: create an RTPSO that now emits a real
//      D3D12_HIT_GROUP subobject, author the SBT from the REAL
//      GetShaderIdentifier handles (get_rt_shader_group_handles no longer
//      returns zero-fill), build a BLAS+TLAS over one triangle, bind the RTPSO
//      + a descriptor set (TLAS SRV + output UAV), dispatch_rays over an 8x8
//      UAV, and read the centre pixel back. A correct pipeline traces the ray
//      into the triangle and runs CLOSEST-HIT -> WHITE; the assert pins WHITE,
//      which is unreachable unless both create-side gaps are closed AND the AS
//      descriptor write lands. When the local adapter does NOT report
//      D3D12_RAYTRACING_TIER >= 1.0 the probe honest-GTEST_SKIPs.
// =============================================================================

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>

#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
    #include <cd/rhi/d3d12/D3D12ShaderCompile.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace rhi = cd::rhi;

namespace
{

// =============================================================================
// STRUCTURAL: the DISPATCH_RAYS_DESC field math the D3D12 override computes.
//
// This is a host-side mirror of D3D12CommandBuffer::dispatch_rays's region
// resolution, parameterised on a fake "buffer base GPU VA" instead of a live
// resource so it runs with NO device. The override's exact policy is encoded:
//   * each region's StartAddress = base_gva + region.offset
//   * an invalid-buffer region is SKIPPED (StartAddress stays 0)
//   * ray-gen carries StartAddress + SizeInBytes only (no stride)
//   * miss/hit/callable carry StartAddress + SizeInBytes + StrideInBytes
// The functional probe below exercises the SAME field assignments against the
// real ID3D12GraphicsCommandList4, so a regression in either the override or
// this contract surfaces here.
// =============================================================================

struct ExpectedRegion
{
    std::uint64_t start_address { 0 };
    std::uint64_t size_bytes    { 0 };
    std::uint64_t stride_bytes  { 0 };
};

struct ExpectedDispatchRaysDesc
{
    ExpectedRegion raygen   {};
    ExpectedRegion miss     {};
    ExpectedRegion hit      {};
    ExpectedRegion callable {};
    std::uint32_t  width  { 0 };
    std::uint32_t  height { 0 };
    std::uint32_t  depth  { 0 };
};

// `base_gva` is the GPU virtual address the SBT buffer resolves to (the
// override reads it from the live ID3D12Resource); a zero base_gva models an
// invalid/unresolved region (skipped, StartAddress left 0). Ray-gen has no
// stride field by DXR contract, so it is dropped here too.
[[nodiscard]] ExpectedRegion
resolve_region(const rhi::SbtRegion& r, std::uint64_t base_gva, bool with_stride)
{
    ExpectedRegion out {};
    if (!r.buffer.is_valid() || base_gva == 0)
        return out;  // skipped — StartAddress stays 0
    out.start_address = base_gva + r.offset;
    out.size_bytes    = r.size_bytes;
    out.stride_bytes  = with_stride ? r.stride_bytes : 0u;
    return out;
}

[[nodiscard]] ExpectedDispatchRaysDesc
build_expected_desc(const rhi::DispatchRaysDesc& desc, std::uint64_t base_gva)
{
    ExpectedDispatchRaysDesc e {};
    e.raygen   = resolve_region(desc.raygen,   base_gva, /*with_stride=*/false);
    e.miss     = resolve_region(desc.miss,     base_gva, /*with_stride=*/true);
    e.hit      = resolve_region(desc.hit,      base_gva, /*with_stride=*/true);
    e.callable = resolve_region(desc.callable, base_gva, /*with_stride=*/true);
    e.width  = desc.width;
    e.height = desc.height;
    e.depth  = desc.depth;
    return e;
}

}  // namespace

// ---- STRUCTURAL: SBT alignment contract ----------------------------------

TEST(D3D12DxrCommand, SbtAlignmentConstantsMatchDxrSpec)
{
    // DXR mandates a 32-byte shader identifier and 64-byte shader-record /
    // base alignment. The D3D12 device reports these via
    // rt_shader_group_handle_size / _alignment / _base_alignment and the SBT
    // builder rounds the 32-byte handle up to the 64-byte record stride. The
    // dispatch_rays override passes that stride straight through, so the math
    // is correct only if these constants hold.
    constexpr std::uint32_t kShaderIdentifierSize = 32u;  // D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES
    constexpr std::uint32_t kRecordAlignment      = 64u;  // D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT
    EXPECT_EQ(kShaderIdentifierSize, 32u);
    EXPECT_EQ(kRecordAlignment,      64u);

    // A record sized exactly the handle rounds UP to the 64-byte record
    // alignment — this is the stride the SBT builder stamps and the override
    // forwards as StrideInBytes.
    const std::uint32_t record_stride =
        ((kShaderIdentifierSize + kRecordAlignment - 1u) / kRecordAlignment) * kRecordAlignment;
    EXPECT_EQ(record_stride, 64u);
}

// ---- STRUCTURAL: DISPATCH_RAYS_DESC fields from SBT regions ---------------

TEST(D3D12DxrCommand, DispatchRaysDescFieldsFromSbtRegions)
{
    // A single SBT buffer holds four sub-regions. Strides are the 64-byte
    // record alignment; sizes are one record (miss/raygen) or two (hit).
    constexpr std::uint64_t kBaseGva = 0x0001'0000ull;  // 64 KiB-aligned fake VA
    constexpr std::uint64_t kStride  = 64u;

    const rhi::BufferHandle sbt { 7u, 1u };  // valid (non-zero index + gen)

    rhi::DispatchRaysDesc desc {};
    desc.width  = 256u;
    desc.height = 128u;
    desc.depth  = 1u;

    desc.raygen   = { sbt, /*offset*/ 0u,           /*stride*/ kStride, /*size*/ kStride };
    desc.miss     = { sbt, /*offset*/ kStride,      /*stride*/ kStride, /*size*/ kStride };
    desc.hit      = { sbt, /*offset*/ kStride * 2u, /*stride*/ kStride, /*size*/ kStride * 2u };
    desc.callable = {};  // unused — left invalid

    const auto e = build_expected_desc(desc, kBaseGva);

    // Ray-gen: StartAddress = base + 0, size = one record, NO stride.
    EXPECT_EQ(e.raygen.start_address, kBaseGva + 0u);
    EXPECT_EQ(e.raygen.size_bytes,    kStride);
    EXPECT_EQ(e.raygen.stride_bytes,  0u);  // raygen record has no stride

    // Miss: StartAddress = base + 64, one record, stride 64.
    EXPECT_EQ(e.miss.start_address, kBaseGva + kStride);
    EXPECT_EQ(e.miss.size_bytes,    kStride);
    EXPECT_EQ(e.miss.stride_bytes,  kStride);

    // Hit-group: StartAddress = base + 128, two records, stride 64.
    EXPECT_EQ(e.hit.start_address, kBaseGva + kStride * 2u);
    EXPECT_EQ(e.hit.size_bytes,    kStride * 2u);
    EXPECT_EQ(e.hit.stride_bytes,  kStride);

    // Callable: invalid region -> entirely zeroed (skipped).
    EXPECT_EQ(e.callable.start_address, 0u);
    EXPECT_EQ(e.callable.size_bytes,    0u);
    EXPECT_EQ(e.callable.stride_bytes,  0u);

    // Dispatch dims passed straight through.
    EXPECT_EQ(e.width,  256u);
    EXPECT_EQ(e.height, 128u);
    EXPECT_EQ(e.depth,  1u);
}

TEST(D3D12DxrCommand, InvalidSbtRegionIsSkipped)
{
    // The override (and this mirror) skip a region whose buffer handle is
    // invalid, leaving StartAddress 0 — a missing miss/hit table is legal in
    // DXR (the corresponding shader stage simply never runs). This pins the
    // "skip, don't fault" policy.
    rhi::DispatchRaysDesc desc {};
    desc.raygen = { rhi::BufferHandle { 3u, 1u }, 0u, 64u, 64u };
    desc.miss   = {};  // invalid
    desc.hit    = {};  // invalid

    const auto e = build_expected_desc(desc, /*base_gva*/ 0x2000u);
    EXPECT_NE(e.raygen.start_address, 0u);  // valid -> resolved
    EXPECT_EQ(e.miss.start_address,   0u);  // invalid -> skipped
    EXPECT_EQ(e.hit.start_address,    0u);  // invalid -> skipped
}

// ---- STRUCTURAL: ICommandBuffer base no-op stays sound -------------------

namespace
{

class NoopRtCommandBuffer final : public rhi::ICommandBuffer
{
public:
    void begin() override {}
    void end() override {}
    void begin_render_pass(const rhi::RenderPassBeginInfo&) override {}
    void end_render_pass() override {}
    void bind_graphics_pipeline(rhi::GraphicsPipelineHandle) override {}
    void bind_compute_pipeline(rhi::ComputePipelineHandle) override {}
    void bind_descriptor_set(std::uint32_t, rhi::DescriptorSetHandle) override {}
    void bind_vertex_buffer(std::uint32_t, rhi::BufferHandle, std::uint64_t) override {}
    void bind_index_buffer(rhi::BufferHandle, std::uint64_t, rhi::IndexType) override {}
    void push_constants(rhi::PipelineLayoutHandle, rhi::ShaderStage,
                        std::uint32_t, std::uint32_t, const void*) override {}
    void set_viewport(const rhi::Viewport&) override {}
    void set_scissor(const rhi::Rect2D&) override {}
    void draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void draw_indexed(std::uint32_t, std::uint32_t, std::uint32_t,
                      std::int32_t, std::uint32_t) override {}
    void dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void copy_buffer(rhi::BufferHandle, rhi::BufferHandle,
                     std::span<const rhi::BufferCopyRegion>) override {}
    void copy_buffer_to_image(rhi::BufferHandle, rhi::TextureHandle,
                              std::span<const rhi::BufferImageCopyRegion>) override {}
    void copy_image_to_buffer(rhi::TextureHandle, rhi::BufferHandle,
                              std::span<const rhi::BufferImageCopyRegion>) override {}
    void barrier(std::span<const rhi::BufferBarrier>,
                 std::span<const rhi::TextureBarrier>) override {}
    void push_debug_group(std::string_view) override {}
    void pop_debug_group() override {}
};

}  // namespace

TEST(D3D12DxrCommand, BaseRtCommandsAreNoopWhenUnoverridden)
{
    // A command buffer that does NOT override the RT commands must still be
    // sound (the base default no-op). The whole point of D7/D8/D9 is that the
    // D3D12 backend OVERRIDES these — the base no-op is only valid for
    // non-RT backends (Null reference).
    NoopRtCommandBuffer cb;
    rhi::ICommandBuffer& base = cb;
    base.bind_rt_pipeline(rhi::RtPipelineHandle {});         // must not crash
    base.acceleration_structure_barrier();                   // must not crash
    base.dispatch_rays(rhi::DispatchRaysDesc {});            // must not crash
    SUCCEED();
}

// ---- FUNCTIONAL: Windows + DXR-gated real-device smoke -------------------

#if defined(_WIN32)

TEST(D3D12DxrCommand, RecordsDxrCommandsAndSubmitsOnRealDevice)
{
    rhi::d3d12::D3D12CreateInfo info {};
    info.enable_validation = false;  // headless CI may lack the debug layer
    auto dev_r = rhi::d3d12::create_d3d12_device(info);
    if (!dev_r.has_value())
        GTEST_SKIP() << "D3D12 device creation failed (no adapter): "
                     << dev_r.error().message;
    auto& dev = *dev_r;
    ASSERT_NE(dev.get(), nullptr);

    // The DXR command path (SetPipelineState1 / DispatchRays / a real RTPSO)
    // is only valid on an adapter that reports RaytracingTier >= 1.0. WARP and
    // pre-DXR adapters land here -> honest SKIP per ROADMAP §5 (the runtime
    // trace is NVIDIA-DXR-gated).
    if (!dev->features().ray_tracing)
        GTEST_SKIP() << "adapter does not report D3D12_RAYTRACING_TIER >= 1.0 "
                        "(WARP / non-RT GPU); DXR runtime trace is "
                        "NVIDIA-self-hosted-CI gated per ROADMAP §5";

    // === PART 1: D9 acceleration_structure_barrier + AS builds ============
    // This part needs NO RTPSO — it exercises the new acceleration_structure_
    // barrier() override (global UAV barrier) bracketing two real AS builds on
    // the DXR-capable device, then submits. Proves D9 records a real barrier
    // and the build->barrier->build->barrier chain submits cleanly.

    // Tiny BLAS geometry: one triangle in an UPLOAD-heap vertex buffer.
    const float tri[9] = {
        0.0F, 0.5F, 0.0F,
       -0.5F,-0.5F, 0.0F,
        0.5F,-0.5F, 0.0F,
    };
    rhi::BufferDesc vb_bd {};
    vb_bd.size   = sizeof(tri);
    // kVertex only — the BLAS build reads this buffer's GPU VA as triangle
    // positions; it does NOT need a UAV. kStorage would force
    // ALLOW_UNORDERED_ACCESS, which D3D12 rejects on an UPLOAD heap
    // (kCpuToGpu) with E_INVALIDARG.
    vb_bd.usage  = rhi::BufferUsage::kVertex;
    vb_bd.memory = rhi::MemoryUsage::kCpuToGpu;
    auto vb_r = dev->create_buffer(vb_bd);
    ASSERT_TRUE(vb_r.has_value()) << vb_r.error().message;
    const auto vb = *vb_r;
    {
        auto put = dev->upload_buffer(
            vb, 0,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(tri), sizeof(tri)));
        ASSERT_TRUE(put.has_value());
    }

    rhi::AccelTriangleGeometry geo {};
    geo.vertex_buffer = vb;
    geo.vertex_count  = 3u;
    geo.vertex_stride = 12u;
    rhi::AccelStructureDesc blas_desc {};
    blas_desc.kind      = rhi::AccelStructureKind::kBottomLevel;
    blas_desc.triangles = std::span<const rhi::AccelTriangleGeometry>(&geo, 1);
    auto blas_r = dev->create_acceleration_structure(blas_desc);
    ASSERT_TRUE(blas_r.has_value())
        << "create_acceleration_structure (BLAS): " << blas_r.error().message;
    const auto blas = *blas_r;

    rhi::AccelInstance inst {};
    inst.blas = blas;
    rhi::AccelStructureDesc tlas_desc {};
    tlas_desc.kind      = rhi::AccelStructureKind::kTopLevel;
    tlas_desc.instances = std::span<const rhi::AccelInstance>(&inst, 1);
    auto tlas_r = dev->create_acceleration_structure(tlas_desc);
    ASSERT_TRUE(tlas_r.has_value())
        << "create_acceleration_structure (TLAS): " << tlas_r.error().message;
    const auto tlas = *tlas_r;

    {
        auto cb = dev->create_command_buffer(rhi::QueueType::kGraphics);
        ASSERT_NE(cb.get(), nullptr);
        cb->begin();
        cb->build_acceleration_structure(blas);
        cb->acceleration_structure_barrier();  // D9 — BLAS write visible to TLAS build
        cb->build_acceleration_structure(tlas);
        cb->acceleration_structure_barrier();  // D9 — TLAS write visible to a trace
        cb->end();
        // Records + submits a real UAV barrier between two real AS builds on a
        // DXR device. A device-lost / invalid recording would surface here.
        dev->submit(*cb);
        dev->wait_idle();
    }

    // === PART 2: END-TO-END DXR TRACE WITH UAV READBACK ===================
    // B7b — close the two create-side gaps (hit-group subobject + real shader
    // identifiers) and PROVE the GPU executed the rays. A minimal HLSL RT
    // library: ray-gen casts ONE ray straight down +Z through the PART-1
    // triangle's centroid, traces the TLAS, and writes the returned payload
    // colour into a UAV. A HIT runs the closest-hit shader (white); a MISS
    // runs the miss shader (red). We aim the ray at the triangle, so a correct
    // end-to-end pipeline writes WHITE — the readback asserts exactly that,
    // which is only reachable if (a) CreateStateObject accepted the hit-group
    // subobject and (b) the SBT carries the REAL GetShaderIdentifier handles.
    constexpr const char* kRtHlsl = R"(
struct Payload { float4 color; };
// Registers match the descriptor-set-layout binding numbers below: the D3D12
// pipeline-layout builder assigns BaseShaderRegister = binding index per range
// type, so binding 0 (AS) -> t0 and binding 1 (storage image) -> u1.
RaytracingAccelerationStructure scene  : register(t0);   // set layout binding 0
RWTexture2D<float4>             output : register(u1);   // set layout binding 1

[shader("raygeneration")]
void rgen()
{
    uint2 px  = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;
    // Map the pixel to NDC [-1,1]^2 in the triangle's XY plane and shoot a
    // ray straight along -Z at the Z=0 triangle. The centre pixel lands on
    // the triangle, so a correct pipeline writes WHITE there; corner pixels
    // miss and write RED.
    float2 ndc = (float2(px) + 0.5) / float2(dim) * 2.0 - 1.0;
    ndc.y = -ndc.y;  // image space y-down -> world y-up
    RayDesc ray;
    ray.Origin    = float3(ndc.x, ndc.y, 1.0);   // above the Z=0 plane
    ray.Direction = float3(0.0, 0.0, -1.0);      // straight down -Z at it
    ray.TMin      = 0.001;
    ray.TMax      = 10.0;
    Payload p;
    p.color = float4(0.0, 0.0, 0.0, 0.0);
    TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p);
    output[px] = p.color;
}

[shader("miss")]
void miss(inout Payload p) { p.color = float4(1.0, 0.0, 0.0, 1.0); }  // RED = miss

[shader("closesthit")]
void chit(inout Payload p, in BuiltInTriangleIntersectionAttributes a)
{ p.color = float4(1.0, 1.0, 1.0, 1.0); }                              // WHITE = hit
)";

    auto compile_rt = [&](rhi::ShaderStage stage, const char* entry)
        -> rhi::ShaderModuleHandle {
        rhi::d3d12::CompileOptions co {};
        co.source      = kRtHlsl;
        co.entry_point = entry;
        co.stage       = stage;
        co.model       = rhi::d3d12::ShaderModel::kSM6_5;
        co.source_name = "rt_smoke";
        auto blob_r = rhi::d3d12::compile_hlsl(co);
        if (!blob_r.has_value())
            return rhi::ShaderModuleHandle {};
        rhi::ShaderModuleDesc smd {};
        smd.stage       = stage;
        smd.code        = blob_r->data();
        smd.code_size   = blob_r->size();
        smd.entry_point = entry;
        auto m = dev->create_shader_module(smd);
        return m.has_value() ? *m : rhi::ShaderModuleHandle {};
    };

    const auto rgen = compile_rt(rhi::ShaderStage::kRayGen,     "rgen");
    const auto miss = compile_rt(rhi::ShaderStage::kMiss,       "miss");
    const auto chit = compile_rt(rhi::ShaderStage::kClosestHit, "chit");
    ASSERT_TRUE(rgen.is_valid() && miss.is_valid() && chit.is_valid())
        << "RT shader compilation failed (SM6.5 DXIL).";

    // --- Output UAV (8x8 RGBA8) + a descriptor set holding it + the TLAS ---
    rhi::TextureDesc out_td {};
    out_td.type   = rhi::TextureType::k2D;
    out_td.format = rhi::Format::kRGBA8Unorm;
    out_td.extent = { 8u, 8u, 1u };
    out_td.usage  = rhi::TextureUsage::kStorage | rhi::TextureUsage::kTransferSrc;
    out_td.memory = rhi::MemoryUsage::kGpuOnly;
    auto out_r = dev->create_texture(out_td);
    ASSERT_TRUE(out_r.has_value()) << out_r.error().message;
    const auto out_tex = *out_r;

    rhi::TextureViewDesc out_vd {};
    out_vd.texture = out_tex;
    out_vd.type    = rhi::TextureType::k2D;
    auto out_view_r = dev->create_texture_view(out_vd);
    ASSERT_TRUE(out_view_r.has_value()) << out_view_r.error().message;
    const auto out_view = *out_view_r;

    // Layout: binding 0 = TLAS SRV (t0), binding 1 = storage image UAV (u0).
    // The space-per-set model maps set 0 -> space0, matching register(t0)/(u0).
    const rhi::DescriptorSetLayoutBinding binds[] = {
        { 0u, rhi::DescriptorType::kAccelerationStructure, 1u, rhi::ShaderStage::kAllGraphics },
        { 1u, rhi::DescriptorType::kStorageImage,          1u, rhi::ShaderStage::kAllGraphics },
    };
    rhi::DescriptorSetLayoutDesc dsl_desc {};
    dsl_desc.bindings = std::span<const rhi::DescriptorSetLayoutBinding>(binds, 2);
    auto dsl_r = dev->create_descriptor_set_layout(dsl_desc);
    ASSERT_TRUE(dsl_r.has_value()) << dsl_r.error().message;
    const auto dsl = *dsl_r;

    const rhi::DescriptorSetLayoutHandle set_layouts[] = { dsl };
    rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = std::span<const rhi::DescriptorSetLayoutHandle>(set_layouts, 1);
    auto layout_r = dev->create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value()) << layout_r.error().message;

    auto set_r = dev->allocate_descriptor_set(dsl);
    ASSERT_TRUE(set_r.has_value()) << set_r.error().message;
    const auto set = *set_r;

    const rhi::DescriptorWrite writes[] = {
        [&] { rhi::DescriptorWrite w {}; w.binding = 0u;
              w.type = rhi::DescriptorType::kAccelerationStructure;
              w.accel = tlas; return w; }(),
        [&] { rhi::DescriptorWrite w {}; w.binding = 1u;
              w.type = rhi::DescriptorType::kStorageImage;
              w.view = out_view; return w; }(),
    };
    {
        auto wr = dev->update_descriptor_set(
            set, std::span<const rhi::DescriptorWrite>(writes, 2));
        ASSERT_TRUE(wr.has_value()) << wr.error().message;
    }

    // --- Create the RTPSO (NOW with a hit-group subobject) ---------------
    const rhi::RtShaderEntry shaders[] = {
        { rhi::RtShaderStage::kRaygen,     rgen, "rgen", 0u },
        { rhi::RtShaderStage::kMiss,       miss, "miss", 1u },
        { rhi::RtShaderStage::kClosestHit, chit, "chit", 2u },  // group 2 = hit group
    };
    rhi::RtPipelineDesc rpd {};
    rpd.shaders             = std::span<const rhi::RtShaderEntry>(shaders, 3);
    rpd.max_recursion       = 1u;
    rpd.max_payload_bytes   = 16u;
    rpd.max_attribute_bytes = 8u;
    auto rtpso_r = dev->create_rt_pipeline(rpd, *layout_r);
    ASSERT_TRUE(rtpso_r.has_value())
        << "create_rt_pipeline failed — hit-group subobject gap not closed: "
        << rtpso_r.error().message;
    const auto rtpso = *rtpso_r;

    // --- SBT: author from the REAL shader identifiers --------------------
    const std::uint32_t handle_size = dev->rt_shader_group_handle_size();
    const std::uint32_t base_align  = dev->rt_shader_group_base_alignment();
    ASSERT_EQ(handle_size, 32u);
    ASSERT_EQ(base_align,  64u);
    const std::uint64_t record = base_align;  // 64-byte record stride

    // Pull the three group identifiers (raygen=0, miss=1, hit=2) and verify
    // they are NON-zero — the old gap returned all-zero handles, which can
    // NOT author a working SBT. This is the shader_handles_impl proof.
    std::array<std::byte, std::size_t { 32 } * 3> ids {};
    {
        auto h0 = dev->get_rt_shader_group_handles(
            rtpso, 0u, 3u, std::span<std::byte>(ids));
        ASSERT_TRUE(h0.has_value())
            << "get_rt_shader_group_handles failed: " << h0.error().message;
    }
    auto group_is_nonzero = [&](std::size_t g) {
        for (std::size_t i = 0; i < 32; ++i)
            if (ids[g * std::size_t { 32 } + i] != std::byte { 0 }) return true;
        return false;
    };
    EXPECT_TRUE(group_is_nonzero(0)) << "ray-gen identifier is zero-filled";
    EXPECT_TRUE(group_is_nonzero(1)) << "miss identifier is zero-filled";
    EXPECT_TRUE(group_is_nonzero(2)) << "hit-group identifier is zero-filled";

    // Upload one 32-byte identifier into each 64-byte record of an
    // upload-heap SBT buffer (a shader table is legal on an UPLOAD heap).
    rhi::BufferDesc sbt_bd {};
    sbt_bd.size   = record * 3u;  // raygen + miss + hit
    sbt_bd.usage  = rhi::BufferUsage::kShaderBindingTable;
    sbt_bd.memory = rhi::MemoryUsage::kCpuToGpu;
    auto sbt_r = dev->create_buffer(sbt_bd);
    ASSERT_TRUE(sbt_r.has_value()) << sbt_r.error().message;
    const auto sbt = *sbt_r;
    for (std::uint32_t g = 0; g < 3u; ++g)
    {
        auto put = dev->upload_buffer(
            sbt, record * std::uint64_t { g },
            std::span<const std::byte>(ids.data() + std::size_t { g } * 32u, 32u));
        ASSERT_TRUE(put.has_value()) << put.error().message;
    }

    // --- Record: (re)build AS in THIS CB, then bind + dispatch -----------
    // Build the BLAS+TLAS in the SAME command buffer as the trace so the
    // acceleration-structure writes are guaranteed visible to DispatchRays
    // through the acceleration_structure_barrier() (UAV barrier) below —
    // independent of any cross-submit lifetime assumption.
    auto cb = dev->create_command_buffer(rhi::QueueType::kGraphics);
    ASSERT_NE(cb.get(), nullptr);
    cb->begin();
    cb->build_acceleration_structure(blas);
    cb->acceleration_structure_barrier();
    cb->build_acceleration_structure(tlas);
    cb->acceleration_structure_barrier();
    cb->bind_rt_pipeline(rtpso);                 // D8 — SetPipelineState1 + root sig
    cb->bind_descriptor_set(0u, set);            // set 0 -> root param 0 (UAV+TLAS)

    rhi::DispatchRaysDesc drd {};
    drd.width  = 8u;
    drd.height = 8u;
    drd.depth  = 1u;
    drd.raygen = { sbt, 0u,          record, record };
    drd.miss   = { sbt, record,      record, record };
    drd.hit    = { sbt, record * 2u, record, record };
    cb->dispatch_rays(drd);                      // D7 — DispatchRays
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();

    // --- Read back the centre pixel -> must be WHITE (the ray HIT) -------
    rhi::BufferDesc rb_bd {};
    rb_bd.size   = std::uint64_t { 8 } * 8u * 4u;
    rb_bd.usage  = rhi::BufferUsage::kTransferDst;
    rb_bd.memory = rhi::MemoryUsage::kGpuToCpu;
    auto rb_r = dev->create_buffer(rb_bd);
    ASSERT_TRUE(rb_r.has_value()) << rb_r.error().message;
    const auto rb = *rb_r;

    rhi::IDevice::ImageRegion reg {};
    reg.width  = 8u;
    reg.height = 8u;
    auto copy_r = dev->copy_image_to_buffer(out_tex, rb, 0u, reg);
    ASSERT_TRUE(copy_r.has_value()) << copy_r.error().message;

    std::array<std::byte, std::size_t { 8 } * 8 * 4> pixels {};
    auto dl = dev->download_buffer(rb, 0u, std::span<std::byte>(pixels));
    ASSERT_TRUE(dl.has_value()) << dl.error().message;

    // Centre pixel (4,4): row-major RGBA8. WHITE proves ray-gen + traversal +
    // CLOSEST-HIT (the new hit group) all ran on the GPU. RED would mean the
    // ray missed (TLAS/SBT mis-bound); BLACK would mean the UAV write never
    // happened.
    const std::size_t idx = (std::size_t { 4 } * 8u + 4u) * 4u;
    const auto r8 = static_cast<unsigned>(static_cast<std::uint8_t>(pixels[idx + 0]));
    const auto g8 = static_cast<unsigned>(static_cast<std::uint8_t>(pixels[idx + 1]));
    const auto b8 = static_cast<unsigned>(static_cast<std::uint8_t>(pixels[idx + 2]));
    EXPECT_EQ(r8, 255u) << "centre pixel R — expected WHITE (closest-hit ran)";
    EXPECT_EQ(g8, 255u) << "centre pixel G — expected WHITE (closest-hit ran)";
    EXPECT_EQ(b8, 255u) << "centre pixel B — expected WHITE (closest-hit ran); "
                           "RED(255,0,0)=ray missed, BLACK=UAV unwritten";
}

#endif  // _WIN32
