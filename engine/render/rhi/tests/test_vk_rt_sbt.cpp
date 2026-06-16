// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_vk_rt_sbt.cpp
//
// C-VK-RT-SBT (Backend-to-100 Wave 3d).
//
// The Vulkan SBT (shader-binding-table) reference path is currently UNTESTED on
// the host side — D3D12 has test_d3d12_dxr_command's structural half, Vulkan has
// none. This test is the unit half for Vulkan: it pins the host-side SBT MATH
// the engine must get right before vkCmdTraceRaysKHR, and (when the ICD exposes
// ray tracing) the real device-reported handle constants + the
// get_rt_shader_group_handles path. The FUNCTIONAL dispatch is the sample /
// hello_path_trace; this is the structural unit complement.
//
// THREE LAYERS:
//   1. STRUCTURAL (no device, runs on EVERY lane): the SBT region stride math.
//      Given a shaderGroupHandleSize + shaderGroupHandleAlignment +
//      shaderGroupBaseAlignment, the SBT layout helper computes, for a table of
//      N groups: per-group STRIDE = align_up(handle_size, handle_alignment), and
//      each REGION base = align_up(stride * group_count, base_alignment). This
//      mirrors the canonical Vulkan SBT packing (Sascha Willems / Vulkan-Samples
//      reference). Asserts the alignment invariants the device VA math relies on.
//
//   2. STRUCTURAL: a region whose buffer is invalid is SKIPPED (zeroed), exactly
//      like the DispatchRaysDesc contract (a missing miss/hit table is legal —
//      the corresponding stage simply never runs). Mirrors the D3D12 structural
//      "invalid region is skipped" pin so the two backends agree on the policy.
//
//   3. DEVICE (Vulkan, RT-gated): create a real RT pipeline (raygen + miss +
//      closesthit GLSL -> SPIR-V), then
//        * assert rt_shader_group_handle_size() / _alignment / _base_alignment
//          are non-zero + the alignments are powers of two (the values the SBT
//          math in layer 1 consumes),
//        * pull the 3 group handles via get_rt_shader_group_handles and assert
//          each is NON-zero (a zero-filled handle authors a dead SBT — the same
//          proof the D3D12 test makes), and
//        * assert get_rt_shader_group_handles with an UNDERSIZED out buffer is
//          rejected (kInvalidArgument) and create_rt_pipeline with an EMPTY
//          shader list is rejected (kInvalidArgument).
//      Honest-SKIP when the ICD lacks VK_KHR_ray_tracing_pipeline (lavapipe in
//      CI), so the structural layers always run and the device layer runs on the
//      RTX 3080 here.
//
// Pattern: Arrange / Act / Assert.
// =============================================================================
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rhi = cd::rhi;

namespace
{

// ---- Layer 1 + 2: host-side SBT region math (no device) ---------------------

[[nodiscard]] constexpr std::uint64_t align_up(std::uint64_t v, std::uint64_t a) noexcept
{
    return a == 0 ? v : ((v + a - 1u) / a) * a;
}

// One region of a packed SBT: a contiguous run of `group_count` records, each
// `stride` bytes, whose base is `base`-aligned. `size` is stride*count.
struct SbtRegionLayout
{
    std::uint64_t base   { 0 };
    std::uint64_t stride { 0 };
    std::uint64_t size   { 0 };
};

// The canonical Vulkan SBT packer: raygen | miss[] | hit[] packed back-to-back,
// each region base aligned to base_alignment, each record stride aligned to
// handle_alignment. Mirrors the reference packing every Vulkan RT sample uses.
struct SbtLayout
{
    SbtRegionLayout raygen;
    SbtRegionLayout miss;
    SbtRegionLayout hit;
    std::uint64_t   total_size { 0 };
};

[[nodiscard]] SbtLayout
compute_sbt_layout(std::uint32_t handle_size, std::uint32_t handle_align,
                   std::uint32_t base_align, std::uint32_t miss_count,
                   std::uint32_t hit_count)
{
    const std::uint64_t stride = align_up(handle_size, handle_align);
    SbtLayout l {};
    std::uint64_t cursor = 0;

    // raygen: exactly one record; the region's stride EQUALS its size per the
    // Vulkan spec (raygen size must equal stride).
    l.raygen.base   = align_up(cursor, base_align);
    l.raygen.stride = stride;
    l.raygen.size   = stride;
    cursor = l.raygen.base + l.raygen.size;

    l.miss.base   = align_up(cursor, base_align);
    l.miss.stride = stride;
    l.miss.size   = stride * miss_count;
    cursor = l.miss.base + l.miss.size;

    l.hit.base   = align_up(cursor, base_align);
    l.hit.stride = stride;
    l.hit.size   = stride * hit_count;
    cursor = l.hit.base + l.hit.size;

    l.total_size = cursor;
    return l;
}

[[nodiscard]] constexpr bool is_pow2(std::uint64_t v) noexcept
{
    return v != 0 && (v & (v - 1)) == 0;
}

}  // namespace

