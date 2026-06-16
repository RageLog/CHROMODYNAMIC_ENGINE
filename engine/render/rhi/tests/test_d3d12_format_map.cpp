// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_format_map.cpp
//
// C-D3D12-FIXES / D-FORMAT-MAP: D3D12 Format → DXGI_FORMAT table coverage vs the
// Vulkan reference.
//
// BACKGROUND. `to_dxgi_format` (D3D12Device.cpp) historically mapped only ~13 of
// the interface's Formats; the remaining ~30 (R8Unorm, R16Float, RG16Float,
// every RGBA16* / *Uint / *Sint variant, RGBA8Snorm, D16, the packed/HDR set,
// and the entire BCn block-compressed family) fell to DXGI_FORMAT_UNKNOWN. Any
// create_texture / create_swapchain / create_texture_view call with one of those
// formats hit the "unsupported texture format" UNKNOWN-mapping reject — a silent
// capability gap vs the Vulkan reference (map_format, VulkanDevice.cpp:286)
// which covers every Format. The fix mirrors the Vulkan coverage.
//
// WHAT THIS TEST PROVES. For EVERY interface Format except kUndefined,
// create_texture must NOT reject with the UNKNOWN-mapping error. That error is
// distinctive ("unsupported texture format for D3D12 backend") and fires ONLY
// when to_dxgi_format returned UNKNOWN — so it isolates the MAPPING from any
// later adapter-side CreateCommittedResource rejection (e.g. RGB32 formats are
// valid DXGI values but not valid texture *resources*; that is a different,
// correct, rejection the engine still surfaces). Pre-fix, ~30 Formats produce
// the UNKNOWN-mapping reject and this test FAILS; post-fix only kUndefined does.
//
// kUndefined is the documented-unsupported sentinel (no pixel format) and is
// asserted to STILL reject — the bidirectional half.
//
// No shaders / DXC needed (pure create-path introspection). Honest-SKIP only
// when no D3D12 adapter (WARP/hardware) is present. Pattern: Arrange/Act/Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/IDevice.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

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

// The exact substring the create_texture UNKNOWN-mapping reject carries. Used
// to DISTINGUISH "to_dxgi_format returned UNKNOWN" from any later
// adapter/resource-side rejection.
constexpr const char* kUnknownMapMsg = "unsupported texture format";

// Try to create a plain sampled 2D texture in `fmt` and report whether the
// failure (if any) was the UNKNOWN-mapping reject specifically.
[[nodiscard]] bool rejected_for_unknown_mapping(cd::rhi::IDevice& dev,
                                                cd::rhi::Format fmt)
{
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = fmt;
    td.extent       = { 4, 4, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kSampled;
    auto r = dev.create_texture(td);
    if (r.has_value())
    {
        dev.destroy_texture(*r);
        return false;  // created fine — definitely not an UNKNOWN map.
    }
    const std::string msg(r.error().message.begin(), r.error().message.end());
    return msg.find(kUnknownMapMsg) != std::string::npos;
}

}  // namespace

// ---- D-FORMAT-MAP: every Format except kUndefined maps to a real DXGI value --
TEST(D3D12FormatMap, AllInterfaceFormatsMapToNonUnknownDxgi)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";
    auto& d = *dev;

    int checked = 0;
    for (std::uint16_t i = 0;
         i < static_cast<std::uint16_t>(cd::rhi::Format::kCount); ++i)
    {
        const auto fmt = static_cast<cd::rhi::Format>(i);
        if (fmt == cd::rhi::Format::kUndefined)
            continue;  // documented-unsupported sentinel — checked separately.
        ++checked;
        EXPECT_FALSE(rejected_for_unknown_mapping(d, fmt))
            << "BUG: Format index " << i
            << " hit the UNKNOWN-mapping reject — to_dxgi_format returned "
               "DXGI_FORMAT_UNKNOWN for a Format the Vulkan reference covers "
               "(D-FORMAT-MAP table is incomplete).";
    }
    // Sanity: we actually walked the whole enum (42 real formats today).
    EXPECT_GE(checked, 40)
        << "expected to walk the full interface Format enum";
}

// ---- D-FORMAT-MAP (bidirectional): kUndefined STILL rejects -----------------
//
// The mapping must reject ONLY the no-pixel-format sentinel. If a future edit
// makes kUndefined map to a real DXGI value, this half catches it.
TEST(D3D12FormatMap, UndefinedFormatIsRejected)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";
    auto& d = *dev;

    EXPECT_TRUE(rejected_for_unknown_mapping(d, cd::rhi::Format::kUndefined))
        << "kUndefined must remain the UNKNOWN-mapping sentinel (no pixel "
           "format) — it must NOT create a texture.";
}

#endif  // _WIN32
