// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_root_cost.cpp
//
// C-D3D12-FIXES / D-ROOTCOST: D3D12 push-constant root-cost overflow is REJECTED,
// not silently clamped.
//
// BACKGROUND. create_pipeline_layout collapses the engine's PushConstantRange
// set into one D3D12 root 32-bit-constants parameter. A D3D12 root signature has
// a HARD 64-DWORD budget (each descriptor table costs 1 DWORD; each push-constant
// DWORD costs 1). The historical code, when the union (tables + push-constant
// DWORDs) exceeded 64, SILENTLY CLAMPED pc_dwords to fit — the silent-truncation
// bug class (same as the BLAS-geo-cap lesson). A caller's push_constants() writes
// past the clamped tail would then land nowhere: an invisible miswire, no error.
// The Vulkan reference rejects an over-budget pipeline layout at
// vkCreatePipelineLayout. The fix mirrors that: return kInvalidArgument instead
// of clamping.
//
// WHAT THIS TEST PROVES.
//   * A push-constant range that ALONE exceeds 64 DWORDs (260 bytes = 65 DWORDs)
//     makes create_pipeline_layout return kInvalidArgument (was: a clamped-but-
//     "successful" layout pre-fix). BIDIRECTIONAL: a temp-revert that restores
//     the clamp makes this an has_value() success -> the EXPECT_FALSE flips.
//   * A small push-constant range (64 bytes = 16 DWORDs) still SUCCEEDS — the
//     fix rejects only genuine overflow, not every push-constant layout.
//
// Pure CPU-side layout creation — no shaders, no DXC, no submit. Honest-SKIP
// only when no D3D12 adapter is present. Pattern: Arrange/Act/Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#if defined(_WIN32)

namespace
{

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

// Build a pipeline layout with a single push-constant range of `pc_bytes`.
[[nodiscard]] bool layout_created(cd::rhi::IDevice& dev, std::uint32_t pc_bytes)
{
    const cd::rhi::PushConstantRange range {
        cd::rhi::ShaderStage::kVertex, 0u, pc_bytes };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.push_constants =
        std::span<const cd::rhi::PushConstantRange>(&range, 1);
    auto r = dev.create_pipeline_layout(pld);
    if (r.has_value())
    {
        dev.destroy_pipeline_layout(*r);
        return true;
    }
    return false;
}

}  // namespace

// ---- D-ROOTCOST: an over-budget push-constant layout is REJECTED ------------
TEST(D3D12RootCost, OverBudgetPushConstantLayoutIsRejected)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";
    auto& d = *dev;

    // 260 bytes = 65 DWORDs > the 64-DWORD root budget — even with ZERO
    // descriptor tables this overflows. Pre-fix: clamped to 64 and "succeeds".
    EXPECT_FALSE(layout_created(d, 260u))
        << "BUG: a 65-DWORD push-constant range was ACCEPTED — "
           "create_pipeline_layout is silently clamping the root cost instead "
           "of rejecting it (the silent-truncation parity bug Vulkan rejects "
           "at vkCreatePipelineLayout).";
}

// ---- D-ROOTCOST (positive control): a small push-constant layout SUCCEEDS ----
TEST(D3D12RootCost, SmallPushConstantLayoutSucceeds)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";
    auto& d = *dev;

    // 64 bytes = 16 DWORDs — comfortably under budget. The fix must reject
    // ONLY genuine overflow, not every push-constant layout.
    EXPECT_TRUE(layout_created(d, 64u))
        << "a 16-DWORD push-constant layout is well under the 64-DWORD budget "
           "and must create successfully";
}

#endif  // _WIN32