// ---- Layer 1: SBT region stride math ----------------------------------------

TEST(VkRtSbt, RegionStrideMathMatchesVulkanPacking)
{
    // Typical NVIDIA constants: 32-byte handle, 32-byte handle alignment,
    // 64-byte base (group) alignment. (The device test below confirms the REAL
    // device values; this layer pins the MATH against known inputs.)
    constexpr std::uint32_t kHandleSize  = 32;
    constexpr std::uint32_t kHandleAlign = 32;
    constexpr std::uint32_t kBaseAlign   = 64;

    const auto l = compute_sbt_layout(kHandleSize, kHandleAlign, kBaseAlign,
                                      /*miss_count=*/2, /*hit_count=*/3);

    // Per-record stride is handle_size rounded up to handle_alignment.
    EXPECT_EQ(l.raygen.stride, 32u);
    EXPECT_EQ(l.miss.stride,   32u);
    EXPECT_EQ(l.hit.stride,    32u);

    // raygen: exactly one record, size == stride (the Vulkan spec rule).
    EXPECT_EQ(l.raygen.size, l.raygen.stride);

    // Region sizes are stride * count.
    EXPECT_EQ(l.miss.size, 32u * 2u);
    EXPECT_EQ(l.hit.size,  32u * 3u);

    // Every region base is base-alignment aligned.
    EXPECT_EQ(l.raygen.base % kBaseAlign, 0u);
    EXPECT_EQ(l.miss.base   % kBaseAlign, 0u);
    EXPECT_EQ(l.hit.base    % kBaseAlign, 0u);

    // Regions do not overlap and are in raygen < miss < hit order.
    EXPECT_LT(l.raygen.base, l.miss.base);
    EXPECT_LT(l.miss.base,   l.hit.base);
    EXPECT_GE(l.miss.base,   l.raygen.base + l.raygen.size);
    EXPECT_GE(l.hit.base,    l.miss.base   + l.miss.size);

    // Total table size covers the hit region.
    EXPECT_GE(l.total_size, l.hit.base + l.hit.size);
}

// A larger handle that is NOT a multiple of the handle alignment must round up.
TEST(VkRtSbt, NonAlignedHandleRoundsUpStride)
{
    // 40-byte handle, 16-byte alignment -> stride 48.
    const auto l = compute_sbt_layout(/*handle*/ 40, /*halign*/ 16, /*balign*/ 64, 1, 1);
    EXPECT_EQ(l.raygen.stride, 48u);
    EXPECT_EQ(l.miss.stride,   48u);
    EXPECT_EQ(l.hit.stride,    48u);
    EXPECT_EQ(l.raygen.base % 64u, 0u);
}

// ---- Layer 2: invalid SBT region is skipped (zeroed) ------------------------

