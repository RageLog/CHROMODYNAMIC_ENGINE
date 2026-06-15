// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_device_features.cpp
//
// parity1121 WAVE A1 / D3: D3D12 DeviceFeatures parity vs the Vulkan reference.
//
// BACKGROUND. The D3D12 device-init block historically set only three of the
// thirteen DeviceFeatures bits (ray_tracing / mesh_shader / bindless_resources)
// from CheckFeatureSupport; the remaining eight stayed default-FALSE even
// though every Feature-Level-11_0+ D3D12 device supports them. The Vulkan
// reference (VulkanDevice.cpp:4593-4623) reports each from the real device
// caps. A caller that gates a code path on `features().geometry_shader` (etc.)
// would therefore get DIFFERENT, wrong answers on D3D12 vs Vulkan — a silent
// capability-reporting parity bug.
//
// THE FIX (D3D12Device.cpp device-init) sets:
//   * ray_query                 — true on D3D12_RAYTRACING_TIER_1_1
//                                 (mirrors VK_KHR_ray_query)
//   * geometry_shader           — true (FL9_1+ guaranteed)
//   * tessellation_shader       — true (FL11_0+ mandatory HS/DS)
//   * sampler_anisotropy        — true (MaxAnisotropy 16 mandated)
//   * depth_clamp               — true (RasterizerState.DepthClipEnable)
//   * dual_source_blend         — true (SRC1_COLOR/SRC1_ALPHA blends)
//   * timestamp_queries         — probed via CreateQueryHeap(TIMESTAMP)
//   * pipeline_statistics_queries — probed via CreateQueryHeap(PIPELINE_STATISTICS)
//
// WHAT THIS TEST PROVES. After creating the real D3D12 backend (WARP if no
// hardware adapter — WARP is a full FL12_x device that supports all of these),
// every one of the eight flags is reported TRUE. This FAILS on the pre-fix code
// (the eight bits are default-false). Honest-SKIP only when no D3D12 adapter is
// present. No shaders / DXC needed (pure device introspection).
//
// Pattern: Arrange/Act/Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/IDevice.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <memory>

#if defined(_WIN32)

namespace
{

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;  // avoid debug-layer dependency in CI
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

}  // namespace

// ---- D3: the previously-unset DeviceFeatures bits are now reported ----------
//
// WARP and any FL11_0+ hardware adapter support all eight. The five
// always-true caps are unconditional; ray_query needs DXR Tier 1.1 and the two
// query caps need a creatable query heap — WARP provides all of these, so on a
// machine where a D3D12 device exists at all every assertion holds.
TEST(D3D12DeviceFeatures, AllParityFlagsReportedOnFl11Device)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";

    const cd::rhi::DeviceFeatures& f = dev->features();

    // Five FL11_0+-mandatory caps — unconditionally true on any real device.
    EXPECT_TRUE(f.geometry_shader)
        << "geometry_shader must be true on any FL11_0+ D3D12 device";
    EXPECT_TRUE(f.tessellation_shader)
        << "tessellation_shader (HS/DS) is FL11_0 mandatory";
    EXPECT_TRUE(f.sampler_anisotropy)
        << "sampler_anisotropy (MaxAnisotropy 16) is mandated";
    EXPECT_TRUE(f.depth_clamp)
        << "depth_clamp (RasterizerState.DepthClipEnable) is supported";
    EXPECT_TRUE(f.dual_source_blend)
        << "dual_source_blend (SRC1_COLOR/SRC1_ALPHA) is supported";

    // Query-heap-probed caps — WARP/hardware both create these heaps.
    EXPECT_TRUE(f.timestamp_queries)
        << "timestamp query heap must be creatable on a direct/compute queue";
    EXPECT_TRUE(f.pipeline_statistics_queries)
        << "pipeline-statistics query heap must be creatable";

    // ray_query mirrors VK_KHR_ray_query — DXR Tier 1.1. WARP exposes Tier 1.1,
    // so this is true on every host that has a D3D12 device at all; the bit must
    // also be consistent with ray_tracing (Tier 1.1 implies Tier 1.0).
    EXPECT_TRUE(f.ray_query)
        << "ray_query must be true on a DXR Tier 1.1 device (incl. WARP)";
    if (f.ray_query)
    {
        EXPECT_TRUE(f.ray_tracing)
            << "ray_query (Tier 1.1) implies ray_tracing (Tier 1.0)";
    }
}

#endif  // _WIN32