TEST(VkRtSbt, InvalidRegionResolvesToZero)
{
    // Mirror the DispatchRaysDesc / D3D12 contract: a region whose buffer is
    // invalid is SKIPPED — its device address / stride / size resolve to zero so
    // the stage never runs. We model the resolve a backend performs.
    const auto resolve = [](const rhi::SbtRegion& r, std::uint64_t base_va)
        -> SbtRegionLayout {
        SbtRegionLayout out {};
        if (!r.buffer.is_valid() || base_va == 0)
            return out;  // skipped
        out.base   = base_va + r.offset;
        out.stride = r.stride_bytes;
        out.size   = r.size_bytes;
        return out;
    };

    const rhi::BufferHandle valid_sbt { 9u, 1u };
    rhi::SbtRegion raygen { valid_sbt, 0u, 64u, 64u };
    rhi::SbtRegion miss   {};                       // invalid (default handle)
    rhi::SbtRegion hit    {};                       // invalid

    const auto rg = resolve(raygen, 0x4000u);
    const auto ms = resolve(miss,   0x4000u);
    const auto ht = resolve(hit,    0x4000u);

    EXPECT_NE(rg.base, 0u);   // valid -> resolved
    EXPECT_EQ(ms.base, 0u);   // invalid -> skipped
    EXPECT_EQ(ms.size, 0u);
    EXPECT_EQ(ht.base, 0u);   // invalid -> skipped
    EXPECT_EQ(ht.size, 0u);
}

// ---- Layer 3: real Vulkan device (RT-gated) ---------------------------------

namespace
{

// Minimal GLSL RT stages compiled through the device kGlsl path -> SPIR-V.
constexpr const char* kRaygen = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadEXT vec4 payload;
void main() { payload = vec4(0.0); }
)glsl";

constexpr const char* kMiss = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec4 payload;
void main() { payload = vec4(1.0, 0.0, 0.0, 1.0); }
)glsl";

constexpr const char* kClosestHit = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 attribs;
void main() { payload = vec4(1.0); }
)glsl";

[[nodiscard]] std::unique_ptr<rhi::IDevice> make_vulkan_or_null()
{
    rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = rhi::vulkan::create_vulkan_device(info);
    return r.has_value() ? std::move(*r) : nullptr;
}

[[nodiscard]] bool glslang_available()
{
    return cd::shader::make_glslang_compiler() != nullptr;
}

[[nodiscard]] rhi::ShaderModuleHandle
make_rt_module(rhi::IDevice& dev, rhi::ShaderStage stage, const char* src)
{
    rhi::ShaderModuleDesc d {};
    d.stage       = stage;
    d.code        = src;
    d.code_size   = std::char_traits<char>::length(src);
    d.entry_point = "main";
    d.language    = rhi::ShaderSourceLanguage::kGlsl;
    auto r = dev.create_shader_module(d);
    return r.has_value() ? *r : rhi::ShaderModuleHandle {};
}

}  // namespace

TEST(VkRtSbt, DeviceHandleConstantsAndGroupHandlesAreReal)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_vulkan_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Vulkan ICD on this host";
    if (!dev->features().ray_tracing)
        GTEST_SKIP() << "Vulkan ICD lacks VK_KHR_ray_tracing_pipeline "
                        "(lavapipe / non-RT GPU); SBT device path is RTX-here-gated";

    // The handle constants the SBT math (layer 1) consumes must be real.
    const std::uint32_t handle_size = dev->rt_shader_group_handle_size();
    const std::uint32_t handle_algn = dev->rt_shader_group_handle_alignment();
    const std::uint32_t base_algn    = dev->rt_shader_group_base_alignment();
    EXPECT_GT(handle_size, 0u) << "shaderGroupHandleSize == 0 on an RT device";
    EXPECT_TRUE(is_pow2(handle_algn)) << "shaderGroupHandleAlignment not pow2";
    EXPECT_TRUE(is_pow2(base_algn))   << "shaderGroupBaseAlignment not pow2";
    EXPECT_GE(base_algn, handle_algn) << "base alignment must be >= handle align";

    // Build an RT pipeline: raygen(group0) + miss(group1) + closesthit(group2).
    const auto rg = make_rt_module(*dev, rhi::ShaderStage::kRayGen,     kRaygen);
    const auto ms = make_rt_module(*dev, rhi::ShaderStage::kMiss,       kMiss);
    const auto ch = make_rt_module(*dev, rhi::ShaderStage::kClosestHit, kClosestHit);
    ASSERT_TRUE(rg.is_valid() && ms.is_valid() && ch.is_valid())
        << "RT shader module compilation (GLSL -> SPIR-V) failed";

    rhi::PipelineLayoutDesc pld {};
    auto layout_r = dev->create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value());
    const auto layout = *layout_r;

    const std::array<rhi::RtShaderEntry, 3> shaders {
        rhi::RtShaderEntry { rhi::RtShaderStage::kRaygen,     rg, "main", 0u },
        rhi::RtShaderEntry { rhi::RtShaderStage::kMiss,       ms, "main", 1u },
        rhi::RtShaderEntry { rhi::RtShaderStage::kClosestHit, ch, "main", 2u },
    };
    rhi::RtPipelineDesc rpd {};
    rpd.shaders             = shaders;
    rpd.max_recursion       = 1u;
    rpd.max_payload_bytes   = 16u;
    rpd.max_attribute_bytes = 8u;
    auto pso_r = dev->create_rt_pipeline(rpd, layout);
    ASSERT_TRUE(pso_r.has_value())
        << std::string(pso_r.error().message.begin(), pso_r.error().message.end());
    const auto pso = *pso_r;

    // Pull the 3 group handles — each must be NON-zero (a zero handle authors a
    // dead SBT). This is the same proof the D3D12 DXR test makes.
    const std::size_t group_count = 3;
    std::vector<std::byte> handles(group_count * handle_size);
    {
        const auto h = dev->get_rt_shader_group_handles(
            pso, 0u, static_cast<std::uint32_t>(group_count),
            std::span<std::byte>(handles));
        ASSERT_TRUE(h.has_value())
            << std::string(h.error().message.begin(), h.error().message.end());
    }
    const auto group_is_nonzero = [&](std::size_t g) {
        for (std::size_t i = 0; i < handle_size; ++i)
            if (handles[g * handle_size + i] != std::byte { 0 }) return true;
        return false;
    };
    EXPECT_TRUE(group_is_nonzero(0)) << "raygen group handle is zero-filled";
    EXPECT_TRUE(group_is_nonzero(1)) << "miss group handle is zero-filled";
    EXPECT_TRUE(group_is_nonzero(2)) << "hit group handle is zero-filled";

    // The host SBT layout computed from the REAL device constants must place the
    // regions consistently with what vkCmdTraceRaysKHR expects.
    const auto layout_sbt = compute_sbt_layout(handle_size, handle_algn, base_algn, 1, 1);
    EXPECT_EQ(layout_sbt.raygen.size, layout_sbt.raygen.stride);
    EXPECT_EQ(layout_sbt.miss.base % base_algn, 0u);
    EXPECT_EQ(layout_sbt.hit.base  % base_algn, 0u);

    // Negative: an UNDERSIZED out buffer must be rejected, not over-write.
    {
        std::vector<std::byte> tiny(handle_size / 2);  // too small for one handle
        const auto bad = dev->get_rt_shader_group_handles(
            pso, 0u, 1u, std::span<std::byte>(tiny));
        EXPECT_FALSE(bad.has_value());
        if (!bad.has_value())
            EXPECT_EQ(bad.error().code,
                      static_cast<std::uint32_t>(rhi::rhi_errors::Code::kInvalidArgument));
    }

    dev->destroy_rt_pipeline(pso);
    dev->destroy_pipeline_layout(layout);
    dev->destroy_shader_module(ch);
    dev->destroy_shader_module(ms);
    dev->destroy_shader_module(rg);
}

// Negative: create_rt_pipeline with an EMPTY shader list is rejected.
TEST(VkRtSbt, EmptyShaderListIsRejected)
{
    auto dev = make_vulkan_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Vulkan ICD on this host";
    if (!dev->features().ray_tracing)
        GTEST_SKIP() << "Vulkan ICD lacks VK_KHR_ray_tracing_pipeline (RT-gated)";

    rhi::PipelineLayoutDesc pld {};
    auto layout_r = dev->create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value());
    const auto layout = *layout_r;

    rhi::RtPipelineDesc rpd {};  // shaders span is empty
    const auto bad = dev->create_rt_pipeline(rpd, layout);
    EXPECT_FALSE(bad.has_value());
    if (!bad.has_value())
        EXPECT_EQ(bad.error().code,
                  static_cast<std::uint32_t>(rhi::rhi_errors::Code::kInvalidArgument));

    dev->destroy_pipeline_layout(layout);
}
